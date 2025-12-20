#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : S10_Simulation_014.h
 * 모듈약어 : S10
 * 모듈명 : Smart Nature Wind 풍속 시뮬레이션 Manager (v014, Full Implementation)
 * ------------------------------------------------------
 * 기능 요약:
 *  - 자연풍 물리 기반 풍속 시뮬레이션 (Phase / 돌풍 / 난류 / 열기포 / 지터)
 *  - PWM 제어기(CL_P10_PWM) 연동
 *  - C10/A10 해석결과(ResolvedWind) 혹은 프리셋 코드 기반 동작
 *  - Chart 버퍼(최근 120초) 제공 (웹 UI 시각화)
 * ------------------------------------------------------
 * [구현 규칙]
 *  - 항상 소스 시작 주석 부분 체계 유지 및 내용 업데이트
 *  - 소스 시작 주석 부분 구현규칙, 코드네이밍규칙 내용 그대로 유지, 수정금지
 *  - ArduinoJson v7.x.x 사용 (v6 이하 사용 금지)
 *  - JsonDocument 단일 타입만 사용
 *  - createNestedArray/Object/containsKey 사용 금지
 *  - memset + strlcpy 기반 안전 초기화
 *  - 주석/필드명은 JSON 구조와 동일하게 유지
 *  - 모듈별 단일 헤더(h)파일로만 구성 (cpp 없음)
 * ------------------------------------------------------
 * [코드 네이밍 규칙]
 *   - 전역 상수,매크로      : G_모듈약어_ 접두사
 *   - 전역 변수             : g_모듈약어_ 접두사
 *   - 전역 함수             : 모듈약어_ 접두사
 *   - type                  : T_모듈약어_ 접두사
 *   - typedef               : _t  접미사
 *   - enum 상수             : EN_모듈약어_ 접두사
 *   - 구조체                : ST_모듈약어_ 접두사
 *   - 클래스명              : CL_모듈약어_ 접두사
 *   - 클래스 private 멤버   : _ 접두사
 *   - 클래스 멤버(함수/변수) : 모듈약어 접두사 미사용
 *   - 클래스 정적 멤버      : s_ 접두사
 *   - 함수 로컬 변수        : v_ 접두사
 *   - 함수 인자             : p_ 접두사
 * ------------------------------------------------------
 */

#include <Arduino.h>
#include <deque>
#include <cmath>
#include <ArduinoJson.h>

#include "A10_Const_014.h"
#include "C10_ConfigManager_014.h"
#include "D10_Logger_011.h"
#include "P10_PWM_ctrl_012.h"

// ======================================================
// CL_S10_Simulation
// ======================================================
class CL_S10_Simulation {
public:
    // ------------------ 상태 ------------------
    bool   active           = false;
    bool   fanPowerEnabled  = true;

    float  currentWindSpeed = 3.6f;
    float  targetWindSpeed  = 3.6f;

    char   presetCode[24]   = {0};
    char   styleCode[24]    = {0};

    // 사용자/해석 파라미터 (resolved)
    float  userIntensity    = 70.0f;   // 0~100
    float  userVariability  = 50.0f;   // 0~100
    float  userGustFreq     = 45.0f;   // 0~100
    float  minFanPct        = 10.0f;   // 0~100
    float  fanLimitPct      = 90.0f;   // 0~100
    float  turbLenScale     = 40.0f;   // >0
    float  turbSigma        = 0.5f;    // >=0
    float  thermalStrength  = 2.0f;    // >=1
    float  thermalRadius    = 18.0f;   // >=0

    // Phase
    T_A10_WindPhase_t phase      = EN_A10_WEATHER_PHASE_NORMAL;
    float  phaseStartSec         = 0.0f;
    float  phaseDurationSec      = 120.0f;
    float  phaseMinWind          = 2.0f;
    float  phaseMaxWind          = 6.0f;

    // Preset 기반 범위/확률
    float  baseMinWind           = 1.8f;
    float  baseMaxWind           = 5.5f;
    float  gustProbBase          = 0.040f;
    float  gustStrengthMax       = 2.1f;
    float  thermalFreqBase       = 0.022f;

    // 난류 버퍼
    float  spectralEnergyBuf     = 0.0f;
    float  spectralPhaseAcc      = 0.0f;
    float  turbTimeScale         = 5.0f;

    // 돌풍
    bool           gustActive    = false;
    float          gustStartSec  = 0.0f;
    float          gustDuration  = 3.0f;
    float          gustIntensity = 1.0f;
    unsigned long  lastGustCheck = 0;

    // 열기포
    bool           thermalActive       = false;
    float          thermalStartSec     = 0.0f;
    float          thermalDuration     = 8.0f;
    float          thermalContribution = 0.0f;
    unsigned long  lastThermalCheck    = 0;

    // 관성/변화율
    float          windChangeRate      = 0.12f;
    float          windMomentum        = 0.0f;
    unsigned long  lastUpdateMs        = 0;

    // 차트 버퍼
    struct ST_ChartEntry {
        unsigned long timestamp;
        float wind_speed;
        float pwm_duty;
        float intensity;
        float variability;
        float turbulence;
        uint8_t preset_id;
        bool gust_active;
        bool thermal_active;
    };
    static std::deque<ST_ChartEntry> s_chartBuffer;
    static unsigned long             s_lastChartLogMs;

public:
    // ==================================================
    // 초기화
    // ==================================================
    void begin(CL_P10_PWM& p_pwm) {
        _pwm = &p_pwm;
        resetDefaults();
        (void)esp_random(); // 지터 초기화
        CL_D10_Logger::log(EN_L10_LOG_INFO, "[S10] begin()");
    }

    // 정지
    inline void stop() {
        active = false;
        phase  = EN_A10_WEATHER_PHASE_CALM;
        targetWindSpeed  = 0.0f;
        currentWindSpeed = 0.0f;
        if (_pwm) _pwm->P10_setDutyPercent(0.0f);
    }

    // ==================================================
    // tick (주기 호출)
    // ==================================================
    void tick() {
        if (!active) return;

        unsigned long v_now = millis();
        static uint32_t s_j = 0;
        uint32_t v_interval = 40 + (s_j % 60); // 40~99ms 지터
        if (v_now - lastUpdateMs < v_interval) return;
        s_j = esp_random();

        float v_dt = (v_now - lastUpdateMs) / 1000.0f;
        lastUpdateMs = v_now;

        updatePhase();
        calcTurb(v_dt);
        calcThermalEnvelope();
        updateGust();
        updateThermal();

        // 목표 풍속으로 천천히 수렴 (관성 + 난류)
        float v_diff   = targetWindSpeed - currentWindSpeed;
        float v_change = v_diff * windChangeRate * v_dt;
        windMomentum   = windMomentum * 0.85f + v_change * 0.15f;
        windMomentum   = constrain(windMomentum, -0.5f, 0.5f);

        float v_new    = currentWindSpeed + windMomentum + spectralEnergyBuf;
        currentWindSpeed = constrain(v_new, 0.2f, 11.0f);

        // 목표 재생성 빈도 (목표 가까우면 잦게, 멀면 드물게)
        float v_changeTh = 0.5f + (currentWindSpeed / 20.0f);
        if (fabsf(v_diff) < v_changeTh) {
            if (A10_randRange(0.0f, 100.0f) < 30.0f) generateTarget();
        } else {
            if (A10_randRange(0.0f, 100.0f) < 6.0f) generateTarget();
        }

        // PWM 반영
        float v_pct = currentWindSpeed * 10.0f + 10.0f;     // 기본 스케일링
        v_pct *= gustIntensity;                              // 돌풍 영향
        v_pct += thermalContribution * 5.0f;                 // 열기포 영향
        applyFan(v_pct);

        // 차트 샘플 (1Hz)
        if (millis() - s_lastChartLogMs > 1000) {
            if (s_chartBuffer.size() >= 120) s_chartBuffer.pop_front();
            ST_ChartEntry v_e {
                millis(),
                currentWindSpeed,
                _pwm ? _pwm->P10_getDutyPercent() : 0.0f,
                userIntensity,
                userVariability,
                turbSigma,
                (uint8_t)A10_getPresetIndexByCode(presetCode),
                gustActive,
                thermalActive
            };
            s_chartBuffer.push_back(v_e);
            s_lastChartLogMs = millis();
        }
    }

    // ==================================================
    // 해석결과 적용 (preset×style×adjust)
    // ==================================================
    void applyResolvedWind(const ST_A10_ResolvedWind_t& p_resolved) {
        strlcpy(presetCode, p_resolved.presetCode, sizeof(presetCode));
        strlcpy(styleCode , p_resolved.styleCode , sizeof(styleCode));

        userIntensity    = constrain(p_resolved.wind_intensity,   0.0f, 100.0f);
        userVariability  = constrain(p_resolved.wind_variability, 0.0f, 100.0f);
        userGustFreq     = constrain(p_resolved.gust_frequency,   0.0f, 100.0f);
        minFanPct        = constrain(p_resolved.min_fan,          0.0f, 100.0f);
        fanLimitPct      = constrain(p_resolved.fan_limit,        0.0f, 100.0f);
        turbLenScale     = max(1.0f, p_resolved.turbulence_length_scale);
        turbSigma        = max(0.0f, p_resolved.turbulence_intensity_sigma);
        thermalStrength  = max(1.0f, p_resolved.thermal_bubble_strength);
        thermalRadius    = max(0.0f, p_resolved.thermal_bubble_radius);

        // preset 정의에서 baseMin/Max/확률/강도 세팅
        applyPresetCore(presetCode);

        // 변화율 (variability 기반)
        windChangeRate = constrain(0.10f + (userVariability/100.0f)*0.20f, 0.06f, 0.34f);

        // Phase 초기화
        initPhaseFromBase();

        active = true;
        generateTarget();
    }

    // ==================================================
    // JSON Export
    // ==================================================
    void toJson(JsonDocument& p_doc) {
        JsonObject v_o = p_doc["sim"].to<JsonObject>();
        v_o["active"]    = active;
        v_o["phase"]     = g_A10_WEATHER_PHASE_NAMES_Arr[phase];
        v_o["wind"]      = currentWindSpeed;
        v_o["target"]    = targetWindSpeed;
        v_o["gust"]      = gustActive;
        v_o["thermal"]   = thermalActive;
        v_o["pwm"]       = _pwm ? _pwm->P10_getDutyPercent() : 0.0f;

        v_o["preset"]    = presetCode;
        v_o["style"]     = styleCode;
        v_o["intensity"] = userIntensity;
        v_o["variability"] = userVariability;
        v_o["turbulence_sigma"] = turbSigma;
        v_o["fan_limit"] = fanLimitPct;
        v_o["min_fan"]   = minFanPct;
    }

    void toChartJson(JsonDocument& p_doc) {
        JsonArray v_arr = p_doc["chart"].to<JsonArray>();
        for (auto& v_e : s_chartBuffer) {
            JsonObject jo = v_arr.add<JsonObject>();
            jo["t"] = v_e.timestamp;
            jo["w"] = v_e.wind_speed;
            jo["p"] = v_e.pwm_duty;
            jo["g"] = v_e.gust_active;
            jo["h"] = v_e.thermal_active;
        }
    }

    // ==================================================
    // 기본값 리셋
    // ==================================================
    void resetDefaults() {
        active = false;
        phase  = EN_A10_WEATHER_PHASE_NORMAL;
        fanPowerEnabled   = true;
        userIntensity     = 70.0f;
        userVariability   = 50.0f;
        userGustFreq      = 45.0f;
        minFanPct         = 10.0f;
        fanLimitPct       = 90.0f;
        turbLenScale      = 40.0f;
        turbSigma         = 0.5f;
        thermalStrength   = 2.0f;
        thermalRadius     = 18.0f;
        strlcpy(presetCode, "OCEAN", sizeof(presetCode));
        strlcpy(styleCode , "BALANCE", sizeof(styleCode));
        currentWindSpeed  = 3.6f;
        targetWindSpeed   = 3.6f;
        windMomentum      = 0.0f;
        spectralEnergyBuf = 0.0f;
        spectralPhaseAcc  = 0.0f;
        lastUpdateMs      = millis();

        applyPresetCore(presetCode);
        initPhaseFromBase();
    }

private:
    CL_P10_PWM* _pwm = nullptr;

    // =========================
    // 내부 로직 구현부
    // =========================
    void applyFan(float p_pct) {
        if (!_pwm) return;

        float v_req   = p_pct / 100.0f;
        float v_limit = fanLimitPct / 100.0f;
        float v_min   = minFanPct   / 100.0f;
        float v_int   = userIntensity / 100.0f;

        if (!fanPowerEnabled || v_int <= 0.01f) {
            _pwm->P10_setDutyPercent(0.0f);
            return;
        }

        if (active) v_req *= v_int;
        v_req = fmaxf(v_min, fminf(v_limit, v_req));

        _pwm->P10_setDutyPercent(v_req * 100.0f);
    }

    // preset 코드 → 베이스 스펙 설정
    void applyPresetCore(const char* p_code) {
        // 코드 표준화
        char v_code[24]; memset(v_code, 0, sizeof(v_code));
        if (p_code && p_code[0]) {
            strlcpy(v_code, p_code, sizeof(v_code));
        } else {
            strlcpy(v_code, "OCEAN", sizeof(v_code));
        }

        // 기본값
        baseMinWind     = 1.8f;
        baseMaxWind     = 5.5f;
        gustProbBase    = 0.040f;
        gustStrengthMax = 2.10f;
        thermalFreqBase = 0.022f;

        // 여러 별칭 대응
        auto eq = [](const char* a, const char* b){
            return strcasecmp(a, b) == 0;
        };

        if (eq(v_code,"COUNTRY") || eq(v_code,"COUNTRY_BREEZE") || eq(v_code,"COUNTRY_B")) {
            baseMinWind=0.7f;  baseMaxWind=3.4f;  gustProbBase=0.006f; gustStrengthMax=1.35f; thermalFreqBase=0.015f;
        } else if (eq(v_code,"MEDITERRANEAN")) {
            baseMinWind=1.6f;  baseMaxWind=3.8f;  gustProbBase=0.012f; gustStrengthMax=1.55f; thermalFreqBase=0.035f;
        } else if (eq(v_code,"OCEAN")) {
            baseMinWind=1.8f;  baseMaxWind=5.5f;  gustProbBase=0.040f; gustStrengthMax=2.10f; thermalFreqBase=0.022f;
        } else if (eq(v_code,"MOUNTAIN")) {
            baseMinWind=2.2f;  baseMaxWind=7.5f;  gustProbBase=0.045f; gustStrengthMax=2.20f; thermalFreqBase=0.028f;
        } else if (eq(v_code,"PLAINS")) {
            baseMinWind=4.0f;  baseMaxWind=8.8f;  gustProbBase=0.070f; gustStrengthMax=2.40f; thermalFreqBase=0.018f;
        } else if (eq(v_code,"HARBOR_BREEZE") || eq(v_code,"HARBOUR_BREEZE")) {
            baseMinWind=2.25f; baseMaxWind=5.35f; gustProbBase=0.025f; gustStrengthMax=1.80f; thermalFreqBase=0.026f;
        } else if (eq(v_code,"FOREST_CANOPY")) {
            baseMinWind=1.35f; baseMaxWind=4.00f; gustProbBase=0.010f; gustStrengthMax=1.50f; thermalFreqBase=0.012f;
        } else if (eq(v_code,"URBAN_SUNSET")) {
            baseMinWind=1.80f; baseMaxWind=4.90f; gustProbBase=0.030f; gustStrengthMax=2.00f; thermalFreqBase=0.020f;
        } else if (eq(v_code,"TROPICAL_RAIN")) {
            baseMinWind=3.15f; baseMaxWind=8.05f; gustProbBase=0.060f; gustStrengthMax=2.20f; thermalFreqBase=0.038f;
        } else if (eq(v_code,"DESERT_NIGHT")) {
            baseMinWind=0.90f; baseMaxWind=3.10f; gustProbBase=0.005f; gustStrengthMax=1.30f; thermalFreqBase=0.008f;
        }
    }

    // preset 기반 Phase 초기화
    void initPhaseFromBase() {
        phase = EN_A10_WEATHER_PHASE_NORMAL;
        phaseStartSec = millis()/1000.0f;
        float v_span = baseMaxWind - baseMinWind;
        phaseMinWind = baseMinWind + v_span * 0.15f;
        phaseMaxWind = baseMinWind + v_span * 0.85f;
        phaseDurationSec = 120.0f;

        float v_mid = (baseMinWind + baseMaxWind) * 0.5f;
        currentWindSpeed  = v_mid;
        targetWindSpeed   = v_mid;
        spectralEnergyBuf = 0.0f;
        spectralPhaseAcc  = 0.0f;
        windMomentum      = 0.0f;
    }

    // Phase 전환
    void updatePhase() {
        if (!active) return;

        float v_now = millis()/1000.0f;
        if (v_now - phaseStartSec < phaseDurationSec) return;

        T_A10_WindPhase_t v_old = phase;
        float v_r = A10_getRandom01();
        if (v_old == EN_A10_WEATHER_PHASE_CALM) {
            phase = (v_r < 0.7f) ? EN_A10_WEATHER_PHASE_NORMAL : EN_A10_WEATHER_PHASE_STRONG;
        } else if (v_old == EN_A10_WEATHER_PHASE_STRONG) {
            phase = (v_r < 0.7f) ? EN_A10_WEATHER_PHASE_NORMAL : EN_A10_WEATHER_PHASE_CALM;
        } else {
            if (v_r < 0.4f) phase = EN_A10_WEATHER_PHASE_CALM;
            else if (v_r < 0.8f) phase = EN_A10_WEATHER_PHASE_NORMAL;
            else phase = EN_A10_WEATHER_PHASE_STRONG;
        }

        phaseStartSec = v_now;
        float v_span = baseMaxWind - baseMinWind;

        if (phase == EN_A10_WEATHER_PHASE_CALM) {
            phaseDurationSec = A10_randRange(90,210);
            phaseMinWind = baseMinWind;
            phaseMaxWind = baseMinWind + v_span * 0.6f;
        } else if (phase == EN_A10_WEATHER_PHASE_NORMAL) {
            phaseDurationSec = A10_randRange(120,300);
            phaseMinWind = baseMinWind + v_span * 0.15f;
            phaseMaxWind = baseMinWind + v_span * 0.85f;
        } else {
            phaseDurationSec = A10_randRange(60,150);
            phaseMinWind = baseMinWind + v_span * 0.4f;
            phaseMaxWind = baseMaxWind;
        }

        phaseMinWind = max(0.2f, phaseMinWind);
        phaseMaxWind = min(11.0f, phaseMaxWind);

        generateTarget();
    }

    // Von Kármán 난류 스펙트럼 근사
    void calcTurb(float p_dt) {
        if (!active) return;

        float v_L     = max(1.0f, turbLenScale);
        float v_sigma = max(0.0f, turbSigma);
        float v_U     = max(0.1f, currentWindSpeed);

        float v_sum = 0.0f;
        for (int i=1; i<=12; i++) {
            float v_n = i * 0.1f;
            float v_f = v_n * v_U / v_L;            // 주파수
            float v_fLU = v_f * v_L / v_U;          // 무차원 주파수
            float v_term = 70.8f * v_fLU * v_fLU;
            float v_numer = 4.0f * v_sigma * v_sigma * (v_L / v_U) * (1.0f + v_term);
            float v_denom = powf(1.0f + v_term, 5.0f/6.0f);
            float v_S = v_numer / v_denom;          // 스펙트럼 밀도 근사

            float v_phaseRate = 2.0f * M_PI * v_f;
            float v_phaseInc  = v_phaseRate * p_dt;
            float v_phase     = spectralPhaseAcc * i + v_phaseInc + A10_randRange(-0.1f, 0.1f);
            float v_amp       = sqrtf(2.0f * v_S * 0.083f); // 밴드폭 ≈0.083
            v_sum += v_amp * sinf(v_phase);
        }

        spectralPhaseAcc += p_dt * 0.5f;
        if (spectralPhaseAcc > 2*M_PI) spectralPhaseAcc -= 2*M_PI;

        float v_corr = expf(-p_dt / turbTimeScale);
        spectralEnergyBuf = spectralEnergyBuf * v_corr + v_sum * (1.0f - v_corr);
    }

    // 열기포 포락선
    void calcThermalEnvelope() {
        if (!active || !thermalActive) return;

        float v_t   = millis()/1000.0f;
        float v_age = v_t - thermalStartSec;
        if (v_age >= thermalDuration) {
            thermalActive = false;
            thermalContribution = 0.0f;
            return;
        }

        float v_prog = v_age / thermalDuration;
        float v_env  = 0.0f;
        if (v_prog < 0.2f) {
            v_env = 1.0f - powf(1.0f - v_prog/0.2f, 2.0f);
        } else if (v_prog < 0.6f) {
            v_env = 1.0f;
            v_env += sinf(v_age * (0.8f + phase*0.2f) * 2.0f * M_PI) * 0.15f;
        } else {
            float v_d = (v_prog - 0.6f)/0.4f;
            v_env = 1.0f - powf(v_d, 1.3f);
        }

        float v_strength = max(1.0f, thermalStrength);
        thermalContribution = (v_strength - 1.0f) * v_env;
    }

    // 돌풍 상태 갱신/트리거
    void updateGust() {
        if (!active) return;
        float v_now = millis()/1000.0f;

        if (gustActive) {
            float v_age = v_now - gustStartSec;
            if (v_age >= gustDuration) {
                gustActive = false;
                gustIntensity = 1.0f;
                return;
            }
            float v_prog = v_age / gustDuration;
            float v_env;
            if (v_prog < 0.25f) {
                v_env = 1.0f - powf(1.0f - v_prog/0.25f, 1.8f);
            } else if (v_prog < 0.65f) {
                v_env = 1.0f;
                v_env += sinf(v_age * (1.5f + phase*0.5f)) * 0.08f;
            } else {
                v_env = 1.0f - powf((v_prog - 0.65f)/0.35f, 1.5f);
            }
            gustIntensity = 1.0f + (gustStrengthMax - 1.0f) * v_env;
            return;
        }

        if (millis() - lastGustCheck >= 500) {
            lastGustCheck = millis();

            float v_base = gustProbBase;
            float v_user = userGustFreq / 100.0f;
            float v_wfac = 1.0f + (currentWindSpeed/8.9f) * 0.5f;
            float v_pmul = (phase == EN_A10_WEATHER_PHASE_CALM) ? (0.3f * v_wfac)
                         : (phase == EN_A10_WEATHER_PHASE_STRONG ? 2.2f * v_wfac : 0.9f * v_wfac);
            float v_p = v_base * v_user * v_pmul;

            if (A10_getRandom01() < v_p) {
                gustActive   = true;
                gustStartSec = v_now;

                float v_speedF = currentWindSpeed / 6.7f;
                if (phase == EN_A10_WEATHER_PHASE_CALM) {
                    gustDuration  = A10_randRange(3.0f, 8.0f);
                    gustIntensity = A10_randRange(1.08f, 1.33f);
                } else if (phase == EN_A10_WEATHER_PHASE_STRONG) {
                    gustDuration  = A10_randRange(0.8f, 3.3f);
                    gustIntensity = A10_randRange(1.3f, 1.3f + 0.9f * (1 + v_speedF * 0.3f));
                } else {
                    gustDuration  = A10_randRange(1.8f, 5.8f);
                    gustIntensity = A10_randRange(1.15f, 1.15f + 0.5f * (1 + v_speedF * 0.2f));
                }
                gustIntensity = min(gustIntensity, gustStrengthMax);
            }
        }
    }

    // 열기포 트리거
    void updateThermal() {
        if (!active || thermalActive) return;
        if (millis() - lastThermalCheck < 700) return;
        lastThermalCheck = millis();

        float v_strength = max(1.0f, thermalStrength);
        float v_wfac     = 1.0f + (currentWindSpeed/8.0f) * 0.3f;
        float v_pmul     = (phase == EN_A10_WEATHER_PHASE_CALM) ? 1.2f
                         : (phase == EN_A10_WEATHER_PHASE_STRONG ? 0.7f : 1.0f);

        float v_freq = thermalFreqBase * (0.6f + 0.4f * min(3.0f, max(0.5f, v_strength))) * v_wfac * v_pmul;

        if (A10_getRandom01() < v_freq) {
            thermalActive   = true;
            thermalStartSec = millis()/1000.0f;
            float v_d = A10_randRange(8.0f, 14.0f);
            if (phase == EN_A10_WEATHER_PHASE_CALM)      v_d *= 1.3f;
            else if (phase == EN_A10_WEATHER_PHASE_STRONG) v_d *= 0.8f;
            thermalDuration = v_d;
        }
    }

    // 목표 풍속 갱신
    void generateTarget() {
        if (!active) return;

        float v_range = phaseMaxWind - phaseMinWind;
        float v_w = phaseMinWind + A10_getRandom01() * v_range;
        float v_mid = (phaseMinWind + phaseMaxWind) * 0.5f;
        float v_bias = A10_randRange(0.0f, 1.0f);
        v_w = (v_w + v_mid * v_bias) / (1.0f + v_bias);
        targetWindSpeed = v_w;

        float v_var  = userVariability / 100.0f;
        float v_base;
        if (phase == EN_A10_WEATHER_PHASE_CALM)      v_base = 0.08f + v_var*0.12f;
        else if (phase == EN_A10_WEATHER_PHASE_STRONG) v_base = 0.25f + v_var*0.35f;
        else                                         v_base = 0.15f + v_var*0.25f;

        float v_U = max(0.1f, currentWindSpeed);
        float v_tscale = turbLenScale / v_U;
        v_base *= (1.0f + v_tscale * 0.1f);

        windChangeRate = constrain(v_base * A10_randRange(0.7f, 1.7f), 0.04f, 0.5f);
    }
};

// ------------------------------------------------------
// 정적 멤버 정의
// ------------------------------------------------------
std::deque<CL_S10_Simulation::ST_ChartEntry> CL_S10_Simulation::s_chartBuffer;
unsigned long CL_S10_Simulation::s_lastChartLogMs = 0;
