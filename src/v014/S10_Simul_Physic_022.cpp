/*
 * ------------------------------------------------------
 * 소스명 : S10_Simul_Physic_022.cpp
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
// #include "P10_PWM_ctrl_014.h"


// ==================================================
// [S10 초(Seconds) 정책 상수]
// ==================================================

// Phase 지속시간 범위(초) 정책
static const float G_S10_PHASE_CALM_DUR_MIN_S   = 90.0f;
static const float G_S10_PHASE_CALM_DUR_MAX_S   = 210.0f;
static const float G_S10_PHASE_NORM_DUR_MIN_S   = 120.0f;
static const float G_S10_PHASE_NORM_DUR_MAX_S   = 300.0f;
static const float G_S10_PHASE_STRONG_DUR_MIN_S = 60.0f;
static const float G_S10_PHASE_STRONG_DUR_MAX_S = 150.0f;

// Thermal(열기포) 지속시간 범위(초) 정책
static const float G_S10_THERM_DUR_MIN_S        = 8.0f;
static const float G_S10_THERM_DUR_MAX_S        = 14.0f;

// Thermal phase 보정 배율(지속시간에 적용)
static const float G_S10_THERM_DUR_MUL_CALM     = 1.3f;
static const float G_S10_THERM_DUR_MUL_STRONG   = 0.8f;




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

    // tick()에서 캡처한 시간 기반(일관성) 사용
    phaseStartSec = _tickNowSec;
    // phaseStartSec = millis() / 1000.0f;

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

    // tick()에서 캡처한 시간(초) 기반 사용 (millis() 재호출 금지)
    const float v_nowSec = _tickNowSec;

    if (v_nowSec - phaseStartSec < phaseDurationSec) {
        return;
    }

    /*
    float v_now = millis() / 1000.0f;
    // 현재 Phase 지속 시간(phaseDurationSec)이 지나지 않았으면 전환하지 않음
    if (v_now - phaseStartSec < phaseDurationSec)
        return;
    */

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

    phaseStartSec = v_nowSec;
    // phaseStartSec = v_now;

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

    // tick() 스냅샷 시간 기반 사용
    const float v_age = _tickNowSec - thermalStartSec;
    // float v_t   = millis() / 1000.0f;
    // float v_age = v_t - thermalStartSec;

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
    const float v_nowSec = _tickNowSec;
    // float v_nowSec = millis() / 1000.0f;

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
    const unsigned long v_nowMs = _tickNowMs;
    if (v_nowMs - lastGustCheckMs < 500UL) {
        return;
    }
    lastGustCheckMs = v_nowMs;
    /*
    unsigned long v_nowMs = millis();
    if (v_nowMs - lastGustCheckMs < 500UL) {
        return;
    }
    lastGustCheckMs = v_nowMs;
    */

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

    const unsigned long v_nowMs = _tickNowMs;
    if (v_nowMs - lastThermalCheckMs < 700UL) {
        return;
    }
    lastThermalCheckMs = v_nowMs;
    /*
    unsigned long v_nowMs = millis();
    if (v_nowMs - lastThermalCheckMs < 700UL) {
        return;
    }
    lastThermalCheckMs = v_nowMs;
    */

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

        // tickNowMs/1000 계산 금지 → tickNowSec 사용
        thermalStartSec = _tickNowSec;
        // thermalStartSec = v_nowMs / 1000.0f;

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

