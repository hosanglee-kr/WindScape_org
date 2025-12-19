/*
 * ------------------------------------------------------
 * 소스명 : S10_Simul_Physic_022.cpp
 * 모듈약어 : S10
 * 모듈명 : Smart Nature Wind 풍속 시뮬레이션 Manager (v022, Full)
 * ------------------------------------------------------
 * 기능 요약:
 * - CL_S10_Simulation 클래스의 물리/확률 모델 구현부
 * - Von Kármán 난류(12-band 근사), 관성/목표 풍속 생성
 * - Phase( CALM/NORMAL/STRONG ) 확률 전환 + 지속시간 정책
 * - 돌풍(Gust) / 열기포(Thermal Bubble) 확률(초당 rate) 모델 + dt 자동 보정
 * - tick()에서 캡처한 _tickNowMs/_tickNowSec 기반으로 millis() 일관성 유지
 * ------------------------------------------------------
 * [구현 규칙]
 * - 항상 소스 시작 주석 체계 유지 및 내용 업데이트
 * - ArduinoJson v7.x.x 사용 (v6 이하 사용 금지)
 * - JsonDocument 단일 타입만 사용
 * - createNestedArray/Object/containsKey 사용 금지
 * - memset + strlcpy 기반 안전 초기화
 * - 주석/필드명은 JSON 구조와 동일하게 유지
 * ------------------------------------------------------
 * [코드 네이밍 규칙]
 * - 전역 상수,매크로      : G_모듈약어_ prefix
 * - 전역 변수             : g_모듈약어_ prefix
 * - 전역 함수             : 모듈약어_ prefix
 * - Types                 : T_모듈약어_ prefix
 * - Typedefs              : _t suffix
 * - Enum constants        : EN_모듈약어_ prefix
 * - Structs               : ST_모듈약어_ prefix
 * - Classes               : CL_모듈약어_ prefix
 * - Private class members : _ prefix
 * - Class members         : (functions/variables) no module prefix
 * - Static class members  : s_ prefix
 * - Function local vars   : v_ prefix
 * - Function arguments    : p_ prefix
 * ------------------------------------------------------
 */

#include "S10_Simul_021.h"

// 외부 종속성 헤더 포함 (외부에서 제공되어야 함: 시스템 상수, 설정, 로그, PWM 제어)
#include "A10_Const_016.h"
#include "C10_Config_029.h"
#include "D10_Logger_016.h"
// #include "P10_PWM_ctrl_014.h"

// ==================================================
// [S10 주기/시간 정책 상수]
// ==================================================

// (1) Phase 지속시간 범위(초) 정책
static const float G_S10_PHASE_CALM_DUR_MIN_S   = 90.0f;
static const float G_S10_PHASE_CALM_DUR_MAX_S   = 210.0f;
static const float G_S10_PHASE_NORM_DUR_MIN_S   = 120.0f;
static const float G_S10_PHASE_NORM_DUR_MAX_S   = 300.0f;
static const float G_S10_PHASE_STRONG_DUR_MIN_S = 60.0f;
static const float G_S10_PHASE_STRONG_DUR_MAX_S = 150.0f;

// (2) Thermal(열기포) 지속시간 범위(초) 정책
static const float G_S10_THERM_DUR_MIN_S        = 8.0f;
static const float G_S10_THERM_DUR_MAX_S        = 14.0f;

// (3) Thermal phase 보정 배율(지속시간에 적용)
static const float G_S10_THERM_DUR_MUL_CALM     = 1.3f;
static const float G_S10_THERM_DUR_MUL_STRONG   = 0.8f;

// (4) 이벤트 평가 최소 간격(밀리초) 정책
// - 확률 의미를 "초당 발생률(rate, 1/sec)"로 정규화하고,
//   실제 평가 구간 dtSec(=경과시간/1000)로 P(N>=1)=1-exp(-rate*dtSec) 변환한다.
// - 최소 간격은 과도한 평가/부하를 줄이기 위한 정책이며, dtSec는 실제 경과로 자동 보정됨.
static const unsigned long G_S10_GUST_EVAL_MIN_MS  = 500UL;
static const unsigned long G_S10_THERM_EVAL_MIN_MS = 700UL;

// ==================================================
// [문자열/확률 헬퍼]
// ==================================================
static inline bool S10_strEqNoCase(const char* p_a, const char* p_b) {
    // [입력 방어] null 안전
    if (!p_a || !p_b) {
        return false;
    }
    return (strcasecmp(p_a, p_b) == 0);
}

static inline float S10_probFromRatePerSec(float p_ratePerSec, float p_dtSec) {
    // [정의]
    // - p_ratePerSec : 초당 발생률(rate, λ) [1/sec]
    // - p_dtSec      : 평가 구간(경과 시간) [sec]
    // [의미]
    // - 포아송 과정 가정 시, dtSec 동안 "최소 1회 이상" 발생할 확률:
    //     P(N>=1) = 1 - exp(-λ * dt)
    // - 평가 주기가 바뀌어도(예: 0.5s -> 0.2s) 동일한 체감 빈도를 유지하도록 자동 보정됨.
    if (p_ratePerSec <= 0.0f || p_dtSec <= 0.0f) {
        return 0.0f;
    }
    float v_p = 1.0f - expf(-p_ratePerSec * p_dtSec);
    return A10_clampf(v_p, 0.0f, 1.0f);
}

// ==================================================
// Preset / Phase
// ==================================================

/**
 * @brief Preset 코드에 따라 기본 스펙(Base Wind, 확률/레이트 등)을 설정합니다.
 * @param p_code Preset 코드 문자열(대소문자 무시). null/empty면 "OCEAN" 기본 적용.
 *
 * [단위/의미]
 * - baseMinWind/baseMaxWind : 기본 풍속 범위 [m/s]
 * - gustProbBase            : 돌풍 초당 기본 발생률(rate, 1/sec)
 * - gustStrengthMax         : 돌풍 최대 배율(>=1.0)
 * - thermalFreqBase         : 열기포 초당 기본 발생률(rate, 1/sec)
 *
 * [중요]
 * - gustProbBase/thermalFreqBase는 "평가주기당 확률"이 아니라 "초당 rate"로 정의한다.
 * - 실제 발생 여부는 updateGust/updateThermal에서 dtSec로 확률 변환하여 판단한다.
 */
void CL_S10_Simulation::applyPresetCore(const char* p_code) {
    char v_code[24];
    memset(v_code, 0, sizeof(v_code));

    if (p_code && p_code[0]) {
        strlcpy(v_code, p_code, sizeof(v_code));
    } else {
        strlcpy(v_code, "OCEAN", sizeof(v_code));
    }

    // ------------------------------------------------------
    // 공통 기본값 (OCEAN 기준)
    // ------------------------------------------------------
    baseMinWind     = 1.8f;   // [m/s]
    baseMaxWind     = 5.5f;   // [m/s]
    gustProbBase    = 0.040f; // [1/sec] 돌풍 초당 기본 발생률(rate)
    gustStrengthMax = 2.10f;  // [-] 최대 돌풍 배율
    thermalFreqBase = 0.022f; // [1/sec] 열기포 초당 기본 발생률(rate)

    // ------------------------------------------------------
    // Preset Code에 따른 개별 상수 설정
    // ------------------------------------------------------
    if (S10_strEqNoCase(v_code, "COUNTRY") || S10_strEqNoCase(v_code, "COUNTRY_BREEZE") || S10_strEqNoCase(v_code, "COUNTRY_B")) {
        baseMinWind     = 0.7f;
        baseMaxWind     = 3.4f;
        gustProbBase    = 0.006f; // [1/sec] 낮은 돌풍 발생률
        gustStrengthMax = 1.35f;
        thermalFreqBase = 0.015f; // [1/sec]
    } else if (S10_strEqNoCase(v_code, "MEDITERRANEAN")) {
        baseMinWind     = 1.6f;
        baseMaxWind     = 3.8f;
        gustProbBase    = 0.012f;
        gustStrengthMax = 1.55f;
        thermalFreqBase = 0.035f;
    } else if (S10_strEqNoCase(v_code, "OCEAN")) {
        // 기본값 유지
    } else if (S10_strEqNoCase(v_code, "MOUNTAIN")) {
        baseMinWind     = 2.2f;
        baseMaxWind     = 7.5f;
        gustProbBase    = 0.045f;
        gustStrengthMax = 2.20f;
        thermalFreqBase = 0.028f;
    } else if (S10_strEqNoCase(v_code, "PLAINS")) {
        baseMinWind     = 4.0f;
        baseMaxWind     = 8.8f;
        gustProbBase    = 0.070f;
        gustStrengthMax = 2.40f;
        thermalFreqBase = 0.018f;
    } else if (S10_strEqNoCase(v_code, "HARBOR_BREEZE") || S10_strEqNoCase(v_code, "HARBOUR_BREEZE")) {
        baseMinWind     = 2.25f;
        baseMaxWind     = 5.35f;
        gustProbBase    = 0.025f;
        gustStrengthMax = 1.80f;
        thermalFreqBase = 0.026f;
    } else if (S10_strEqNoCase(v_code, "FOREST_CANOPY")) {
        baseMinWind     = 1.35f;
        baseMaxWind     = 4.00f;
        gustProbBase    = 0.010f;
        gustStrengthMax = 1.50f;
        thermalFreqBase = 0.012f;
    } else if (S10_strEqNoCase(v_code, "URBAN_SUNSET")) {
        baseMinWind     = 1.80f;
        baseMaxWind     = 4.90f;
        gustProbBase    = 0.030f;
        gustStrengthMax = 2.00f;
        thermalFreqBase = 0.020f;
    } else if (S10_strEqNoCase(v_code, "TROPICAL_RAIN")) {
        baseMinWind     = 3.15f;
        baseMaxWind     = 8.05f;
        gustProbBase    = 0.060f;
        gustStrengthMax = 2.20f;
        thermalFreqBase = 0.038f;
    } else if (S10_strEqNoCase(v_code, "DESERT_NIGHT")) {
        baseMinWind     = 0.90f;
        baseMaxWind     = 3.10f;
        gustProbBase    = 0.005f;
        gustStrengthMax = 1.30f;
        thermalFreqBase = 0.008f;
    }

    // [입력 방어/안정성] 최소/최대 범위 보정
    if (baseMaxWind < baseMinWind) {
        const float v_tmp = baseMaxWind;
        baseMaxWind = baseMinWind;
        baseMinWind = v_tmp;
    }
    baseMinWind     = max(0.2f, baseMinWind);
    baseMaxWind     = min(11.0f, baseMaxWind);
    gustStrengthMax = max(1.0f, gustStrengthMax);
    gustProbBase    = max(0.0f, gustProbBase);
    thermalFreqBase = max(0.0f, thermalFreqBase);
}

/**
 * @brief Preset 기반으로 Phase 상태를 초기화합니다.
 *
 * [시간 기준]
 * - millis()를 직접 호출하지 않고 tick()에서 캡처한 _tickNowSec 사용.
 *
 * [단위]
 * - phaseStartSec/phaseDurationSec : [sec]
 * - phaseMinWind/phaseMaxWind      : [m/s]
 */
void CL_S10_Simulation::initPhaseFromBase() {
    phase         = EN_A10_WEATHER_PHASE_NORMAL; // NORMAL 상태로 시작
    phaseStartSec = _tickNowSec;                 // tick() 스냅샷 시간 기반

    float v_span = baseMaxWind - baseMinWind;
    if (v_span < 0.5f) {
        v_span = 0.5f;
    }

    // Normal Phase의 Min/Max Wind를 Base Wind Span 기준으로 설정
    phaseMinWind = baseMinWind + v_span * 0.15f;
    phaseMaxWind = baseMinWind + v_span * 0.85f;

    // 초기 phase 지속시간 정책(초)
    phaseDurationSec = A10_randRange(G_S10_PHASE_NORM_DUR_MIN_S, G_S10_PHASE_NORM_DUR_MAX_S);

    // 현재 풍속 및 목표 풍속을 기본 중간값으로 설정
    const float v_mid = (baseMinWind + baseMaxWind) * 0.5f;
    currentWindSpeed  = v_mid;
    targetWindSpeed   = v_mid;

    spectralEnergyBuf = 0.0f;
    spectralPhaseAcc  = 0.0f;
    windMomentum      = 0.0f;

    // [안정성] 최종 범위 보정
    phaseMinWind = max(0.2f, phaseMinWind);
    phaseMaxWind = min(11.0f, phaseMaxWind);
    if (phaseMaxWind < phaseMinWind + 0.2f) {
        phaseMaxWind = phaseMinWind + 0.2f;
    }
}

/**
 * @brief Phase 전환 조건을 체크하고 필요 시 Phase를 전환합니다.
 *
 * [전환 정책]
 * - CALM  -> 70% NORMAL, 30% STRONG
 * - STRONG-> 70% NORMAL, 30% CALM
 * - NORMAL-> 40% CALM, 40% NORMAL, 20% STRONG
 *
 * [시간/단위]
 * - phaseDurationSec : [sec]
 * - _tickNowSec      : tick()에서 캡처된 현재 시간 [sec]
 */
void CL_S10_Simulation::updatePhase() {
    if (!active) {
        return;
    }

    const float v_nowSec = _tickNowSec; // tick 스냅샷 시간 기반

    // 아직 현재 phase 지속시간이 끝나지 않으면 전환하지 않음
    if (v_nowSec - phaseStartSec < phaseDurationSec) {
        return;
    }

    const T_A10_WindPhase_t v_old = phase;
    const float             v_r   = A10_getRandom01(); // 0.0 ~ 1.0

    // 확률적 Phase 전환 로직
    if (v_old == EN_A10_WEATHER_PHASE_CALM) {
        phase = (v_r < 0.7f) ? EN_A10_WEATHER_PHASE_NORMAL : EN_A10_WEATHER_PHASE_STRONG;
    } else if (v_old == EN_A10_WEATHER_PHASE_STRONG) {
        phase = (v_r < 0.7f) ? EN_A10_WEATHER_PHASE_NORMAL : EN_A10_WEATHER_PHASE_CALM;
    } else { // NORMAL
        if (v_r < 0.4f) {
            phase = EN_A10_WEATHER_PHASE_CALM;
        } else if (v_r < 0.8f) {
            phase = EN_A10_WEATHER_PHASE_NORMAL;
        } else {
            phase = EN_A10_WEATHER_PHASE_STRONG;
        }
    }

    phaseStartSec = v_nowSec;

    float v_span = baseMaxWind - baseMinWind;
    if (v_span < 0.5f) {
        v_span = 0.5f;
    }

    // 새로운 Phase에 따른 Duration 및 풍속 범위 설정 (정책 상수 적용)
    if (phase == EN_A10_WEATHER_PHASE_CALM) {
        phaseDurationSec = A10_randRange(G_S10_PHASE_CALM_DUR_MIN_S, G_S10_PHASE_CALM_DUR_MAX_S);
        phaseMinWind     = baseMinWind;
        phaseMaxWind     = baseMinWind + v_span * 0.6f;
    } else if (phase == EN_A10_WEATHER_PHASE_NORMAL) {
        phaseDurationSec = A10_randRange(G_S10_PHASE_NORM_DUR_MIN_S, G_S10_PHASE_NORM_DUR_MAX_S);
        phaseMinWind     = baseMinWind + v_span * 0.15f;
        phaseMaxWind     = baseMinWind + v_span * 0.85f;
    } else { // STRONG
        phaseDurationSec = A10_randRange(G_S10_PHASE_STRONG_DUR_MIN_S, G_S10_PHASE_STRONG_DUR_MAX_S);
        phaseMinWind     = baseMinWind + v_span * 0.4f;
        phaseMaxWind     = baseMaxWind;
    }

    // 최종 풍속 범위 제한/보정
    phaseMinWind = max(0.2f, phaseMinWind);
    phaseMaxWind = min(11.0f, phaseMaxWind);
    if (phaseMaxWind < phaseMinWind + 0.2f) {
        phaseMaxWind = phaseMinWind + 0.2f;
    }

    // Phase 변경 후 새 목표 풍속 생성
    generateTarget();
}

// ==================================================
// Turbulence / Thermal Envelope
// ==================================================

/**
 * @brief Von Kármán 난류 모델을 12개 주파수 밴드의 합으로 근사하여 난류 성분을 계산합니다.
 * @param p_dt Delta Time [sec]
 *
 * [입력 방어/단위]
 * - p_dt: tick()에서 계산된 dt(초). 0 이하이면 의미가 없으므로 상위에서 클램프 권장.
 * - turbLenScale(L): 난류 길이 스케일(>=1.0)
 * - turbSigma(σ)   : 난류 세기(>=0.0)
 * - currentWindSpeed(U): 평균 풍속(>=0.1)
 *
 * [출력]
 * - spectralEnergyBuf: 난류 성분(평균 0 근처) [m/s] 형태로 누적/관성 반영
 */
void CL_S10_Simulation::calcTurb(float p_dt) {
    if (!active) {
        return;
    }

    // [입력 방어]
    if (p_dt <= 0.0f) {
        return;
    }

    const float v_L     = max(1.0f, turbLenScale);          // [m] 또는 상대 스케일
    const float v_sigma = max(0.0f, turbSigma);             // [-] (모델 내 표준편차)
    const float v_U     = max(0.1f, currentWindSpeed);       // [m/s]

    float v_sum = 0.0f;

    // 12개 주파수 밴드의 합으로 난류 스펙트럼 근사
    for (int v_i = 1; v_i <= 12; v_i++) {
        const float v_n   = (float)v_i * 0.1f;
        const float v_f   = v_n * v_U / v_L;   // [Hz] 유사
        const float v_fLU = v_f * v_L / v_U;   // 무차원

        const float v_term  = 70.8f * v_fLU * v_fLU;
        const float v_numer = 4.0f * v_sigma * v_sigma * (v_L / v_U) * (1.0f + v_term);
        const float v_denom = powf(1.0f + v_term, 5.0f / 6.0f);
        const float v_S     = (v_denom > 0.0f) ? (v_numer / v_denom) : 0.0f;

        const float v_phaseRate = 2.0f * (float)M_PI * v_f;
        const float v_phaseInc  = v_phaseRate * p_dt;

        // 위상: 누적 위상 + 현재 증가량 + 랜덤 노이즈
        const float v_phase = spectralPhaseAcc * (float)v_i + v_phaseInc + A10_randRange(-0.1f, 0.1f);

        const float v_bandWidth = 0.083f; // 1/12 근사
        const float v_amp       = sqrtf(max(0.0f, 2.0f * v_S * v_bandWidth));

        v_sum += v_amp * sinf(v_phase);
    }

    // 위상 누적
    spectralPhaseAcc += p_dt * 0.5f;
    if (spectralPhaseAcc > 2.0f * (float)M_PI) {
        spectralPhaseAcc -= 2.0f * (float)M_PI;
    }

    // 난류 에너지 버퍼에 관성(지수평활) 적용
    // - turbTimeScale: [sec] (클래스 멤버로 존재한다고 가정)
    const float v_tscale = max(0.001f, turbTimeScale);
    const float v_corr   = expf(-p_dt / v_tscale);

    spectralEnergyBuf = spectralEnergyBuf * v_corr + v_sum * (1.0f - v_corr);
}

/**
 * @brief 열기포가 활성화되었을 때, 시간 경과에 따른 가산 기여도(Envelope)를 계산합니다.
 *
 * [단위]
 * - thermalStartSec/_tickNowSec : [sec]
 * - thermalDuration             : [sec]
 * - thermalContribution         : 풍속 가산값 [m/s] (엄밀히는 모델 내부 가산)
 */
void CL_S10_Simulation::calcThermalEnvelope() {
    if (!active || !thermalActive) {
        return;
    }

    // tick 스냅샷 시간 기반
    const float v_age = _tickNowSec - thermalStartSec;

    // [입력 방어] duration은 0보다 커야 함
    if (thermalDuration <= 0.0f) {
        thermalActive       = false;
        thermalContribution = 0.0f;
        return;
    }

    // 지속 시간 초과 시 비활성화
    if (v_age >= thermalDuration) {
        thermalActive       = false;
        thermalContribution = 0.0f;
        return;
    }

    const float v_prog = A10_clampf(v_age / thermalDuration, 0.0f, 1.0f);
    float       v_env  = 0.0f;

    // 포락선: 증가(0~0.2) - 유지+지터(0.2~0.6) - 감소(0.6~1.0)
    if (v_prog < 0.2f) {
        const float v_r = v_prog / 0.2f;
        v_env = 1.0f - powf(1.0f - v_r, 2.0f);
    } else if (v_prog < 0.6f) {
        v_env = 1.0f;
        v_env += sinf(v_age * (0.8f + (float)phase * 0.2f) * 2.0f * (float)M_PI) * 0.15f;
    } else {
        const float v_r = (v_prog - 0.6f) / 0.4f;
        v_env = 1.0f - powf(v_r, 1.3f);
    }

    if (v_env < 0.0f) {
        v_env = 0.0f;
    }

    const float v_strength = max(1.0f, thermalStrength); // 1.0=영향 없음 기준
    thermalContribution = (v_strength - 1.0f) * v_env;
}

// ==================================================
// Gust / Thermal Spawn
// ==================================================

/**
 * @brief 돌풍 발생 조건을 체크하고, 활성화 시 돌풍 강도(gustIntensity)를 갱신합니다.
 *
 * [확률 의미(정규화)]
 * - gustProbBase는 "초당 발생률(rate, 1/sec)"이며,
 *   평가 주기 dtSec(실제 경과 시간)로 P=1-exp(-rate*dt)로 변환 후 비교한다.
 *
 * [시간 기준]
 * - tick() 스냅샷(_tickNowMs/_tickNowSec)만 사용. millis() 재호출 금지.
 */
void CL_S10_Simulation::updateGust() {
    if (!active) {
        return;
    }

    const float v_nowSec = _tickNowSec;

    // ------------------------------------------------------
    // (1) 돌풍 진행 중이면: 포락선 기반으로 강도 갱신
    // ------------------------------------------------------
    if (gustActive) {
        const float v_age = v_nowSec - gustStartSec;

        if (gustDuration <= 0.0f || v_age >= gustDuration) {
            gustActive    = false;
            gustIntensity = 1.0f;
            return;
        }

        const float v_prog = A10_clampf(v_age / gustDuration, 0.0f, 1.0f);
        float       v_env  = 0.0f;

        if (v_prog < 0.25f) {
            const float v_r = v_prog / 0.25f;
            v_env = 1.0f - powf(1.0f - v_r, 1.8f);
        } else if (v_prog < 0.65f) {
            v_env = 1.0f;
            v_env += sinf(v_age * (1.5f + (float)phase * 0.5f)) * 0.08f;
        } else {
            const float v_r = (v_prog - 0.65f) / 0.35f;
            v_env = 1.0f - powf(v_r, 1.5f);
        }

        if (v_env < 0.0f) {
            v_env = 0.0f;
        }

        const float v_maxMul = max(1.0f, gustStrengthMax);
        gustIntensity = 1.0f + (v_maxMul - 1.0f) * v_env;
        return;
    }

    // ------------------------------------------------------
    // (2) 새 돌풍 트리거: 평가 간격 + dt 기반 확률 보정
    // ------------------------------------------------------
    const unsigned long v_nowMs     = _tickNowMs;
    const unsigned long v_elapsedMs = (v_nowMs >= lastGustCheckMs) ? (v_nowMs - lastGustCheckMs) : 0UL;

    // 최소 평가 간격 정책
    if (v_elapsedMs < G_S10_GUST_EVAL_MIN_MS) {
        return;
    }

    lastGustCheckMs = v_nowMs;

    const float v_dtSec = (float)v_elapsedMs / 1000.0f;
    if (v_dtSec <= 0.0f) {
        return;
    }

    // ------------------------------------------------------
    // (3) rate(1/sec) 구성: base * user * wind/phase 가중치
    // ------------------------------------------------------
    const float v_user = A10_clampf(userGustFreq, 0.0f, 100.0f) / 100.0f; // [0..1]
    const float v_wfac = 1.0f + (currentWindSpeed / 8.9f) * 0.5f;

    float v_phaseMul = 1.0f;
    if (phase == EN_A10_WEATHER_PHASE_CALM) {
        v_phaseMul = 0.3f * v_wfac;
    } else if (phase == EN_A10_WEATHER_PHASE_STRONG) {
        v_phaseMul = 2.2f * v_wfac;
    } else { // NORMAL
        v_phaseMul = 0.9f * v_wfac;
    }

    // gustProbBase: [1/sec]
    const float v_ratePerSec = max(0.0f, gustProbBase) * v_user * v_phaseMul;

    // dt에 따른 확률로 변환(자동 보정)
    const float v_p = S10_probFromRatePerSec(v_ratePerSec, v_dtSec);

    if (A10_getRandom01() < v_p) {
        gustActive   = true;
        gustStartSec = v_nowSec;

        const float v_speedF = currentWindSpeed / 6.7f;

        // Phase에 따라 지속 시간/강도 랜덤 설정
        if (phase == EN_A10_WEATHER_PHASE_CALM) {
            gustDuration  = A10_randRange(3.0f, 8.0f);
            gustIntensity = A10_randRange(1.08f, 1.33f);
        } else if (phase == EN_A10_WEATHER_PHASE_STRONG) {
            gustDuration  = A10_randRange(0.8f, 3.3f);
            gustIntensity = A10_randRange(1.3f, 1.3f + 0.9f * (1.0f + v_speedF * 0.3f));
        } else { // NORMAL
            gustDuration  = A10_randRange(1.8f, 5.8f);
            gustIntensity = A10_randRange(1.15f, 1.15f + 0.5f * (1.0f + v_speedF * 0.2f));
        }

        // 최대 제한값 적용
        const float v_maxMul = max(1.0f, gustStrengthMax);
        if (gustIntensity > v_maxMul) {
            gustIntensity = v_maxMul;
        }
    }
}

/**
 * @brief 열기포 발생 조건을 체크하고, 발생 시 상태를 활성화합니다.
 *
 * [확률 의미(정규화)]
 * - thermalFreqBase는 "초당 발생률(rate, 1/sec)"이며,
 *   평가 주기 dtSec(실제 경과 시간)로 P=1-exp(-rate*dt)로 변환 후 비교한다.
 *
 * [주의]
 * - thermalActive가 true인 동안에는 중복 발생을 막는다.
 */
void CL_S10_Simulation::updateThermal() {
    if (!active || thermalActive) {
        return;
    }

    const unsigned long v_nowMs     = _tickNowMs;
    const unsigned long v_elapsedMs = (v_nowMs >= lastThermalCheckMs) ? (v_nowMs - lastThermalCheckMs) : 0UL;

    if (v_elapsedMs < G_S10_THERM_EVAL_MIN_MS) {
        return;
    }

    lastThermalCheckMs = v_nowMs;

    const float v_dtSec = (float)v_elapsedMs / 1000.0f;
    if (v_dtSec <= 0.0f) {
        return;
    }

    // rate(1/sec) 구성: base * strengthMul * windMul * phaseMul
    const float v_strength = max(1.0f, thermalStrength);
    const float v_strengthMul = (0.6f + 0.4f * min(3.0f, max(0.5f, v_strength)));

    const float v_wfac = 1.0f + (currentWindSpeed / 8.0f) * 0.3f;

    // 열기포는 약풍에서 더 잘 발생한다는 가정: CALM 가중↑, STRONG 가중↓
    const float v_phaseMul = (phase == EN_A10_WEATHER_PHASE_CALM) ? 1.2f
                           : (phase == EN_A10_WEATHER_PHASE_STRONG ? 0.7f : 1.0f);

    // thermalFreqBase: [1/sec]
    const float v_ratePerSec = max(0.0f, thermalFreqBase) * v_strengthMul * v_wfac * v_phaseMul;

    // dt에 따른 확률로 변환(자동 보정)
    const float v_p = S10_probFromRatePerSec(v_ratePerSec, v_dtSec);

    if (A10_getRandom01() < v_p) {
        thermalActive   = true;
        thermalStartSec = _tickNowSec; // tickNowMs/1000 금지 → tickNowSec 사용

        // 지속 시간 정책 상수 적용 + phase 보정
        float v_d = A10_randRange(G_S10_THERM_DUR_MIN_S, G_S10_THERM_DUR_MAX_S);
        if (phase == EN_A10_WEATHER_PHASE_CALM) {
            v_d *= G_S10_THERM_DUR_MUL_CALM;
        } else if (phase == EN_A10_WEATHER_PHASE_STRONG) {
            v_d *= G_S10_THERM_DUR_MUL_STRONG;
        }

        thermalDuration = max(0.1f, v_d); // [입력 방어]
    }
}

// ==================================================
// Target generation
// ==================================================

/**
 * @brief 새로운 목표 풍속(targetWindSpeed)을 생성합니다.
 *
 * [의미/단위]
 * - phaseMinWind~phaseMaxWind 범위 내에서 목표 생성 [m/s]
 * - 중앙값 바이어스로 극단값을 줄임
 * - userVariability/phase/turbLenScale/currentWindSpeed를 반영해 windChangeRate(수렴률) 결정
 *
 * [입력 방어]
 * - active=false면 동작하지 않음
 */
void CL_S10_Simulation::generateTarget() {
    if (!active) {
        return;
    }

    float v_range = phaseMaxWind - phaseMinWind;
    if (v_range < 0.2f) {
        v_range = 0.2f;
    }

    // Phase 범위 내 1차 목표 생성
    float v_w    = phaseMinWind + A10_getRandom01() * v_range;
    float v_mid  = (phaseMinWind + phaseMaxWind) * 0.5f;
    float v_bias = A10_randRange(0.0f, 1.0f);

    // 중앙값 바이어스(극단 방지)
    v_w = (v_w + v_mid * v_bias) / (1.0f + v_bias);
    targetWindSpeed = v_w;

    // variability(0~100) -> 0~1
    const float v_var = A10_clampf(userVariability, 0.0f, 100.0f) / 100.0f;

    float v_base = 0.15f;

    // Phase별 변화율 기본값
    if (phase == EN_A10_WEATHER_PHASE_CALM) {
        v_base = 0.08f + v_var * 0.12f;
    } else if (phase == EN_A10_WEATHER_PHASE_STRONG) {
        v_base = 0.25f + v_var * 0.35f;
    } else { // NORMAL
        v_base = 0.15f + v_var * 0.25f;
    }

    // 풍속/난류 길이 스케일 가중치
    const float v_U      = max(0.1f, currentWindSpeed);
    const float v_tscale = turbLenScale / v_U;
    v_base *= (1.0f + v_tscale * 0.1f);

    // 최종 변화율: 지터 + 제한
    windChangeRate = constrain(v_base * A10_randRange(0.7f, 1.7f), 0.04f, 0.5f);
}
