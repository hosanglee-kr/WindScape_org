#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : S10_Simulation_010.h
 * 모듈명 : Smart Nature Wind 풍속 시뮬레이션 Manager
 * ------------------------------------------------------
 * 기능 요약:
 *  - 자연풍 물리 기반 풍속 시뮬레이션 (Phase / 돌풍 / 난류 / 열기포)
 *  - PWM 제어기(CL_P10_PWM)와 연동
 *  - cfg_sim_021.json 기반의 사용자 설정값 반영
 *  - 프리셋 테이블/Phase 전환/실시간 Chart 기록 지원
 * ------------------------------------------------------
 * [코드 네이밍 규칙]
 * 		- 현재 파일 모듈약어    : S10
 * 		- 전역 상수,매크로      : G_모듈약어_ 접두사
 * 		- 전역 변수             : g_모듈약어_ 접두사
 * 		- 전역 함수             : 모듈약어_ 접두사
 * 		- type                  : T_모듈약어_ 접두사
 * 		- enum 상수             : EN_모듈약어_ 접두사
 * 		- 구조체                : ST_모듈약어_ 접두사
 * 		- 클래스명              : CL_모듈약어_ 접두사
 * 		- 클래스 private 멤버   : _ 접두사
 * 		- 클래스 정적 멤버      : s_ 접두사
 * 		- 로컬 변수             : v_ 접두사
 * 		- 함수 인자             : p_ 접두사
 */

#include <Arduino.h>
#include <deque>
#include <cmath>
#include <ArduinoJson.h>

#include "A10_Const_010.h"
#include "D10_Logger_010.h"
#include "P10_PWM_ctrl_010.h"

class CL_S10_Simulation {
public:
	// ===== 외부에서 읽는 상태 =====
	bool  S10_active               = false;
	bool  S10_fanPowerEnabled      = true;
	float S10_currentWindSpeed     = 3.6f;   // m/s
	float S10_targetWindSpeed      = 3.6f;   // m/s
	float S10_windChangeRate       = 0.1f;
	float S10_windMomentum         = 0.0f;

	// Phase
	T_A10_WindPhase_t S10_phase    = EN_A10_WEATHER_PHASE_NORMAL;
	float S10_phaseStartSec        = 0.0f; // sec
	float S10_phaseDurationSec     = 120.0f; // sec
	float S10_phaseMinWind         = 2.0f;  // m/s
	float S10_phaseMaxWind         = 6.0f;  // m/s

	// 프리셋 기본 범위(초기값)
	float S10_baseMinWind          = 1.8f;
	float S10_baseMaxWind          = 5.5f;
	float S10_gustProbBase         = 0.040f;
	float S10_gustStrengthMax      = 2.1f;
	float S10_thermalFreqBase      = 0.022f; // 빈도 기본값(가중)

	// 난류
	float S10_spectralEnergyBuf    = 0.0f;
	float S10_spectralPhaseAcc     = 0.0f;
	float S10_turbTimeScale        = 5.0f;

	// 돌풍
	bool          S10_gustActive     = false;
	float         S10_gustStartSec   = 0.0f; // sec
	float         S10_gustDuration   = 3.0f; // sec
	float         S10_gustIntensity  = 1.0f; // ×배
	unsigned long S10_lastGustCheck  = 0;

	// 열기포
	bool          S10_thermalActive        = false;
	float         S10_thermalStartSec      = 0.0f; // sec
	float         S10_thermalDuration      = 8.0f; // sec
	float         S10_thermalContribution  = 0.0f; // +/- (팬 duty 보정에 사용)
	unsigned long S10_lastThermalCheck     = 0;

	// 틱 타이머
	unsigned long S10_lastUpdateMs         = 0;

	// ===== 실시간 차트 로그 버퍼 =====
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
	void S10_begin(CL_P10_PWM& p_pwm, bool p_applyPreset = true) {
		_p_pwmCtrl = &p_pwm;
		if (p_applyPreset) {
			_S10_applyPresetFromConfig();
		}
		CL_D10_Logger::log(EN_L10_LOG_INFO, "S10: begin()");
	}


// 바람 시뮬레이션 중단용 헬퍼
inline void S10_stop() {
    S10_active = false;
    S10_phase  = EN_A10_WEATHER_PHASE_CALM;
    S10_targetSpeed = 0.0f;
    S10_currentSpeed = 0.0f;
}

	// ==================================================
	// 한 틱 업데이트
	// ==================================================
	void S10_tick() {
		if (!S10_active) return;
		if (!g_A10_config_root.sim) return;

		unsigned long v_nowMs = millis();

		// 업데이트 간격 + 지터
		static uint32_t s_jitter = 0;
		uint32_t v_interval = 40 + (s_jitter % 60); // 40~99ms 사이 지터
		if (v_nowMs - S10_lastUpdateMs < v_interval) {
			yield();
			return;
		}
		s_jitter = esp_random();
		float v_dt = (float)(v_nowMs - S10_lastUpdateMs) / 1000.0f;
		S10_lastUpdateMs = v_nowMs;

		_S10_updatePhase();
		_S10_calcTurb(v_dt);
		_S10_calcThermalEnvelope();

		// 목표 풍속으로 수렴(모멘텀 포함)
		float v_diff   = S10_targetWindSpeed - S10_currentWindSpeed;
		float v_change = v_diff * S10_windChangeRate * v_dt;
		S10_windMomentum = S10_windMomentum * 0.85f + v_change * 0.15f;
		S10_windMomentum = fmaxf(-0.5f, fminf(0.5f, S10_windMomentum));
		v_change = S10_windMomentum;

		float v_new = S10_currentWindSpeed + v_change + S10_spectralEnergyBuf;
		v_new = fmaxf(0.2f, fminf(11.0f, v_new));
		S10_currentWindSpeed = v_new;

		// 목표 재생성 규칙
		float v_changeTh = 0.5f + (S10_currentWindSpeed / 20.0f);
		float v_closeCh  = 30.0f;
		float v_farCh    = 6.0f;
		if (g_A10_config_root.sim->wind_variability > 70.0f) {
			v_closeCh *= 1.5f;
			v_farCh   *= 1.5f;
		}
		if (fabsf(v_diff) < v_changeTh) {
			if (A10_randRange(0.0f, 100.0f) < v_closeCh)
				_S10_generateTarget();
		} else if (A10_randRange(0.0f, 100.0f) < v_farCh) {
			_S10_generateTarget();
		}

		// 돌풍/열기포 상태 갱신
		_S10_updateGust();
		_S10_updateThermal();

		// PWM 반영
		float v_fanPct = S10_currentWindSpeed * 10.0f + 10.0f; // 0.2m/s≈12%, 6m/s≈70%
		v_fanPct *= S10_gustIntensity;
		v_fanPct += S10_thermalContribution * 5.0f;
		_S10_applyFan(v_fanPct);

		// 차트 기록
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
	// 상태 JSON 직렬화
	// ==================================================
	void S10_toJson(JsonDocument& p_doc) {
		JsonObject o = p_doc["sim"].to<JsonObject>();
		o["active"]     = S10_active;
		o["phase"]      = g_A10_WEATHER_PHASE_NAMES_Arr[S10_phase];
		o["wind_speed"] = S10_currentWindSpeed;
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

	// ==================================================
	// 차트 JSON 직렬화
	// ==================================================
	void S10_toChartJson(JsonDocument& p_doc) {
		JsonArray arr = p_doc["chart"].to<JsonArray>();
		for (auto& e : s_chartBuffer) {
			JsonObject jo = arr.add<JsonObject>();
			jo["t"] = e.timestamp;
			jo["w"] = e.wind_speed;
			jo["p"] = e.pwm_duty;
			jo["g"] = e.gust_active;
			jo["h"] = e.thermal_active;
		}
	}

	// 프리셋 강제 적용(필요 시)
	void S10_applyPreset(const char* p_presetName) {
		if (!g_A10_config_root.sim) return;
		strlcpy(g_A10_config_root.sim->preset, p_presetName, sizeof(g_A10_config_root.sim->preset));
		_S10_applyPresetFromConfig();
	}

	// 디버그 출력
	void S10_debugPrint() {
		CL_D10_Logger::log(
			EN_L10_LOG_DEBUG,
			"[S10] phase=%s wind=%.2f target=%.2f gust=%d thermal=%d",
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
		if (!g_A10_config_root.sim) {
			S10_active = false;
			return;
		}
		// 공통 초기화
		S10_active          = false;
		S10_gustActive      = false;
		S10_thermalActive   = false;
		S10_gustIntensity   = 1.0f;

		int8_t v_preset = A10_getPresetIndex(g_A10_config_root.sim->preset);
		if (v_preset < 0) v_preset = EN_A10_PRESET_OCEAN;

		switch (v_preset) {
			case EN_A10_PRESET_COUNTRY:
				S10_baseMinWind = 0.7f;  S10_baseMaxWind = 3.4f;
				S10_gustProbBase= 0.006f; S10_gustStrengthMax=1.35f;
				S10_thermalFreqBase=0.015f; break;
			case EN_A10_PRESET_MEDITERRANEAN:
				S10_baseMinWind = 1.6f;  S10_baseMaxWind = 3.8f;
				S10_gustProbBase= 0.012f; S10_gustStrengthMax=1.55f;
				S10_thermalFreqBase=0.035f; break;
			case EN_A10_PRESET_OCEAN:
			default:
				S10_baseMinWind = 1.8f;  S10_baseMaxWind = 5.5f;
				S10_gustProbBase= 0.040f; S10_gustStrengthMax=2.1f;
				S10_thermalFreqBase=0.022f; break;
			case EN_A10_PRESET_MOUNTAIN:
				S10_baseMinWind = 2.2f;  S10_baseMaxWind = 7.5f;
				S10_gustProbBase= 0.045f; S10_gustStrengthMax=2.2f;
				S10_thermalFreqBase=0.028f; break;
			case EN_A10_PRESET_PLAINS:
				S10_baseMinWind = 4.0f;  S10_baseMaxWind = 8.8f;
				S10_gustProbBase= 0.070f; S10_gustStrengthMax=2.4f;
				S10_thermalFreqBase=0.018f; break;
			case EN_A10_PRESET_HARBOR_BREEZE:
				S10_baseMinWind = 2.25f; S10_baseMaxWind = 5.35f;
				S10_gustProbBase= 0.025f; S10_gustStrengthMax=1.80f;
				S10_thermalFreqBase=0.026f; break;
			case EN_A10_PRESET_FOREST_CANOPY:
				S10_baseMinWind = 1.35f; S10_baseMaxWind = 4.00f;
				S10_gustProbBase= 0.010f; S10_gustStrengthMax=1.50f;
				S10_thermalFreqBase=0.012f; break;
			case EN_A10_PRESET_URBAN_SUNSET:
				S10_baseMinWind = 1.80f; S10_baseMaxWind = 4.90f;
				S10_gustProbBase= 0.030f; S10_gustStrengthMax=2.00f;
				S10_thermalFreqBase=0.020f; break;
			case EN_A10_PRESET_TROPICAL_RAIN:
				S10_baseMinWind = 3.15f; S10_baseMaxWind = 8.05f;
				S10_gustProbBase= 0.060f; S10_gustStrengthMax=2.20f;
				S10_thermalFreqBase=0.038f; break;
			case EN_A10_PRESET_DESERT_NIGHT:
				S10_baseMinWind = 0.90f; S10_baseMaxWind = 3.10f;
				S10_gustProbBase= 0.005f; S10_gustStrengthMax=1.30f;
				S10_thermalFreqBase=0.008f; break;
		}

		// Phase 초기화
		S10_phase = EN_A10_WEATHER_PHASE_NORMAL;
		S10_phaseStartSec = millis() / 1000.0f;
		float v_span = S10_baseMaxWind - S10_baseMinWind;
		S10_phaseMinWind = S10_baseMinWind + v_span * 0.15f;
		S10_phaseMaxWind = S10_baseMinWind + v_span * 0.85f;
		S10_phaseDurationSec = 120.0f;

		// 시작 풍속
		float v_mid = (S10_baseMinWind + S10_baseMaxWind) * 0.5f;
		S10_currentWindSpeed = v_mid;
		S10_targetWindSpeed  = v_mid;
		S10_spectralEnergyBuf = 0.0f;
		S10_spectralPhaseAcc  = 0.0f;
		S10_windMomentum      = 0.0f;
		S10_active            = true;

		_S10_generateTarget();
	}

	// --------------------------------------------------
	// 난류 계산 (Von Kármán 근사)
	// --------------------------------------------------
	void _S10_calcTurb(float p_dt) {
		if (!S10_active || !g_A10_config_root.sim) return;

		float v_L     = g_A10_config_root.sim->turbulence.length_scale;
		float v_sigma = g_A10_config_root.sim->turbulence.intensity_sigma;
		float v_U     = S10_currentWindSpeed; if (v_U < 0.1f) v_U = 0.1f;

		float v_sum = 0.0f;
		for (int v_i = 1; v_i <= 12; v_i++) {
			float v_n   = v_i * 0.1f;
			float v_f   = v_n * v_U / v_L;
			float v_fL_U= v_f * v_L / v_U;
			float v_term= 70.8f * v_fL_U * v_fL_U;

			float v_numer = 4.0f * v_sigma * v_sigma * (v_L / v_U) * (1.0f + v_term);
			float v_denom = powf(1.0f + v_term, 5.0f / 6.0f);
			float v_S     = v_numer / v_denom;

			float v_phase_rate = 2.0f * M_PI * v_f;
			float v_phase_inc  = v_phase_rate * p_dt;
			float v_phase_jit  = A10_randRange(-0.1f, 0.1f);
			float v_phase      = S10_spectralPhaseAcc * v_i + v_phase_inc + v_phase_jit;

			float v_amp = sqrtf(2.0f * v_S * 0.083f);
			v_sum += v_amp * sinf(v_phase);
		}

		S10_spectralPhaseAcc += p_dt * 0.5f;
		if (S10_spectralPhaseAcc > 2.0f * M_PI)
			S10_spectralPhaseAcc -= 2.0f * M_PI;

		float v_corr = expf(-p_dt / S10_turbTimeScale);
		S10_spectralEnergyBuf = S10_spectralEnergyBuf * v_corr + v_sum * (1.0f - v_corr);
	}

	// --------------------------------------------------
	// 열기포 포락선(현재 활성시)의 보정치 계산
	// --------------------------------------------------
	void _S10_calcThermalEnvelope() {
		if (!S10_active) return;

		S10_thermalContribution = 0.0f;
		if (!S10_thermalActive)  return;

		float v_t   = millis() / 1000.0f;
		float v_age = v_t - S10_thermalStartSec;
		if (v_age >= S10_thermalDuration) {
			S10_thermalActive = false;
			return;
		}

		float v_prog = v_age / S10_thermalDuration;
		float v_env  = 0.0f;
		if (v_prog < 0.2f) {
			v_env = 1.0f - powf(1.0f - v_prog / 0.2f, 2.0f);
		} else if (v_prog < 0.6f) {
			v_env = 1.0f;
			float v_osc = 0.8f + (S10_phase * 0.2f);
			v_env += sinf(v_age * v_osc * 2.0f * M_PI) * 0.15f;
		} else {
			float v_d = (v_prog - 0.6f) / 0.4f;
			v_env     = 1.0f - powf(v_d, 1.3f);
		}

		// config의 thermal.bubble_strength를 보정 계수로 활용
		float v_strength = (g_A10_config_root.sim ? g_A10_config_root.sim->thermal.bubble_strength : 2.0f);
		S10_thermalContribution = (v_strength - 1.0f) * v_env; // -1~+
	}

	// --------------------------------------------------
	// 돌풍 상태 갱신/트리거
	// --------------------------------------------------
	void _S10_updateGust() {
		if (!S10_active) return;

		float v_now = millis() / 1000.0f;
		if (S10_gustActive) {
			float v_age = v_now - S10_gustStartSec;
			if (v_age >= S10_gustDuration) {
				S10_gustActive   = false;
				S10_gustIntensity= 1.0f;
				return;
			}
			float v_prog = v_age / S10_gustDuration;
			float v_env;
			if (v_prog < 0.25f) {
				v_env = 1.0f - powf(1.0f - v_prog / 0.25f, 1.8f);
			} else if (v_prog < 0.65f) {
				v_env = 1.0f;
				float v_osc = 1.5f + (S10_phase * 0.5f);
				v_env += sinf(v_age * v_osc) * 0.08f;
			} else {
				float v_d = (v_prog - 0.65f) / 0.35f;
				v_env     = 1.0f - powf(v_d, 1.5f);
			}
			S10_gustIntensity = 1.0f + (S10_gustStrengthMax - 1.0f) * v_env;
			return;
		}

		if ((millis() - S10_lastGustCheck) >= 500) {
			S10_lastGustCheck = millis();

			float v_base  = S10_gustProbBase;
			float v_user  = g_A10_config_root.sim ? (g_A10_config_root.sim->gust_frequency / 100.0f) : 0.5f;
			float v_wfac  = 1.0f + (S10_currentWindSpeed / 8.9f) * 0.5f;
			float v_pmul  = (S10_phase == EN_A10_WEATHER_PHASE_CALM) ? (0.3f * v_wfac)
			                : (S10_phase == EN_A10_WEATHER_PHASE_STRONG ? (2.2f * v_wfac) : (0.9f * v_wfac));
			float v_p = v_base * v_user * v_pmul;

			if (A10_getRandom01() < v_p) {
				S10_gustActive     = true;
				S10_gustStartSec   = v_now;

				float v_speed_f = S10_currentWindSpeed / 6.7f;
				if (S10_phase == EN_A10_WEATHER_PHASE_CALM) {
					S10_gustDuration  = A10_randRange(3.0f, 8.0f);
					S10_gustIntensity = A10_randRange(1.08f, 1.33f);
				} else if (S10_phase == EN_A10_WEATHER_PHASE_STRONG) {
					S10_gustDuration  = A10_randRange(0.8f, 3.3f);
					S10_gustIntensity = A10_randRange(1.3f, 1.3f + 0.9f * (1.0f + v_speed_f * 0.3f));
				} else {
					S10_gustDuration  = A10_randRange(1.8f, 5.8f);
					S10_gustIntensity = A10_randRange(1.15f, 1.15f + 0.5f * (1.0f + v_speed_f * 0.2f));
				}
				S10_gustIntensity = fminf(S10_gustIntensity, S10_gustStrengthMax);
			}
		}
	}

	// --------------------------------------------------
	// 열기포 트리거
	// --------------------------------------------------
	void _S10_updateThermal() {
		if (!S10_active || S10_thermalActive) return;
		if ((millis() - S10_lastThermalCheck) < 700) return;

		S10_lastThermalCheck = millis();

		// config에 thermal.frequency가 없으므로, bubble_strength를 빈도 가중치로 사용
		float v_strength = g_A10_config_root.sim ? g_A10_config_root.sim->thermal.bubble_strength : 2.0f;
		float v_wfac  = 1.0f + (S10_currentWindSpeed / 8.0f) * 0.3f;
		float v_pmul  = (S10_phase == EN_A10_WEATHER_PHASE_CALM) ? 1.2f
		             : (S10_phase == EN_A10_WEATHER_PHASE_STRONG ? 0.7f : 1.0f);

		float v_freqFactor = 0.6f + 0.4f * fminf(3.0f, fmaxf(0.5f, v_strength)); // 0.6~1.8
		float v_p = S10_thermalFreqBase * v_freqFactor * v_wfac * v_pmul;

		if (A10_getRandom01() < v_p) {
			S10_thermalActive     = true;
			S10_thermalStartSec   = millis() / 1000.0f;
			float v_dur = A10_randRange(8.0f, 14.0f);
			if (S10_phase == EN_A10_WEATHER_PHASE_CALM)      v_dur *= 1.3f;
			else if (S10_phase == EN_A10_WEATHER_PHASE_STRONG)v_dur *= 0.8f;
			S10_thermalDuration = v_dur;
		}
	}

	// --------------------------------------------------
	// Phase 전환 및 목표값 갱신
	// --------------------------------------------------
	void _S10_updatePhase() {
		if (!S10_active) return;

		float v_now = millis() / 1000.0f;
		if (v_now - S10_phaseStartSec < S10_phaseDurationSec) return;

		T_A10_WindPhase_t v_old = S10_phase;
		float v_r = A10_getRandom01();

		if (v_old == EN_A10_WEATHER_PHASE_CALM) {
			S10_phase = (v_r < 0.7f) ? EN_A10_WEATHER_PHASE_NORMAL : EN_A10_WEATHER_PHASE_STRONG;
		} else if (v_old == EN_A10_WEATHER_PHASE_STRONG) {
			S10_phase = (v_r < 0.7f) ? EN_A10_WEATHER_PHASE_NORMAL : EN_A10_WEATHER_PHASE_CALM;
		} else {
			if (v_r < 0.4f)      S10_phase = EN_A10_WEATHER_PHASE_CALM;
			else if (v_r < 0.8f) S10_phase = EN_A10_WEATHER_PHASE_NORMAL;
			else                 S10_phase = EN_A10_WEATHER_PHASE_STRONG;
		}

		S10_phaseStartSec = v_now;
		float v_span = S10_baseMaxWind - S10_baseMinWind;

		if (S10_phase == EN_A10_WEATHER_PHASE_CALM) {
			S10_phaseDurationSec = A10_randRange(90.0f, 210.0f);
			S10_phaseMinWind = S10_baseMinWind;
			S10_phaseMaxWind = S10_baseMinWind + v_span * 0.6f;
		} else if (S10_phase == EN_A10_WEATHER_PHASE_NORMAL) {
			S10_phaseDurationSec = A10_randRange(120.0f, 300.0f);
			S10_phaseMinWind = S10_baseMinWind + v_span * 0.15f;
			S10_phaseMaxWind = S10_baseMinWind + v_span * 0.85f;
		} else {
			S10_phaseDurationSec = A10_randRange(60.0f, 150.0f);
			S10_phaseMinWind = S10_baseMinWind + v_span * 0.4f;
			S10_phaseMaxWind = S10_baseMaxWind;
		}

		S10_phaseMinWind = fmaxf(0.2f, S10_phaseMinWind);
		S10_phaseMaxWind = fminf(11.0f, S10_phaseMaxWind);

		_S10_generateTarget();
	}

	// --------------------------------------------------
	// 새 목표 풍속 생성
	// --------------------------------------------------
	void _S10_generateTarget() {
		if (!S10_active) return;

		float v_range = S10_phaseMaxWind - S10_phaseMinWind;
		float v_new   = S10_phaseMinWind + A10_getRandom01() * v_range;
		float v_mid   = (S10_phaseMinWind + S10_phaseMaxWind) * 0.5f;
		float v_bias  = A10_randRange(0.0f, 1.0f); // 중간값 쏠림
		v_new         = (v_new + v_mid * v_bias) / (1.0f + v_bias);
		S10_targetWindSpeed = v_new;

		// 변화 속도
		float v_var  = g_A10_config_root.sim ? (g_A10_config_root.sim->wind_variability / 100.0f) : 0.5f;
		float v_base = 0.0f;
		if (S10_phase == EN_A10_WEATHER_PHASE_CALM)       v_base = 0.08f + v_var * 0.12f;
		else if (S10_phase == EN_A10_WEATHER_PHASE_STRONG) v_base = 0.25f + v_var * 0.35f;
		else                                               v_base = 0.15f + v_var * 0.25f;

		float v_U = S10_currentWindSpeed; if (v_U < 0.1f) v_U = 0.1f;
		float v_tscale = (g_A10_config_root.sim ? g_A10_config_root.sim->turbulence.length_scale : 40.0f) / v_U;
		v_base *= (1.0f + v_tscale * 0.1f);

		S10_windChangeRate = v_base * A10_randRange(0.7f, 1.7f);
	}

	// --------------------------------------------------
	// 팬 속도 반영
	// --------------------------------------------------
	void _S10_applyFan(float p_speedPercent) {
		if (!_p_pwmCtrl) return;

		float v_req       = p_speedPercent / 100.0f; // 0~1.0
		float v_limit     = g_A10_config_root.sim ? (g_A10_config_root.sim->fan_limit / 100.0f) : 1.0f;
		float v_min       = g_A10_config_root.sim ? (g_A10_config_root.sim->min_fan / 100.0f)   : 0.12f;
		float v_intensity = g_A10_config_root.sim ? (g_A10_config_root.sim->wind_intensity / 100.0f) : 1.0f;

		if (!S10_fanPowerEnabled || v_intensity <= 0.01f) {
			_p_pwmCtrl->P10_setDutyPercent(0.0f);
			return;
		}
		if (S10_active) v_req *= v_intensity;

		v_req = fmaxf(v_min, fminf(v_limit, v_req));
		_p_pwmCtrl->P10_setDutyPercent(v_req * 100.0f);
	}
};

// 정적 멤버 정의
std::deque<CL_S10_Simulation::ST_S10_ChartEntry> CL_S10_Simulation::s_chartBuffer;
unsigned long CL_S10_Simulation::s_lastChartLogMs = 0;
