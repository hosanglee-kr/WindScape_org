/*
 * ------------------------------------------------------
 * 소스명 : S10_Simul_Core_021.cpp
 * 모듈약어 : S10
 * 모듈명 : Smart Nature Wind 풍속 시뮬레이션 Manager (v019, Full)
 * ------------------------------------------------------
 * 기능 요약:
 * - CL_S10_Simulation 클래스의 구현부
 * - Von Kármán 난류 모델, 관성, 돌풍/열기포 확률 모델 구현
 * ------------------------------------------------------
 */

#include "S10_Simulation_020.h" // 해당 클래스 헤더 파일 포함

// 외부 종속성 헤더 포함 (외부에서 제공되어야 함: 시스템 상수, 설정, 로그, PWM 제어)
#include "A10_Const_016.h"
#include "C10_Config_029.h"
#include "D10_Logger_016.h"
#include "P10_PWM_ctrl_014.h"



// ==================================================
// 초기화 / 정지 / 리셋
// ==================================================
/**
 * @brief 시뮬레이션 객체를 초기화하고 PWM 객체 레퍼런스를 설정합니다.
 * @param p_pwm PWM 제어 모듈 인스턴스
 */
void CL_S10_Simulation::begin(CL_P10_PWM& p_pwm) {
    _pwm = &p_pwm;
    resetDefaults(); // 시뮬레이션 파라미터를 기본값으로 설정
    // 풍속 이력 버퍼(history) 초기화 및 인덱스 리셋 (평균 풍속 계산용)
    memset(history, 0, sizeof(history));
    historyIndex = 0;
    historyCount = 0;
    (void)esp_random();  // ESP32의 하드웨어 기반 랜덤 시드를 호출하여 지터 유도/난수 안정화

    CL_D10_Logger::log(EN_L10_LOG_INFO, "[S10] begin()");
}

/**
 * @brief 시뮬레이션을 정지하고 팬을 끕니다.
 */
void CL_S10_Simulation::stop() {
    active           = false;
    phase            = EN_A10_WEATHER_PHASE_CALM;
    targetWindSpeed  = 0.0f;
    currentWindSpeed = 0.0f;
    if (_pwm) {
        _pwm->P10_setDutyPercent(0.0f); // PWM 0% 설정으로 팬 정지
    }
}

/**
 * @brief 모든 시뮬레이션 파라미터를 기본 Preset("OCEAN") 값으로 리셋합니다.
 */
void CL_S10_Simulation::resetDefaults() {
    active          = false;
    fanPowerEnabled = true;

    // 기본 Preset/Style 설정
    strlcpy(presetCode, "OCEAN", sizeof(presetCode));
    strlcpy(styleCode, "BALANCE", sizeof(styleCode));

    // 사용자 설정 (UI/API에서 제어 가능한 파라미터)
    userIntensity   = 70.0f;    // 전체 풍속 세기 (0~100)
    userVariability = 50.0f;    // 목표 풍속 변화율/빈도 (0~100)
    userGustFreq    = 45.0f;    // 돌풍 발생 빈도 (0~100)
    minFanPct       = 10.0f;    // 최소 팬 구동 Duty (%)
    fanLimitPct     = 90.0f;    // 최대 팬 구동 Duty (%)

    // 물리 파라미터: 난류
    turbLenScale    = 40.0f;    // 난류 길이 스케일 (L)
    turbSigma       = 0.5f;     // 난류 세기 (Sigma)
    // 물리 파라미터: 열기포
    thermalStrength = 2.0f;     // 열기포 강도 (1.0 이상)
    thermalRadius   = 18.0f;    // 열기포 반경 (사용하지 않을 수도 있음)

    // Preset 기반 물리 상수 (applyPresetCore에서 재설정됨)
    baseMinWind     = 1.8f;     // 기본 최소 풍속 (m/s)
    baseMaxWind     = 5.5f;     // 기본 최대 풍속 (m/s)
    gustProbBase    = 0.040f;   // 기본 돌풍 발생 확률
    gustStrengthMax = 2.10f;    // 최대 돌풍 배율
    thermalFreqBase = 0.022f;   // 기본 열기포 발생 확률

    // 현재 상태 변수
    currentWindSpeed = 3.6f;
    targetWindSpeed  = 3.6f;
    windMomentum     = 0.0f;    // 풍속 변화의 관성 성분

    // 난류/위상 변수
    spectralEnergyBuf = 0.0f;   // Von Kármán 난류에 의해 계산된 현재 풍속 기여분
    spectralPhaseAcc  = 0.0f;   // 난류 위상 누적값
    lastUpdateMs      = millis(); // 마지막 업데이트 시간

    // 이벤트 상태 변수
    gustActive          = false;
    gustIntensity       = 1.0f;     // 현재 돌풍 배율 (1.0 = 정상)
    thermalActive       = false;
    thermalContribution = 0.0f; // 현재 열기포 가산값 (m/s)

    applyPresetCore(presetCode); // Preset 코어 값 (Base Wind/확률) 적용
    initPhaseFromBase();         // Phase 상태 초기화 (NORMAL 상태로 시작)
}

// ==================================================
// 메인 tick (CT10에서 주기 호출)
// ==================================================
/**
 * @brief 시뮬레이션의 모든 물리 및 상태를 업데이트하는 메인 함수.
 * 주기적으로 호출되어 풍속을 계산하고 PWM에 반영합니다.
 */
void CL_S10_Simulation::tick() {
    // [스레드 안전성]: Critical Section 시작 (멀티 코어 환경에서 상태 변수 동시 접근 방지)
    portENTER_CRITICAL(&_simMutex); 

    if (!active){
        portEXIT_CRITICAL(&_simMutex); // 시뮬레이션 비활성 시 바로 종료
        return;
    }

    unsigned long   v_now        = millis();
    static uint32_t s_jitterSeed = 0;

    // 업데이트 주기에 랜덤 지터 (40ms + 0~60ms)를 적용하여 불규칙성 추가 (자연스러운 떨림 유도)
    uint32_t v_interval = 40u + (s_jitterSeed % 60u);
    if (v_now - lastUpdateMs < v_interval){
        portEXIT_CRITICAL(&_simMutex); 
        return;
    }
    
    s_jitterSeed = esp_random(); // 다음 지터 생성을 위한 시드 갱신

    // 시간 델타 (Delta Time, v_dt) 계산
    float v_dt   = (v_now - lastUpdateMs) / 1000.0f;
    // 시간 델타 상한선 설정 (오류 방지 및 최대 0.5초로 제한)
    v_dt = A10_clampf(v_dt, 0.001f, 0.5f); 
    lastUpdateMs = v_now;

    float             v_prevWind  = currentWindSpeed;
    T_A10_WindPhase_t v_prevPhase = phase;
    updatePhase(); // Phase(CALM/NORMAL/STRONG) 변화 로직 실행

    // Phase 변화 또는 급격한 풍속 변화(2.0f m/s 초과) 시 WebAPI 브로드캐스트 요청
    float v_delta = fabsf(currentWindSpeed - v_prevWind);
    if (phase != v_prevPhase || v_delta > 2.0f) {
        float        v_avg = _getAvgWindFast(); // 최근 평균 풍속
        JsonDocument v_doc;
        JsonObject   o = v_doc["sim"].to<JsonObject>();
        // 현재 상태 정보를 JSON에 담아 WebAPI로 전송 요청
        o["phase"]     = g_A10_WEATHER_PHASE_NAMES_Arr[(uint8_t)phase];
        o["avgWind"]   = v_avg;
        o["target"]    = targetWindSpeed;
        o["samples"]   = historyCount;
        o["delta"]     = v_delta;
        SC10_broadcastChart(v_doc, true);    // diffOnly = true: 실시간 효율 전송
        SC10_markDirty("chart");             // 차트 데이터 갱신 알림
    }

    // ---- 내부 물리 계산 ----
    calcTurb(v_dt);             // Von Kármán 난류 성분 계산 (spectralEnergyBuf 갱신)
    calcThermalEnvelope();      // 열기포 포락선(Envelope) 갱신 (thermalContribution 갱신)
    updateGust();               // 돌풍 발생 확률 및 상태 갱신 (gustIntensity 갱신)
    updateThermal();            // 열기포 발생 확률 및 상태 갱신

    // 목표 풍속으로 점진 수렴 + 관성 반영
    float v_diff   = targetWindSpeed - currentWindSpeed;
    // 목표로 향하는 기본 변화량 (풍속 차이 * 변화율 * 시간 델타)
    float v_change = v_diff * windChangeRate * v_dt;
    
    // 모멘텀 (관성): 이전 모멘텀(85%) + 새로운 변화량(15%) = 부드러운 변화
    windMomentum   = windMomentum * 0.85f + v_change * 0.15f;
    // 모멘텀 변화량 제한 (최대/최소 +/- 0.5 m/s)
    windMomentum   = constrain(windMomentum, -0.5f, 0.5f);

    // 최종 풍속 = 현재 풍속 + 모멘텀(관성) + 난류 에너지
    float v_new      = currentWindSpeed + windMomentum + spectralEnergyBuf;
    // 최종 풍속을 물리적 범위(0.2 ~ 11.0 m/s) 내로 제한
    currentWindSpeed = constrain(v_new, 0.2f, 11.0f); 

    // 목표 풍속 재생성 주기 결정 로직 (풍속이 목표에 가까워졌는지 확인)
    float v_th = 0.5f + (currentWindSpeed / 20.0f); // 현재 풍속에 따라 허용 오차 동적 설정
    if (fabsf(v_diff) < v_th) {
        // 목표에 가까워지면 30% 확률로 목표 재설정
        if (A10_randRange(0.0f, 100.0f) < 30.0f)
            generateTarget();
    } else {
        // 목표에서 멀면 6% 확률로 목표 재설정 (목표를 향해 수렴을 유도)
        if (A10_randRange(0.0f, 100.0f) < 6.0f)
            generateTarget();
    }

    // PWM 제어 반영: 풍속 -> PWM Duty로 변환
    // 기본 선형 변환 (10% + 10 * Speed, m/s -> %)
    float v_pwmPct = currentWindSpeed * 10.0f + 10.0f;
    // 돌풍 영향 적용 (배율)
    v_pwmPct *= gustIntensity;
    // 열기포 영향 적용 (가산)
    v_pwmPct += thermalContribution * 5.0f;
    applyFan(v_pwmPct); // 최종 Duty를 PWM 모듈에 전달

    // 풍속 히스토리 갱신 (60초 평균용 순환 버퍼)
    _updateWindHistory(currentWindSpeed);

    // 차트 샘플링 (기본 1Hz, 돌풍/열기포 시 2Hz)
    v_interval = (gustActive || thermalActive) ? 500UL : 1000UL;
    if (millis() - s_lastChartLogMs > v_interval) {
        // 120개 초과 시 가장 오래된 샘플 제거 (FIFO)
        if (s_chartBuffer.size() >= 120)
            s_chartBuffer.pop_front(); 

        // 차트 엔트리 생성 및 버퍼에 추가
        ST_ChartEntry v_e{};
        v_e.timestamp        = millis();
        v_e.wind_speed       = currentWindSpeed;
        v_e.pwm_duty         = _pwm ? _pwm->P10_getDutyPercent() : 0.0f;
        v_e.intensity        = userIntensity;
        v_e.variability      = userVariability;
        v_e.turbulence_sigma = turbSigma;
        v_e.preset_index     = (uint8_t)A10_getPresetIndexByCode(presetCode);
        v_e.gust_active      = gustActive;
        v_e.thermal_active   = thermalActive;
        s_chartBuffer.push_back(v_e);

        s_lastChartLogMs = millis();
    }

    portEXIT_CRITICAL(&_simMutex); // Critical Section 종료
}

// ==================================================
// 해석 결과 적용: resolveWindParams → 여기 호출
// ==================================================
/**
 * @brief WindParam 해석 결과(ST_A10_ResolvedWind_t)를 시뮬레이션 파라미터에 적용합니다.
 */
void CL_S10_Simulation::applyResolvedWind(const ST_A10_ResolvedWind_t& p_resolved) {
    // 코드명 복사
    memset(presetCode, 0, sizeof(presetCode));
    memset(styleCode, 0, sizeof(styleCode));
    strlcpy(presetCode, p_resolved.presetCode, sizeof(presetCode));
    strlcpy(styleCode, p_resolved.styleCode, sizeof(styleCode));

    // 사용자 파라미터 복사 및 제한 (Constrain)
    userIntensity   = constrain(p_resolved.wind_intensity, 0.0f, 100.0f);
    userVariability = constrain(p_resolved.wind_variability, 0.0f, 100.0f);
    userGustFreq    = constrain(p_resolved.gust_frequency, 0.0f, 100.0f);
    fanLimitPct     = constrain(p_resolved.fan_limit, 0.0f, 100.0f);
    minFanPct       = constrain(p_resolved.min_fan, 0.0f, 100.0f);

    // 물리 파라미터 복사 및 제한 (최소값 설정)
    turbLenScale    = max(1.0f, p_resolved.turbulence_length_scale);
    turbSigma       = max(0.0f, p_resolved.turbulence_intensity_sigma);
    thermalStrength = max(1.0f, p_resolved.thermal_bubble_strength);
    thermalRadius   = max(0.0f, p_resolved.thermal_bubble_radius);

    // Preset 코드 기반 물리 상수 (baseMin/Max, 확률 등) 재설정
    applyPresetCore(presetCode);

    // variability 기반 목표 풍속 변화율(windChangeRate) 재설정
    // variability가 높을수록 목표 풍속으로 빠르게 수렴하게 함 (잦은 변화)
    float v_varNorm = userVariability / 100.0f;  // 0~1 정규화
    windChangeRate  = constrain(0.10f + v_varNorm * 0.20f, 0.06f, 0.34f);

    initPhaseFromBase(); // Phase 상태 (NORMAL) 초기화
    
    // 시뮬레이션 상태 재설정
    active              = true;
    gustActive          = false;
    thermalActive       = false;
    gustIntensity       = 1.0f;
    thermalContribution = 0.0f;

    generateTarget(); // 새로운 목표 풍속 생성
}
