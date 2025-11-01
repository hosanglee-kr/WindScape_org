#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : S10_Simulation_013.h
 * 모듈약어 : S10
 * 모듈명 : Smart Nature Wind 풍속 시뮬레이션 Manager (v013)
 * ------------------------------------------------------
 * 기능 요약:
 *  - 자연풍 물리 기반 풍속 시뮬레이션 (Phase / 돌풍 / 난류 / 열기포 / 지터)
 *  - PWM 제어기(CL_P10_PWM)와 연동하여 실제 팬 제어
 *  - Control(Continuous/Schedule)에서 주입된 사용자 파라미터로 동작
 *  - Preset 기반 자연풍 모드 & 사용자 조정치 반영
 *  - Chart 버퍼 제공 (웹 UI 실시간 분석)
 * ------------------------------------------------------
 * [구현 규칙]
 *  - 항상 소스 시작 주석 체계 유지
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

#include "A10_Const_012.h"
#include "D10_Logger_011.h"
#include "P10_PWM_ctrl_012.h"

class CL_S10_Simulation {
public:
	// ======= 외부 노출 상태(멤버 접두사 금지) =======
	bool  active               = false;
	bool  fanPowerEnabled      = true;
	float currentWindSpeed     = 3.6f;
	float targetWindSpeed      = 3.6f;
	float windChangeRate       = 0.1f;
	float windMomentum         = 0.0f;

	// Phase
	T_A10_WindPhase_t phase    = EN_A10_WEATHER_PHASE_NORMAL;
	float phaseStartSec        = 0.0f;
	float phaseDurationSec     = 120.0f;
	float phaseMinWind         = 2.0f;
	float phaseMaxWind         = 6.0f;

	// Preset 기본 범위
	float baseMinWind          = 1.8f;
	float baseMaxWind          = 5.5f;
	float gustProbBase         = 0.040f;
	float gustStrengthMax      = 2.1f;
	float thermalFreqBase      = 0.022f;

	// 난류
	float spectralEnergyBuf    = 0.0f;
	float spectralPhaseAcc     = 0.0f;
	float turbTimeScale        = 5.0f;

	// 돌풍
	bool          gustActive     = false;
	float         gustStartSec   = 0.0f;
	float         gustDuration   = 3.0f;
	float         gustIntensity  = 1.0f;
	unsigned long lastGustCheck  = 0;

	// 열기포
	bool          thermalActive        = false;
	float         thermalStartSec      = 0.0f;
	float         thermalDuration      = 8.0f;
	float         thermalContribution  = 0.0f;
	unsigned long lastThermalCheck     = 0;

	// tick timer
	unsigned long lastUpdateMs         = 0;

	// ======= 사용자 파라미터 (Control → Simulation 주입) =======
	typedef struct {
		float intensity;                 // wind_intensity (0~100)
		float variability;               // wind_variability (0~100)
		float gustFreq;                  // gust_frequency (0~100)
		float minFan;                    // min_fan (0~100)
		float fanLimit;                  // fan_limit (0~100)
		float turb_length_scale;         // turbulence_length_scale
		float turb_intensity_sigma;      // turbulence_intensity_sigma
		float thermal_bubble_strength;   // thermal_bubble_strength
		float thermal_bubble_radius;     // thermal_bubble_radius (현재 포락선 내 내부 계산에서 사용 여지)
		char  preset[24];                // preset name
	} ST_S10_userParams_t;

	ST_S10_userParams_t user;

	// PWM
	CL_P10_PWM* pwm = nullptr;

	// ======= 차트 버퍼 =======
	struct ST_S10_ChartEntry {
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
	static std::deque<ST_S10_ChartEntry> s_chartBuffer;
	static unsigned long s_lastChartLogMs;

public:
	// ==================================================
	// 시작/정지
	// ==================================================
	void begin(CL_P10_PWM& p_pwm, bool p_applyPreset=true) {
		pwm = &p_pwm;
		active = true;
		if (p_applyPreset) {
			_applyPresetFromUser();
		}
		CL_D10_Logger::log(EN_L10_LOG_INFO, "[S10] begin()");
	}

	inline void stop() {
		active = false;
		phase  = EN_A10_WEATHER_PHASE_CALM;
		targetWindSpeed   = 0.0f;
		currentWindSpeed  = 0.0f;
		if (pwm) pwm->P10_setDutyPercent(0.0f);
	}

	// ==================================================
	// 사용자 파라미터 주입
	// ==================================================
	void applyUserWindParams(
		float p_intensity,
		float p_variability,
		float p_gust,
		float p_minFan,
		float p_fanLimit,
		const char* p_preset
	) {
		user.intensity   = constrain(p_intensity,   0.0f,100.0f);
		user.variability = constrain(p_variability, 0.0f,100.0f);
		user.gustFreq    = constrain(p_gust,        0.0f,100.0f);
		user.minFan      = constrain(p_minFan,      0.0f,100.0f);
		user.fanLimit    = constrain(p_fanLimit,    0.0f,100.0f);
		strlcpy(user.preset, p_preset?p_preset:"OCEAN", sizeof(user.preset));
		// 고급 파라미터는 기존값 유지
		_applyPresetFromUser();
	}

	void applyUserWindAdvanced(
		float p_turbLenScale,
		float p_turbSigma,
		float p_thermalStrength,
		float p_thermalRadius
	) {
		user.turb_length_scale       = max(0.01f, p_turbLenScale);
		user.turb_intensity_sigma    = max(0.0f,  p_turbSigma);
		user.thermal_bubble_strength = max(0.0f,  p_thermalStrength);
		user.thermal_bubble_radius   = max(0.0f,  p_thermalRadius);
	}

	void applyPreset(const char* p_name) {
		strlcpy(user.preset, p_name?p_name:"OCEAN", sizeof(user.preset));
		_applyPresetFromUser();
	}

	// ==================================================
	// tick
	// ==================================================
	void tick() {
		if (!active) return;

		unsigned long v_now = millis();

		// 40~99ms 지터
		static uint32_t s_j = 0;
		uint32_t interval = 40 + (s_j % 60);
		if (v_now - lastUpdateMs < interval) { yield(); return; }
		s_j = esp_random();
		float v_dt = (v_now - lastUpdateMs) / 1000.0f;
		lastUpdateMs = v_now;

		_updatePhase();
		_calcTurb(v_dt);
		_calcThermalEnvelope();
		_updateGust();
		_updateThermal();

		// 목표 풍속으로 접근(모멘텀 + 스펙트럼 에너지)
		float v_diff   = targetWindSpeed - currentWindSpeed;
		float v_change = v_diff * windChangeRate * v_dt;
		windMomentum = windMomentum * 0.85f + v_change * 0.15f;
		windMomentum = fmaxf(-0.5f, fminf(0.5f, windMomentum));
		float v_new = currentWindSpeed + windMomentum + spectralEnergyBuf;
		v_new = fmaxf(0.2f, fminf(11.0f, v_new));
		currentWindSpeed = v_new;

		// 가까우면 빈도 ↑, 멀면 빈도 ↓ (variability 영향)
		float v_changeTh = 0.5f + (currentWindSpeed / 20.0f);
		float v_closeCh  = 30.0f;
		float v_farCh    = 6.0f;
		if (user.variability > 70.0f) { v_closeCh *= 1.5f; v_farCh *= 1.5f; }

		if (fabsf(v_diff) < v_changeTh) {
			if (A10_randRange(0.0f,100.0f) < v_closeCh) _generateTarget();
		} else {
			if (A10_randRange(0.0f,100.0f) < v_farCh) _generateTarget();
		}

		// PWM 반영
		float v_pct = currentWindSpeed * 10.0f + 10.0f; // base scaling
		v_pct *= gustIntensity;
		v_pct += thermalContribution * 5.0f;
		_applyFan(v_pct);

		// 차트 로그
		if (millis() - s_lastChartLogMs > 1000) {
			ST_S10_ChartEntry e;
			e.timestamp   = millis();
			e.wind_speed  = currentWindSpeed;
			e.pwm_duty    = pwm ? pwm->P10_getDutyPercent() : 0.0f;
			e.intensity   = user.intensity;
			e.variability = user.variability;
			e.turbulence  = user.turb_intensity_sigma;
			e.preset_id   = (uint8_t)A10_getPresetIndex(user.preset);
			e.gust_active = gustActive;
			e.thermal_active = thermalActive;
			s_chartBuffer.push_back(e);
			if (s_chartBuffer.size() > 120) s_chartBuffer.pop_front();
			s_lastChartLogMs = millis();
		}

		yield();
	}

	// ==================================================
	// JSON Export
	// ==================================================
	void toJson(JsonDocument& p) {
		JsonObject o = p["sim"].to<JsonObject>();
		o["active"]     = active;
		o["phase"]      = g_A10_WEATHER_PHASE_NAMES_Arr[phase];
		o["wind"]       = currentWindSpeed;
		o["target"]     = targetWindSpeed;
		o["gust"]       = gustActive;
		o["thermal"]    = thermalActive;
		o["pwm"]        = pwm ? pwm->P10_getDutyPercent() : 0.0f;

		o["preset"]      = user.preset;
		o["intensity"]   = user.intensity;
		o["variability"] = user.variability;
		o["turbulence"]  = user.turb_intensity_sigma;
		o["fan_limit"]   = user.fanLimit;
		o["min_fan"]     = user.minFan;
	}

	void toChartJson(JsonDocument& p) {
		JsonArray arr = p["chart"].to<JsonArray>();
		for (auto& e : s_chartBuffer) {
			JsonObject jo = arr.add<JsonObject>();
			jo["t"] = e.timestamp;
			jo["w"] = e.wind_speed;
			jo["p"] = e.pwm_duty;
			jo["g"] = e.gust_active;
			jo["h"] = e.thermal_active;
		}
	}

	// ==================================================
	// 디버그
	// ==================================================
	void debugPrint() {
		CL_D10_Logger::log(EN_L10_LOG_DEBUG,
			"[S10] phase=%s wind=%.2f target=%.2f gust=%d therm=%d",
			g_A10_WEATHER_PHASE_NAMES_Arr[phase],
			currentWindSpeed,
			targetWindSpeed,
			gustActive,
			thermalActive
		);
	}

	// ==================================================
	// (임시) 구버전 호환 래퍼 — 향후 제거 권장
	// ==================================================
	inline void S10_setParams(float a,float b,float c,float d,float e,const char* n) {
		applyUserWindParams(a,b,c,d,e,n);
	}
	inline void S10_applyPreset(const char* n) { applyPreset(n); }
	inline void S10_toJson(JsonDocument& d)    { toJson(d); }
	inline void S10_toChartJson(JsonDocument& d){ toChartJson(d); }
	bool  S10_active          () const { return active; }   // 구코드에서 bool처럼 쓰던 케이스 방지용 미제공
	void  S10_debugPrint      ()       { debugPrint(); }     // 동일 명령형

private:
	// ======= Private Helpers (언더바 접두사) =======
	void _applyPresetFromUser() {
		active = false;
		gustActive = thermalActive = false;
		gustIntensity = 1.0f;

		int8_t p = A10_getPresetIndex(user.preset);
		if (p < 0) p = EN_A10_PRESET_OCEAN;

		switch (p) {
			case EN_A10_PRESET_COUNTRY:        baseMinWind=0.7f;  baseMaxWind=3.4f;  gustProbBase=0.006f; gustStrengthMax=1.35f; thermalFreqBase=0.015f; break;
			case EN_A10_PRESET_MEDITERRANEAN:  baseMinWind=1.6f;  baseMaxWind=3.8f;  gustProbBase=0.012f; gustStrengthMax=1.55f; thermalFreqBase=0.035f; break;
			case EN_A10_PRESET_OCEAN: default: baseMinWind=1.8f;  baseMaxWind=5.5f;  gustProbBase=0.040f; gustStrengthMax=2.1f;  thermalFreqBase=0.022f; break;
			case EN_A10_PRESET_MOUNTAIN:       baseMinWind=2.2f;  baseMaxWind=7.5f;  gustProbBase=0.045f; gustStrengthMax=2.2f;  thermalFreqBase=0.028f; break;
			case EN_A10_PRESET_PLAINS:         baseMinWind=4.0f;  baseMaxWind=8.8f;  gustProbBase=0.070f; gustStrengthMax=2.4f;  thermalFreqBase=0.018f; break;
			case EN_A10_PRESET_HARBOR_BREEZE:  baseMinWind=2.25f; baseMaxWind=5.35f; gustProbBase=0.025f; gustStrengthMax=1.80f; thermalFreqBase=0.026f; break;
			case EN_A10_PRESET_FOREST_CANOPY:  baseMinWind=1.35f; baseMaxWind=4.00f; gustProbBase=0.010f; gustStrengthMax=1.50f; thermalFreqBase=0.012f; break;
			case EN_A10_PRESET_URBAN_SUNSET:   baseMinWind=1.80f; baseMaxWind=4.90f; gustProbBase=0.030f; gustStrengthMax=2.00f; thermalFreqBase=0.020f; break;
			case EN_A10_PRESET_TROPICAL_RAIN:  baseMinWind=3.15f; baseMaxWind=8.05f; gustProbBase=0.060f; gustStrengthMax=2.20f; thermalFreqBase=0.038f; break;
			case EN_A10_PRESET_DESERT_NIGHT:   baseMinWind=0.90f; baseMaxWind=3.10f; gustProbBase=0.005f; gustStrengthMax=1.30f; thermalFreqBase=0.008f; break;
		}

		// Phase 초기화
		phase = EN_A10_WEATHER_PHASE_NORMAL;
		phaseStartSec = millis()/1000.0f;
		float span = baseMaxWind - baseMinWind;
		phaseMinWind = baseMinWind + span*0.15f;
		phaseMaxWind = baseMinWind + span*0.85f;
		phaseDurationSec = 120.0f;

		float mid = (baseMinWind + baseMaxWind)*0.5f;
		currentWindSpeed = mid;
		targetWindSpeed  = mid;
		spectralEnergyBuf = 0.0f;
		spectralPhaseAcc  = 0.0f;
		windMomentum      = 0.0f;
		active = true;

		_generateTarget();
	}

	void _calcTurb(float p_dt) {
		if (!active) return;

		float v_L     = max(0.01f, user.turb_length_scale);
		float v_sigma = max(0.0f,  user.turb_intensity_sigma);
		float v_U     = max(0.1f,  currentWindSpeed);

		float v_sum = 0.0f;
		for (int i=1; i<=12; i++) {
			float n = i * 0.1f;
			float f = n * v_U / v_L;
			float fLU = f * v_L / v_U;
			float term = 70.8f * fLU * fLU;
			float numer = 4.0f*v_sigma*v_sigma*(v_L/v_U)*(1.0f+term);
			float denom = powf(1.0f+term, 5.0f/6.0f);
			float S = numer/denom;

			float phase_rate = 2.0f * M_PI * f;
			float phase_inc = phase_rate * p_dt;
			float phasev = spectralPhaseAcc * i + phase_inc + A10_randRange(-0.1f,0.1f);
			float amp = sqrtf(2.0f*S*0.083f);
			v_sum += amp * sinf(phasev);
		}

		spectralPhaseAcc += p_dt * 0.5f;
		if (spectralPhaseAcc > 2*M_PI) spectralPhaseAcc -= 2*M_PI;

		float corr = expf(-p_dt/turbTimeScale);
		spectralEnergyBuf = spectralEnergyBuf*corr + v_sum*(1.0f-corr);
	}

	void _calcThermalEnvelope() {
		if (!active || !thermalActive) return;

		float t = millis()/1000.0f;
		float age = t - thermalStartSec;
		if (age >= thermalDuration) { thermalActive = false; return; }

		float prog = age / thermalDuration;
		float env=0;
		if (prog < 0.2f) {
			env = 1.0f - powf(1.0f - prog/0.2f,2.0f);
		} else if (prog < 0.6f) {
			env = 1.0f;
			env += sinf(age*(0.8f + phase*0.2f)*2.0f*M_PI)*0.15f;
		} else {
			float d = (prog-0.6f)/0.4f;
			env = 1.0f - powf(d,1.3f);
		}

		float strength = max(0.0f, user.thermal_bubble_strength);
		thermalContribution = (strength - 1.0f)*env;
	}

	void _updateGust() {
		if (!active) return;
		float now = millis()/1000.0f;

		if (gustActive) {
			float age = now - gustStartSec;
			if (age >= gustDuration) { gustActive=false; gustIntensity=1.0f; return; }
			float prog = age/gustDuration;
			float env;
			if (prog<0.25f) env=1.0f - powf(1.0f - prog/0.25f,1.8f);
			else if (prog<0.65f) { env=1.0f; env+=sinf(age*(1.5f+phase*0.5f))*0.08f; }
			else env = 1.0f - powf((prog-0.65f)/0.35f,1.5f);
			gustIntensity = 1.0f + (gustStrengthMax -1.0f)*env;
			return;
		}

		if (millis() - lastGustCheck >= 500) {
			lastGustCheck = millis();
			float base = gustProbBase;
			float userF = user.gustFreq/100.0f;
			float wfac = 1.0f + (currentWindSpeed/8.9f)*0.5f;
			float pmul = (phase == EN_A10_WEATHER_PHASE_CALM) ? (0.3f*wfac)
			            : (phase == EN_A10_WEATHER_PHASE_STRONG?2.2f*wfac:0.9f*wfac);
			float p = base*userF*pmul;
			if (A10_getRandom01() < p) {
				gustActive=true;
				gustStartSec = now;
				float speedF = currentWindSpeed/6.7f;
				if (phase==EN_A10_WEATHER_PHASE_CALM) {
					gustDuration = A10_randRange(3,8);
					gustIntensity = A10_randRange(1.08f,1.33f);
				} else if (phase==EN_A10_WEATHER_PHASE_STRONG) {
					gustDuration = A10_randRange(0.8f,3.3f);
					gustIntensity = A10_randRange(1.3f,1.3f+0.9f*(1+speedF*0.3f));
				} else {
					gustDuration = A10_randRange(1.8f,5.8f);
					gustIntensity = A10_randRange(1.15f,1.15f+0.5f*(1+speedF*0.2f));
				}
				gustIntensity = min(gustIntensity, gustStrengthMax);
			}
		}
	}

	void _updateThermal() {
		if (!active || thermalActive) return;
		if (millis() - lastThermalCheck < 700) return;
		lastThermalCheck = millis();

		float strength = max(0.0f, user.thermal_bubble_strength);
		float wfac = 1.0f + (currentWindSpeed/8.0f)*0.3f;
		float pmul = (phase == EN_A10_WEATHER_PHASE_CALM)?1.2f:
		             (phase==EN_A10_WEATHER_PHASE_STRONG?0.7f:1.0f);
		float freq = thermalFreqBase * (0.6f + 0.4f*min(3.0f,max(0.5f,strength))) * wfac * pmul;
		if (A10_getRandom01() < freq) {
			thermalActive = true;
			thermalStartSec = millis()/1000.0f;
			float d = A10_randRange(8.0f,14.0f);
			if (phase == EN_A10_WEATHER_PHASE_CALM) d*=1.3f;
			else if (phase == EN_A10_WEATHER_PHASE_STRONG) d*=0.8f;
			thermalDuration = d;
		}
	}

	void _updatePhase() {
		if (!active) return;

		float now = millis()/1000.0f;
		if (now - phaseStartSec < phaseDurationSec) return;

		T_A10_WindPhase_t old = phase;
		float r = A10_getRandom01();
		if (old == EN_A10_WEATHER_PHASE_CALM)
			phase = (r<0.7f)?EN_A10_WEATHER_PHASE_NORMAL:EN_A10_WEATHER_PHASE_STRONG;
		else if (old == EN_A10_WEATHER_PHASE_STRONG)
			phase = (r<0.7f)?EN_A10_WEATHER_PHASE_NORMAL:EN_A10_WEATHER_PHASE_CALM;
		else {
			if (r<0.4f) phase=EN_A10_WEATHER_PHASE_CALM;
			else if (r<0.8f) phase=EN_A10_WEATHER_PHASE_NORMAL;
			else phase=EN_A10_WEATHER_PHASE_STRONG;
		}

		phaseStartSec = now;
		float span = baseMaxWind - baseMinWind;

		if (phase==EN_A10_WEATHER_PHASE_CALM) {
			phaseDurationSec = A10_randRange(90,210);
			phaseMinWind = baseMinWind;
			phaseMaxWind = baseMinWind + span*0.6f;
		} else if (phase==EN_A10_WEATHER_PHASE_NORMAL) {
			phaseDurationSec = A10_randRange(120,300);
			phaseMinWind = baseMinWind + span*0.15f;
			phaseMaxWind = baseMinWind + span*0.85f;
		} else {
			phaseDurationSec = A10_randRange(60,150);
			phaseMinWind = baseMinWind + span*0.4f;
			phaseMaxWind = baseMaxWind;
		}
		phaseMinWind = max(0.2f,phaseMinWind);
		phaseMaxWind = min(11.0f,phaseMaxWind);

		_generateTarget();
	}

	void _generateTarget() {
		if (!active) return;
		float range = phaseMaxWind - phaseMinWind;
		float w = phaseMinWind + A10_getRandom01()*range;
		float mid = (phaseMinWind+phaseMaxWind)*0.5f;
		float bias = A10_randRange(0,1);
		w = (w + mid*bias)/(1+bias);
		targetWindSpeed = w;

		float var = user.variability/100.0f;
		float base;
		if (phase==EN_A10_WEATHER_PHASE_CALM) base=0.08f + var*0.12f;
		else if (phase==EN_A10_WEATHER_PHASE_STRONG) base=0.25f + var*0.35f;
		else base=0.15f + var*0.25f;

		float U = max(0.1f, currentWindSpeed);
		float tscale = max(0.01f, user.turb_length_scale)/U;
		base *= (1.0f + tscale*0.1f);
		windChangeRate = base * A10_randRange(0.7f, 1.7f);
	}

	void _applyFan(float p_pct) {
		if (!pwm) return;

		float req = p_pct/100.0f;
		float limit = user.fanLimit/100.0f;
		float minf  = user.minFan/100.0f;
		float intensity = user.intensity/100.0f;

		if (!fanPowerEnabled || intensity <= 0.01f) {
			pwm->P10_setDutyPercent(0.0f);
			return;
		}

		if (active) req *= intensity;
		req = fmaxf(minf, fminf(limit, req));

		pwm->P10_setDutyPercent(req * 100.0f);
	}
};

// ======= 정적 멤버 정의 =======
std::deque<CL_S10_Simulation::ST_S10_ChartEntry>
	CL_S10_Simulation::s_chartBuffer;
unsigned long
	CL_S10_Simulation::s_lastChartLogMs = 0;

