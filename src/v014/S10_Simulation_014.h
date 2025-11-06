#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : S10_Simulation_014.h
 * 모듈약어 : S10
 * 모듈명 : Smart Nature Wind 풍속 시뮬레이션 Manager (v014)
 * ------------------------------------------------------
 * 기능 요약:
 *  - 자연풍 물리 기반 풍속 시뮬레이션 (Phase / 돌풍 / 난류 / 열기포)
 *  - PWM 제어기(CL_P10_PWM)와 연동하여 실제 팬 제어
 *  - WindProfileDict(preset × style × adjust)로 해석된
 *    ST_A10_ResolvedWind_t 기반 동작
 *  - 장시간 안정작동을 위한 지터, 모멘텀, 스펙트럼 합성, 목표 재생성
 *  - Chart 버퍼 제공 (웹 UI 실시간 분석)
 *  - CT10(Control Manager)에서 enable/disable 게이트/파라미터 전달
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
#include <ArduinoJson.h>
#include <deque>
#include <cmath>

#include "A10_Const_014.h"
#include "P10_PWM_ctrl_012.h"

// ======================================================
// 런타임 상태 구조체
// ======================================================
typedef struct {
	bool  active               = false;  // 시뮬레이터 동작 여부
	bool  fanPowerEnabled      = true;   // 팬 전원 게이트 (CT10에서 제어)

	// 현재/목표 풍속 및 동역학
	float currentWindSpeed     = 3.6f;   // m/s
	float targetWindSpeed      = 3.6f;   // m/s
	float windChangeRate       = 0.1f;   // 목표로 수렴 속도 계수
	float windMomentum         = 0.0f;   // 관성 성분

	// Phase
	T_A10_WindPhase_t phase    = EN_A10_WEATHER_PHASE_NORMAL;
	float phaseStartSec        = 0.0f;
	float phaseDurationSec     = 120.0f;
	float phaseMinWind         = 2.0f;
	float phaseMaxWind         = 6.0f;

	// Preset 기반 범위
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
	bool          gustActive        = false;
	float         gustStartSec      = 0.0f;
	float         gustDuration      = 3.0f;
	float         gustIntensity     = 1.0f;
	unsigned long lastGustCheckMs   = 0;

	// 열기포
	bool          thermalActive       = false;
	float         thermalStartSec     = 0.0f;
	float         thermalDuration     = 8.0f;
	float         thermalContribution = 0.0f;
	unsigned long lastThermalCheckMs  = 0;

	// tick timer
	unsigned long lastUpdateMs        = 0;

	// 해석된 바람 파라미터
	ST_A10_ResolvedWind_t resolved; // preset × style × adjust 결과

	// 현재 적용 중인 preset/style 코드 (디버그/웹UI용)
	char presetCode[A10_Const::LEN_PRESET] = {0};
	char styleCode[A10_Const::LEN_PRESET]  = {0};

} ST_S10_Runtime_t;

// ======================================================
// 차트 엔트리
// ======================================================
class CL_S10_Simulation {
public:
	struct ST_S10_ChartEntry {
		unsigned long timestamp;
		float         wind_speed;
		float         pwm_duty;
		float         intensity;
		float         variability;
		float         turbulence;
		int8_t        preset_index;   // A10_getPresetIndex(presetCode)
		bool          gust_active;
		bool          thermal_active;
	};

private:
	ST_S10_Runtime_t _rt;
	CL_P10_PWM*      _pwm = nullptr;

	static std::deque<ST_S10_ChartEntry> s_chartBuffer;
	static unsigned long                  s_lastChartLogMs;

public:
	// ==================================================
	// 초기화
	// ==================================================
	void begin(CL_P10_PWM& p_pwm) {
		memset(&_rt, 0, sizeof(_rt));
		_rt.fanPowerEnabled = true;
		_rt.turbTimeScale   = 5.0f;
		_pwm = &p_pwm;
	}

	// 외부에서 바람 파라미터 적용
	void applyResolvedWind(const ST_A10_ResolvedWind_t& p_resolved,
	                       const char*                  p_presetCode,
	                       const char*                  p_styleCode) {
		_rt.resolved = p_resolved;

		if (p_presetCode) {
			strlcpy(_rt.presetCode, p_presetCode, sizeof(_rt.presetCode));
		} else {
			strlcpy(_rt.presetCode, "OCEAN", sizeof(_rt.presetCode));
		}

		if (p_styleCode) {
			strlcpy(_rt.styleCode, p_styleCode, sizeof(_rt.styleCode));
		} else {
			strlcpy(_rt.styleCode, "BALANCE", sizeof(_rt.styleCode));
		}

		_resetPresetShape();
	}

	// --------------------------------------------------
	// 바람 시뮬레이션 중단 (CT10 게이트 때 호출)
	// --------------------------------------------------
	void stop() {
		_rt.active           = false;
		_rt.phase            = EN_A10_WEATHER_PHASE_CALM;
		_rt.targetWindSpeed  = 0.0f;
		_rt.currentWindSpeed = 0.0f;
		_rt.gustActive       = false;
		_rt.thermalActive    = false;
		_rt.gustIntensity    = 1.0f;
		if (_pwm) _pwm->P10_setDutyPercent(0.0f);
	}

	// 팬 전원 게이트 (CT10에서 제어)
	void setFanPowerEnabled(bool p_enabled) {
		_rt.fanPowerEnabled = p_enabled;
		if (!p_enabled && _pwm) {
			_pwm->P10_setDutyPercent(0.0f);
		}
	}

	bool isActive() const { return _rt.active; }

	// ==================================================
	// tick
	// ==================================================
	void tick() {
		if (!_rt.active) return;
		if (!_pwm)       return;

		unsigned long v_now = millis();

		// 40~99ms 지터
		static uint32_t s_j = 0;
		uint32_t v_interval = 40U + (s_j % 60U);
		if (v_now - _rt.lastUpdateMs < v_interval) {
			yield();
			return;
		}
		s_j                = esp_random();
		float v_dt         = (v_now - _rt.lastUpdateMs) / 1000.0f;
		_rt.lastUpdateMs   = v_now;

		_updatePhase();
		_calcTurbulence(v_dt);
		_calcThermalEnvelope();
		_updateGust();
		_updateThermal();

		// 목표 풍속으로 천천히 접근
		float v_diff   = _rt.targetWindSpeed - _rt.currentWindSpeed;
		float v_change = v_diff * _rt.windChangeRate * v_dt;
		_rt.windMomentum = _rt.windMomentum * 0.85f + v_change * 0.15f;
		_rt.windMomentum = fmaxf(-0.5f, fminf(0.5f, _rt.windMomentum));

		float v_new = _rt.currentWindSpeed + _rt.windMomentum + _rt.spectralEnergyBuf;
		v_new       = fmaxf(0.2f, fminf(11.0f, v_new));
		_rt.currentWindSpeed = v_new;

		// 가까우면 목표 재생성 빈도 증가 / 멀면 감소
		float v_changeTh = 0.5f + (_rt.currentWindSpeed / 20.0f);
		float v_closeCh  = 30.0f;
		float v_farCh    = 6.0f;
		if (_rt.resolved.wind_variability > 70.0f) {
			v_closeCh *= 1.5f;
			v_farCh   *= 1.5f;
		}
		if (fabsf(v_diff) < v_changeTh) {
			if (A10_randRange(0.0f, 100.0f) < v_closeCh) {
				_generateTarget();
			}
		} else {
			if (A10_randRange(0.0f, 100.0f) < v_farCh) {
				_generateTarget();
			}
		}

		// PWM 반영
		float v_pct = _rt.currentWindSpeed * 10.0f + 10.0f; // base scaling
		v_pct *= _rt.gustIntensity;
		v_pct += _rt.thermalContribution * 5.0f;
		_applyFan(v_pct);

		// 차트 로그 (1초 주기)
		if (millis() - s_lastChartLogMs > 1000UL) {
			ST_S10_ChartEntry v_e;
			v_e.timestamp   = millis();
			v_e.wind_speed  = _rt.currentWindSpeed;
			v_e.pwm_duty    = _pwm ? _pwm->P10_getDutyPercent() : 0.0f;
			v_e.intensity   = _rt.resolved.wind_intensity;
			v_e.variability = _rt.resolved.wind_variability;
			v_e.turbulence  = _rt.resolved.turbulence_intensity_sigma;
			v_e.preset_index = A10_getPresetIndex(_rt.presetCode);
			v_e.gust_active   = _rt.gustActive;
			v_e.thermal_active= _rt.thermalActive;

			s_chartBuffer.push_back(v_e);
			while (s_chartBuffer.size() > 120U) {
				s_chartBuffer.pop_front();
			}
			s_lastChartLogMs = millis();
		}

		yield();
	}

	// ==================================================
	// JSON 출력
	// ==================================================
	void toJson(JsonDocument& p_doc) {
		JsonObject v_o = p_doc["sim"].to<JsonObject>();
		v_o["active"]  = _rt.active;
		v_o["phase"]   = g_A10_WEATHER_PHASE_NAMES_Arr[_rt.phase];
		v_o["wind"]    = _rt.currentWindSpeed;
		v_o["target"]  = _rt.targetWindSpeed;
		v_o["gust"]    = _rt.gustActive;
		v_o["thermal"] = _rt.thermalActive;
		v_o["pwm"]     = _pwm ? _pwm->P10_getDutyPercent() : 0.0f;

		v_o["presetCode"] = _rt.presetCode;
		v_o["styleCode"]  = _rt.styleCode;

		v_o["params"]["wind_intensity"]             = _rt.resolved.wind_intensity;
		v_o["params"]["gust_frequency"]             = _rt.resolved.gust_frequency;
		v_o["params"]["wind_variability"]           = _rt.resolved.wind_variability;
		v_o["params"]["fan_limit"]                  = _rt.resolved.fan_limit;
		v_o["params"]["min_fan"]                    = _rt.resolved.min_fan;
		v_o["params"]["turbulence_length_scale"]    = _rt.resolved.turbulence_length_scale;
		v_o["params"]["turbulence_intensity_sigma"] = _rt.resolved.turbulence_intensity_sigma;
		v_o["params"]["thermal_bubble_strength"]    = _rt.resolved.thermal_bubble_strength;
		v_o["params"]["thermal_bubble_radius"]      = _rt.resolved.thermal_bubble_radius;
	}

	void toChartJson(JsonDocument& p_doc) {
		JsonArray v_arr = p_doc["chart"].to<JsonArray>();
		for (std::deque<ST_S10_ChartEntry>::const_iterator v_it = s_chartBuffer.begin();
		     v_it != s_chartBuffer.end();
		     ++v_it) {
			JsonObject v_jo = v_arr.add<JsonObject>();
			v_jo["t"] = v_it->timestamp;
			v_jo["w"] = v_it->wind_speed;
			v_jo["p"] = v_it->pwm_duty;
			v_jo["g"] = v_it->gust_active;
			v_jo["h"] = v_it->thermal_active;
		}
	}

	// 디버그 로그
	void debugPrint() {
		CL_D10_Logger::log(
			EN_L10_LOG_DEBUG,
			"[S10] phase=%s wind=%.2f target=%.2f gust=%d therm=%d",
			g_A10_WEATHER_PHASE_NAMES_Arr[_rt.phase],
			_rt.currentWindSpeed,
			_rt.targetWindSpeed,
			_rt.gustActive,
			_rt.thermalActive
		);
	}

private:
	// --------------------------------------------------
	// 프리셋 기반 초기 설정
	// --------------------------------------------------
	void _resetPresetShape() {
		_rt.active        = false;
		_rt.gustActive    = false;
		_rt.thermalActive = false;
		_rt.gustIntensity = 1.0f;
		_rt.spectralEnergyBuf = 0.0f;
		_rt.spectralPhaseAcc  = 0.0f;
		_rt.windMomentum      = 0.0f;

		int8_t v_presetIdx = A10_getPresetIndex(_rt.presetCode);
		if (v_presetIdx < 0) {
			v_presetIdx = EN_A10_PRESET_OCEAN;
		}

		// 기본 preset shape (기존 v012 로직 재사용)
		switch (v_presetIdx) {
			case EN_A10_PRESET_COUNTRY:
				_rt.baseMinWind     = 0.7f;
				_rt.baseMaxWind     = 3.4f;
				_rt.gustProbBase    = 0.006f;
				_rt.gustStrengthMax = 1.35f;
				_rt.thermalFreqBase = 0.015f;
				break;
			case EN_A10_PRESET_MEDITERRANEAN:
				_rt.baseMinWind     = 1.6f;
				_rt.baseMaxWind     = 3.8f;
				_rt.gustProbBase    = 0.012f;
				_rt.gustStrengthMax = 1.55f;
				_rt.thermalFreqBase = 0.035f;
				break;
			case EN_A10_PRESET_OCEAN:
			default:
				_rt.baseMinWind     = 1.8f;
				_rt.baseMaxWind     = 5.5f;
				_rt.gustProbBase    = 0.040f;
				_rt.gustStrengthMax = 2.1f;
				_rt.thermalFreqBase = 0.022f;
				break;
			case EN_A10_PRESET_MOUNTAIN:
				_rt.baseMinWind     = 2.2f;
				_rt.baseMaxWind     = 7.5f;
				_rt.gustProbBase    = 0.045f;
				_rt.gustStrengthMax = 2.2f;
				_rt.thermalFreqBase = 0.028f;
				break;
			case EN_A10_PRESET_PLAINS:
				_rt.baseMinWind     = 4.0f;
				_rt.baseMaxWind     = 8.8f;
				_rt.gustProbBase    = 0.070f;
				_rt.gustStrengthMax = 2.4f;
				_rt.thermalFreqBase = 0.018f;
				break;
			case EN_A10_PRESET_HARBOR_BREEZE:
				_rt.baseMinWind     = 2.25f;
				_rt.baseMaxWind     = 5.35f;
				_rt.gustProbBase    = 0.025f;
				_rt.gustStrengthMax = 1.80f;
				_rt.thermalFreqBase = 0.026f;
				break;
			case EN_A10_PRESET_FOREST_CANOPY:
				_rt.baseMinWind     = 1.35f;
				_rt.baseMaxWind     = 4.00f;
				_rt.gustProbBase    = 0.010f;
				_rt.gustStrengthMax = 1.50f;
				_rt.thermalFreqBase = 0.012f;
				break;
			case EN_A10_PRESET_URBAN_SUNSET:
				_rt.baseMinWind     = 1.80f;
				_rt.baseMaxWind     = 4.90f;
				_rt.gustProbBase    = 0.030f;
				_rt.gustStrengthMax = 2.00f;
				_rt.thermalFreqBase = 0.020f;
				break;
			case EN_A10_PRESET_TROPICAL_RAIN:
				_rt.baseMinWind     = 3.15f;
				_rt.baseMaxWind     = 8.05f;
				_rt.gustProbBase    = 0.060f;
				_rt.gustStrengthMax = 2.20f;
				_rt.thermalFreqBase = 0.038f;
				break;
			case EN_A10_PRESET_DESERT_NIGHT:
				_rt.baseMinWind     = 0.90f;
				_rt.baseMaxWind     = 3.10f;
				_rt.gustProbBase    = 0.005f;
				_rt.gustStrengthMax = 1.30f;
				_rt.thermalFreqBase = 0.008f;
				break;
		}

		// Phase 초기화
		_rt.phase          = EN_A10_WEATHER_PHASE_NORMAL;
		_rt.phaseStartSec  = millis() / 1000.0f;
		float v_span       = _rt.baseMaxWind - _rt.baseMinWind;
		_rt.phaseMinWind   = _rt.baseMinWind + v_span * 0.15f;
		_rt.phaseMaxWind   = _rt.baseMinWind + v_span * 0.85f;
		_rt.phaseDurationSec = 120.0f;

		float v_mid = (_rt.baseMinWind + _rt.baseMaxWind) * 0.5f;
		_rt.currentWindSpeed = v_mid;
		_rt.targetWindSpeed  = v_mid;

		_rt.spectralEnergyBuf = 0.0f;
		_rt.spectralPhaseAcc  = 0.0f;
		_rt.windMomentum      = 0.0f;
		_rt.active            = true;

		_generateTarget();
	}

	// --------------------------------------------------
	// 난류 계산
	// --------------------------------------------------
	void _calcTurbulence(float p_dt) {
		if (!_rt.active) return;

		float v_L     = _rt.resolved.turbulence_length_scale;
		float v_sigma = _rt.resolved.turbulence_intensity_sigma;
		float v_U     = max(0.1f, _rt.currentWindSpeed);

		float v_sum = 0.0f;
		for (int v_i = 1; v_i <= 12; v_i++) {
			float v_n    = static_cast<float>(v_i) * 0.1f;
			float v_f    = v_n * v_U / v_L;
			float v_fLU  = v_f * v_L / v_U;
			float v_term = 70.8f * v_fLU * v_fLU;
			float v_numer = 4.0f * v_sigma * v_sigma * (v_L / v_U) * (1.0f + v_term);
			float v_denom = powf(1.0f + v_term, 5.0f / 6.0f);
			float v_S     = v_numer / v_denom;

			float v_phaseRate = 2.0f * static_cast<float>(M_PI) * v_f;
			float v_phaseInc  = v_phaseRate * p_dt;
			float v_phase     = _rt.spectralPhaseAcc * static_cast<float>(v_i)
			                    + v_phaseInc
			                    + A10_randRange(-0.1f, 0.1f);
			float v_amp       = sqrtf(2.0f * v_S * 0.083f);
			v_sum += v_amp * sinf(v_phase);
		}

		_rt.spectralPhaseAcc += p_dt * 0.5f;
		if (_rt.spectralPhaseAcc > 2.0f * static_cast<float>(M_PI)) {
			_rt.spectralPhaseAcc -= 2.0f * static_cast<float>(M_PI);
		}

		float v_corr = expf(-p_dt / _rt.turbTimeScale);
		_rt.spectralEnergyBuf = _rt.spectralEnergyBuf * v_corr + v_sum * (1.0f - v_corr);
	}

	// --------------------------------------------------
	// 열기포 포락선
	// --------------------------------------------------
	void _calcThermalEnvelope() {
		if (!_rt.active || !_rt.thermalActive) return;

		float v_t   = millis() / 1000.0f;
		float v_age = v_t - _rt.thermalStartSec;
		if (v_age >= _rt.thermalDuration) {
			_rt.thermalActive = false;
			_rt.thermalContribution = 0.0f;
			return;
		}

		float v_prog = v_age / _rt.thermalDuration;
		float v_env  = 0.0f;
		if (v_prog < 0.2f) {
			v_env = 1.0f - powf(1.0f - v_prog / 0.2f, 2.0f);
		} else if (v_prog < 0.6f) {
			v_env = 1.0f;
			v_env += sinf(v_age * (0.8f + _rt.phase * 0.2f) * 2.0f * static_cast<float>(M_PI)) * 0.15f;
		} else {
			float v_d = (v_prog - 0.6f) / 0.4f;
			v_env = 1.0f - powf(v_d, 1.3f);
		}

		float v_strength = _rt.resolved.thermal_bubble_strength;
		_rt.thermalContribution = (v_strength - 1.0f) * v_env;
	}

	// --------------------------------------------------
	// 돌풍 업데이트
	// --------------------------------------------------
	void _updateGust() {
		if (!_rt.active) return;
		float v_nowSec = millis() / 1000.0f;

		if (_rt.gustActive) {
			float v_age = v_nowSec - _rt.gustStartSec;
			if (v_age >= _rt.gustDuration) {
				_rt.gustActive    = false;
				_rt.gustIntensity = 1.0f;
				return;
			}

			float v_prog = v_age / _rt.gustDuration;
			float v_env;
			if (v_prog < 0.25f) {
				v_env = 1.0f - powf(1.0f - v_prog / 0.25f, 1.8f);
			} else if (v_prog < 0.65f) {
				v_env  = 1.0f;
				v_env += sinf(v_age * (1.5f + _rt.phase * 0.5f)) * 0.08f;
			} else {
				v_env = 1.0f - powf((v_prog - 0.65f) / 0.35f, 1.5f);
			}
			_rt.gustIntensity = 1.0f + (_rt.gustStrengthMax - 1.0f) * v_env;
			return;
		}

		if (millis() - _rt.lastGustCheckMs >= 500UL) {
			_rt.lastGustCheckMs = millis();
			float v_base = _rt.gustProbBase;
			float v_user = _rt.resolved.gust_frequency / 100.0f;
			float v_wfac = 1.0f + (_rt.currentWindSpeed / 8.9f) * 0.5f;

			float v_pmul;
			if (_rt.phase == EN_A10_WEATHER_PHASE_CALM) {
				v_pmul = 0.3f * v_wfac;
			} else if (_rt.phase == EN_A10_WEATHER_PHASE_STRONG) {
				v_pmul = 2.2f * v_wfac;
			} else {
				v_pmul = 0.9f * v_wfac;
			}
			float v_p = v_base * v_user * v_pmul;
			if (A10_getRandom01() < v_p) {
				_rt.gustActive   = true;
				_rt.gustStartSec = v_nowSec;
				float v_speedF   = _rt.currentWindSpeed / 6.7f;

				if (_rt.phase == EN_A10_WEATHER_PHASE_CALM) {
					_rt.gustDuration  = A10_randRange(3.0f, 8.0f);
					_rt.gustIntensity = A10_randRange(1.08f, 1.33f);
				} else if (_rt.phase == EN_A10_WEATHER_PHASE_STRONG) {
					_rt.gustDuration  = A10_randRange(0.8f, 3.3f);
					_rt.gustIntensity = A10_randRange(1.3f, 1.3f + 0.9f * (1.0f + v_speedF * 0.3f));
				} else {
					_rt.gustDuration  = A10_randRange(1.8f, 5.8f);
					_rt.gustIntensity = A10_randRange(1.15f, 1.15f + 0.5f * (1.0f + v_speedF * 0.2f));
				}
				_rt.gustIntensity = min(_rt.gustIntensity, _rt.gustStrengthMax);
			}
		}
	}

	// --------------------------------------------------
	// 열기포 업데이트
	// --------------------------------------------------
	void _updateThermal() {
		if (!_rt.active || _rt.thermalActive) return;
		if (millis() - _rt.lastThermalCheckMs < 700UL) return;
		_rt.lastThermalCheckMs = millis();

		float v_strength = _rt.resolved.thermal_bubble_strength;
		float v_wfac     = 1.0f + (_rt.currentWindSpeed / 8.0f) * 0.3f;
		float v_pmul;
		if (_rt.phase == EN_A10_WEATHER_PHASE_CALM) {
			v_pmul = 1.2f;
		} else if (_rt.phase == EN_A10_WEATHER_PHASE_STRONG) {
			v_pmul = 0.7f;
		} else {
			v_pmul = 1.0f;
		}
		float v_freq = _rt.thermalFreqBase
		               * (0.6f + 0.4f * min(3.0f, max(0.5f, v_strength)))
		               * v_wfac
		               * v_pmul;

		if (A10_getRandom01() < v_freq) {
			_rt.thermalActive   = true;
			_rt.thermalStartSec = millis() / 1000.0f;
			float v_d = A10_randRange(8.0f, 14.0f);
			if (_rt.phase == EN_A10_WEATHER_PHASE_CALM) {
				v_d *= 1.3f;
			} else if (_rt.phase == EN_A10_WEATHER_PHASE_STRONG) {
				v_d *= 0.8f;
			}
			_rt.thermalDuration = v_d;
		}
	}

	// --------------------------------------------------
	// Phase 전환
	// --------------------------------------------------
	void _updatePhase() {
		if (!_rt.active) return;

		float v_now = millis() / 1000.0f;
		if (v_now - _rt.phaseStartSec < _rt.phaseDurationSec) return;

		T_A10_WindPhase_t v_old = _rt.phase;
		float v_r = A10_getRandom01();
		if (v_old == EN_A10_WEATHER_PHASE_CALM) {
			_rt.phase = (v_r < 0.7f) ? EN_A10_WEATHER_PHASE_NORMAL : EN_A10_WEATHER_PHASE_STRONG;
		} else if (v_old == EN_A10_WEATHER_PHASE_STRONG) {
			_rt.phase = (v_r < 0.7f) ? EN_A10_WEATHER_PHASE_NORMAL : EN_A10_WEATHER_PHASE_CALM;
		} else {
			if (v_r < 0.4f) {
				_rt.phase = EN_A10_WEATHER_PHASE_CALM;
			} else if (v_r < 0.8f) {
				_rt.phase = EN_A10_WEATHER_PHASE_NORMAL;
			} else {
				_rt.phase = EN_A10_WEATHER_PHASE_STRONG;
			}
		}

		_rt.phaseStartSec = v_now;
		float v_span      = _rt.baseMaxWind - _rt.baseMinWind;

		if (_rt.phase == EN_A10_WEATHER_PHASE_CALM) {
			_rt.phaseDurationSec = A10_randRange(90.0f, 210.0f);
			_rt.phaseMinWind     = _rt.baseMinWind;
			_rt.phaseMaxWind     = _rt.baseMinWind + v_span * 0.6f;
		} else if (_rt.phase == EN_A10_WEATHER_PHASE_NORMAL) {
			_rt.phaseDurationSec = A10_randRange(120.0f, 300.0f);
			_rt.phaseMinWind     = _rt.baseMinWind + v_span * 0.15f;
			_rt.phaseMaxWind     = _rt.baseMinWind + v_span * 0.85f;
		} else { // STRONG
			_rt.phaseDurationSec = A10_randRange(60.0f, 150.0f);
			_rt.phaseMinWind     = _rt.baseMinWind + v_span * 0.4f;
			_rt.phaseMaxWind     = _rt.baseMaxWind;
		}

		_rt.phaseMinWind = max(0.2f, _rt.phaseMinWind);
		_rt.phaseMaxWind = min(11.0f, _rt.phaseMaxWind);

		_generateTarget();
	}

	// --------------------------------------------------
	// 목표 풍속 갱신
	// --------------------------------------------------
	void _generateTarget() {
		if (!_rt.active) return;

		float v_range = _rt.phaseMaxWind - _rt.phaseMinWind;
		if (v_range <= 0.01f) {
			_rt.targetWindSpeed = _rt.phaseMinWind;
			return;
		}

		float v_w   = _rt.phaseMinWind + A10_getRandom01() * v_range;
		float v_mid = (_rt.phaseMinWind + _rt.phaseMaxWind) * 0.5f;
		float v_bias = A10_randRange(0.0f, 1.0f);
		v_w = (v_w + v_mid * v_bias) / (1.0f + v_bias);
		_rt.targetWindSpeed = v_w;

		float v_var  = _rt.resolved.wind_variability / 100.0f;
		float v_base;
		if (_rt.phase == EN_A10_WEATHER_PHASE_CALM) {
			v_base = 0.08f + v_var * 0.12f;
		} else if (_rt.phase == EN_A10_WEATHER_PHASE_STRONG) {
			v_base = 0.25f + v_var * 0.35f;
		} else {
			v_base = 0.15f + v_var * 0.25f;
		}

		float v_U = max(0.1f, _rt.currentWindSpeed);
		float v_tscale = (_rt.resolved.turbulence_length_scale / max(0.1f, v_U));
		v_base *= (1.0f + v_tscale * 0.1f);

		_rt.windChangeRate = v_base * A10_randRange(0.7f, 1.7f);
	}

	// --------------------------------------------------
	// 팬 반영
	// --------------------------------------------------
	void _applyFan(float p_pct) {
		if (!_pwm) return;

		float v_req   = p_pct / 100.0f;
		float v_limit = _rt.resolved.fan_limit / 100.0f;
		float v_min   = _rt.resolved.min_fan / 100.0f;
		float v_int   = _rt.resolved.wind_intensity / 100.0f;

		if (!_rt.fanPowerEnabled || v_int <= 0.01f) {
			_pwm->P10_setDutyPercent(0.0f);
			return;
		}

		if (_rt.active) {
			v_req *= v_int;
		}
		if (v_req < v_min)   v_req = v_min;
		if (v_req > v_limit) v_req = v_limit;

		_pwm->P10_setDutyPercent(v_req * 100.0f);
	}
};

// ------------------------------------------------------
// 정적 멤버 정의
// ------------------------------------------------------
inline std::deque<CL_S10_Simulation::ST_S10_ChartEntry>
	CL_S10_Simulation::s_chartBuffer;

inline unsigned long
	CL_S10_Simulation::s_lastChartLogMs = 0;
