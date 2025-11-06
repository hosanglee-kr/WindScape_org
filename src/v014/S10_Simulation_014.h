#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : S10_Simulation_014.h
 * 모듈약어 : S10
 * 모듈명 : Smart Nature Wind 풍속 시뮬레이션 Manager (v014)
 * ------------------------------------------------------
 * 기능 요약:
 *  - 자연풍 물리 기반 풍속 시뮬레이션 (Phase / 돌풍 / 난류 / 열기포 / 지터)
 *  - PWM 제어기(CL_P10_PWM)와 연동하여 실제 팬 제어
 *  - 바람 프로파일 해석 결과(ResolvedWind) 즉시 적용 지원
 *  - CT10(Control Task) 세그먼트 전환과 연동, ON/OFF 게이팅 대응
 *  - Chart 버퍼(최근 120초) 제공 (웹 UI 실시간 분석)
 * ------------------------------------------------------
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
 * 의존:
 *  - A10_Const_014.h         (타입/유틸: ST_A10_ResolvedWind_t, A10_getRandom01, A10_randRange 등)
 *  - C10_ConfigManager_014.h (전역 g_A10_windDict 등 참조 가능)
 *  - D10_Logger_011.h
 *  - P10_PWM_ctrl_012.h
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
	// 외부에서 참조하는 최소 상태
	bool  S10_active           = false;   // 시뮬레이터 활성 여부 (CT10이 ON phase에서 true 유지)
	bool  S10_fanPowerEnabled  = true;    // 전원/모션 게이트용 소프트 스위치
	float S10_currentWindSpeed = 3.6f;    // [m/s] 현재 풍속 내부상태(추정치)
	float S10_targetWindSpeed  = 3.6f;    // [m/s] 목표 풍속

	// 현재 적용 중인 해석 파라미터(ResolvedWind 기반, UI/로그 확인용)
	char  S10_presetCode[24]   = {0};
	char  S10_styleCode[24]    = {0};
	float S10_userIntensity    = 70.0f;   // 0~100
	float S10_userVariability  = 50.0f;   // 0~100
	float S10_userGustFreq     = 45.0f;   // 0~100(상대)
	float S10_minFanPct        = 10.0f;   // 0~100
	float S10_fanLimitPct      = 90.0f;   // 0~100
	float S10_turbLenScale     = 40.0f;   // 특성 길이
	float S10_turbSigma        = 0.50f;   // 난류 강도 sigma
	float S10_thermalStrength  = 2.0f;    // 열기포 강도(1.0=무효)
	float S10_thermalRadius    = 18.0f;   // 열기포 반경(상대단위)

	// Phase 상태
	T_A10_WindPhase_t S10_phase           = EN_A10_WEATHER_PHASE_NORMAL;
	float             S10_phaseStartSec   = 0.0f;
	float             S10_phaseDurationSec= 120.0f;
	float             S10_phaseMinWind    = 2.0f;
	float             S10_phaseMaxWind    = 6.0f;

	// Preset 기반 범위(해석 적용 후 내부 재계산에 사용)
	float S10_baseMinWind      = 1.8f;
	float S10_baseMaxWind      = 5.5f;
	float S10_gustProbBase     = 0.040f;
	float S10_gustStrengthMax  = 2.1f;

	// 난류 합성 버퍼
	float S10_spectralEnergyBuf= 0.0f;
	float S10_spectralPhaseAcc = 0.0f;
	float S10_turbTimeScale    = 5.0f;

	// 돌풍 상태
	bool          S10_gustActive    = false;
	float         S10_gustStartSec  = 0.0f;
	float         S10_gustDuration  = 3.0f;
	float         S10_gustIntensity = 1.0f;
	unsigned long S10_lastGustCheck = 0;

	// 열기포 상태
	bool          S10_thermalActive       = false;
	float         S10_thermalStartSec     = 0.0f;
	float         S10_thermalDuration     = 8.0f;
	float         S10_thermalContribution = 0.0f;
	unsigned long S10_lastThermalCheck    = 0;

	// 내부 보간/지터
	float         S10_windChangeRate = 0.12f; // 목표 접근율(상대)
	float         S10_windMomentum   = 0.0f;  // 저역 통과된 변화량
	unsigned long S10_lastUpdateMs   = 0;

	// 차트 엔트리(최근 120개 ≒ 120초)
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
	// 초기화/종료
	// ==================================================
	void begin(CL_P10_PWM& p_pwm) {
		_p_pwm = &p_pwm;
		S10_active = false;
		S10_fanPowerEnabled = true;
		S10_currentWindSpeed = 3.6f;
		S10_targetWindSpeed  = 3.6f;
		S10_spectralEnergyBuf= 0.0f;
		S10_spectralPhaseAcc = 0.0f;
		S10_windMomentum     = 0.0f;
		S10_lastUpdateMs     = millis();
		CL_D10_Logger::log(EN_L10_LOG_INFO, "[S10] begin()");
	}

	inline void stop() {
		S10_active = false;
		S10_phase  = EN_A10_WEATHER_PHASE_CALM;
		S10_targetWindSpeed   = 0.0f;
		S10_currentWindSpeed  = 0.0f;
		if (_p_pwm) _p_pwm->P10_setDutyPercent(0.0f);
	}

	// ==================================================
	// 메인 tick (CT10 주기 호출)
	// ==================================================
	void tick() {
		if (!S10_active) return;

		unsigned long v_now = millis();

		// 40~99ms 사이 지터 인터벌
		static uint32_t s_j = 0;
		uint32_t v_interval = 40 + (s_j % 60);
		if (v_now - S10_lastUpdateMs < v_interval) {
			yield();
			return;
		}
		s_j = esp_random();
		float v_dt = (v_now - S10_lastUpdateMs) / 1000.0f;
		S10_lastUpdateMs = v_now;

		_updatePhase();
		_calcTurb(v_dt);
		_calcThermalEnvelope();
		_updateGust();
		_updateThermal();

		// 목표 풍속으로 서서히 접근 (+난류 스펙트럼)
		float v_diff   = S10_targetWindSpeed - S10_currentWindSpeed;
		float v_change = v_diff * S10_windChangeRate * v_dt;
		S10_windMomentum = S10_windMomentum * 0.85f + v_change * 0.15f;
		S10_windMomentum = fmaxf(-0.5f, fminf(0.5f, S10_windMomentum));
		float v_new = S10_currentWindSpeed + S10_windMomentum + S10_spectralEnergyBuf;
		v_new = fmaxf(0.2f, fminf(11.0f, v_new));
		S10_currentWindSpeed = v_new;

		// 타겟 재생성 빈도 동적 제어
		float v_changeTh = 0.5f + (S10_currentWindSpeed / 20.0f);
		float v_closeCh  = 30.0f;
		float v_farCh    = 6.0f;
		if (S10_userVariability > 70.0f) { v_closeCh *= 1.5f; v_farCh *= 1.5f; }
		if (fabsf(v_diff) < v_changeTh) {
			if (A10_randRange(0.0f,100.0f) < v_closeCh) _generateTarget();
		} else {
			if (A10_randRange(0.0f,100.0f) < v_farCh) _generateTarget();
		}

		// PWM 반영
		float v_pct = S10_currentWindSpeed * 10.0f + 10.0f; // 실측 스케일 대응용 기저
		v_pct *= S10_gustIntensity;
		v_pct += S10_thermalContribution * 5.0f;
		_applyFan(v_pct);

		// 차트 로그(1Hz)
		if (millis() - s_lastChartLogMs > 1000) {
			ST_S10_ChartEntry v_e;
			v_e.timestamp   = millis();
			v_e.wind_speed  = S10_currentWindSpeed;
			v_e.pwm_duty    = _p_pwm ? _p_pwm->P10_getDutyPercent() : 0.0f;
			v_e.intensity   = S10_userIntensity;
			v_e.variability = S10_userVariability;
			v_e.turbulence  = S10_turbSigma;
			v_e.preset_id   = (uint8_t)A10_getPresetIndexByCode(S10_presetCode); // A10_Const_014.h 제공
			v_e.gust_active = S10_gustActive;
			v_e.thermal_active = S10_thermalActive;
			s_chartBuffer.push_back(v_e);
			if (s_chartBuffer.size() > 120) s_chartBuffer.pop_front();
			s_lastChartLogMs = millis();
		}

		yield();
	}

	// ==================================================
	// 파라미터 설정 API (하위호환 + 신규)
	// ==================================================
	// (하위호환) 연속모드/구버전 제어용
	void S10_setParams(float p_wind_intensity,
	                   float p_wind_variability,
	                   float p_gust_freq,
	                   float p_min_fan,
	                   float p_fan_limit,
	                   const char* p_presetCode) {
		S10_userIntensity   = constrain(p_wind_intensity,   0.0f, 100.0f);
		S10_userVariability = constrain(p_wind_variability, 0.0f, 100.0f);
		S10_userGustFreq    = constrain(p_gust_freq,        0.0f, 100.0f);
		S10_minFanPct       = constrain(p_min_fan,          0.0f, 100.0f);
		S10_fanLimitPct     = constrain(p_fan_limit,        0.0f, 100.0f);
		strlcpy(S10_presetCode, p_presetCode?p_presetCode:"OCEAN", sizeof(S10_presetCode));
		if (S10_styleCode[0] == '\0') strlcpy(S10_styleCode, "BALANCE", sizeof(S10_styleCode));

		_applyPresetCore(S10_presetCode); // 베이스 범위/확률 테이블 갱신
		_initPhaseFromBase();
		S10_active = true;
		_generateTarget();
	}

	// (하위호환) presetCode만 교체
	void S10_applyPreset(const char* p_presetCode) {
		if (!p_presetCode || !p_presetCode[0]) return;
		strlcpy(S10_presetCode, p_presetCode, sizeof(S10_presetCode));
		_applyPresetCore(S10_presetCode);
		_initPhaseFromBase();
		_generateTarget();
	}

	// (신규) windProfile 해석 결과를 한 번에 적용
	void S10_applyResolvedWind(const ST_A10_ResolvedWind_t& p_resolved) {
		strlcpy(S10_presetCode, p_resolved.presetCode, sizeof(S10_presetCode));
		strlcpy(S10_styleCode , p_resolved.styleCode , sizeof(S10_styleCode));

		S10_userIntensity   = constrain(p_resolved.wind_intensity,   0.0f, 100.0f);
		S10_userVariability = constrain(p_resolved.wind_variability, 0.0f, 100.0f);
		S10_userGustFreq    = constrain(p_resolved.gust_frequency,   0.0f, 100.0f);
		S10_minFanPct       = constrain(p_resolved.min_fan,          0.0f, 100.0f);
		S10_fanLimitPct     = constrain(p_resolved.fan_limit,        0.0f, 100.0f);
		S10_turbLenScale    = max(1.0f, p_resolved.turbulence_length_scale);
		S10_turbSigma       = max(0.0f, p_resolved.turbulence_intensity_sigma);
		S10_thermalStrength = max(1.0f, p_resolved.thermal_bubble_strength);
		S10_thermalRadius   = max(0.0f, p_resolved.thermal_bubble_radius);

		// preset 테이블로부터 베이스 범위/확률 재설정
		_applyPresetCore(S10_presetCode);

		// 스타일/어드저스트 적용에 따라 변화율/타겟 재생성 빈도 보정
		S10_windChangeRate = 0.10f + (S10_userVariability/100.0f)*0.20f; // 0.10 ~ 0.30
		S10_windChangeRate = constrain(S10_windChangeRate, 0.06f, 0.34f);

		_initPhaseFromBase();
		S10_active = true;
		_generateTarget();
	}

	// ==================================================
	// JSON Export (UI/디버깅)
	// ==================================================
	void S10_toJson(JsonDocument& p_doc) {
		JsonObject v_o = p_doc["sim"];
		v_o["active"]     = S10_active;
		v_o["phase"]      = g_A10_WEATHER_PHASE_NAMES_Arr[S10_phase];
		v_o["wind"]       = S10_currentWindSpeed;
		v_o["target"]     = S10_targetWindSpeed;
		v_o["gust"]       = S10_gustActive;
		v_o["thermal"]    = S10_thermalActive;
		v_o["pwm"]        = _p_pwm ? _p_pwm->P10_getDutyPercent() : 0.0f;

		v_o["presetCode"] = S10_presetCode;
		v_o["styleCode"]  = S10_styleCode;

		v_o["intensity"]   = S10_userIntensity;
		v_o["variability"] = S10_userVariability;
		v_o["gust_freq"]   = S10_userGustFreq;

		v_o["fan_limit"]   = S10_fanLimitPct;
		v_o["min_fan"]     = S10_minFanPct;

		v_o["turbulence_len"]   = S10_turbLenScale;
		v_o["turbulence_sigma"] = S10_turbSigma;
		v_o["thermal_strength"] = S10_thermalStrength;
		v_o["thermal_radius"]   = S10_thermalRadius;

		v_o["phase_min"]   = S10_phaseMinWind;
		v_o["phase_max"]   = S10_phaseMaxWind;
		v_o["base_min"]    = S10_baseMinWind;
		v_o["base_max"]    = S10_baseMaxWind;
	}

	void S10_toChartJson(JsonDocument& p_doc) {
		JsonArray v_arr = p_doc["chart"];
		for (auto& v_e : s_chartBuffer) {
			JsonObject v_jo = v_arr.add<JsonObject>();
			v_jo["t"] = v_e.timestamp;
			v_jo["w"] = v_e.wind_speed;
			v_jo["p"] = v_e.pwm_duty;
			v_jo["g"] = v_e.gust_active;
			v_jo["h"] = v_e.thermal_active;
		}
	}

private:
	CL_P10_PWM* _p_pwm = nullptr;

	// --------------------------------------------------
	// 프리셋 베이스 범위/확률 테이블 적용
	//  - 각 프리셋은 대략적인 풍속 범위/돌풍 확률/강도를 달리 갖는다
	//  - 코드값은 A10_Const_014.h 의 enum/헬퍼로 매핑
	// --------------------------------------------------
	void _applyPresetCore(const char* p_presetCode) {
		int8_t v_idx = A10_getPresetIndexByCode(p_presetCode?p_presetCode:"OCEAN");
		if (v_idx < 0) v_idx = EN_A10_PRESET_OCEAN;

		switch (v_idx) {
			case EN_A10_PRESET_COUNTRY:
				S10_baseMinWind=0.7f;  S10_baseMaxWind=3.4f;  S10_gustProbBase=0.006f; S10_gustStrengthMax=1.35f; break;
			case EN_A10_PRESET_MEDITERRANEAN:
				S10_baseMinWind=1.6f;  S10_baseMaxWind=3.8f;  S10_gustProbBase=0.012f; S10_gustStrengthMax=1.55f; break;
			case EN_A10_PRESET_OCEAN:
			default:
				S10_baseMinWind=1.8f;  S10_baseMaxWind=5.5f;  S10_gustProbBase=0.040f; S10_gustStrengthMax=2.10f; break;
			case EN_A10_PRESET_MOUNTAIN:
				S10_baseMinWind=2.2f;  S10_baseMaxWind=7.5f;  S10_gustProbBase=0.045f; S10_gustStrengthMax=2.20f; break;
			case EN_A10_PRESET_PLAINS:
				S10_baseMinWind=4.0f;  S10_baseMaxWind=8.8f;  S10_gustProbBase=0.070f; S10_gustStrengthMax=2.40f; break;
			case EN_A10_PRESET_HARBOR_BREEZE:
				S10_baseMinWind=2.25f; S10_baseMaxWind=5.35f; S10_gustProbBase=0.025f; S10_gustStrengthMax=1.80f; break;
			case EN_A10_PRESET_FOREST_CANOPY:
				S10_baseMinWind=1.35f; S10_baseMaxWind=4.00f; S10_gustProbBase=0.010f; S10_gustStrengthMax=1.50f; break;
			case EN_A10_PRESET_URBAN_SUNSET:
				S10_baseMinWind=1.80f; S10_baseMaxWind=4.90f; S10_gustProbBase=0.030f; S10_gustStrengthMax=2.00f; break;
			case EN_A10_PRESET_TROPICAL_RAIN:
				S10_baseMinWind=3.15f; S10_baseMaxWind=8.05f; S10_gustProbBase=0.060f; S10_gustStrengthMax=2.20f; break;
			case EN_A10_PRESET_DESERT_NIGHT:
				S10_baseMinWind=0.90f; S10_baseMaxWind=3.10f; S10_gustProbBase=0.005f; S10_gustStrengthMax=1.30f; break;
		}
	}

	// --------------------------------------------------
	// Phase 초기화 (preset 베이스 + 사용자 가변성 반영)
	// --------------------------------------------------
	void _initPhaseFromBase() {
		S10_phase = EN_A10_WEATHER_PHASE_NORMAL;
		S10_phaseStartSec = millis()/1000.0f;
		float v_span = S10_baseMaxWind - S10_baseMinWind;
		S10_phaseMinWind  = S10_baseMinWind + v_span*0.15f;
		S10_phaseMaxWind  = S10_baseMinWind + v_span*0.85f;
		S10_phaseDurationSec = 120.0f;

		float v_mid = (S10_baseMinWind + S10_baseMaxWind)*0.5f;
		S10_currentWindSpeed = v_mid;
		S10_targetWindSpeed  = v_mid;

		S10_gustActive = false;
		S10_thermalActive = false;
		S10_gustIntensity = 1.0f;
		S10_spectralEnergyBuf = 0.0f;
		S10_spectralPhaseAcc  = 0.0f;
		S10_windMomentum      = 0.0f;
	}

	// --------------------------------------------------
	// 난류 합성(카르만 스펙트럼 유사)
	// --------------------------------------------------
	void _calcTurb(float p_dt) {
		float v_L     = S10_turbLenScale;
		float v_sigma = S10_turbSigma;
		float v_U     = max(0.1f, S10_currentWindSpeed);

		float v_sum = 0.0f;
		for (int i=1; i<=12; i++) {
			float v_n = i * 0.1f;
			float v_f = v_n * v_U / v_L;
			float v_fLU = v_f * v_L / v_U;
			float v_term = 70.8f * v_fLU * v_fLU;
			float v_numer = 4.0f*v_sigma*v_sigma*(v_L/v_U)*(1.0f+v_term);
			float v_denom = powf(1.0f+v_term, 5.0f/6.0f);
			float v_S = v_numer/v_denom;

			float v_phaseRate = 2.0f * M_PI * v_f;
			float v_phaseInc  = v_phaseRate * p_dt;
			float v_phase     = S10_spectralPhaseAcc * i + v_phaseInc + A10_randRange(-0.1f,0.1f);
			float v_amp       = sqrtf(2.0f*v_S*0.083f);
			v_sum += v_amp * sinf(v_phase);
		}

		S10_spectralPhaseAcc += p_dt * 0.5f;
		if (S10_spectralPhaseAcc > 2*M_PI) S10_spectralPhaseAcc -= 2*M_PI;

		float v_corr = expf(-p_dt/S10_turbTimeScale);
		S10_spectralEnergyBuf = S10_spectralEnergyBuf*v_corr + v_sum*(1.0f-v_corr);
	}

	// --------------------------------------------------
	// 열기포 포락선
	// --------------------------------------------------
	void _calcThermalEnvelope() {
		if (!S10_thermalActive) return;

		float v_t = millis()/1000.0f;
		float v_age = v_t - S10_thermalStartSec;
		if (v_age >= S10_thermalDuration) {
			S10_thermalActive = false;
			return;
		}

		float v_prog = v_age / S10_thermalDuration;
		float v_env = 0.0f;
		if (v_prog < 0.2f) {
			v_env = 1.0f - powf(1.0f - v_prog/0.2f,2.0f);
		} else if (v_prog < 0.6f) {
			v_env = 1.0f;
			v_env += sinf(v_age*(0.8f + S10_phase*0.2f)*2.0f*M_PI)*0.15f;
		} else {
			float v_d = (v_prog-0.6f)/0.4f;
			v_env = 1.0f - powf(v_d,1.3f);
		}

		float v_str = S10_thermalStrength;
		S10_thermalContribution = (v_str - 1.0f)*v_env;
	}

	// --------------------------------------------------
	// 돌풍 상태 갱신
	// --------------------------------------------------
	void _updateGust() {
		float v_now = millis()/1000.0f;

		if (S10_gustActive) {
			float v_age = v_now - S10_gustStartSec;
			if (v_age >= S10_gustDuration) {
				S10_gustActive=false; S10_gustIntensity=1.0f;
				return;
			}
			float v_prog = v_age/S10_gustDuration;
			float v_env;
			if (v_prog<0.25f) v_env=1.0f - powf(1.0f - v_prog/0.25f,1.8f);
			else if (v_prog<0.65f) {
				v_env=1.0f;
				v_env+=sinf(v_age*(1.5f+S10_phase*0.5f))*0.08f;
			} else v_env = 1.0f - powf((v_prog-0.65f)/0.35f,1.5f);
			S10_gustIntensity = 1.0f + (S10_gustStrengthMax -1.0f)*v_env;
			return;
		}

		if (millis() - S10_lastGustCheck >= 500) {
			S10_lastGustCheck = millis();
			float v_base = S10_gustProbBase;
			float v_user = S10_userGustFreq/100.0f;
			float v_wfac = 1.0f + (S10_currentWindSpeed/8.9f)*0.5f;
			float v_pmul = (S10_phase == EN_A10_WEATHER_PHASE_CALM) ? (0.3f*v_wfac)
			            : (S10_phase == EN_A10_WEATHER_PHASE_STRONG?2.2f*v_wfac:0.9f*v_wfac);
			float v_p = v_base*v_user*v_pmul;
			if (A10_getRandom01() < v_p) {
				S10_gustActive   = true;
				S10_gustStartSec = v_now;
				float v_speedF = S10_currentWindSpeed/6.7f;
				if (S10_phase==EN_A10_WEATHER_PHASE_CALM) {
					S10_gustDuration  = A10_randRange(3.0f,8.0f);
					S10_gustIntensity = A10_randRange(1.08f,1.33f);
				} else if (S10_phase==EN_A10_WEATHER_PHASE_STRONG) {
					S10_gustDuration  = A10_randRange(0.8f,3.3f);
					S10_gustIntensity = A10_randRange(1.3f,1.3f+0.9f*(1+v_speedF*0.3f));
				} else {
					S10_gustDuration  = A10_randRange(1.8f,5.8f);
					S10_gustIntensity = A10_randRange(1.15f,1.15f+0.5f*(1+v_speedF*0.2f));
				}
				S10_gustIntensity = min(S10_gustIntensity, S10_gustStrengthMax);
			}
		}
	}

	// --------------------------------------------------
	// 열기포 시작 결정
	// --------------------------------------------------
	void _updateThermal() {
		if (S10_thermalActive) return;
		if (millis() - S10_lastThermalCheck < 700) return;
		S10_lastThermalCheck = millis();

		float v_str = S10_thermalStrength;
		float v_wfac = 1.0f + (S10_currentWindSpeed/8.0f)*0.3f;
		float v_pmul = (S10_phase == EN_A10_WEATHER_PHASE_CALM)?1.2f:
		               (S10_phase==EN_A10_WEATHER_PHASE_STRONG?0.7f:1.0f);
		float v_freq = 0.022f * (0.6f + 0.4f*min(3.0f,max(0.5f,v_str))) * v_wfac * v_pmul;
		if (A10_getRandom01() < v_freq) {
			S10_thermalActive   = true;
			S10_thermalStartSec = millis()/1000.0f;
			float v_d = A10_randRange(8.0f,14.0f);
			if (S10_phase == EN_A10_WEATHER_PHASE_CALM) v_d*=1.3f;
			else if (S10_phase == EN_A10_WEATHER_PHASE_STRONG) v_d*=0.8f;
			S10_thermalDuration = v_d;
		}
	}

	// --------------------------------------------------
	// Phase 전환
	// --------------------------------------------------
	void _updatePhase() {
		float v_now = millis()/1000.0f;
		if (v_now - S10_phaseStartSec < S10_phaseDurationSec) return;

		T_A10_WindPhase_t v_old = S10_phase;
		float v_r = A10_getRandom01();
		if (v_old == EN_A10_WEATHER_PHASE_CALM)
			S10_phase = (v_r<0.7f)?EN_A10_WEATHER_PHASE_NORMAL:EN_A10_WEATHER_PHASE_STRONG;
		else if (v_old == EN_A10_WEATHER_PHASE_STRONG)
			S10_phase = (v_r<0.7f)?EN_A10_WEATHER_PHASE_NORMAL:EN_A10_WEATHER_PHASE_CALM;
		else {
			if (v_r<0.4f) S10_phase=EN_A10_WEATHER_PHASE_CALM;
			else if (v_r<0.8f) S10_phase=EN_A10_WEATHER_PHASE_NORMAL;
			else S10_phase=EN_A10_WEATHER_PHASE_STRONG;
		}

		S10_phaseStartSec = v_now;
		float v_span = S10_baseMaxWind - S10_baseMinWind;

		if (S10_phase==EN_A10_WEATHER_PHASE_CALM) {
			S10_phaseDurationSec = A10_randRange(90.0f,210.0f);
			S10_phaseMinWind = S10_baseMinWind;
			S10_phaseMaxWind = S10_baseMinWind + v_span*0.6f;
		} else if (S10_phase==EN_A10_WEATHER_PHASE_NORMAL) {
			S10_phaseDurationSec = A10_randRange(120.0f,300.0f);
			S10_phaseMinWind = S10_baseMinWind + v_span*0.15f;
			S10_phaseMaxWind = S10_baseMinWind + v_span*0.85f;
		} else {
			S10_phaseDurationSec = A10_randRange(60.0f,150.0f);
			S10_phaseMinWind = S10_baseMinWind + v_span*0.4f;
			S10_phaseMaxWind = S10_baseMaxWind;
		}
		S10_phaseMinWind = max(0.2f,S10_phaseMinWind);
		S10_phaseMaxWind = min(11.0f,S10_phaseMaxWind);

		_generateTarget();
	}

	// --------------------------------------------------
	// 목표 풍속 갱신
	// --------------------------------------------------
	void _generateTarget() {
		float v_range = S10_phaseMaxWind - S10_phaseMinWind;
		float v_w = S10_phaseMinWind + A10_getRandom01()*v_range;
		float v_mid = (S10_phaseMinWind+S10_phaseMaxWind)*0.5f;
		float v_bias = A10_randRange(0.0f,1.0f);
		v_w = (v_w + v_mid*v_bias)/(1.0f+v_bias);
		S10_targetWindSpeed = v_w;

		float v_var = S10_userVariability/100.0f;
		float v_base;
		if (S10_phase==EN_A10_WEATHER_PHASE_CALM) v_base=0.08f + v_var*0.12f;
		else if (S10_phase==EN_A10_WEATHER_PHASE_STRONG) v_base=0.25f + v_var*0.35f;
		else v_base=0.15f + v_var*0.25f;

		float v_U = max(0.1f, S10_currentWindSpeed);
		float v_tscale = S10_turbLenScale/v_U;
		v_base *= (1.0f + v_tscale*0.1f);
		S10_windChangeRate = constrain(v_base * A10_randRange(0.7f, 1.7f), 0.05f, 0.40f);
	}

	// --------------------------------------------------
	// PWM 반영
	// --------------------------------------------------
	void _applyFan(float p_pct) {
		if (!_p_pwm) return;

		float v_req   = p_pct/100.0f;
		float v_limit = S10_fanLimitPct/100.0f;
		float v_minf  = S10_minFanPct/100.0f;
		float v_inten = S10_userIntensity/100.0f;

		if (!S10_fanPowerEnabled || v_inten <= 0.01f) {
			_p_pwm->P10_setDutyPercent(0.0f);
			return;
		}

		if (S10_active) v_req *= v_inten;
		v_req = fmaxf(v_minf, fminf(v_limit, v_req));

		_p_pwm->P10_setDutyPercent(v_req * 100.0f);
	}
};

// ------------------------------------------------------
// 정적 멤버 정의
// ------------------------------------------------------
std::deque<CL_S10_Simulation::ST_S10_ChartEntry>
	CL_S10_Simulation::s_chartBuffer;

unsigned long
	CL_S10_Simulation::s_lastChartLogMs = 0;
