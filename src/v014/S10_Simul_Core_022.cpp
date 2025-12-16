/*
 * ------------------------------------------------------
 * 소스명 : S10_Simul_Core_022.cpp
 * 모듈약어 : S10
 * 모듈명 : Smart Nature Wind 풍속 시뮬레이션 Manager (v019, Full)
 * ------------------------------------------------------
 * 기능 요약:
 * - CL_S10_Simulation 클래스의 구현부
 * - Von Kármán 난류 모델, 관성, 돌풍/열기포 확률 모델 구현
 * ------------------------------------------------------
 */

#include "S10_Simul_021.h" // 해당 클래스 헤더 파일 포함

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
    // fanConfig 스냅샷 초기화
    _fanCfgSnap = nullptr;
    
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
    // fanConfig 스냅샷 초기화
    _fanCfgSnap = nullptr;
    
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
    // fanConfig 스냅샷 초기화
    _fanCfgSnap = nullptr;
    
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

    // ---- (A) 락 밖에서 브로드캐스트 수행을 위한 로컬 스냅샷 ----
    // (주의) 락 안에서 JsonDocument 생성/SC10_broadcast 호출 금지
    bool            v_needBroadcast = false;
    float           v_bc_avgWind    = 0.0f;
    float           v_bc_target     = 0.0f;
    uint8_t         v_bc_samples    = 0;
    float           v_bc_delta      = 0.0f;
    T_A10_WindPhase_t v_bc_phase    = EN_A10_WEATHER_PHASE_NORMAL;

    // [스레드 안전성] S10 내부 상태 변수 보호용 Critical Section 시작
    portENTER_CRITICAL(&_simMutex);

    // 1) 비활성 상태면 즉시 종료
    if (!active) {
        portEXIT_CRITICAL(&_simMutex);
        return;
    }

    // [fanConfig 스냅샷] 이번 tick에서 사용할 fanConfig 포인터를 1회 캡처
    // - 락 안에서 수행(일관성)
    // - system이 null이면 fanConfig도 null 처리 → applyFanConfigCurve가 기본 동작으로 처리해야 함
    _fanCfgSnap = nullptr;
    if (g_A10_config_root.system != nullptr) {
        _fanCfgSnap = &g_A10_config_root.system->hw.fanConfig;
    }

    // 2) 이번 tick의 기준 시간은 "딱 1번만" 읽어서 끝까지 재사용
    //    - 락 안에서 millis()를 여러 번 부르면 미세한 시간 튐으로 샘플링/interval 판단이 흔들릴 수 있음
    const unsigned long v_now = millis();

    // 3) 업데이트 주기 지터(불규칙성) 적용
    //    - 자연스러운 떨림을 위해 40ms + (0~59ms) 범위의 랜덤 지터
    static uint32_t s_jitterSeed = 0;
    const uint32_t  v_minIntervalMs = 40u + (s_jitterSeed % 60u);

    // 아직 업데이트할 시간이 아니면 종료
    if (v_now - lastUpdateMs < v_minIntervalMs) {
        portEXIT_CRITICAL(&_simMutex);
        return;
    }

    // 다음 지터를 위한 seed 갱신
    s_jitterSeed = esp_random();

    // 4) delta time 계산
    float v_dt = (v_now - lastUpdateMs) / 1000.0f;
    v_dt = A10_clampf(v_dt, 0.001f, 0.5f); // 안정성 상한/하한
    lastUpdateMs = v_now;

    // 5) 변화 감지를 위해 "이전 상태" 기록
    const float           v_prevWind  = currentWindSpeed;
    const T_A10_WindPhase_t v_prevPhase = phase;

    // 6) Phase 업데이트 (지속시간 만료 시 확률 전환)
    updatePhase();

    // ---- 내부 물리 계산 ----
    calcTurb(v_dt);
    calcThermalEnvelope();
    updateGust();
    updateThermal();

    // 7) 목표 풍속(target)으로 점진 수렴 + 관성 적용
    const float v_diff   = targetWindSpeed - currentWindSpeed;
    const float v_change = v_diff * windChangeRate * v_dt;

    windMomentum = windMomentum * 0.85f + v_change * 0.15f;
    windMomentum = constrain(windMomentum, -0.5f, 0.5f);

    // 8) 최종 풍속 업데이트 (관성 + 난류)
    const float v_new = currentWindSpeed + windMomentum + spectralEnergyBuf;
    currentWindSpeed  = constrain(v_new, 0.2f, 11.0f);

    // 9) 목표 재생성(확률) - 현재 목표에 가까우면 더 자주 바꿈
    const float v_th = 0.5f + (currentWindSpeed / 20.0f);
    if (fabsf(v_diff) < v_th) {
        if (A10_randRange(0.0f, 100.0f) < 30.0f) {
            generateTarget();
        }
    } else {
        if (A10_randRange(0.0f, 100.0f) < 6.0f) {
            generateTarget();
        }
    }

    // 10) 풍속 -> PWM duty 변환 + 이벤트 영향 적용
    float v_pwmPct = currentWindSpeed * 10.0f + 10.0f;  // 기본 선형 매핑
    v_pwmPct *= gustIntensity;                           // 돌풍 배율
    v_pwmPct += thermalContribution * 5.0f;              // 열기포 가산
    applyFan(v_pwmPct);

    // 11) 평균 풍속(history/avgWindCached) 갱신
    //     - 브로드캐스트 스냅샷은 반드시 이 이후에 잡아야 "이번 tick 결과"와 정합성이 맞음
    _updateWindHistory(currentWindSpeed);

    // 12) 브로드캐스트 조건 판단 및 스냅샷 생성 (락 안에서는 '값만 복사')
    //     - Phase 변경 또는 풍속 급변(2.0 m/s 초과) 시 전송 요청
    const float v_delta = fabsf(currentWindSpeed - v_prevWind);
    if (phase != v_prevPhase || v_delta > 2.0f) {
        v_needBroadcast = true;
        v_bc_avgWind    = _getAvgWindFast();   // (중요) 이제 막 갱신된 avgWindCached 기반
        v_bc_target     = targetWindSpeed;
        v_bc_samples    = historyCount;
        v_bc_delta      = v_delta;
        v_bc_phase      = phase;
    }

    // 13) 차트 샘플링(1Hz, 이벤트 중 2Hz) - 시간 기준은 v_now 재사용(일관성)
    const unsigned long v_chartIntervalMs = (gustActive || thermalActive) ? 500UL : 1000UL;
    if (v_now - s_lastChartLogMs > v_chartIntervalMs) {

        if (s_chartBuffer.size() >= 120) {
            s_chartBuffer.pop_front();
        }

        ST_ChartEntry v_e{};
        v_e.timestamp        = v_now; // millis() 재호출 금지: tick 기준 시간 사용
        v_e.wind_speed       = currentWindSpeed;
        v_e.pwm_duty         = _pwm ? _pwm->P10_getDutyPercent() : 0.0f;
        v_e.intensity        = userIntensity;
        v_e.variability      = userVariability;
        v_e.turbulence_sigma = turbSigma;
        v_e.preset_index     = (uint8_t)A10_getPresetIndexByCode(presetCode);
        v_e.gust_active      = gustActive;
        v_e.thermal_active   = thermalActive;

        s_chartBuffer.push_back(v_e);

        // (중요) s_lastChartLogMs도 v_now로 갱신 (일관된 시간축 유지)
        s_lastChartLogMs = v_now;
    }

    // Critical Section 종료
    portEXIT_CRITICAL(&_simMutex);

    // ---- (B) 락 밖에서 브로드캐스트 수행 ----
    if (v_needBroadcast) {
        JsonDocument v_doc;                     // JsonDocument 단일 타입 사용
        JsonObject   v_sim = v_doc["sim"].to<JsonObject>();

        v_sim["phase"]   = g_A10_WEATHER_PHASE_NAMES_Arr[(uint8_t)v_bc_phase];
        v_sim["avgWind"] = v_bc_avgWind;
        v_sim["target"]  = v_bc_target;
        v_sim["samples"] = v_bc_samples;
        v_sim["delta"]   = v_bc_delta;

        // (주의) 브로드캐스트/Dirty 마킹은 반드시 락 밖에서 처리
        SC10_broadcastChart(v_doc, true);
        SC10_markDirty("chart");
    }
}



// ==================================================
// 해석 결과 적용: resolveWindParams → 여기 호출
// ==================================================
/**
 * @brief WindParam 해석 결과(ST_A10_ResolvedWind_t)를 시뮬레이션 파라미터에 적용합니다.
 */
void CL_S10_Simulation::applyResolvedWind(const ST_A10_ResolvedWind_t& p_resolved) {

    // [스레드 안전성]
    // - tick()이 동시에 currentWindSpeed/phase/userIntensity 등을 읽고 쓰므로
    //   applyResolvedWind()는 반드시 _simMutex로 보호해야 합니다.
    portENTER_CRITICAL(&_simMutex);

    // [fanConfig 스냅샷] applyResolvedWind 호출 시점에도 1회 캡처
    _fanCfgSnap = nullptr;
    if (g_A10_config_root.system != nullptr) {
        _fanCfgSnap = &g_A10_config_root.system->hw.fanConfig;
    }

    // 1) preset/style 코드 복사 (안전 초기화 후 복사)
    memset(presetCode, 0, sizeof(presetCode));
    memset(styleCode,  0, sizeof(styleCode));
    strlcpy(presetCode, p_resolved.presetCode, sizeof(presetCode));
    strlcpy(styleCode,  p_resolved.styleCode,  sizeof(styleCode));

    // 2) 사용자 파라미터(0~100) 클램프
    //    - UI/웹/API 등 다양한 입력 경로에서 들어오기 때문에 항상 방어
    userIntensity   = constrain(p_resolved.wind_intensity,   0.0f, 100.0f);
    userVariability = constrain(p_resolved.wind_variability, 0.0f, 100.0f);
    userGustFreq    = constrain(p_resolved.gust_frequency,   0.0f, 100.0f);

    // fan limit/min도 클램프 후 "관계 보정(min <= limit)" 유지
    float v_limit = constrain(p_resolved.fan_limit, 0.0f, 100.0f);
    float v_min   = constrain(p_resolved.min_fan,   0.0f, 100.0f);
    if (v_min > v_limit) {
        v_min = v_limit; // 정책: min이 limit를 넘으면 min을 limit에 맞춘다
    }
    fanLimitPct = v_limit;
    minFanPct   = v_min;

    // 3) 물리 파라미터 최소값 보정
    //    - 길이 스케일은 1.0 이상, sigma는 0 이상
    //    - thermalStrength는 1.0 이상(1.0=영향 없음 기준), radius는 0 이상
    turbLenScale    = max(1.0f, p_resolved.turbulence_length_scale);
    turbSigma       = max(0.0f, p_resolved.turbulence_intensity_sigma);
    thermalStrength = max(1.0f, p_resolved.thermal_bubble_strength);
    thermalRadius   = max(0.0f, p_resolved.thermal_bubble_radius);

    // 4) Preset 코어 파라미터 재설정
    //    - presetCode에 따라 baseMin/baseMax/확률 등이 바뀌므로 반드시 재적용
    applyPresetCore(presetCode);

    // 5) variability -> windChangeRate 재계산
    //    - variability가 높을수록 목표 풍속으로 빨리 수렴(변화가 잦아짐)
    const float v_varNorm = userVariability / 100.0f; // 0~1
    windChangeRate = constrain(0.10f + v_varNorm * 0.20f, 0.06f, 0.34f);

    // 6) Phase/상태 초기화
    //    - Preset 변경 시 Phase 범위도 달라지므로 initPhaseFromBase()로 재시작
    initPhaseFromBase();

    // 7) 이벤트 상태 리셋 (새 파라미터 적용 시 깔끔하게 시작)
    active              = true;
    gustActive          = false;
    thermalActive       = false;
    gustIntensity       = 1.0f;
    thermalContribution = 0.0f;

    // 8) 새 목표 생성
    generateTarget();

    portEXIT_CRITICAL(&_simMutex);
}



/**
 * @brief 최종 계산된 풍속 기반 Duty Percent를 PWM 모듈에 적용합니다.
 * 사용자 Intensity, Min/Limit 값을 반영합니다.
 */

void CL_S10_Simulation::applyFan(float p_pct) {
    if (!_pwm) {
        return;
    }

    // 1) 요청 duty(%)를 0~1로 정규화
    float v_req01 = A10_clampf(p_pct, 0.0f, 100.0f) / 100.0f;

    // 2) 사용자 intensity(0~1)
    const float v_int01 = A10_clampf(userIntensity, 0.0f, 100.0f) / 100.0f;

    // 3) 팬 전원 OFF 또는 intensity가 거의 0이면 즉시 정지
    if (!fanPowerEnabled || v_int01 <= 0.01f) {
        _pwm->P10_setDutyPercent(0.0f);
        return;
    }

    // 4) 시뮬레이션이 active인 동안에만 intensity 스케일 적용
    //    - “논리적인 바람 세기”를 줄여도 min/limit/커브는 이후 단계에서 적용됨
    if (active) {
        v_req01 *= v_int01;
    }

    // 5) min/limit(%) -> 0~1 변환 + 관계 보정(min <= limit)
    float v_min01 = A10_clampf(minFanPct,   0.0f, 100.0f) / 100.0f;
    float v_max01 = A10_clampf(fanLimitPct, 0.0f, 100.0f) / 100.0f;

    if (v_min01 > v_max01) {
        // 정책: min이 limit를 넘으면 min을 limit에 맞춘다
        v_min01 = v_max01;
    }

    const ST_A10_FanConfig_t* v_fc = _fanCfgSnap;
    /*
    // 6) hw.fanConfig 포인터 스냅샷
    //    - system 포인터가 null일 수 있으므로 방어
    //    - applyFanConfigCurve()는 v_fc가 null이어도 “기본 커브”로 처리하도록 설계하는 것이 이상적
    const ST_A10_FanConfig_t* v_fc = nullptr;
    if (g_A10_config_root.system != nullptr) {
        v_fc = &g_A10_config_root.system->hw.fanConfig;
    }
    */

    // 7) 커브 적용: 논리 duty(0~1) -> 실제 PWM duty(0~1)
    //    - min/max 제한과 fan curve(저속 보정/선형/감마 등)를 함께 적용
    const float v_phy01 = _pwm->applyFanConfigCurve(v_fc, v_req01, v_min01, v_max01);

    // 8) PWM 모듈에 최종 %로 전달
    _pwm->P10_setDutyPercent(v_phy01 * 100.0f);
}

