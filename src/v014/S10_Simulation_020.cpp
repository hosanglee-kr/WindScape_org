/*
 * ------------------------------------------------------
 * 소스명 : S10_Simulation_020.cpp
 * 모듈약어 : S10
 * 모듈명 : Smart Nature Wind 풍속 시뮬레이션 Manager (v019, Full)
 * ------------------------------------------------------
 * 기능 요약:
 * - CL_S10_Simulation 클래스의 구현부
 * - 난류, 돌풍, 열기포 등 복합적인 자연풍 시뮬레이션 로직 포함
 * ------------------------------------------------------
 */

#include "S10_Simulation_020.h" // 해당 클래스 헤더 파일 포함

// 외부 종속성 헤더 포함 (실제 환경에서는 각 모듈 헤더를 통해 가져옴)
#include "A10_Const_015.h"
#include "C10_ConfigManager_024.h"
#include "D10_Logger_016.h"
#include "P10_PWM_ctrl_014.h"

// ------------------------------------------------------
// 정적 멤버 정의
// ------------------------------------------------------
// 차트 버퍼 (최대 120개 샘플)
std::deque<CL_S10_Simulation::ST_ChartEntry> CL_S10_Simulation::s_chartBuffer;
// 차트 로그/샘플링 시간 추적
unsigned long                                CL_S10_Simulation::s_lastChartLogMs    = 0;
unsigned long                                CL_S10_Simulation::s_lastChartSampleMs = 0;


// ==================================================
// 초기화 / 정지 / 리셋
// ==================================================
void CL_S10_Simulation::begin(CL_P10_PWM& p_pwm) {
    _pwm = &p_pwm; // PWM 제어기 포인터 저장
    resetDefaults();
    memset(history, 0, sizeof(history)); // 풍속 이력 버퍼 초기화
    historyIndex = 0;
    historyCount = 0;
    (void)esp_random();  // ESP32의 하드웨어 기반 랜덤 시드로 지터(Jitter) 유도
    CL_D10_Logger::log(EN_L10_LOG_INFO, "[S10] begin()");
}

void CL_S10_Simulation::stop() {
    active           = false;
    phase            = EN_A10_WEATHER_PHASE_CALM;
    targetWindSpeed  = 0.0f;
    currentWindSpeed = 0.0f;
    if (_pwm) {
        _pwm->P10_setDutyPercent(0.0f); // PWM 0% 설정
    }
}

void CL_S10_Simulation::resetDefaults() {
    // ... (기본값 설정 코드는 주석 생략) ...
    // 기본 파라미터 값 설정
    // ...
    applyPresetCore(presetCode); // 설정된 PresetCode 기반으로 base 파라미터 재설정
    initPhaseFromBase(); // base 파라미터 기반으로 초기 Phase 상태 설정
}

// ==================================================
// 메인 tick (CT10에서 주기 호출)
// ==================================================
void CL_S10_Simulation::tick() {
    portENTER_CRITICAL(&_simMutex); // 임계 영역 진입: 다중 Task 접근 방지

    if (!active){
        portEXIT_CRITICAL(&_simMutex);
        return;
    }

    unsigned long   v_now        = millis();
    static uint32_t s_jitterSeed = 0;

    // 업데이트 주기에 랜덤 지터(40ms + 0~60ms) 적용하여 자연스러운 불규칙성 추가
    uint32_t v_interval = 40u + (s_jitterSeed % 60u);
    if (v_now - lastUpdateMs < v_interval){
        portEXIT_CRITICAL(&_simMutex);
        return;
    }
    
    s_jitterSeed = esp_random(); // 다음 지터 생성 시드 갱신

    // Time Delta (dt) 계산 및 상한선 설정 (오작동 방지)
    float v_dt   = (v_now - lastUpdateMs) / 1000.0f;
    v_dt = A10_clampf(v_dt, 0.001f, 0.5f);
    lastUpdateMs = v_now;

    float             v_prevWind  = currentWindSpeed;
    T_A10_WindPhase_t v_prevPhase = phase;
    updatePhase(); // Phase 전환 로직 체크

    // Phase 변화 또는 급격한 풍속 변화(> 2.0f) 시 WebSocket 브로드캐스트 요청
    float v_delta = fabsf(currentWindSpeed - v_prevWind);
    if (phase != v_prevPhase || v_delta > 2.0f) {
        // ... (JSON 생성 및 SC10 브로드캐스트/Dirty 마크 로직) ...
        SC10_broadcastChart(v_doc, true);    // diffOnly 모드로 실시간 전송
        SC10_markDirty("chart");             // Chart 갱신 필요 표시
    }

    // ---- 내부 물리 계산 ----
    calcTurb(v_dt);           // 난류 에너지 계산
    calcThermalEnvelope();    // 열기포 가산값 포락선 계산
    updateGust();             // 돌풍 상태 갱신/트리거 체크
    updateThermal();          // 열기포 상태 갱신/트리거 체크

    // 목표 풍속으로 점진 수렴 + 관성 반영
    float v_diff   = targetWindSpeed - currentWindSpeed;
    float v_change = v_diff * windChangeRate * v_dt;
    // 관성 (이전 모멘텀 85% + 신규 변화량 15%) 적용
    windMomentum   = windMomentum * 0.85f + v_change * 0.15f;
    windMomentum   = constrain(windMomentum, -0.5f, 0.5f); // 모멘텀 상한/하한 제한

    // 최종 풍속 = 현재 풍속 + 모멘텀 + 난류 에너지
    float v_new      = currentWindSpeed + windMomentum + spectralEnergyBuf;
    currentWindSpeed = constrain(v_new, 0.2f, 11.0f); // 최종 풍속 범위 제한 (0.2~11.0)

    // 목표 풍속 재생성 로직: 풍속이 목표에 근접했거나, 일정 확률로 새로운 목표를 생성
    float v_th = 0.5f + (currentWindSpeed / 20.0f);
    if (fabsf(v_diff) < v_th) {
        if (A10_randRange(0.0f, 100.0f) < 30.0f)
            generateTarget();
    } else {
        if (A10_randRange(0.0f, 100.0f) < 6.0f)
            generateTarget();
    }

    // PWM 제어 반영: 풍속 기반 PWM 계산 후, 돌풍 및 열기포 영향을 최종 반영
    float v_pwmPct = currentWindSpeed * 10.0f + 10.0f;
    v_pwmPct *= gustIntensity;
    v_pwmPct += thermalContribution * 5.0f;
    applyFan(v_pwmPct); // min/max/intensity 제한을 거쳐 _pwm에 전달

    // 풍속 히스토리 갱신 (순환 버퍼) 및 평균 캐시 갱신
    _updateWindHistory(currentWindSpeed);

    // 차트 샘플링 (기본 1Hz, 돌풍/열기포 시 2Hz)
    v_interval = (gustActive || thermalActive) ? 500UL : 1000UL;
    if (millis() - s_lastChartLogMs > v_interval) {
        if (s_chartBuffer.size() >= 120)
            s_chartBuffer.pop_front(); // 120개 초과 시 가장 오래된 샘플 제거

        // ... (ST_ChartEntry 구조체에 현재 상태 값 저장) ...
        s_chartBuffer.push_back(v_e);
        s_lastChartLogMs = millis();
    }

    portEXIT_CRITICAL(&_simMutex); // 임계 영역 종료
}

// ==================================================
// 해석 결과 적용: resolveWindParams → 여기 호출
// ==================================================
void CL_S10_Simulation::applyResolvedWind(const ST_A10_ResolvedWind_t& p_resolved) {
    // ... (PresetCode, StyleCode 저장 및 파라미터 constrain 적용 후 저장) ...
    // C10 해석 결과(Intensity, Variability, Limit 등)를 S10 멤버 변수에 매핑

    // preset별 baseMin/baseMax/확률/강도 설정
    applyPresetCore(presetCode);

    // Variability에 따라 풍속 수렴 변화율(windChangeRate) 재설정
    float v_varNorm = userVariability / 100.0f;
    windChangeRate  = constrain(0.10f + v_varNorm * 0.20f, 0.06f, 0.34f);

    // Phase 초기화 및 시뮬레이션 활성화
    initPhaseFromBase();
    active              = true;
    // ... (Gust/Thermal 상태 초기화) ...

    generateTarget(); // 즉시 새로운 목표 풍속 생성
}

// ==================================================
// JSON Export
// ==================================================
void CL_S10_Simulation::toJson(JsonObject& p_obj) {
    portENTER_CRITICAL(&_simMutex); // 읽기 시에도 데이터 안정성을 위해 Mutex 사용
    
    // ... (active, phase, windSpeed, targetWind, gustActive, pwmDuty 등 모든 상태를 JSON 객체에 담음) ...

    portEXIT_CRITICAL(&_simMutex);
}

void CL_S10_Simulation::toChartJson(JsonDocument& p_doc, bool p_diffOnly) {
    // ... (차트 메타 정보 추가) ...

    // 10초 간격 전송 제한 (WebAPI 부하 경감)
    if (millis() - s_lastChartSampleMs < 10000UL)
        return;
    s_lastChartSampleMs = millis();

    if (s_chartBuffer.empty())
        return;

    if (p_diffOnly) {
        // diffOnly 모드: 마지막 1개 샘플만 전송 (WebSocket 최적화)
        // ...
        return;
    }

    // 전체 chartBuffer 전송 (기본 모드)
    // ...
}

// ==================================================
// 내부 구현부
// ==================================================

// PWM 적용 (min/max/intensity 반영)
void CL_S10_Simulation::applyFan(float p_pct) {
    if (!_pwm) return;

    float v_req   = p_pct / 100.0f;
    float v_limit = fanLimitPct / 100.0f;
    float v_min   = minFanPct / 100.0f;
    float v_int   = userIntensity / 100.0f;

    // Intensity와 Fan Limit/Min을 적용하여 최종 PWM Duty를 계산
    if (active) {
        v_req *= v_int;
    }

    v_req = max(v_req, v_min);
    v_req = min(v_req, v_limit);

    _pwm->P10_setDutyPercent(v_req * 100.0f);
}

// presetCode에 따라 기본 스펙 셋업
void CL_S10_Simulation::applyPresetCore(const char* p_code) {
    // ... (strcasecmp을 이용해 Preset Code에 따른 baseMinWind, gustProbBase 등 물리 상수 설정) ...
    // 각 Preset (OCEAN, MOUNTAIN 등)에 맞는 고유한 풍속/확률/강도 기본값 설정
}

// preset 기반 Phase 초기화
void CL_S10_Simulation::initPhaseFromBase() {
    // ... (NORMAL Phase로 초기화 및 baseWind를 기반으로 phaseMinWind, phaseMaxWind 설정) ...
}

// Phase 전환 로직
void CL_S10_Simulation::updatePhase() {
    if (!active) return;
    float v_now = millis() / 1000.0f;

    // 현재 Phase 지속 시간이 끝나지 않았으면 리턴
    if (v_now - phaseStartSec < phaseDurationSec)
        return;

    // 확률적 다음 Phase 결정 (CALM, NORMAL, STRONG 간 전환 확률 적용)
    T_A10_WindPhase_t v_old = phase;
    float             v_r   = A10_getRandom01();
    // ... (전환 확률 로직 구현) ...
    
    phaseStartSec = v_now;

    // 새로운 Phase에 맞는 지속 시간(Duration)과 풍속 범위(Min/Max Wind) 설정
    // 각 Phase는 서로 다른 지속 시간과 풍속 분포를 가짐 (예: CALM은 지속 시간 길고 범위 좁음)
    if (phase == EN_A10_WEATHER_PHASE_CALM) {
        phaseDurationSec = A10_randRange(90.0f, 210.0f);
        // ... (Phase별 Min/Max Wind 설정) ...
    } else if (phase == EN_A10_WEATHER_PHASE_NORMAL) {
        // ...
    } else {  // STRONG
        // ...
    }
    
    // 최종 Min/Max Wind 값 제한
    phaseMinWind = max(0.2f, phaseMinWind);
    phaseMaxWind = min(11.0f, phaseMaxWind);

    generateTarget(); // Phase 전환 후 새 목표 풍속 즉시 생성
}

// Von Kármán 난류 근사
void CL_S10_Simulation::calcTurb(float p_dt) {
    // ... (난류 스케일, 강도, 현재 풍속 등을 이용해 난류 성분 계산) ...

    // 12개 주파수 밴드의 합으로 Von Kármán 스펙트럼 근사 (난류 주파수 성분 분리)
    float v_sum = 0.0f;
    for (int v_i = 1; v_i <= 12; v_i++) {
        // v_S: 난류 스펙트럼 밀도 계산
        // v_phase: 각 밴드의 위상 증가 및 랜덤 지터 적용
        // v_amp: 진폭 계산 (sqrt(2 * S * BandWidth))
        v_sum += v_amp * sinf(v_phase);
    }

    // 위상 누적 값 업데이트
    spectralPhaseAcc += p_dt * 0.5f;

    // 지수적 상관 관계(Exponential Correlation)를 적용하여 이전 난류 성분과 현재 성분을 혼합 (시간적 연속성 부여)
    float v_corr      = expf(-p_dt / turbTimeScale);
    spectralEnergyBuf = spectralEnergyBuf * v_corr + v_sum * (1.0f - v_corr);
}

// 열기포 포락(Active 시)
void CL_S10_Simulation::calcThermalEnvelope() {
    if (!active || !thermalActive) return;

    // 열기포 지속 시간(thermalDuration) 대비 경과 시간(v_age)에 따른 포락선(v_env) 계산
    float v_age = millis() / 1000.0f - thermalStartSec;
    if (v_age >= thermalDuration) {
        thermalActive       = false;
        thermalContribution = 0.0f;
        return;
    }

    float v_prog = v_age / thermalDuration; // 진행률 (0~1)
    float v_env  = 0.0f;

    // 진입(0.0~0.2) -> 유지(0.2~0.6) -> 이탈(0.6~1.0) 단계별 포락선 곡선 적용
    if (v_prog < 0.2f) {
        // ...
    } else if (v_prog < 0.6f) {
        // ... (유지 + 사인파 변동 추가)
    } else {
        // ... (부드러운 하강)
    }

    // 최종 열기포 기여도 계산: (강도 - 1.0f) * 포락선
    float v_strength    = max(1.0f, thermalStrength);
    thermalContribution = (v_strength - 1.0f) * v_env;
}

// 돌풍 상태 갱신
void CL_S10_Simulation::updateGust() {
    if (!active) return;

    if (gustActive) {
        // 돌풍 활성 시: 경과 시간에 따른 강도 포락선 계산 (진입-유지-이탈)
        // ... (v_prog를 이용한 포락선 v_env 계산) ...
        gustIntensity = 1.0f + (gustStrengthMax - 1.0f) * v_env; // 1.0f + (최대 강도 배율 - 1.0f) * 포락선
        return;
    }

    // 새로운 돌풍 트리거: 0.5초 주기로 확률 체크
    unsigned long v_nowMs = millis();
    if (v_nowMs - lastGustCheckMs < 500UL) return;
    lastGustCheckMs = v_nowMs;

    // 돌풍 발생 확률(v_p) 계산: Base 확률 * 사용자 주파수 * (Phase & 풍속 가중치)
    float v_p = v_base * v_user * v_pmul;

    if (A10_getRandom01() < v_p) {
        gustActive   = true;
        gustStartSec = v_nowSec;

        // Phase에 따라 돌풍 지속 시간(gustDuration) 및 초기 강도(gustIntensity) 무작위 결정
        if (phase == EN_A10_WEATHER_PHASE_CALM) {
            // ... (약하고 긴 돌풍)
        } else if (phase == EN_A10_WEATHER_PHASE_STRONG) {
            // ... (강하고 짧은 돌풍)
        } else {
            // ...
        }

        // 최대 강도 제한
        if (gustIntensity > gustStrengthMax) {
            gustIntensity = gustStrengthMax;
        }
    }
}

// 열기포 트리거
void CL_S10_Simulation::updateThermal() {
    if (!active || thermalActive) return;

    // 0.7초 주기로 확률 체크
    // ...
    
    // 열기포 발생 확률(v_freq) 계산: Base 빈도 * 강도 * 풍속 * Phase 가중치
    float v_freq = thermalFreqBase * (0.6f + 0.4f * min(3.0f, max(0.5f, v_strength))) * v_wfac * v_phaseMul;

    if (A10_getRandom01() < v_freq) {
        thermalActive   = true;
        thermalStartSec = v_nowMs / 1000.0f;

        // Phase에 따라 열기포 지속 시간 무작위 결정
        float v_d = A10_randRange(8.0f, 14.0f);
        if (phase == EN_A10_WEATHER_PHASE_CALM) {
            v_d *= 1.3f;
        } // ...
        thermalDuration = v_d;
    }
}

// 목표 풍속 재설정
void CL_S10_Simulation::generateTarget() {
    if (!active) return;

    // Phase의 Min/Max Wind 범위 내에서 무작위 목표 풍속(v_w) 생성
    float v_range = phaseMaxWind - phaseMinWind;
    float v_w    = phaseMinWind + A10_getRandom01() * v_range;
    
    // 중간값(v_mid)으로 살짝 끌어당기는 바이어스(Bias) 적용 (극단값 생성 방지)
    float v_mid  = (phaseMinWind + phaseMaxWind) * 0.5f;
    float v_bias = A10_randRange(0.0f, 1.0f);
    v_w             = (v_w + v_mid * v_bias) / (1.0f + v_bias);
    targetWindSpeed = v_w;

    // Phase, Variability, 난류 스케일에 따라 목표 풍속으로 수렴하는 변화율(windChangeRate) 재결정
    float v_var = userVariability / 100.0f;
    // ... (Phase별 변화율 기본값 + Variability 반영) ...
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

    // 매 샘플마다 평균을 계산하여 캐시(avgWindCached) 업데이트 (O(N) 계산이지만 N=60으로 작음)
    float v_sum = 0.0f;
    for (uint8_t i = 0; i < historyCount; i++) v_sum += history[i];
    avgWindCached = v_sum / (float)historyCount;
}

// --------------------------------------------------
// 캐시된 평균 풍속 반환 (O(1))
// --------------------------------------------------
float CL_S10_Simulation::_getAvgWindFast() const {
    // 캐시된 평균 풍속을 반환 (별도 계산 없이 즉시 접근 가능)
    return (historyCount > 0) ? avgWindCached : currentWindSpeed;
}
