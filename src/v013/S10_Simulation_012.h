#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : S10_Simulation_012.h
 * 모듈약어 : S10
 * 모듈명 : Smart Nature Wind 풍속 시뮬레이션 Manager (v012)
 * ------------------------------------------------------
 * 기능 요약:
 *  - 자연풍 물리 기반 풍속 시뮬레이션 (Phase / 돌풍 / 난류 / 열기포 / 지터)
 *  - PWM 제어기(CL_P10_PWM)와 연동하여 실제 팬 제어
 *  - cfg_sim_022.json 프리셋 기반 사용자 설정 반영
 *  - 장시간 안정작동을 위한 지터, 모멘텀, 스펙트럼 합성, 목표 재생성 제어
 *  - Chart 버퍼 제공 (웹 UI 실시간 분석)
 *  - CT10(Control Manager)에서 enable/disable 게이트 신호 받을 준비
 * ------------------------------------------------------
 * [구현 규칙]
 *  - 항상 소스 시작 주석 체계 유지
 * [구현 규칙]
 *  - 항상 소스 시작 주석 체계 유지
 *  - ArduinoJson v7.x.x 사용 (v6 이하 사용 금지)
 *  - JsonDocument 단일 타입만 사용
 *  - createNestedArray/Object/containsKey 사용 금지
 *  - memset + strlcpy 기반 안전 초기화
 *  - 주석/필드명은 JSON 구조와 동일하게 유지
 *  - 모듈별 단일 헤더(h)파일로만 구성 (cpp 없음)
 *  - 소스 앞부분 구현규칙, 코드네이밍규칙 변경 금지
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

 * ------------------------------------------------------
 * 의존:
 *  - A10_Const_012.h (phase/ preset / turbulence config)
 *  - C10_ConfigManager_012.h (g_A10_config_root.sim)
 *  - D10_Logger_011.h
 *  - P10_PWM_ctrl_011.h
 * ------------------------------------------------------
 */

#include <Arduino.h>
#include <deque>
#include <cmath>
#include <ArduinoJson.h>

#include "A10_Const_012.h"
#include "C10_ConfigManager_013.h"
#include "D10_Logger_011.h"
#include "P10_PWM_ctrl_012.h"

// ======================================================
// CL_S10_Simulation
// ======================================================
class CL_S10_Simulation {
public:
	// 외부에서 쓰는 상태
	bool  S10_active               = false;
	bool  S10_fanPowerEnabled      = true;
	float S10_currentWindSpeed     = 3.6f;
	float S10_targetWindSpeed      = 3.6f;
	float S10_windChangeRate       = 0.1f;
	float S10_windMomentum         = 0.0f;

	// Phase
	T_A10_WindPhase_t S10_phase    = EN_A10_WEATHER_PHASE_NORMAL;
	float S10_phaseStartSec        = 0.0f;
	float S10_phaseDurationSec     = 120.0f;
	float S10_phaseMinWind         = 2.0f;
	float S10_phaseMaxWind         = 6.0f;

	// Preset 기반 범위
	float S10_baseMinWind          = 1.8f;
	float S10_baseMaxWind          = 5.5f;
	float S10_gustProbBase         = 0.040f;
	float S10_gustStrengthMax      = 2.1f;
	float S10_thermalFreqBase      = 0.022f;

	// 난류
	float S10_spectralEnergyBuf    = 0.0f;
	float S10_spectralPhaseAcc     = 0.0f;
	float S10_turbTimeScale        = 5.0f;

	// 돌풍
	bool          S10_gustActive     = false;
	float         S10_gustStartSec   = 0.0f;
	float         S10_gustDuration   = 3.0f;
	float         S10_gustIntensity  = 1.0f;
	unsigned long S10_lastGustCheck  = 0;

	// 열기포
	bool          S10_thermalActive        = false;
	float         S10_thermalStartSec      = 0.0f;
	float         S10_thermalDuration      = 8.0f;
	float         S10_thermalContribution  = 0.0f;
	unsigned long S10_lastThermalCheck     = 0;

	// tick timer
	unsigned long S10_lastUpdateMs         = 0;

	// 차트 엔트리
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
	// 초기화
	// ==================================================
	void S10_begin(CL_P10_PWM& p_pwm, bool p_applyPreset=true) {
		_p_pwmCtrl = &p_pwm;
		if (p_applyPreset) {
			_S10_applyPresetFromConfig();
		}
		CL_D10_Logger::log(EN_L10_LOG_INFO, "[S10] begin()");
	}

	// --------------------------------------------------
	// 바람 시뮬레이션 중단 (CT10 게이트 때 호출)
	// --------------------------------------------------
	inline void S10_stop() {
		S10_active = false;
		S10_phase  = EN_A10_WEATHER_PHASE_CALM;
		S10_targetWindSpeed   = 0.0f;
		S10_currentWindSpeed  = 0.0f;
		if (_p_pwmCtrl) _p_pwmCtrl->P10_setDutyPercent(0.0f);
	}

	// ==================================================
	// tick
	// ==================================================
	void S10_tick() {
		if (!S10_active) return;
		if (!g_A10_config_root.sim) return;

		unsigned long v_now = millis();

		// 40~99ms 지터
		static uint32_t s_j = 0;
		uint32_t interval = 40 + (s_j % 60);
		if (v_now - S10_lastUpdateMs < interval) {
			yield();
			return;
		}
		s_j = esp_random();
		float v_dt = (v_now - S10_lastUpdateMs) / 1000.0f;
		S10_lastUpdateMs = v_now;

		_S10_updatePhase();
		_S10_calcTurb(v_dt);
		_S10_calcThermalEnvelope();
		_S10_updateGust();
		_S10_updateThermal();

		// 목표 풍속으로 천천히 접근
		float v_diff   = S10_targetWindSpeed - S10_currentWindSpeed;
		float v_change = v_diff * S10_windChangeRate * v_dt;
		S10_windMomentum = S10_windMomentum * 0.85f + v_change * 0.15f;
		S10_windMomentum = fmaxf(-0.5f, fminf(0.5f, S10_windMomentum));
		float v_new = S10_currentWindSpeed + S10_windMomentum + S10_spectralEnergyBuf;
		v_new = fmaxf(0.2f, fminf(11.0f, v_new));
		S10_currentWindSpeed = v_new;

		// 가까우면 빈도 높이고, 멀면 낮게
		float v_changeTh = 0.5f + (S10_currentWindSpeed / 20.0f);
		float v_closeCh  = 30.0f;
		float v_farCh    = 6.0f;
		if (g_A10_config_root.sim->wind_variability > 70.0f) {
			v_closeCh *= 1.5f; v_farCh *= 1.5f;
		}
		if (fabsf(v_diff) < v_changeTh) {
			if (A10_randRange(0.0f,100.0f) < v_closeCh) _S10_generateTarget();
		} else {
			if (A10_randRange(0.0f,100.0f) < v_farCh) _S10_generateTarget();
		}

		// PWM 반영
		float v_pct = S10_currentWindSpeed * 10.0f + 10.0f; // base scaling
		v_pct *= S10_gustIntensity;
		v_pct += S10_thermalContribution * 5.0f;
		_S10_applyFan(v_pct);

		// 차트 로그
		if (millis() - s_lastChartLogMs > 1000) {
			ST_S10_ChartEntry e;
			e.timestamp   = millis();
			e.wind_speed  = S10_currentWindSpeed;
			e.pwm_duty    = _p_pwmCtrl ? _p_pwmCtrl->P10_getDutyPercent() : 0.0f;
			e.intensity   = g_A10_config_root.sim->wind_intensity;
			e.variability = g_A10_config_root.sim->wind_variability;
			e.turbulence  = g_A10_config_root.sim->turbulence.intensity_sigma;
			e.preset_id   = (uint8_t)A10_getPresetIndex(g_A10_config_root.sim->preset);
			e.gust_active = S10_gustActive;
			e.thermal_active = S10_thermalActive;
			s_chartBuffer.push_back(e);
			if (s_chartBuffer.size() > 120) s_chartBuffer.pop_front();
			s_lastChartLogMs = millis();
		}

		yield();
	}

	// ==================================================
	// JSON 출력
	// ==================================================
	void S10_toJson(JsonDocument& p) {
		JsonObject o = p["sim"].to<JsonObject>();
		o["active"]     = S10_active;
		o["phase"]      = g_A10_WEATHER_PHASE_NAMES_Arr[S10_phase];
		o["wind"]       = S10_currentWindSpeed;
		o["target"]     = S10_targetWindSpeed;
		o["gust"]       = S10_gustActive;
		o["thermal"]    = S10_thermalActive;
		o["pwm"]        = _p_pwmCtrl ? _p_pwmCtrl->P10_getDutyPercent() : 0.0f;

		if (g_A10_config_root.sim) {
			o["preset"]      = g_A10_config_root.sim->preset;
			o["intensity"]   = g_A10_config_root.sim->wind_intensity;
			o["variability"] = g_A10_config_root.sim->wind_variability;
			o["turbulence"]  = g_A10_config_root.sim->turbulence.intensity_sigma;
			o["fan_limit"]   = g_A10_config_root.sim->fan_limit;
			o["min_fan"]     = g_A10_config_root.sim->min_fan;
		}
	}

	void S10_toChartJson(JsonDocument& p) {
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

	void S10_applyPreset(const char* p_name) {
		if (!g_A10_config_root.sim) return;
		strlcpy(g_A10_config_root.sim->preset, p_name, sizeof(g_A10_config_root.sim->preset));
		_S10_applyPresetFromConfig();
	}

	void S10_debugPrint() {
		CL_D10_Logger::log(EN_L10_LOG_DEBUG,
			"[S10] phase=%s wind=%.2f target=%.2f gust=%d therm=%d",
			g_A10_WEATHER_PHASE_NAMES_Arr[S10_phase],
			S10_currentWindSpeed,
			S10_targetWindSpeed,
			S10_gustActive,
			S10_thermalActive
		);
	}

private:
	CL_P10_PWM* _p_pwmCtrl = nullptr;

	// --------------------------------------------------
	// 프리셋 적용
	// --------------------------------------------------
	void _S10_applyPresetFromConfig() {
		if (!g_A10_config_root.sim) { S10_active=false; return; }
		S10_active = false;
		S10_gustActive = S10_thermalActive = false;
		S10_gustIntensity = 1.0f;

		int8_t p = A10_getPresetIndex(g_A10_config_root.sim->preset);
		if (p < 0) p = EN_A10_PRESET_OCEAN;

		// preset switch (원본 그대로 유지)
		switch (p) {
			case EN_A10_PRESET_COUNTRY:        S10_baseMinWind=0.7f;  S10_baseMaxWind=3.4f;  S10_gustProbBase=0.006f; S10_gustStrengthMax=1.35f; S10_thermalFreqBase=0.015f; break;
			case EN_A10_PRESET_MEDITERRANEAN:  S10_baseMinWind=1.6f;  S10_baseMaxWind=3.8f;  S10_gustProbBase=0.012f; S10_gustStrengthMax=1.55f; S10_thermalFreqBase=0.035f; break;
			case EN_A10_PRESET_OCEAN: default: S10_baseMinWind=1.8f;  S10_baseMaxWind=5.5f;  S10_gustProbBase=0.040f; S10_gustStrengthMax=2.1f;  S10_thermalFreqBase=0.022f; break;
			case EN_A10_PRESET_MOUNTAIN:       S10_baseMinWind=2.2f;  S10_baseMaxWind=7.5f;  S10_gustProbBase=0.045f; S10_gustStrengthMax=2.2f;  S10_thermalFreqBase=0.028f; break;
			case EN_A10_PRESET_PLAINS:         S10_baseMinWind=4.0f;  S10_baseMaxWind=8.8f;  S10_gustProbBase=0.070f; S10_gustStrengthMax=2.4f;  S10_thermalFreqBase=0.018f; break;
			case EN_A10_PRESET_HARBOR_BREEZE:  S10_baseMinWind=2.25f; S10_baseMaxWind=5.35f; S10_gustProbBase=0.025f; S10_gustStrengthMax=1.80f; S10_thermalFreqBase=0.026f; break;
			case EN_A10_PRESET_FOREST_CANOPY:  S10_baseMinWind=1.35f; S10_baseMaxWind=4.00f; S10_gustProbBase=0.010f; S10_gustStrengthMax=1.50f; S10_thermalFreqBase=0.012f; break;
			case EN_A10_PRESET_URBAN_SUNSET:   S10_baseMinWind=1.80f; S10_baseMaxWind=4.90f; S10_gustProbBase=0.030f; S10_gustStrengthMax=2.00f; S10_thermalFreqBase=0.020f; break;
			case EN_A10_PRESET_TROPICAL_RAIN:  S10_baseMinWind=3.15f; S10_baseMaxWind=8.05f; S10_gustProbBase=0.060f; S10_gustStrengthMax=2.20f; S10_thermalFreqBase=0.038f; break;
			case EN_A10_PRESET_DESERT_NIGHT:   S10_baseMinWind=0.90f; S10_baseMaxWind=3.10f; S10_gustProbBase=0.005f; S10_gustStrengthMax=1.30f; S10_thermalFreqBase=0.008f; break;
		}

		// Phase 초기화
		S10_phase = EN_A10_WEATHER_PHASE_NORMAL;
		S10_phaseStartSec = millis()/1000.0f;
		float span = S10_baseMaxWind - S10_baseMinWind;
		S10_phaseMinWind = S10_baseMinWind + span*0.15f;
		S10_phaseMaxWind = S10_baseBaseWind + span*0.85f;
		S10_phaseDurationSec = 120.0f;

		float mid = (S10_baseMinWind + S10_baseMaxWind)*0.5f;
		S10_currentWindSpeed = mid;
		S10_targetWindSpeed  = mid;
		S10_spectralEnergyBuf = 0.0f;
		S10_spectralPhaseAcc  = 0.0f;
		S10_windMomentum      = 0.0f;
		S10_active = true;

		_S10_generateTarget();
	}

	// 난류 계산
	void _S10_calcTurb(float p_dt) {
		if (!S10_active || !g_A10_config_root.sim) return;

		float v_L     = g_A10_config_root.sim->turbulence.length_scale;
		float v_sigma = g_A10_config_root.sim->turbulence.intensity_sigma;
		float v_U     = max(0.1f, S10_currentWindSpeed);

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
			float phase = S10_spectralPhaseAcc * i + phase_inc + A10_randRange(-0.1f,0.1f);
			float amp = sqrtf(2.0f*S*0.083f);
			v_sum += amp * sinf(phase);
		}

		S10_spectralPhaseAcc += p_dt * 0.5f;
		if (S10_spectralPhaseAcc > 2*M_PI) S10_spectralPhaseAcc -= 2*M_PI;

		float corr = expf(-p_dt/S10_turbTimeScale);
		S10_spectralEnergyBuf = S10_spectralEnergyBuf*corr + v_sum*(1.0f-corr);
	}

	// 열기포 포락선
	void _S10_calcThermalEnvelope() {
		if (!S10_active || !S10_thermalActive) return;

		float t = millis()/1000.0f;
		float age = t - S10_thermalStartSec;
		if (age >= S10_thermalDuration) {
			S10_thermalActive = false;
			return;
		}

		float prog = age / S10_thermalDuration;
		float env=0;
		if (prog < 0.2f) {
			env = 1.0f - powf(1.0f - prog/0.2f,2.0f);
		} else if (prog < 0.6f) {
			env = 1.0f;
			env += sinf(age*(0.8f + S10_phase*0.2f)*2.0f*M_PI)*0.15f;
		} else {
			float d = (prog-0.6f)/0.4f;
			env = 1.0f - powf(d,1.3f);
		}

		float strength = g_A10_config_root.sim->thermal.bubble_strength;
		S10_thermalContribution = (strength - 1.0f)*env;
	}

	// 돌풍
	void _S10_updateGust() {
		if (!S10_active) return;
		float now = millis()/1000.0f;

		if (S10_gustActive) {
			float age = now - S10_gustStartSec;
			if (age >= S10_gustDuration) {
				S10_gustActive=false; S10_gustIntensity=1.0f;
				return;
			}
			float prog = age/S10_gustDuration;
			float env;
			if (prog<0.25f) env=1.0f - powf(1.0f - prog/0.25f,1.8f);
			else if (prog<0.65f) {
				env=1.0f;
				env+=sinf(age*(1.5f+S10_phase*0.5f))*0.08f;
			} else env = 1.0f - powf((prog-0.65f)/0.35f,1.5f);
			S10_gustIntensity = 1.0f + (S10_gustStrengthMax -1.0f)*env;
			return;
		}

		if (millis() - S10_lastGustCheck >= 500) {
			S10_lastGustCheck = millis();
			float base = S10_gustProbBase;
			float user = g_A10_config_root.sim->gust_frequency/100.0f;
			float wfac = 1.0f + (S10_currentWindSpeed/8.9f)*0.5f;
			float pmul = (S10_phase == EN_A10_WEATHER_PHASE_CALM) ? (0.3f*wfac)
			          : (S10_phase == EN_A10_WEATHER_PHASE_STRONG?2.2f*wfac:0.9f*wfac);
			float p = base*user*pmul;
			if (A10_getRandom01() < p) {
				S10_gustActive=true;
				S10_gustStartSec = now;
				float speedF = S10_currentWindSpeed/6.7f;
				if (S10_phase==EN_A10_WEATHER_PHASE_CALM) {
					S10_gustDuration = A10_randRange(3,8);
					S10_gustIntensity = A10_randRange(1.08f,1.33f);
				} else if (S10_phase==EN_A10_WEATHER_PHASE_STRONG) {
					S10_gustDuration = A10_randRange(0.8f,3.3f);
					S10_gustIntensity = A10_randRange(1.3f,1.3f+0.9f*(1+speedF*0.3f));
				} else {
					S10_gustDuration = A10_randRange(1.8f,5.8f);
					S10_gustIntensity = A10_randRange(1.15f,1.15f+0.5f*(1+speedF*0.2f));
				}
				S10_gustIntensity = min(S10_gustIntensity, S10_gustStrengthMax);
			}
		}
	}

	// 열기포
	void _S10_updateThermal() {
		if (!S10_active || S10_thermalActive) return;
		if (millis() - S10_lastThermalCheck < 700) return;
		S10_lastThermalCheck = millis();

		float strength = g_A10_config_root.sim->thermal.bubble_strength;
		float wfac = 1.0f + (S10_currentWindSpeed/8.0f)*0.3f;
		float pmul = (S10_phase == EN_A10_WEATHER_PHASE_CALM)?1.2f:
		             (S10_phase==EN_A10_WEATHER_PHASE_STRONG?0.7f:1.0f);
		float freq = S10_thermalFreqBase * (0.6f + 0.4f*min(3.0f,max(0.5f,strength))) * wfac * pmul;
		if (A10_getRandom01() < freq) {
            S10_thermalActive = true;
            S10_thermalStartSec = millis()/1000.0f;
            float d = A10_randRange(8.0f,14.0f);
            if (S10_phase == EN_A10_WEATHER_PHASE_CALM) d*=1.3f;
            else if (S10_phase == EN_A10_WEATHER_PHASE_STRONG) d*=0.8f;
            S10_thermalDuration = d;
        }
	}

	// Phase 전환
	void _S10_updatePhase() {
		if (!S10_active) return;

		float now = millis()/1000.0f;
		if (now - S10_phaseStartSec < S10_phaseDurationSec) return;

		T_A10_WindPhase_t old = S10_phase;
		float r = A10_getRandom01();
		if (old == EN_A10_WEATHER_PHASE_CALM)
			S10_phase = (r<0.7f)?EN_A10_WEATHER_PHASE_NORMAL:EN_A10_WEATHER_PHASE_STRONG;
		else if (old == EN_A10_WEATHER_PHASE_STRONG)
			S10_phase = (r<0.7f)?EN_A10_WEATHER_PHASE_NORMAL:EN_A10_WEATHER_PHASE_CALM;
		else {
			if (r<0.4f) S10_phase=EN_A10_WEATHER_PHASE_CALM;
			else if (r<0.8f) S10_phase=EN_A10_WEATHER_PHASE_NORMAL;
			else S10_phase=EN_A10_WEATHER_PHASE_STRONG;
		}

		S10_phaseStartSec = now;
		float span = S10_baseMaxWind - S10_baseMinWind;

		if (S10_phase==EN_A10_WEATHER_PHASE_CALM) {
			S10_phaseDurationSec = A10_randRange(90,210);
			S10_phaseMinWind = S10_baseMinWind;
			S10_phaseMaxWind = S10_baseMinWind + span*0.6f;
		} else if (S10_phase==EN_A10_WEATHER_PHASE_NORMAL) {
			S10_phaseDurationSec = A10_randRange(120,300);
			S10_phaseMinWind = S10_baseMinWind + span*0.15f;
			S10_phaseMaxWind = S10_baseMinWind + span*0.85f;
		} else {
			S10_phaseDurationSec = A10_randRange(60,150);
			S10_phaseMinWind = S10_baseMinWind + span*0.4f;
			S10_phaseMaxWind = S10_baseMaxWind;
		}
		S10_phaseMinWind = max(0.2f,S10_phaseMinWind);
		S10_phaseMaxWind = min(11.0f,S10_phaseMaxWind);

		_S10_generateTarget();
	}

	// 목표 풍속 갱신
	void _S10_generateTarget() {
		if (!S10_active) return;
		float range = S10_phaseMaxWind - S10_phaseMinWind;
		float w = S10_phaseMinWind + A10_getRandom01()*range;
		float mid = (S10_phaseMinWind+S10_phaseMaxWind)*0.5f;
		float bias = A10_randRange(0,1);
		w = (w + mid*bias)/(1+bias);
		S10_targetWindSpeed = w;

		float var = g_A10_config_root.sim->wind_variability/100.0f;
		float base;
		if (S10_phase==EN_A10_WEATHER_PHASE_CALM) base=0.08f + var*0.12f;
		else if (S10_phase==EN_A10_WEATHER_PHASE_STRONG) base=0.25f + var*0.35f;
		else base=0.15f + var*0.25f;

		float U = max(0.1f, S10_currentWindSpeed);
		float tscale = g_A10_config_root.sim->turbulence.length_scale/U;
		base *= (1.0f + tscale*0.1f);
    S10_windChangeRate = base * A10_randRange(0.7f, 1.7f);
	}

	// 팬반영
	void _S10_applyFan(float p_pct) {
		if (!_p_pwmCtrl) return;

		float req = p_pct/100.0f;
		float limit = g_A10_config_root.sim->fan_limit/100.0f;
		float minf  = g_A10_config_root.sim->min_fan/100.0f;
		float intensity = g_A10_config_root.sim->wind_intensity/100.0f;

		// 모션/전원 gating 참고용 (CT10 에서 fanPowerEnabled 바월)
		if (!S10_fanPowerEnabled || intensity <= 0.01f) {
			_p_pwmCtrl->P10_setDutyPercent(0.0f);
			return;
		}

		if (S10_active) req *= intensity;
		req = fmaxf(minf, fminf(limit, req));

		_p_pwmCtrl->P10_setDutyPercent(req * 100.0f);
	}
};

// ------------------------------------------------------
// 정적 멤버 정의
// ------------------------------------------------------
std::deque<CL_S10_Simulation::ST_S10_ChartEntry> 
	CL_S10_Simulation::s_chartBuffer;

unsigned long 
	CL_S10_Simulation::s_lastChartLogMs = 0;

