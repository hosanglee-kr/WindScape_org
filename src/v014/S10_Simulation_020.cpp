/*
 * ------------------------------------------------------
 * 소스명 : S10_Simulation_020.cpp
 * 모듈약어 : S10
 * 모듈명 : Smart Nature Wind 풍속 시뮬레이션 Manager (v019, Full)
 * ------------------------------------------------------
 * 기능 요약:
 * - CL_S10_Simulation 클래스의 구현부
 * - Von Kármán 난류 모델, 관성, 돌풍/열기포 확률 모델 구현
 * ------------------------------------------------------
 */

#include "S10_Simulation_019.h" // 해당 클래스 헤더 파일 포함

// 외부 종속성 헤더 포함 (외부에서 제공되어야 함: 시스템 상수, 설정, 로그, PWM 제어)
#include "A10_Const_015.h"
#include "C10_ConfigManager_024.h"
#include "D10_Logger_016.h"
#include "P10_PWM_ctrl_014.h"

// ------------------------------------------------------
// 정적 멤버 정의 (클래스 인스턴스와 무관하게 유지되는 공유 데이터)
// ------------------------------------------------------
// 차트 데이터를 저장하는 순환 버퍼 (최대 120개 샘플 = 2분 분량)
std::deque<CL_S10_Simulation::ST_ChartEntry> CL_S10_Simulation::s_chartBuffer;
// 차트 로그를 기록한 마지막 시간 (Hz 제어용)
unsigned long                                CL_S10_Simulation::s_lastChartLogMs    = 0;
// 차트 JSON을 웹으로 전송한 마지막 시간 (API 부하 제어용)
unsigned long                                CL_S10_Simulation::s_lastChartSampleMs = 0;


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

// ==================================================
// JSON Export (현재 시뮬레이션 상태)
// ==================================================
/**
 * @brief 현재 시뮬레이션 상태 변수들을 JSON Object에 직렬화합니다.
 */
void CL_S10_Simulation::toJson(JsonObject& p_obj) {
    portENTER_CRITICAL(&_simMutex); // 상태 읽기 중 변수 변경 방지
    
    p_obj["active"]        = active;
    p_obj["phase"]         = g_A10_WEATHER_PHASE_NAMES_Arr[(uint8_t)phase];
    p_obj["windSpeed"]     = currentWindSpeed;
    p_obj["targetWind"]    = targetWindSpeed;
    p_obj["gustActive"]    = gustActive;
    p_obj["thermalActive"] = thermalActive;
    p_obj["pwmDuty"]       = _pwm ? _pwm->P10_getDutyPercent() : 0.0f;
    p_obj["presetCode"]    = presetCode;
    p_obj["styleCode"]     = styleCode;
    p_obj["intensity"]     = userIntensity;
    p_obj["variability"]   = userVariability;
    p_obj["gustFreq"]      = userGustFreq;
    p_obj["fan_limit"]     = fanLimitPct;
    p_obj["min_fan"]       = minFanPct;
    p_obj["turbSigma"]     = turbSigma;
    p_obj["turbScale"]     = turbLenScale;
    p_obj["thermalPower"]  = thermalStrength;
    p_obj["thermalRadius"] = thermalRadius;

    portEXIT_CRITICAL(&_simMutex);
}

// ==================================================
// 차트 데이터 JSON Export (/api/sim/chart)
// ==================================================
/**
 * @brief 차트 버퍼(s_chartBuffer)의 내용을 JSON Array로 직렬화합니다.
 * @param p_doc JSON 문서
 * @param p_diffOnly true인 경우, 마지막 1개 샘플만 전송 (WebSocket용)
 */
void CL_S10_Simulation::toChartJson(JsonDocument& p_doc, bool p_diffOnly) {
    // JSON 구조: p_doc["sim"]["chart"] 배열
    JsonArray arr = p_doc["sim"]["chart"].to<JsonArray>();

    // 메타 정보 추가 (차트의 현재 상태)
    JsonObject meta = p_doc["sim"]["meta"].to<JsonObject>();
    meta["phase"]   = g_A10_WEATHER_PHASE_NAMES_Arr[(uint8_t)phase];
    meta["avgWind"] = _getAvgWindFast();
    meta["gust"]    = gustActive;
    meta["thermal"] = thermalActive;
    meta["samples"] = historyCount;

    // WebAPI 부하 감소를 위해 Full Dump는 10초 간격으로 제한
    if (millis() - s_lastChartSampleMs < 10000UL)
        return;
    s_lastChartSampleMs = millis();

    if (s_chartBuffer.empty())
        return;

    if (p_diffOnly) {
        // diffOnly 모드: 마지막 1개 샘플만 전송 (WebSocket 실시간 업데이트용)
        const ST_ChartEntry& e  = s_chartBuffer.back();
        JsonObject           jo = arr.add<JsonObject>();
        jo["ts"]                = e.timestamp / 1000UL;
        jo["wind"]              = e.wind_speed;
        jo["pwm"]               = e.pwm_duty;
        jo["gust"]              = e.gust_active;
        jo["thermal"]           = e.thermal_active;
        jo["phase"]             = g_A10_WEATHER_PHASE_NAMES_Arr[(uint8_t)phase];
        jo["avgWind"]           = _getAvgWindFast();
        jo["samples"]           = historyCount;
        return;
    }

    // 기본 모드: 전체 chartBuffer 전송 (Full Dump)
    for (const auto& e : s_chartBuffer) {
        JsonObject jo = arr.add<JsonObject>();
        jo["ts"]      = e.timestamp / 1000UL;
        jo["wind"]    = e.wind_speed;
        jo["pwm"]     = e.pwm_duty;
        jo["gust"]    = e.gust_active;
        jo["thermal"] = e.thermal_active;
    }

    p_doc["sim"]["chartCount"] = (int)s_chartBuffer.size();
}

// ==================================================
// 내부 구현부
// ==================================================

/**
 * @brief 최종 계산된 풍속 기반 Duty Percent를 PWM 모듈에 적용합니다.
 * 사용자 Intensity, Min/Limit 값을 반영합니다.
 */
void CL_S10_Simulation::applyFan(float p_pct) {
    if (!_pwm)
        return;

    float v_req   = p_pct / 100.0f; // 요청된 Duty (0~1)
    float v_limit = fanLimitPct / 100.0f;
    float v_min   = minFanPct / 100.0f;
    float v_int   = userIntensity / 100.0f;

    // 팬 전원 비활성 또는 Intensity가 0에 가까우면 팬 정지
    if (!fanPowerEnabled || v_int <= 0.01f) {
        _pwm->P10_setDutyPercent(0.0f);
        return;
    }

    if (active) {
        v_req *= v_int; // 사용자 Intensity 적용 (전체 출력 배율)
    }

    // Min/Limit 값 적용
    if (v_req < v_min)
        v_req = v_min;
    if (v_req > v_limit)
        v_req = v_limit;

    _pwm->P10_setDutyPercent(v_req * 100.0f); // PWM 모듈에 최종 % 전달
}

/**
 * @brief Preset 코드에 따라 기본 스펙(Base Wind, 확률 등)을 설정합니다.
 */
void CL_S10_Simulation::applyPresetCore(const char* p_code) {
    char v_code[24];
    memset(v_code, 0, sizeof(v_code));
    if (p_code && p_code[0]) {
        strlcpy(v_code, p_code, sizeof(v_code));
    } else {
        strlcpy(v_code, "OCEAN", sizeof(v_code));
    }

    // 공통 기본값 (OCEAN 기준)
    baseMinWind     = 1.8f;
    baseMaxWind     = 5.5f;
    gustProbBase    = 0.040f;
    gustStrengthMax = 2.10f;
    thermalFreqBase = 0.022f;

    auto eq = [](const char* a, const char* b) -> bool {
        return (strcasecmp(a, b) == 0); // 대소문자 무시 비교
    };

    // Preset Code에 따른 개별 상수 설정
    if (eq(v_code, "COUNTRY") || eq(v_code, "COUNTRY_BREEZE") || eq(v_code, "COUNTRY_B")) {
        baseMinWind     = 0.7f;
        baseMaxWind     = 3.4f;
        gustProbBase    = 0.006f; // 낮은 돌풍 확률
        gustStrengthMax = 1.35f;
        thermalFreqBase = 0.015f;
    } else if (eq(v_code, "MEDITERRANEAN")) {
        baseMinWind     = 1.6f;
        baseMaxWind     = 3.8f;
        gustProbBase    = 0.012f;
        gustStrengthMax = 1.55f;
        thermalFreqBase = 0.035f;
    } else if (eq(v_code, "OCEAN")) {
        // 기본값 유지
    } else if (eq(v_code, "MOUNTAIN")) {
        baseMinWind     = 2.2f;
        baseMaxWind     = 7.5f;
        gustProbBase    = 0.045f;
        gustStrengthMax = 2.20f;
        thermalFreqBase = 0.028f;
    } else if (eq(v_code, "PLAINS")) {
        baseMinWind     = 4.0f;
        baseMaxWind     = 8.8f;
        gustProbBase    = 0.070f;
        gustStrengthMax = 2.40f;
        thermalFreqBase = 0.018f;
    } else if (eq(v_code, "HARBOR_BREEZE") || eq(v_code, "HARBOUR_BREEZE")) {
        baseMinWind     = 2.25f;
        baseMaxWind     = 5.35f;
        gustProbBase    = 0.025f;
        gustStrengthMax = 1.80f;
        thermalFreqBase = 0.026f;
    } else if (eq(v_code, "FOREST_CANOPY")) {
        baseMinWind     = 1.35f;
        baseMaxWind     = 4.00f;
        gustProbBase    = 0.010f;
        gustStrengthMax = 1.50f;
        thermalFreqBase = 0.012f;
    } else if (eq(v_code, "URBAN_SUNSET")) {
        baseMinWind     = 1.80f;
        baseMaxWind     = 4.90f;
        gustProbBase    = 0.030f;
        gustStrengthMax = 2.00f;
        thermalFreqBase = 0.020f;
    } else if (eq(v_code, "TROPICAL_RAIN")) {
        baseMinWind     = 3.15f;
        baseMaxWind     = 8.05f;
        gustProbBase    = 0.060f;
        gustStrengthMax = 2.20f;
        thermalFreqBase = 0.038f;
    } else if (eq(v_code, "DESERT_NIGHT")) {
        baseMinWind     = 0.90f;
        baseMaxWind     = 3.10f;
        gustProbBase    = 0.005f;
        gustStrengthMax = 1.30f;
        thermalFreqBase = 0.008f;
    }
}

/**
 * @brief Preset 기반으로 Phase 상태를 초기화합니다.
 */
void CL_S10_Simulation::initPhaseFromBase() {
    phase         = EN_A10_WEATHER_PHASE_NORMAL; // NORMAL 상태로 시작
    phaseStartSec = millis() / 1000.0f;

    float v_span = baseMaxWind - baseMinWind;
    if (v_span < 0.5f)
        v_span = 0.5f;

    // Normal Phase의 Min/Max Wind를 Base Wind의 Span을 기준으로 설정
    phaseMinWind     = baseMinWind + v_span * 0.15f;
    phaseMaxWind     = baseMinWind + v_span * 0.85f;
    phaseDurationSec = 120.0f; // 초기 Phase 지속 시간

    // 현재 풍속 및 목표 풍속을 초기 Phase의 중간값으로 설정
    float v_mid       = (baseMinWind + baseMaxWind) * 0.5f;
    currentWindSpeed  = v_mid;
    targetWindSpeed   = v_mid;
    spectralEnergyBuf = 0.0f;
    spectralPhaseAcc  = 0.0f;
    windMomentum      = 0.0f;
}

/**
 * @brief Phase 전환 조건을 체크하고, 필요 시 Phase를 전환합니다.
 * Phase 전환 시, 새로운 Phase의 지속 시간과 풍속 범위를 설정합니다.
 */
void CL_S10_Simulation::updatePhase() {
    if (!active)
        return;

    float v_now = millis() / 1000.0f;
    // 현재 Phase 지속 시간(phaseDurationSec)이 지나지 않았으면 전환하지 않음
    if (v_now - phaseStartSec < phaseDurationSec)
        return;

    T_A10_WindPhase_t v_old = phase;
    float             v_r   = A10_getRandom01(); // 0.0 ~ 1.0 랜덤 값

    // 확률적 Phase 전환 로직
    if (v_old == EN_A10_WEATHER_PHASE_CALM) {
        // CALM -> 70% NORMAL, 30% STRONG
        phase = (v_r < 0.7f) ? EN_A10_WEATHER_PHASE_NORMAL : EN_A10_WEATHER_PHASE_STRONG;
    } else if (v_old == EN_A10_WEATHER_PHASE_STRONG) {
        // STRONG -> 70% NORMAL, 30% CALM
        phase = (v_r < 0.7f) ? EN_A10_WEATHER_PHASE_NORMAL : EN_A10_WEATHER_PHASE_CALM;
    } else { // NORMAL
        // NORMAL -> 40% CALM, 40% NORMAL, 20% STRONG
        if (v_r < 0.4f)
            phase = EN_A10_WEATHER_PHASE_CALM;
        else if (v_r < 0.8f)
            phase = EN_A10_WEATHER_PHASE_NORMAL;
        else
            phase = EN_A10_WEATHER_PHASE_STRONG;
    }

    phaseStartSec = v_now;

    float v_span = baseMaxWind - baseMinWind;
    if (v_span < 0.5f)
        v_span = 0.5f;

    // 새로운 Phase에 따른 Duration 및 풍속 범위 설정
    if (phase == EN_A10_WEATHER_PHASE_CALM) {
        phaseDurationSec = A10_randRange(90.0f, 210.0f); // 긴 지속 시간
        phaseMinWind     = baseMinWind;
        phaseMaxWind     = baseMinWind + v_span * 0.6f; // 낮은 풍속 범위
    } else if (phase == EN_A10_WEATHER_PHASE_NORMAL) {
        phaseDurationSec = A10_randRange(120.0f, 300.0f);
        phaseMinWind     = baseMinWind + v_span * 0.15f;
        phaseMaxWind     = baseMinWind + v_span * 0.85f;
    } else {  // STRONG
        phaseDurationSec = A10_randRange(60.0f, 150.0f); // 짧은 지속 시간
        phaseMinWind     = baseMinWind + v_span * 0.4f;
        phaseMaxWind     = baseMaxWind; // 높은 풍속 범위
    }

    // 최종 풍속 범위 제한
    phaseMinWind = max(0.2f, phaseMinWind);
    phaseMaxWind = min(11.0f, phaseMaxWind);

    generateTarget(); // Phase 변경 후 새 목표 풍속 생성
}

/**
 * @brief Von Kármán 난류 모델을 12개 주파수 밴드의 합으로 근사하여 난류 성분을 계산합니다.
 * @param p_dt Delta Time (초)
 */
void CL_S10_Simulation::calcTurb(float p_dt) {
    if (!active)
        return;

    float v_L     = max(1.0f, turbLenScale); // 난류 길이 스케일 (L)
    float v_sigma = max(0.0f, turbSigma);    // 난류 세기 (표준편차 σ)
    float v_U     = max(0.1f, currentWindSpeed); // 현재 평균 풍속 (U)

    float v_sum = 0.0f; // 12개 밴드의 난류 성분 합

    // 12개 주파수 밴드의 합으로 난류 스펙트럼 근사
    for (int v_i = 1; v_i <= 12; v_i++) {
        float v_n   = (float)v_i * 0.1f; // 주파수 인덱스
        float v_f   = v_n * v_U / v_L;   // 주파수 (f)
        float v_fLU = v_f * v_L / v_U;   // 무차원 주파수 (fL/U)

        // E(f) * df 를 근사하여 각 주파수 밴드의 에너지 기여분 계산
        float v_term  = 70.8f * v_fLU * v_fLU;
        // Von Kármán 스펙트럼 밀도 S(f) 공식의 분자
        float v_numer = 4.0f * v_sigma * v_sigma * (v_L / v_U) * (1.0f + v_term);
        // Von Kármán 스펙트럼 밀도 S(f) 공식의 분모
        float v_denom = powf(1.0f + v_term, 5.0f / 6.0f);
        float v_S     = v_numer / v_denom; // 스펙트럼 에너지 밀도

        float v_phaseRate = 2.0f * (float)M_PI * v_f;
        float v_phaseInc  = v_phaseRate * p_dt;
        // 위상: 누적 위상 + 현재 증가량 + 랜덤 노이즈
        float v_phase     = spectralPhaseAcc * (float)v_i + v_phaseInc + A10_randRange(-0.1f, 0.1f); 

        float v_bandWidth = 0.083f; // 1/12 (주파수 밴드 폭)
        // 진폭 (A) = sqrt(2 * S(f) * df)
        float v_amp       = sqrtf(2.0f * v_S * v_bandWidth); 

        v_sum += v_amp * sinf(v_phase); // 사인파 형태로 총 난류 성분 합산
    }

    // 위상 누적 (다음 계산을 위해)
    spectralPhaseAcc += p_dt * 0.5f;
    if (spectralPhaseAcc > 2.0f * (float)M_PI) {
        spectralPhaseAcc -= 2.0f * (float)M_PI;
    }

    // 난류 에너지 버퍼에 시계열 관성 적용 (Exponential smoothing)
    // turbTimeScale에 따라 이전 값을 얼마나 유지할지 결정
    float v_corr      = expf(-p_dt / turbTimeScale);
    // 이전 값 (v_corr) + 새 값 (1-v_corr) 혼합
    spectralEnergyBuf = spectralEnergyBuf * v_corr + v_sum * (1.0f - v_corr);
}

/**
 * @brief 열기포가 활성화되었을 때, 시간 경과에 따른 가산 기여도(Envelope)를 계산합니다.
 */
void CL_S10_Simulation::calcThermalEnvelope() {
    if (!active || !thermalActive) {
        return;
    }

    float v_t   = millis() / 1000.0f;
    float v_age = v_t - thermalStartSec;

    // 지속 시간 초과 시 비활성화
    if (v_age >= thermalDuration) {
        thermalActive       = false;
        thermalContribution = 0.0f;
        return;
    }

    float v_prog = v_age / thermalDuration; // 진행률 (0~1)
    float v_env  = 0.0f; // 포락선 값

    // 포락선 모양: 시작(점진 증가) - 중간(최대치 유지 + 지터) - 끝(점진 감소)
    if (v_prog < 0.2f) {
        // 증가 구간 (0.0 ~ 0.2)
        float v_r = v_prog / 0.2f;
        v_env     = 1.0f - powf(1.0f - v_r, 2.0f); // Ease-in
    } else if (v_prog < 0.6f) {
        // 최대치 유지 구간 (0.2 ~ 0.6)
        v_env = 1.0f;
        v_env += sinf(v_age * (0.8f + (float)phase * 0.2f) * 2.0f * (float)M_PI) * 0.15f; // 중간 지터
    } else {
        // 감소 구간 (0.6 ~ 1.0)
        float v_r = (v_prog - 0.6f) / 0.4f;
        v_env     = 1.0f - powf(v_r, 1.3f); // Ease-out
    }

    if (v_env < 0.0f)
        v_env = 0.0f;

    float v_strength    = max(1.0f, thermalStrength);
    // 열기포 강도(1.0 이상) 기반으로 최종 기여도 (가산값) 계산
    thermalContribution = (v_strength - 1.0f) * v_env;
}

/**
 * @brief 돌풍 발생 조건을 체크하고, 활성화 시 돌풍 강도(gustIntensity)를 갱신합니다.
 */
void CL_S10_Simulation::updateGust() {
    if (!active)
        return;

    float v_nowSec = millis() / 1000.0f;

    if (gustActive) {
        // 돌풍 진행 중: 시간 경과에 따른 포락선(Envelope) 기반 강도 갱신
        float v_age = v_nowSec - gustStartSec;
        if (v_age >= gustDuration) {
            gustActive    = false;
            gustIntensity = 1.0f; // 강도 리셋
            return;
        }

        float v_prog = v_age / gustDuration;
        float v_env; // 포락선 값

        // 포락선 모양: 증가 - 최대치+지터 - 감소
        if (v_prog < 0.25f) {
            // 증가 구간 (0.0 ~ 0.25)
            float v_r = v_prog / 0.25f;
            v_env     = 1.0f - powf(1.0f - v_r, 1.8f);
        } else if (v_prog < 0.65f) {
            // 최대치 유지 구간 (0.25 ~ 0.65)
            v_env = 1.0f;
            v_env += sinf(v_age * (1.5f + (float)phase * 0.5f)) * 0.08f; // 중간 지터
        } else {
            // 감소 구간 (0.65 ~ 1.0)
            float v_r = (v_prog - 0.65f) / 0.35f;
            v_env     = 1.0f - powf(v_r, 1.5f);
        }

        if (v_env < 0.0f)
            v_env = 0.0f;
        // 최대 강도(gustStrengthMax) 기반 최종 배율 계산
        gustIntensity = 1.0f + (gustStrengthMax - 1.0f) * v_env;
        return;
    }

    // 새로운 돌풍 트리거 (최소 500ms 간격)
    unsigned long v_nowMs = millis();
    if (v_nowMs - lastGustCheckMs < 500UL) {
        return;
    }
    lastGustCheckMs = v_nowMs;

    // 확률 계산: 기본 확률 * 사용자 주파수 * 풍속/Phase 가중치
    float v_base = gustProbBase;
    float v_user = userGustFreq / 100.0f; // 사용자 주파수 (0~1)
    // 풍속 가중치: 풍속이 빠를수록 발생 확률 증가
    float v_wfac = 1.0f + (currentWindSpeed / 8.9f) * 0.5f; 

    float v_pmul; // Phase에 따른 확률 배율
    if (phase == EN_A10_WEATHER_PHASE_CALM) {
        v_pmul = 0.3f * v_wfac; // CALM 시 확률 감소
    } else if (phase == EN_A10_WEATHER_PHASE_STRONG) {
        v_pmul = 2.2f * v_wfac; // STRONG 시 확률 크게 증가
    } else { // NORMAL
        v_pmul = 0.9f * v_wfac;
    }

    float v_p = v_base * v_user * v_pmul; // 최종 발생 확률

    if (A10_getRandom01() < v_p) {
        // 돌풍 발생
        gustActive   = true;
        gustStartSec = v_nowSec;

        float v_speedF = currentWindSpeed / 6.7f;

        // Phase에 따라 지속 시간 및 강도 랜덤 설정
        if (phase == EN_A10_WEATHER_PHASE_CALM) {
            gustDuration  = A10_randRange(3.0f, 8.0f);
            gustIntensity = A10_randRange(1.08f, 1.33f); // 약한 돌풍
        } else if (phase == EN_A10_WEATHER_PHASE_STRONG) {
            gustDuration  = A10_randRange(0.8f, 3.3f);
            gustIntensity = A10_randRange(1.3f,
                                            1.3f + 0.9f * (1.0f + v_speedF * 0.3f)); // 강한 돌풍
        } else { // NORMAL
            gustDuration  = A10_randRange(1.8f, 5.8f);
            gustIntensity = A10_randRange(1.15f,
                                            1.15f + 0.5f * (1.0f + v_speedF * 0.2f));
        }

        // 강도를 최대 제한값으로 제한
        if (gustIntensity > gustStrengthMax) {
            gustIntensity = gustStrengthMax;
        }
    }
}

/**
 * @brief 열기포 발생 조건을 체크하고, 발생 시 상태를 활성화합니다.
 */
void CL_S10_Simulation::updateThermal() {
    if (!active || thermalActive)
        return;

    unsigned long v_nowMs = millis();
    if (v_nowMs - lastThermalCheckMs < 700UL) {
        return;
    }
    lastThermalCheckMs = v_nowMs;

    // 확률 계산: 기본 확률 * 강도/풍속/Phase 가중치
    float v_strength = max(1.0f, thermalStrength);
    float v_wfac     = 1.0f + (currentWindSpeed / 8.0f) * 0.3f; // 풍속 가중치
    // Phase 가중치: CALM 시 확률 증가, STRONG 시 확률 감소 (열기포는 보통 약풍 시 발생)
    float v_phaseMul = (phase == EN_A10_WEATHER_PHASE_CALM) ? 1.2f
                                                             : (phase == EN_A10_WEATHER_PHASE_STRONG ? 0.7f : 1.0f);

    // 최종 발생 빈도 (freq) 계산
    float v_freq = thermalFreqBase * (0.6f + 0.4f * min(3.0f, max(0.5f, v_strength))) * v_wfac * v_phaseMul;

    if (A10_getRandom01() < v_freq) {
        // 열기포 발생
        thermalActive   = true;
        thermalStartSec = v_nowMs / 1000.0f;

        // 지속 시간 랜덤 설정
        float v_d = A10_randRange(8.0f, 14.0f);
        if (phase == EN_A10_WEATHER_PHASE_CALM) {
            v_d *= 1.3f; // CALM 시 지속 시간 증가
        } else if (phase == EN_A10_WEATHER_PHASE_STRONG) {
            v_d *= 0.8f; // STRONG 시 지속 시간 감소
        }
        thermalDuration = v_d;
    }
}

/**
 * @brief 새로운 목표 풍속(targetWindSpeed)을 생성합니다.
 * Phase의 풍속 범위와 중간값 바이어스를 적용합니다.
 */
void CL_S10_Simulation::generateTarget() {
    if (!active)
        return;

    float v_range = phaseMaxWind - phaseMinWind;
    if (v_range < 0.2f)
        v_range = 0.2f;

    // Phase 범위 내에서 1차 목표 생성
    float v_w    = phaseMinWind + A10_getRandom01() * v_range;
    float v_mid  = (phaseMinWind + phaseMaxWind) * 0.5f;
    float v_bias = A10_randRange(0.0f, 1.0f);

    // 중간값(v_mid)에 가깝게 끌어당겨 극단적인 값 방지 (바이어스 적용)
    // (v_w + v_mid * v_bias) / (1.0f + v_bias)
    v_w             = (v_w + v_mid * v_bias) / (1.0f + v_bias);
    targetWindSpeed = v_w;

    // variability, phase, 난류 스케일에 따라 풍속 변화율(windChangeRate) 결정
    float v_var = userVariability / 100.0f;
    float v_base;

    // Phase별 변화율 기본값 설정
    if (phase == EN_A10_WEATHER_PHASE_CALM) {
        v_base = 0.08f + v_var * 0.12f; // CALM 시 변화율 낮음
    } else if (phase == EN_A10_WEATHER_PHASE_STRONG) {
        v_base = 0.25f + v_var * 0.35f; // STRONG 시 변화율 높음
    } else { // NORMAL
        v_base = 0.15f + v_var * 0.25f;
    }

    // 풍속과 난류 길이 스케일에 따라 변화율 가중치 추가
    float v_U      = max(0.1f, currentWindSpeed);
    float v_tscale = turbLenScale / v_U;
    v_base *= (1.0f + v_tscale * 0.1f);

    // 최종 변화율에 랜덤 지터 적용 및 제한
    windChangeRate = constrain(v_base * A10_randRange(0.7f, 1.7f),
                                0.04f, 0.5f);
}

// --------------------------------------------------
// 최근 풍속 이력 관리 (순환 버퍼 기반)
// --------------------------------------------------
/**
 * @brief 현재 풍속을 순환 버퍼(history)에 저장하고 평균 풍속 캐시를 갱신합니다.
 */
void CL_S10_Simulation::_updateWindHistory(float p_speed) {
    history[historyIndex] = p_speed;
    historyIndex          = (historyIndex + 1) % HISTORY_SIZE; // 인덱스 순환
    if (historyCount < HISTORY_SIZE)
        historyCount++; // 카운트 증가

    // 평균 풍속 캐시 갱신
    float v_sum = 0.0f;
    for (uint8_t i = 0; i < historyCount; i++) v_sum += history[i];
    avgWindCached = v_sum / (float)historyCount;
}

// --------------------------------------------------
// 캐시된 평균 풍속 반환 (O(1))
// --------------------------------------------------
/**
 * @brief 캐시된 평균 풍속을 반환합니다. (O(1) 접근)
 */
float CL_S10_Simulation::_getAvgWindFast() const {
    return (historyCount > 0) ? avgWindCached : currentWindSpeed;
}
