#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : S10_Simulation_017.h
 * 모듈약어 : S10
 * 모듈명 : Smart Nature Wind 풍속 시뮬레이션 Manager (v017, Full)
 * ------------------------------------------------------
 * 기능 요약:
 *  - 자연풍 기반 풍속 시뮬레이션 (Phase / 돌풍 / 난류 / 열기포 / 지터)
 *  - PWM 제어기(CL_P10_PWM) 연동
 *  - C10 해석 결과(ST_A10_ResolvedWind_t) 기반 동작
 *  - Schedules / UserProfiles 세그먼트에서 presetCode + styleCode + adjust
 *    -> C10_resolveWindParams -> applyResolvedWind(...) 연계
 *  - 최근 120초 Chart 버퍼 제공 (웹 UI 시각화용)
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
#include <ArduinoJson.h>
#include <deque>
#include <cmath>
#include <string.h>

#include "A10_Const_014.h"
#include "C10_ConfigManager_020.h"
#include "D10_Logger_014.h"
#include "P10_PWM_ctrl_014.h"


// ✅ 전방 선언으로 순환참조 방지
class CL_W10_WebAPI;

// ======================================================
// CL_S10_Simulation
//  - 헤더 전용, 전체 알고리즘 포함
// ======================================================
class CL_S10_Simulation {
public:
	// ------------------ 상태/파라미터 ------------------
	bool   active          = false;
	bool   fanPowerEnabled = true;

	float  currentWindSpeed = 3.6f;  // m/s 개념의 내부 단위
	float  targetWindSpeed  = 3.6f;

	char   presetCode[24]   = {0};
	char   styleCode[24]    = {0};

	// 해석 결과 기반 파라미터 (ST_A10_ResolvedWind_t 매핑)
	float  userIntensity    = 70.0f;   // 0~100
	float  userVariability  = 50.0f;   // 0~100
	float  userGustFreq     = 45.0f;   // 0~100
	float  minFanPct        = 10.0f;   // 0~100
	float  fanLimitPct      = 90.0f;   // 0~100

	float  turbLenScale     = 40.0f;   // 난류 스케일
	float  turbSigma        = 0.5f;    // 난류 세기
	float  thermalStrength  = 2.0f;    // 열기포 강도
	float  thermalRadius    = 18.0f;   // 열기포 특성 반경 (사용처 확장 여지)

	// Phase 상태
	T_A10_WindPhase_t phase          = EN_A10_WEATHER_PHASE_NORMAL;
	float  phaseStartSec             = 0.0f;
	float  phaseDurationSec          = 120.0f;
	float  phaseMinWind              = 2.0f;
	float  phaseMaxWind              = 6.0f;

	// Preset 기반 범위/확률
	float  baseMinWind               = 1.8f;
	float  baseMaxWind               = 5.5f;
	float  gustProbBase              = 0.040f;
	float  gustStrengthMax           = 2.1f;
	float  thermalFreqBase           = 0.022f;

	// 난류(스펙트럼) 상태
	float  spectralEnergyBuf         = 0.0f;
	float  spectralPhaseAcc          = 0.0f;
	float  turbTimeScale             = 5.0f;

	// 돌풍 상태
	bool           gustActive        = false;
	float          gustStartSec      = 0.0f;
	float          gustDuration      = 3.0f;
	float          gustIntensity     = 1.0f;     // PWM 배율
	unsigned long  lastGustCheckMs   = 0;

	// 열기포 상태
	bool           thermalActive       = false;
	float          thermalStartSec     = 0.0f;
	float          thermalDuration     = 8.0f;
	float          thermalContribution = 0.0f;   // PWM 가산 영향
	unsigned long  lastThermalCheckMs  = 0;

	// 관성/업데이트
	float          windChangeRate      = 0.12f;
	float          windMomentum        = 0.0f;
	unsigned long  lastUpdateMs        = 0;

	// 차트 버퍼 (최근 120 샘플, 1Hz 기준)
	struct ST_ChartEntry {
		unsigned long timestamp;
		float wind_speed;
		float pwm_duty;
		float intensity;
		float variability;
		float turbulence_sigma;
		uint8_t preset_index;
		bool gust_active;
		bool thermal_active;
	};
	static std::deque<ST_ChartEntry> s_chartBuffer;
	static unsigned long             s_lastChartLogMs;

public:
	// ==================================================
	// 초기화 / 정지 / 리셋
	// ==================================================
	void begin(CL_P10_PWM& p_pwm) {
		_pwm = &p_pwm;
		resetDefaults();
		(void)esp_random(); // 랜덤 시드/지터 유도
		CL_D10_Logger::log(EN_L10_LOG_INFO, "[S10] begin()");
	}

	void stop() {
      active           = false;
      phase            = EN_A10_WEATHER_PHASE_CALM;
      targetWindSpeed  = 0.0f;
      currentWindSpeed = 0.0f;
      if (_pwm) {
          _pwm->P10_setDutyPercent(0.0f);
      }
    }

	void resetDefaults() {
		active          = false;
		fanPowerEnabled = true;

		strlcpy(presetCode, "OCEAN", sizeof(presetCode));
		strlcpy(styleCode,  "BALANCE", sizeof(styleCode));

		userIntensity    = 70.0f;
		userVariability  = 50.0f;
		userGustFreq     = 45.0f;
		minFanPct        = 10.0f;
		fanLimitPct      = 90.0f;

		turbLenScale     = 40.0f;
		turbSigma        = 0.5f;
		thermalStrength  = 2.0f;
		thermalRadius    = 18.0f;

		baseMinWind      = 1.8f;
		baseMaxWind      = 5.5f;
		gustProbBase     = 0.040f;
		gustStrengthMax  = 2.10f;
		thermalFreqBase  = 0.022f;

		currentWindSpeed = 3.6f;
		targetWindSpeed  = 3.6f;
		windMomentum     = 0.0f;

		spectralEnergyBuf= 0.0f;
		spectralPhaseAcc = 0.0f;
		lastUpdateMs     = millis();

		gustActive       = false;
		gustIntensity    = 1.0f;
		thermalActive    = false;
		thermalContribution = 0.0f;

		applyPresetCore(presetCode);
		initPhaseFromBase();
	}

	// ==================================================
	// 메인 tick (CT10에서 주기 호출)
	// ==================================================
	void tick() {
		if (!active) return;

		unsigned long v_now = millis();
		static uint32_t s_jitterSeed = 0;

		uint32_t v_interval = 40u + (s_jitterSeed % 60u); // 40~99ms 가변 샘플링
		if (v_now - lastUpdateMs < v_interval) return;
		s_jitterSeed = esp_random();

		float v_dt = (v_now - lastUpdateMs) / 1000.0f;
		lastUpdateMs = v_now;

		updatePhase();
		// ✅ phase 변화 감지 시 broadcastChart() 호출
        if (phase != v_prev) {
            JsonDocument v_doc;
            toChartJson(v_doc);
            CL_W10_WebAPI::broadcastChart(v_doc);  // 전방 선언으로 해결됨
        }


		calcTurb(v_dt);
		calcThermalEnvelope();
		updateGust();
		updateThermal();

		// 목표 풍속으로 점진 수렴 + 난류/관성 반영
		float v_diff   = targetWindSpeed - currentWindSpeed;
		float v_change = v_diff * windChangeRate * v_dt;
		windMomentum   = windMomentum * 0.85f + v_change * 0.15f;
		windMomentum   = constrain(windMomentum, -0.5f, 0.5f);

		float v_new = currentWindSpeed + windMomentum + spectralEnergyBuf;
		currentWindSpeed = constrain(v_new, 0.2f, 11.0f);

		// target 재생성 빈도: 목표와 근접하면 잦게 변경
		float v_th = 0.5f + (currentWindSpeed / 20.0f);
		if (fabsf(v_diff) < v_th) {
			if (A10_randRange(0.0f, 100.0f) < 30.0f) {
				generateTarget();
			}
		} else {
			if (A10_randRange(0.0f, 100.0f) < 6.0f) {
				generateTarget();
			}
		}

		// PWM 변환: 기본 스케일 + 돌풍 + 열기포
		float v_pwmPct = currentWindSpeed * 10.0f + 10.0f; // 내부 스케일링
		v_pwmPct *= gustIntensity;
		v_pwmPct += thermalContribution * 5.0f;

		applyFan(v_pwmPct);

		// 차트 샘플링 (1Hz, 최근 120개 유지)
		// 차트 샘플링 (1Hz, 최근 120개 유지)
if (millis() - s_lastChartLogMs > 1000UL) {
    if (s_chartBuffer.size() >= 120) {
        s_chartBuffer.pop_front();
    }

    ST_ChartEntry v_e{};
    v_e.timestamp        = millis();
    v_e.wind_speed       = currentWindSpeed;
    v_e.pwm_duty         = _pwm ? _pwm->P10_getDutyPercent() : 0.0f; // 안전 처리 ✅
    v_e.intensity        = userIntensity;
    v_e.variability      = userVariability;
    v_e.turbulence_sigma = turbSigma;
    v_e.preset_index     = (uint8_t)A10_getPresetIndexByCode(presetCode);
    v_e.gust_active      = gustActive;
    v_e.thermal_active   = thermalActive;
    s_chartBuffer.push_back(v_e);

    s_lastChartLogMs = millis();
}
	}

	// ==================================================
	// 해석 결과 적용: C10_resolveWindParams → 여기 호출
	// ==================================================
	void applyResolvedWind(const ST_A10_ResolvedWind_t& p_resolved) {
		// 코드명 저장 (정보용)
		memset(presetCode, 0, sizeof(presetCode));
		memset(styleCode,  0, sizeof(styleCode));
		strlcpy(presetCode, p_resolved.presetCode, sizeof(presetCode));
		strlcpy(styleCode,  p_resolved.styleCode,  sizeof(styleCode));

		userIntensity    = constrain(p_resolved.wind_intensity,   0.0f, 100.0f);
		userVariability  = constrain(p_resolved.wind_variability, 0.0f, 100.0f);
		userGustFreq     = constrain(p_resolved.gust_frequency,   0.0f, 100.0f);
		fanLimitPct      = constrain(p_resolved.fan_limit,        0.0f, 100.0f);
		minFanPct        = constrain(p_resolved.min_fan,          0.0f, 100.0f);

		turbLenScale     = max(1.0f, p_resolved.turbulence_length_scale);
		turbSigma        = max(0.0f, p_resolved.turbulence_intensity_sigma);
		thermalStrength  = max(1.0f, p_resolved.thermal_bubble_strength);
		thermalRadius    = max(0.0f, p_resolved.thermal_bubble_radius);

		// preset별 baseMin/baseMax/확률/강도 설정
		applyPresetCore(presetCode);

		// variability 기반 변화율 재설정
		float v_varNorm = userVariability / 100.0f; // 0~1
		windChangeRate  = constrain(0.10f + v_varNorm * 0.20f, 0.06f, 0.34f);

		// Phase 초기화
		initPhaseFromBase();

		active = true;
		gustActive         = false;
		thermalActive      = false;
		gustIntensity      = 1.0f;
		thermalContribution= 0.0f;

		generateTarget();
	}

	// ==================================================
	// JSON Export
	// ==================================================
	void toJson(JsonDocument& p_doc) {
    JsonObject o = p_doc["sim"].to<JsonObject>();
    o["active"]        = active;
    o["phase"]         = g_A10_WEATHER_PHASE_NAMES_Arr[(uint8_t)phase];
    o["wind"]          = currentWindSpeed;
    o["target"]        = targetWindSpeed;
    o["gustActive"]    = gustActive;
    o["thermalActive"] = thermalActive;
    o["pwmDuty"]       = _pwm ? _pwm->P10_getDutyPercent() : 0.0f;  // 변경됨 ✅

    o["presetCode"]    = presetCode;
    o["styleCode"]     = styleCode;
    o["intensity"]     = userIntensity;
    o["variability"]   = userVariability;
    o["gustFreq"]      = userGustFreq;
    o["fan_limit"]     = fanLimitPct;
    o["min_fan"]       = minFanPct;

    o["turbulence_sigma"] = turbSigma;
    o["turbulence_len"]   = turbLenScale;
    o["thermal_strength"] = thermalStrength;
    o["thermal_radius"]   = thermalRadius;
}

	void toChartJson(JsonDocument& p_doc) {
    // 비활성 상태에서도 최소 1개 데이터 유지
    if (!active && s_chartBuffer.empty()) {
        ST_ChartEntry v_e{};
        v_e.timestamp  = millis();
        v_e.wind_speed = 0.0f;
        v_e.pwm_duty   = 0.0f;
        v_e.intensity  = 0.0f;
        v_e.variability = 0.0f;
        v_e.turbulence_sigma = 0.0f;
        v_e.preset_index = 0;
        v_e.gust_active = false;
        v_e.thermal_active = false;
        s_chartBuffer.push_back(v_e);
    }

    // 루트키 "sim.chart"로 변경 ✅
    JsonArray arr = p_doc["sim"]["chart"].to<JsonArray>();

    for (size_t v_i = 0; v_i < s_chartBuffer.size(); v_i++) {
        const ST_ChartEntry& e = s_chartBuffer[v_i];
        JsonObject jo = arr.add<JsonObject>();
        jo["t"]  = e.timestamp / 1000UL;   // 초 단위 변환 ✅
        jo["w"]  = e.wind_speed;
        jo["p"]  = e.pwm_duty;
        jo["i"]  = e.intensity;
        jo["v"]  = e.variability;
        jo["ts"] = e.turbulence_sigma;
        jo["pi"] = e.preset_index;
        jo["g"]  = e.gust_active;
        jo["h"]  = e.thermal_active;
    }
}

private:
	CL_P10_PWM* _pwm = nullptr;

	// ==================================================
	// 내부 구현부
	// ==================================================

	// PWM 적용 (min/max/intensity 반영)
	void applyFan(float p_pct) {
    if (!_pwm) return;  // null 보호 추가 ✅

    float v_req   = p_pct / 100.0f;
    float v_limit = fanLimitPct / 100.0f;
    float v_min   = minFanPct   / 100.0f;
    float v_int   = userIntensity / 100.0f;

    if (!fanPowerEnabled || v_int <= 0.01f) {
        _pwm->P10_setDutyPercent(0.0f);
        return;
    }

    if (active) {
        v_req *= v_int;
    }

    if (v_req < v_min)  v_req = v_min;
    if (v_req > v_limit) v_req = v_limit;

    _pwm->P10_setDutyPercent(v_req * 100.0f);
}

	// presetCode에 따라 기본 스펙 셋업
	void applyPresetCore(const char* p_code) {
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

		auto eq = [](const char* a, const char* b)->bool {
			return (strcasecmp(a,b) == 0);
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
		phase         = EN_A10_WEATHER_PHASE_NORMAL;
		phaseStartSec = millis() / 1000.0f;

		float v_span = baseMaxWind - baseMinWind;
		if (v_span < 0.5f) v_span = 0.5f;

		phaseMinWind      = baseMinWind + v_span * 0.15f;
		phaseMaxWind      = baseMinWind + v_span * 0.85f;
		phaseDurationSec  = 120.0f;

		float v_mid       = (baseMinWind + baseMaxWind) * 0.5f;
		currentWindSpeed  = v_mid;
		targetWindSpeed   = v_mid;
		spectralEnergyBuf = 0.0f;
		spectralPhaseAcc  = 0.0f;
		windMomentum      = 0.0f;
	}

	// Phase 전환 로직
	void updatePhase() {
		if (!active) return;

		float v_now = millis() / 1000.0f;
		if (v_now - phaseStartSec < phaseDurationSec) return;

		T_A10_WindPhase_t v_old = phase;
		float v_r = A10_getRandom01();

		if (v_old == EN_A10_WEATHER_PHASE_CALM) {
			phase = (v_r < 0.7f) ? EN_A10_WEATHER_PHASE_NORMAL : EN_A10_WEATHER_PHASE_STRONG;
		} else if (v_old == EN_A10_WEATHER_PHASE_STRONG) {
			phase = (v_r < 0.7f) ? EN_A10_WEATHER_PHASE_NORMAL : EN_A10_WEATHER_PHASE_CALM;
		} else {
			if      (v_r < 0.4f) phase = EN_A10_WEATHER_PHASE_CALM;
			else if (v_r < 0.8f) phase = EN_A10_WEATHER_PHASE_NORMAL;
			else                 phase = EN_A10_WEATHER_PHASE_STRONG;
		}

		phaseStartSec = v_now;

		float v_span = baseMaxWind - baseMinWind;
		if (v_span < 0.5f) v_span = 0.5f;

		if (phase == EN_A10_WEATHER_PHASE_CALM) {
			phaseDurationSec = A10_randRange(90.0f, 210.0f);
			phaseMinWind     = baseMinWind;
			phaseMaxWind     = baseMinWind + v_span * 0.6f;
		} else if (phase == EN_A10_WEATHER_PHASE_NORMAL) {
			phaseDurationSec = A10_randRange(120.0f, 300.0f);
			phaseMinWind     = baseMinWind + v_span * 0.15f;
			phaseMaxWind     = baseMinWind + v_span * 0.85f;
		} else { // STRONG
			phaseDurationSec = A10_randRange(60.0f, 150.0f);
			phaseMinWind     = baseMinWind + v_span * 0.4f;
			phaseMaxWind     = baseMaxWind;
		}

		phaseMinWind = max(0.2f, phaseMinWind);
		phaseMaxWind = min(11.0f, phaseMaxWind);

		generateTarget();
	}

	// Von Kármán 난류 근사
	void calcTurb(float p_dt) {
		if (!active) return;

		float v_L     = max(1.0f, turbLenScale);
		float v_sigma = max(0.0f, turbSigma);
		float v_U     = max(0.1f, currentWindSpeed);

		float v_sum = 0.0f;

		for (int v_i = 1; v_i <= 12; v_i++) {
			float v_n   = (float)v_i * 0.1f;
			float v_f   = v_n * v_U / v_L;
			float v_fLU = v_f * v_L / v_U;

			float v_term  = 70.8f * v_fLU * v_fLU;
			float v_numer = 4.0f * v_sigma * v_sigma * (v_L / v_U) * (1.0f + v_term);
			float v_denom = powf(1.0f + v_term, 5.0f / 6.0f);
			float v_S     = v_numer / v_denom;

			float v_phaseRate = 2.0f * (float)M_PI * v_f;
			float v_phaseInc  = v_phaseRate * p_dt;
			float v_phase     = spectralPhaseAcc * (float)v_i
								+ v_phaseInc
								+ A10_randRange(-0.1f, 0.1f);

			float v_bandWidth = 0.083f;
			float v_amp       = sqrtf(2.0f * v_S * v_bandWidth);

			v_sum += v_amp * sinf(v_phase);
		}

		spectralPhaseAcc += p_dt * 0.5f;
		if (spectralPhaseAcc > 2.0f * (float)M_PI) {
			spectralPhaseAcc -= 2.0f * (float)M_PI;
		}

		float v_corr = expf(-p_dt / turbTimeScale);
		spectralEnergyBuf = spectralEnergyBuf * v_corr + v_sum * (1.0f - v_corr);
	}

	// 열기포 포락(Active 시)
	void calcThermalEnvelope() {
		if (!active || !thermalActive) {
			return;
		}

		float v_t   = millis() / 1000.0f;
		float v_age = v_t - thermalStartSec;

		if (v_age >= thermalDuration) {
			thermalActive       = false;
			thermalContribution = 0.0f;
			return;
		}

		float v_prog = v_age / thermalDuration;
		float v_env  = 0.0f;

		if (v_prog < 0.2f) {
			float v_r = v_prog / 0.2f;
			v_env = 1.0f - powf(1.0f - v_r, 2.0f);
		} else if (v_prog < 0.6f) {
			v_env = 1.0f;
			v_env += sinf(v_age * (0.8f + (float)phase * 0.2f) * 2.0f * (float)M_PI) * 0.15f;
		} else {
			float v_r = (v_prog - 0.6f) / 0.4f;
			v_env = 1.0f - powf(v_r, 1.3f);
		}

		if (v_env < 0.0f) v_env = 0.0f;

		float v_strength = max(1.0f, thermalStrength);
		thermalContribution = (v_strength - 1.0f) * v_env;
	}

	// 돌풍 상태 갱신
	void updateGust() {
		if (!active) return;

		float v_nowSec = millis() / 1000.0f;

		if (gustActive) {
			float v_age = v_nowSec - gustStartSec;
			if (v_age >= gustDuration) {
				gustActive    = false;
				gustIntensity = 1.0f;
				return;
			}

			float v_prog = v_age / gustDuration;
			float v_env;

			if (v_prog < 0.25f) {
				float v_r = v_prog / 0.25f;
				v_env = 1.0f - powf(1.0f - v_r, 1.8f);
			} else if (v_prog < 0.65f) {
				v_env = 1.0f;
				v_env += sinf(v_age * (1.5f + (float)phase * 0.5f)) * 0.08f;
			} else {
				float v_r = (v_prog - 0.65f) / 0.35f;
				v_env = 1.0f - powf(v_r, 1.5f);
			}

			if (v_env < 0.0f) v_env = 0.0f;
			gustIntensity = 1.0f + (gustStrengthMax - 1.0f) * v_env;
			return;
		}

		// 새로운 돌풍 트리거
		unsigned long v_nowMs = millis();
		if (v_nowMs - lastGustCheckMs < 500UL) {
			return;
		}
		lastGustCheckMs = v_nowMs;

		float v_base = gustProbBase;
		float v_user = userGustFreq / 100.0f;
		float v_wfac = 1.0f + (currentWindSpeed / 8.9f) * 0.5f;

		float v_pmul;
		if (phase == EN_A10_WEATHER_PHASE_CALM) {
			v_pmul = 0.3f * v_wfac;
		} else if (phase == EN_A10_WEATHER_PHASE_STRONG) {
			v_pmul = 2.2f * v_wfac;
		} else {
			v_pmul = 0.9f * v_wfac;
		}

		float v_p = v_base * v_user * v_pmul;
		if (A10_getRandom01() < v_p) {
			gustActive   = true;
			gustStartSec = v_nowSec;

			float v_speedF = currentWindSpeed / 6.7f;

			if (phase == EN_A10_WEATHER_PHASE_CALM) {
				gustDuration  = A10_randRange(3.0f, 8.0f);
				gustIntensity = A10_randRange(1.08f, 1.33f);
			} else if (phase == EN_A10_WEATHER_PHASE_STRONG) {
				gustDuration  = A10_randRange(0.8f, 3.3f);
				gustIntensity = A10_randRange(1.3f,
				                              1.3f + 0.9f * (1.0f + v_speedF * 0.3f));
			} else {
				gustDuration  = A10_randRange(1.8f, 5.8f);
				gustIntensity = A10_randRange(1.15f,
				                              1.15f + 0.5f * (1.0f + v_speedF * 0.2f));
			}

			if (gustIntensity > gustStrengthMax) {
				gustIntensity = gustStrengthMax;
			}
		}
	}

	// 열기포 트리거
	void updateThermal() {
		if (!active || thermalActive) return;

		unsigned long v_nowMs = millis();
		if (v_nowMs - lastThermalCheckMs < 700UL) {
			return;
		}
		lastThermalCheckMs = v_nowMs;

		float v_strength = max(1.0f, thermalStrength);
		float v_wfac     = 1.0f + (currentWindSpeed / 8.0f) * 0.3f;
		float v_phaseMul = (phase == EN_A10_WEATHER_PHASE_CALM) ? 1.2f
		                   : (phase == EN_A10_WEATHER_PHASE_STRONG ? 0.7f : 1.0f);

		float v_freq = thermalFreqBase
		             * (0.6f + 0.4f * min(3.0f, max(0.5f, v_strength)))
		             * v_wfac * v_phaseMul;

		if (A10_getRandom01() < v_freq) {
			thermalActive   = true;
			thermalStartSec = v_nowMs / 1000.0f;

			float v_d = A10_randRange(8.0f, 14.0f);
			if (phase == EN_A10_WEATHER_PHASE_CALM) {
				v_d *= 1.3f;
			} else if (phase == EN_A10_WEATHER_PHASE_STRONG) {
				v_d *= 0.8f;
			}
			thermalDuration = v_d;
		}
	}

	// 목표 풍속 재설정
	void generateTarget() {
		if (!active) return;

		float v_range = phaseMaxWind - phaseMinWind;
		if (v_range < 0.2f) v_range = 0.2f;

		float v_w   = phaseMinWind + A10_getRandom01() * v_range;
		float v_mid = (phaseMinWind + phaseMaxWind) * 0.5f;
		float v_bias = A10_randRange(0.0f, 1.0f);

		// mid에 살짝 끌어당기는 형태
		v_w = (v_w + v_mid * v_bias) / (1.0f + v_bias);
		targetWindSpeed = v_w;

		// variability, phase, 난류 스케일에 따라 변화율 결정
		float v_var = userVariability / 100.0f;
		float v_base;

		if (phase == EN_A10_WEATHER_PHASE_CALM) {
			v_base = 0.08f + v_var * 0.12f;
		} else if (phase == EN_A10_WEATHER_PHASE_STRONG) {
			v_base = 0.25f + v_var * 0.35f;
		} else {
			v_base = 0.15f + v_var * 0.25f;
		}

		float v_U = max(0.1f, currentWindSpeed);
		float v_tscale = turbLenScale / v_U;
		v_base *= (1.0f + v_tscale * 0.1f);

		windChangeRate = constrain(v_base * A10_randRange(0.7f, 1.7f),
		                           0.04f, 0.5f);
	}
};

// ------------------------------------------------------
// 정적 멤버 정의
// ------------------------------------------------------
std::deque<CL_S10_Simulation::ST_ChartEntry> CL_S10_Simulation::s_chartBuffer;
unsigned long CL_S10_Simulation::s_lastChartLogMs = 0;

