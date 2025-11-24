/*
 * ------------------------------------------------------
 * 소스명 : S10_Simulation_020.cpp
 * 모듈약어 : S10
 * 모듈명 : Smart Nature Wind 풍속 시뮬레이션 Manager
 * ------------------------------------------------------
 * 기능 요약:
 * - CL_S10_Simulation 클래스의 구현부
 * - Von Kármán 난류 모델, 관성, 돌풍/열기포 확률 모델 구현
 * ------------------------------------------------------
 */

#include "S10_Simulation_020.h" // 해당 클래스 헤더 파일 포함

// 외부 종속성 헤더 포함
#include "A10_Const_015.h"
#include "C10_ConfigManager_024.h"
#include "D10_Logger_016.h"
#include "P10_PWM_ctrl_014.h"

// ------------------------------------------------------
// 정적 멤버 정의
// ------------------------------------------------------
std::deque<CL_S10_Simulation::ST_ChartEntry> CL_S10_Simulation::s_chartBuffer;
unsigned long                                CL_S10_Simulation::s_lastChartLogMs    = 0;
unsigned long                                CL_S10_Simulation::s_lastChartSampleMs = 0;


// ==================================================
// 초기화 / 정지 / 리셋
// ==================================================
void CL_S10_Simulation::begin(CL_P10_PWM& p_pwm) {
    _pwm = &p_pwm;
    resetDefaults();
    memset(history, 0, sizeof(history));
    historyIndex = 0;
    historyCount = 0;
    (void)esp_random();  // ESP32의 하드웨어 기반 랜덤 시드를 호출하여 지터 유도
    CL_D10_Logger::log(EN_L10_LOG_INFO, "[S10] begin()");
}

void CL_S10_Simulation::stop() {
    active           = false;
    phase            = EN_A10_WEATHER_PHASE_CALM;
    targetWindSpeed  = 0.0f;
    currentWindSpeed = 0.0f;
    if (_pwm) {
        _pwm->P10_setDutyPercent(0.0f); // PWM 0% 설정으로 팬 정지
    }
}

void CL_S10_Simulation::resetDefaults() {
    // [중략] 초기화 로직
    active          = false;
    fanPowerEnabled = true;

    strlcpy(presetCode, "OCEAN", sizeof(presetCode));
    strlcpy(styleCode, "BALANCE", sizeof(styleCode));

    userIntensity   = 70.0f;
    userVariability = 50.0f;
    userGustFreq    = 45.0f;
    minFanPct       = 10.0f;
    fanLimitPct     = 90.0f;

    turbLenScale    = 40.0f;
    turbSigma       = 0.5f;
    thermalStrength = 2.0f;
    thermalRadius   = 18.0f;

    baseMinWind     = 1.8f;
    baseMaxWind     = 5.5f;
    gustProbBase    = 0.040f;
    gustStrengthMax = 2.10f;
    thermalFreqBase = 0.022f;

    currentWindSpeed = 3.6f;
    targetWindSpeed  = 3.6f;
    windMomentum     = 0.0f;

    spectralEnergyBuf = 0.0f;
    spectralPhaseAcc  = 0.0f;
    lastUpdateMs      = millis();

    gustActive          = false;
    gustIntensity       = 1.0f;
    thermalActive       = false;
    thermalContribution = 0.0f;

    applyPresetCore(presetCode); // Preset 코어 값 적용
    initPhaseFromBase();         // Phase 상태 초기화
}

// ==================================================
// 메인 tick (CT10에서 주기 호출)
// ==================================================
void CL_S10_Simulation::tick() {
    portENTER_CRITICAL(&_simMutex); // [스레드 안전성] Critical Section 시작: 상태 변수 보호

    if (!active){
        portEXIT_CRITICAL(&_simMutex); // Critical Section 종료
        return;
    }

    unsigned long   v_now        = millis();
    static uint32_t s_jitterSeed = 0;

    // 업데이트 주기에 랜덤 지터 (40ms + 0~60ms) 적용하여 자연스러운 불규칙성 추가 (약 10~25Hz)
    uint32_t v_interval = 40u + (s_jitterSeed % 60u);
    if (v_now - lastUpdateMs < v_interval){
        portEXIT_CRITICAL(&_simMutex); 
        return;
    }
    
    s_jitterSeed = esp_random(); // 다음 지터 생성을 위한 시드 갱신

    // 시간 델타 (Delta Time, v_dt) 계산 및 상한선 설정 (0.5초 초과 방지)
    float v_dt   = (v_now - lastUpdateMs) / 1000.0f;
    v_dt = A10_clampf(v_dt, 0.001f, 0.5f);
    lastUpdateMs = v_now;

    float             v_prevWind  = currentWindSpeed;
    T_A10_WindPhase_t v_prevPhase = phase;
    updatePhase(); // Phase 변화 로직 실행

    // Phase 변화 또는 급격한 풍속 변화 (2.0f m/s) 시 WebAPI 브로드캐스트 요청
    float v_delta = fabsf(currentWindSpeed - v_prevWind);
    if (phase != v_prevPhase || v_delta > 2.0f) {
        // [중략] JSON 문서 생성 및 SC10_broadcastChart 호출
        float        v_avg = _getAvgWindFast();
        JsonDocument v_doc;
        JsonObject   o = v_doc["sim"].to<JsonObject>();
        o["phase"]     = g_A10_WEATHER_PHASE_NAMES_Arr[(uint8_t)phase];
        o["avgWind"]   = v_avg;
        o["target"]    = targetWindSpeed;
        o["samples"]   = historyCount;
        o["delta"]     = v_delta;
        SC10_broadcastChart(v_doc, true);    // diffOnly = true: 실시간 효율 전송
        SC10_markDirty("chart"); // 차트 데이터 갱신 알림
    }

    // ---- 내부 물리 계산 ----
    calcTurb(v_dt);             // 난류 성분 계산
    calcThermalEnvelope();      // 열기포 포락선(Envelope) 갱신
    updateGust();               // 돌풍 상태 갱신
    updateThermal();            // 열기포 상태 갱신

    // 목표 풍속으로 점진 수렴 + 관성 반영
    float v_diff   = targetWindSpeed - currentWindSpeed;
    float v_change = v_diff * windChangeRate * v_dt;
    
    // 모멘텀 (관성): 이전 모멘텀(85%) + 새로운 변화량(15%) = 부드러운 변화
    windMomentum   = windMomentum * 0.85f + v_change * 0.15f;
    windMomentum   = constrain(windMomentum, -0.5f, 0.5f);

    // 최종 풍속 = 현재 풍속 + 모멘텀 + 난류 에너지
    float v_new      = currentWindSpeed + windMomentum + spectralEnergyBuf;
    currentWindSpeed = constrain(v_new, 0.2f, 11.0f); // 최소/최대 풍속 제한

    // 목표 풍속 재생성 주기 결정 로직
    float v_th = 0.5f + (currentWindSpeed / 20.0f); // 풍속에 따라 허용 오차 동적 설정
    if (fabsf(v_diff) < v_th) {
        // 목표에 가까워지면 30% 확률로 목표 재설정
        if (A10_randRange(0.0f, 100.0f) < 30.0f)
            generateTarget();
    } else {
        // 목표에서 멀면 6% 확률로 목표 재설정
        if (A10_randRange(0.0f, 100.0f) < 6.0f)
            generateTarget();
    }

    // PWM 제어 반영: 풍속 -> PWM Duty로 변환
    float v_pwmPct = currentWindSpeed * 10.0f + 10.0f; // 기본 선형 변환
    v_pwmPct *= gustIntensity; // 돌풍 영향 (배율)
    v_pwmPct += thermalContribution * 5.0f; // 열기포 영향 (가산)
    applyFan(v_pwmPct); // 최종 Duty를 PWM 모듈에 전달

    _updateWindHistory(currentWindSpeed); // 풍속 히스토리 갱신 (60초 평균용)

    // 차트 샘플링 (기본 1Hz, 돌풍/열기포 시 2Hz)
    v_interval = (gustActive || thermalActive) ? 500UL : 1000UL;
    if (millis() - s_lastChartLogMs > v_interval) {
        if (s_chartBuffer.size() >= 120)
            s_chartBuffer.pop_front(); // 120개 초과 시 가장 오래된 샘플 제거 (FIFO)

        // [중략] ST_ChartEntry 생성 및 s_chartBuffer에 push_back
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
void CL_S10_Simulation::applyResolvedWind(const ST_A10_ResolvedWind_t& p_resolved) {
    // [중략] Resolved Wind 파라미터를 멤버 변수에 constrain 적용하여 복사
    // 코드명 저장 (정보용)
    memset(presetCode, 0, sizeof(presetCode));
    memset(styleCode, 0, sizeof(styleCode));
    strlcpy(presetCode, p_resolved.presetCode, sizeof(presetCode));
    strlcpy(styleCode, p_resolved.styleCode, sizeof(styleCode));

    userIntensity   = constrain(p_resolved.wind_intensity, 0.0f, 100.0f);
    userVariability = constrain(p_resolved.wind_variability, 0.0f, 100.0f);
    userGustFreq    = constrain(p_resolved.gust_frequency, 0.0f, 100.0f);
    fanLimitPct     = constrain(p_resolved.fan_limit, 0.0f, 100.0f);
    minFanPct       = constrain(p_resolved.min_fan, 0.0f, 100.0f);

    turbLenScale    = max(1.0f, p_resolved.turbulence_length_scale);
    turbSigma       = max(0.0f, p_resolved.turbulence_intensity_sigma);
    thermalStrength = max(1.0f, p_resolved.thermal_bubble_strength);
    thermalRadius   = max(0.0f, p_resolved.thermal_bubble_radius);

    applyPresetCore(presetCode); // 새 Preset 기반 물리 상수 재설정

    // variability 기반 변화율 (windChangeRate) 재설정 (변화율이 클수록 빠르게 수렴)
    float v_varNorm = userVariability / 100.0f;
    windChangeRate  = constrain(0.10f + v_varNorm * 0.20f, 0.06f, 0.34f);

    initPhaseFromBase(); // Phase 초기화
    
    // 시뮬레이션 상태 재설정
    active              = true;
    gustActive          = false;
    thermalActive       = false;
    gustIntensity       = 1.0f;
    thermalContribution = 0.0f;

    generateTarget(); // 새로운 목표 풍속 생성
}

// ==================================================
// JSON Export
// ==================================================
void CL_S10_Simulation::toJson(JsonObject& p_obj) {
    portENTER_CRITICAL(&_simMutex); // 상태 읽기 중 변수 변경 방지
    
    p_obj["active"]        = active;
    p_obj["phase"]         = g_A10_WEATHER_PHASE_NAMES_Arr[(uint8_t)phase];
    p_obj["windSpeed"]     = currentWindSpeed;
    // [중략] 나머지 상태 변수 JSON 직렬화
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
void CL_S10_Simulation::toChartJson(JsonDocument& p_doc, bool p_diffOnly) {
    // [중략] JSON 생성 로직
    JsonArray arr = p_doc["sim"]["chart"].to<JsonArray>();

    JsonObject meta = p_doc["sim"]["meta"].to<JsonObject>();
    meta["phase"]   = g_A10_WEATHER_PHASE_NAMES_Arr[(uint8_t)phase];
    meta["avgWind"] = _getAvgWindFast();
    meta["gust"]    = gustActive;
    meta["thermal"] = thermalActive;
    meta["samples"] = historyCount;

    // 10초 간격 전송 제한 (WebAPI 부하 감소)
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

// PWM 적용 (min/max/intensity 반영)
void CL_S10_Simulation::applyFan(float p_pct) {
    // [중략] Duty 계산 및 P10_setDutyPercent 호출
    if (!_pwm) return;

    float v_req   = p_pct / 100.0f; // 요청된 Duty (0~1)
    float v_limit = fanLimitPct / 100.0f;
    float v_min   = minFanPct / 100.0f;
    float v_int   = userIntensity / 100.0f;

    if (!fanPowerEnabled || v_int <= 0.01f) {
        _pwm->P10_setDutyPercent(0.0f);
        return;
    }

    if (active) {
        v_req *= v_int; // 사용자 Intensity 적용
    }

    // Min/Limit 값 적용
    if (v_req < v_min) v_req = v_min;
    if (v_req > v_limit) v_req = v_limit;

    _pwm->P10_setDutyPercent(v_req * 100.0f); // PWM 모듈에 최종 % 전달
}

// presetCode에 따라 기본 스펙 셋업
void CL_S10_Simulation::applyPresetCore(const char* p_code) {
    // [중략] Preset 코드에 따른 baseMinWind, gustProbBase 등 물리 상수 설정
    char v_code[24];
    memset(v_code, 0, sizeof(v_code));
    if (p_code && p_code[0]) {
        strlcpy(v_code, p_code, sizeof(v_code));
    } else {
        strlcpy(v_code, "OCEAN", sizeof(v_code));
    }

    // 공통 기본값
    baseMinWind     = 1.8f;
    baseMaxWind     = 5.5f;
    gustProbBase    = 0.040f;
    gustStrengthMax = 2.10f;
    thermalFreqBase = 0.022f;

    auto eq = [](const char* a, const char* b) -> bool {
        return (strcasecmp(a, b) == 0); // 대소문자 무시 비교
    };
    
    // Preset Code에 따른 개별 상수 설정 (COUNTRY, OCEAN, MOUNTAIN 등)
    if (eq(v_code, "COUNTRY") || eq(v_code, "COUNTRY_BREEZE") || eq(v_code, "COUNTRY_B")) {
        baseMinWind     = 0.7f;
        baseMaxWind     = 3.4f;
        gustProbBase    = 0.006f; // 낮은 돌풍 확률
        gustStrengthMax = 1.35f;
        thermalFreqBase = 0.015f;
    } else if (eq(v_code, "MEDITERRANEAN")) {
        // [중략]
    } 
    // ... 나머지 Preset 로직 [중략]
}

// preset 기반 Phase 초기화
void CL_S10_Simulation::initPhaseFromBase() {
    // [중략] Phase 초기화 로직
    phase         = EN_A10_WEATHER_PHASE_NORMAL;
    phaseStartSec = millis() / 1000.0f;

    float v_span = baseMaxWind - baseMinWind;
    if (v_span < 0.5f)
        v_span = 0.5f;

    // Normal Phase의 Min/Max Wind를 Base Wind의 Span을 기준으로 설정
    phaseMinWind     = baseMinWind + v_span * 0.15f;
    phaseMaxWind     = baseMinWind + v_span * 0.85f;
    phaseDurationSec = 120.0f;

    // 현재 풍속 및 목표 풍속을 초기 Phase의 중간값으로 설정
    float v_mid       = (baseMinWind + baseMaxWind) * 0.5f;
    currentWindSpeed  = v_mid;
    targetWindSpeed   = v_mid;
    spectralEnergyBuf = 0.0f;
    spectralPhaseAcc  = 0.0f;
    windMomentmentum      = 0.0f;
}

// Phase 전환 로직
void CL_S10_Simulation::updatePhase() {
    // [중략] Phase 전환 로직
    if (!active) return;

    float v_now = millis() / 1000.0f;
    if (v_now - phaseStartSec < phaseDurationSec)
        return; // 현재 Phase 지속 시간 미만이면 종료

    T_A10_WindPhase_t v_old = phase;
    float             v_r   = A10_getRandom01();

    // 확률적 Phase 전환 로직 (예: CALM에서 NORMAL/STRONG으로 전환)
    if (v_old == EN_A10_WEATHER_PHASE_CALM) {
        phase = (v_r < 0.7f) ? EN_A10_WEATHER_PHASE_NORMAL : EN_A10_WEATHER_PHASE_STRONG;
    } else if (v_old == EN_A10_WEATHER_PHASE_STRONG) {
        phase = (v_r < 0.7f) ? EN_A10_WEATHER_PHASE_NORMAL : EN_A10_WEATHER_PHASE_CALM;
    } else { // NORMAL
        if (v_r < 0.4f)
            phase = EN_A10_WEATHER_PHASE_CALM;
        else if (v_r < 0.8f)
            phase = EN_A10_WEATHER_PHASE_NORMAL;
        else
            phase = EN_A10_WEATHER_PHASE_STRONG;
    }

    phaseStartSec = v_now;

    // 새로운 Phase에 따른 Duration 및 풍속 범위 설정
    float v_span = baseMaxWind - baseMinWind;

    if (phase == EN_A10_WEATHER_PHASE_CALM) {
        phaseDurationSec = A10_randRange(90.0f, 210.0f);
        phaseMinWind     = baseMinWind;
        phaseMaxWind     = baseMinWind + v_span * 0.6f;
    } else if (phase == EN_A10_WEATHER_PHASE_NORMAL) {
        phaseDurationSec = A10_randRange(120.0f, 300.0f);
        // [중략]
    } else {  // STRONG
        phaseDurationSec = A10_randRange(60.0f, 150.0f);
        // [중략]
    }

    phaseMinWind = max(0.2f, phaseMinWind);
    phaseMaxWind = min(11.0f, phaseMaxWind);

    generateTarget(); // Phase 변경 후 새 목표 풍속 생성
}

// Von Kármán 난류 근사
void CL_S10_Simulation::calcTurb(float p_dt) {
    // [중략] 난류 성분 계산 로직
    if (!active) return;

    float v_L     = max(1.0f, turbLenScale); // 난류 길이 스케일
    float v_sigma = max(0.0f, turbSigma);    // 난류 세기 (표준편차)
    float v_U     = max(0.1f, currentWindSpeed); // 평균 풍속

    float v_sum = 0.0f;

    // 12개 주파수 밴드의 합으로 난류 스펙트럼 근사 (Von Kármán 모델)
    for (int v_i = 1; v_i <= 12; v_i++) {
        float v_n   = (float)v_i * 0.1f; // 주파수 인덱스
        float v_f   = v_n * v_U / v_L;   // 주파수 (f)
        float v_fLU = v_f * v_L / v_U;

        // E(f) * df 를 근사하여 각 주파수 밴드의 에너지 기여분 계산
        float v_term  = 70.8f * v_fLU * v_fLU;
        float v_numer = 4.0f * v_sigma * v_sigma * (v_L / v_U) * (1.0f + v_term);
        float v_denom = powf(1.0f + v_term, 5.0f / 6.0f);
        float v_S     = v_numer / v_denom; // 스펙트럼 에너지 밀도

        float v_phaseRate = 2.0f * (float)M_PI * v_f;
        float v_phaseInc  = v_phaseRate * p_dt;
        float v_phase     = spectralPhaseAcc * (float)v_i + v_phaseInc + A10_randRange(-0.1f, 0.1f); // 위상

        float v_bandWidth = 0.083f;
        float v_amp       = sqrtf(2.0f * v_S * v_bandWidth); // 주파수 밴드의 진폭

        v_sum += v_amp * sinf(v_phase); // 총 난류 성분 합산
    }

    // [중략] 위상 누적 및 난류 버퍼 업데이트
    spectralPhaseAcc += p_dt * 0.5f;
    if (spectralPhaseAcc > 2.0f * (float)M_PI) {
        spectralPhaseAcc -= 2.0f * (float)M_PI;
    }

    // 난류 에너지 버퍼에 시계열 관성 적용 (Exponential smoothing)
    float v_corr      = expf(-p_dt / turbTimeScale);
    spectralEnergyBuf = spectralEnergyBuf * v_corr + v_sum * (1.0f - v_corr);
}

// 열기포 포락(Active 시)
void CL_S10_Simulation::calcThermalEnvelope() {
    // [중략] 열기포 포락선 계산 로직
    if (!active || !thermalActive) return;

    float v_t   = millis() / 1000.0f;
    float v_age = v_t - thermalStartSec;

    if (v_age >= thermalDuration) {
        thermalActive       = false;
        thermalContribution = 0.0f;
        return;
    }

    float v_prog = v_age / thermalDuration; // 진행률 (0~1)
    float v_env  = 0.0f; // 포락선 값

    // 포락선 모양: 시작(점진 증가) - 중간(최대치 유지 + 지터) - 끝(점진 감소)
    if (v_prog < 0.2f) {
        // [중략] 증가 구간
    } else if (v_prog < 0.6f) {
        v_env = 1.0f;
        v_env += sinf(v_age * (0.8f + (float)phase * 0.2f) * 2.0f * (float)M_PI) * 0.15f; // 중간 지터
    } else {
        // [중략] 감소 구간
    }

    if (v_env < 0.0f) v_env = 0.0f;

    float v_strength    = max(1.0f, thermalStrength);
    thermalContribution = (v_strength - 1.0f) * v_env; // 강도 기반 최종 기여도
}

// 돌풍 상태 갱신
void CL_S10_Simulation::updateGust() {
    // [중략] 돌풍 발생 확률 및 강도/지속 시간 계산 로직
    if (!active) return;

    float v_nowSec = millis() / 1000.0f;

    if (gustActive) {
        // 돌풍 진행 중: 포락선(Envelope)에 따른 강도 갱신
        float v_age = v_nowSec - gustStartSec;
        if (v_age >= gustDuration) {
            gustActive    = false;
            gustIntensity = 1.0f;
            return;
        }

        float v_prog = v_age / gustDuration;
        float v_env; // 포락선 값

        // [중략] 포락선 모양: 증가 - 최대치+지터 - 감소
        if (v_prog < 0.25f) {
            // 증가 구간
        } else if (v_prog < 0.65f) {
            v_env = 1.0f;
            v_env += sinf(v_age * (1.5f + (float)phase * 0.5f)) * 0.08f; // 중간 지터
        } else {
            // 감소 구간
        }

        if (v_env < 0.0f) v_env = 0.0f;
        gustIntensity = 1.0f + (gustStrengthMax - 1.0f) * v_env; // 최대 강도 기반 최종 배율
        return;
    }

    // 새로운 돌풍 트리거 (최소 500ms 간격)
    unsigned long v_nowMs = millis();
    if (v_nowMs - lastGustCheckMs < 500UL) return;
    lastGustCheckMs = v_nowMs;

    // 확률 계산: 기본 확률 * 사용자 주파수 * 풍속/Phase 가중치
    float v_base = gustProbBase;
    float v_user = userGustFreq / 100.0f;
    float v_wfac = 1.0f + (currentWindSpeed / 8.9f) * 0.5f;
    float v_pmul; 
    // [중략] Phase에 따른 확률 배율 v_pmul 계산

    float v_p = v_base * v_user * v_pmul; // 최종 발생 확률

    if (A10_getRandom01() < v_p) {
        // 돌풍 발생: 지속 시간 및 강도 랜덤 설정 (Phase에 따라 범위 조정)
        gustActive   = true;
        gustStartSec = v_nowSec;

        // [중략] Phase에 따른 gustDuration 및 gustIntensity 설정
        if (gustIntensity > gustStrengthMax) {
            gustIntensity = gustStrengthMax;
        }
    }
}

// 열기포 트리거
void CL_S10_Simulation::updateThermal() {
    // [중략] 열기포 발생 확률 체크 로직
    if (!active || thermalActive) return;

    unsigned long v_nowMs = millis();
    if (v_nowMs - lastThermalCheckMs < 700UL) return; // 최소 700ms 간격
    lastThermalCheckMs = v_nowMs;

    // 확률 계산: 기본 확률 * 강도/풍속/Phase 가중치
    float v_strength = max(1.0f, thermalStrength);
    float v_wfac     = 1.0f + (currentWindSpeed / 8.0f) * 0.3f;
    float v_phaseMul = (phase == EN_A10_WEATHER_PHASE_CALM) ? 1.2f : (phase == EN_A10_WEATHER_PHASE_STRONG ? 0.7f : 1.0f);

    float v_freq = thermalFreqBase * (0.6f + 0.4f * min(3.0f, max(0.5f, v_strength))) * v_wfac * v_phaseMul;

    if (A10_getRandom01() < v_freq) {
        // 열기포 발생: 지속 시간 랜덤 설정 (Phase에 따라 범위 조정)
        thermalActive   = true;
        thermalStartSec = v_nowMs / 1000.0f;

        float v_d = A10_randRange(8.0f, 14.0f);
        // [중략] Phase에 따른 v_d 조정
        thermalDuration = v_d;
    }
}

// 목표 풍속 재설정
void CL_S10_Simulation::generateTarget() {
    // [중략] 새로운 targetWindSpeed 생성 로직
    if (!active) return;

    float v_range = phaseMaxWind - phaseMinWind;
    if (v_range < 0.2f) v_range = 0.2f;

    float v_w    = phaseMinWind + A10_getRandom01() * v_range;
    float v_mid  = (phaseMinWind + phaseMaxWind) * 0.5f;
    float v_bias = A10_randRange(0.0f, 1.0f);

    // 중간값(v_mid)에 가깝게 끌어당겨 극단적인 값 방지
    v_w             = (v_w + v_mid * v_bias) / (1.0f + v_bias);
    targetWindSpeed = v_w;

    // variability, phase, 난류 스케일에 따라 풍속 변화율(windChangeRate) 결정
    float v_var = userVariability / 100.0f;
    float v_base;
    
    // [중략] Phase와 Variability에 따른 v_base 계산

    float v_U      = max(0.1f, currentWindSpeed);
    float v_tscale = turbLenScale / v_U;
    v_base *= (1.0f + v_tscale * 0.1f);

    windChangeRate = constrain(v_base * A10_randRange(0.7f, 1.7f), 0.04f, 0.5f);
}

// --------------------------------------------------
// 최근 풍속 이력 관리 (순환 버퍼 기반)
// --------------------------------------------------
void CL_S10_Simulation::_updateWindHistory(float p_speed) {
    history[historyIndex] = p_speed;
    historyIndex          = (historyIndex + 1) % HISTORY_SIZE;
    if (historyCount < HISTORY_SIZE)
        historyCount++;

    // 평균 풍속 캐시 갱신 (historyCount에 따라 정확한 평균 계산)
    float v_sum = 0.0f;
    for (uint8_t i = 0; i < historyCount; i++) v_sum += history[i];
    avgWindCached = v_sum / (float)historyCount;
}

// --------------------------------------------------
// 캐시된 평균 풍속 반환 (O(1))
// --------------------------------------------------
float CL_S10_Simulation::_getAvgWindFast() const {
    return (historyCount > 0) ? avgWindCached : currentWindSpeed;
}

