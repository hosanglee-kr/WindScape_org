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
#include <cmath>
#include <deque>
#include <ArduinoJson.h>

#include "A10_Const_010.h"
#include "D10_Logger_010.h"
#include "P10_PWM_ctrl_010.h"

// ------------------------------------------------------
// 시뮬레이션 관리자
// ------------------------------------------------------
class CL_S10_Simulation {
public:
	// ==============================
	// 외부 상태
	// ==============================
	bool  S10_active               = false;
	bool  S10_fanPowerEnabled      = true;
	float S10_currentWindSpeed     = 3.6f;  // m/s
	float S10_targetWindSpeed      = 3.6f;
	float S10_windChangeRate       = 0.1f;
	float S10_windMomentum         = 0.0f;

	T_A10_WindPhase_t S10_phase    = EN_A10_WEATHER_PHASE_NORMAL;
	float S10_phaseStartSec        = 0.0f;
	float S10_phaseDurationSec     = 120.0f;
	float S10_phaseMinWind         = 2.0f;
	float S10_phaseMaxWind         = 6.0f;

	// 프리셋 관련
	char  S10_presetName[32]       = "OCEAN";

	// 시뮬 내부 요소
	float S10_spectralEnergyBuf    = 0.0f;
	float S10_spectralPhaseAcc     = 0.0f;
	float S10_turbTimeScale        = 5.0f;

	bool  S10_gustActive           = false;
	bool  S10_thermalActive        = false;
	float S10_gustIntensity        = 1.0f;
	float S10_thermalContrib       = 0.0f;

	// 로그 버퍼
	struct ST_S10_ChartEntry {
		unsigned long timestamp;
		float wind_speed;
		float pwm_duty;
		bool gust;
		bool thermal;
	};
	static std::deque<ST_S10_ChartEntry> s_chart;
	static unsigned long s_lastChartLogMs;

private:
	CL_P10_PWM* _p_pwmCtrl = nullptr;

public:
	// ==================================================
	// 초기화
	// ==================================================
	void S10_begin(CL_P10_PWM& p_pwm) {
		_p_pwmCtrl = &p_pwm;
		CL_D10_Logger::log(EN_L10_LOG_INFO, "S10 Simulation Init OK");
		_applyPreset(g_A10_config_root.sim ? g_A10_config_root.sim->preset : "OCEAN");
	}

	// ==================================================
	// 프리셋 적용
	// ==================================================
	void _applyPreset(const char* p_name) {
		strlcpy(S10_presetName, p_name, sizeof(S10_presetName));

		// 문자열 → enum 매핑
		int8_t v_idx = A10_getPresetIndex(p_name);
		if (v_idx < 0) v_idx = EN_A10_PRESET_OCEAN;

		switch (v_idx) {
			case EN_A10_PRESET_COUNTRY:
				S10_phaseMinWind = 0.8f; S10_phaseMaxWind = 3.5f; break;
			case EN_A10_PRESET_MEDITERRANEAN:
				S10_phaseMinWind = 1.5f; S10_phaseMaxWind = 3.8f; break;
			case EN_A10_PRESET_OCEAN:
			default:
				S10_phaseMinWind = 1.8f; S10_phaseMaxWind = 5.5f; break;
			case EN_A10_PRESET_MOUNTAIN:
				S10_phaseMinWind = 2.5f; S10_phaseMaxWind = 7.2f; break;
		}

		S10_phase = EN_A10_WEATHER_PHASE_NORMAL;
		S10_active = true;
		S10_phaseStartSec = millis() / 1000.0f;
		CL_D10_Logger::log(EN_L10_LOG_INFO, "Preset applied: %s", S10_presetName);
	}

	// ==================================================
	// 한 틱 업데이트
	// ==================================================
	void S10_tick() {
		if (!S10_active) return;
		if (!g_A10_config_root.sim) return;

		ST_A10_SimConfig& v_sim = *g_A10_config_root.sim;

		unsigned long v_now = millis();
		float v_dt = (v_now - s_lastChartLogMs) / 1000.0f;
		s_lastChartLogMs = v_now;

		// 간단한 난류 합성
		float v_rand = A10_randRange(-0.5f, 0.5f);
		S10_spectralEnergyBuf = 0.9f * S10_spectralEnergyBuf + 0.1f * v_rand;

		// 풍속 수렴
		float v_diff = S10_targetWindSpeed - S10_currentWindSpeed;
		S10_currentWindSpeed += v_diff * v_sim.wind_variability * 0.005f + S10_spectralEnergyBuf * 0.1f;
		S10_currentWindSpeed = constrain(S10_currentWindSpeed, S10_phaseMinWind, S10_phaseMaxWind);

		// PWM 반영
		float v_pwmDuty = (S10_currentWindSpeed / S10_phaseMaxWind) * v_sim.wind_intensity;
		if (_p_pwmCtrl)
			_p_pwmCtrl->P10_setDutyPercent(v_pwmDuty);

		// Chart 기록
		if (millis() - s_lastChartLogMs > 1000) {
			ST_S10_ChartEntry e;
			e.timestamp = millis();
			e.wind_speed = S10_currentWindSpeed;
			e.pwm_duty = v_pwmDuty;
			e.gust = S10_gustActive;
			e.thermal = S10_thermalActive;
			s_chart.push_back(e);
			if (s_chart.size() > 120) s_chart.pop_front();
			s_lastChartLogMs = millis();
		}
		yield();
	}

	// ==================================================
	// 상태 JSON
	// ==================================================
	void S10_toJson(JsonDocument& p_doc) {
		JsonObject o = p_doc["sim"].to<JsonObject>();
		o["preset"]     = S10_presetName;
		o["wind_speed"] = S10_currentWindSpeed;
		o["phase"]      = A10_getPresetName(S10_phase);
		o["active"]     = S10_active;
		o["gust"]       = S10_gustActive;
		o["thermal"]    = S10_thermalActive;
		o["pwm"]        = _p_pwmCtrl ? _p_pwmCtrl->P10_getDutyPercent() : 0.0f;
	}

	// ==================================================
	// Chart JSON
	// ==================================================
	void S10_toChartJson(JsonDocument& p_doc) {
		JsonArray arr = p_doc["chart"].to<JsonArray>();
		for (auto& e : s_chart) {
			JsonObject o = arr.add<JsonObject>();
			o["t"] = e.timestamp;
			o["w"] = e.wind_speed;
			o["p"] = e.pwm_duty;
			o["g"] = e.gust;
			o["h"] = e.thermal;
		}
	}
};

// ----------------------
// 정적 멤버 정의
// ----------------------
std::deque<CL_S10_Simulation::ST_S10_ChartEntry> CL_S10_Simulation::s_chart;
unsigned long CL_S10_Simulation::s_lastChartLogMs = 0;
