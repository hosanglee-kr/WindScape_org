#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : S10_Simulation_010.h
 * 모듈명 : Smart Nature Wind 풍속 시뮬레이션 Manager
 * ------------------------------------------------------
 * 기능 요약:
 *  - 프리셋 기반 자연풍 물리 시뮬레이션
 *  - Von Kármán 난류 / 돌풍 / 열기포 / Phase 전환
 *  - PWM 제어 및 실시간 차트 기록
 *  - cfg_sim_021.json 기반 사용자 설정 반영
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
 * 		- 클래스 정적 멤버      : s_접두사
 * 		- 로컬 변수             : v_접두사
 * 		- 함수 인자             : p_접두사
 */

#include <Arduino.h>
#include <cmath>
#include <deque>
#include <ArduinoJson.h>

#include "A10_Const_010.h"
#include "D10_Logger_010.h"
#include "P10_PWM_ctrl_010.h"

class CL_S10_Simulation {
public:
	// =====================================================
	// 외부 읽기 상태
	// =====================================================
	bool  S10_active            = false;
	bool  S10_fanPowerEnabled   = true;
	float S10_currentWindSpeed  = 3.6f;
	float S10_targetWindSpeed   = 3.6f;
	float S10_windChangeRate    = 0.1f;
	float S10_windMomentum      = 0.0f;

	T_A10_WindPhase_t S10_phase = EN_A10_WEATHER_PHASE_NORMAL;
	float S10_phaseStartSec     = 0.0f;
	float S10_phaseDurationSec  = 120.0f;
	float S10_phaseMinWind      = 2.0f;
	float S10_phaseMaxWind      = 6.0f;

	// 프리셋 파라미터
	float S10_baseMinWind       = 1.8f;
	float S10_baseMaxWind       = 5.5f;
	float S10_gustProbBase      = 0.04f;
	float S10_gustStrengthMax   = 2.1f;
	float S10_thermalFreqBase   = 0.022f;
	char  S10_presetName[32]    = "OCEAN";

	// 내부 난류 / 돌풍 / 열기포
	float S10_spectralBuf       = 0.0f;
	float S10_spectralPhaseAcc  = 0.0f;
	float S10_turbTimeScale     = 5.0f;

	bool  S10_gustActive        = false;
	float S10_gustStartSec      = 0.0f;
	float S10_gustDurSec        = 3.0f;
	float S10_gustIntensity     = 1.0f;
	unsigned long S10_lastGustCheck = 0;

	bool  S10_thermalActive     = false;
	float S10_thermalStartSec   = 0.0f;
	float S10_thermalDurSec     = 8.0f;
	float S10_thermalContrib    = 0.0f;
	unsigned long S10_lastThermalCheck = 0;

	unsigned long S10_lastTickMs = 0;

	// =====================================================
	// Chart 버퍼
	// =====================================================
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
	static std::deque<ST_S10_ChartEntry> s_chart;
	static unsigned long s_lastChartLogMs;

private:
	CL_P10_PWM* _p_pwm = nullptr;

public:
	// =====================================================
	// 초기화
	// =====================================================
	void S10_begin(CL_P10_PWM& p_pwm) {
		_p_pwm = &p_pwm;
		CL_D10_Logger::log(EN_L10_LOG_INFO, "Simulation Init OK");
		S10_applyPreset(g_A10_config_root.sim ? g_A10_config_root.sim->preset : "OCEAN");
	}

	// =====================================================
	// 프리셋 적용 (세부 테이블 통합)
	// =====================================================
	void S10_applyPreset(const char* p_name) {
		strlcpy(S10_presetName, p_name, sizeof(S10_presetName));
		int v_idx = A10_getPresetIndex(p_name);
		if (v_idx < 0) v_idx = EN_A10_PRESET_OCEAN;

		switch (v_idx) {
			case EN_A10_PRESET_COUNTRY:
				S10_baseMinWind=0.7f;S10_baseMaxWind=3.4f;S10_gustProbBase=0.006f;S10_gustStrengthMax=1.35f;S10_thermalFreqBase=0.015f;break;
			case EN_A10_PRESET_MEDITERRANEAN:
				S10_baseMinWind=1.6f;S10_baseMaxWind=3.8f;S10_gustProbBase=0.012f;S10_gustStrengthMax=1.55f;S10_thermalFreqBase=0.035f;break;
			case EN_A10_PRESET_OCEAN:
				S10_baseMinWind=1.8f;S10_baseMaxWind=5.5f;S10_gustProbBase=0.04f;S10_gustStrengthMax=2.1f;S10_thermalFreqBase=0.022f;break;
			case EN_A10_PRESET_MOUNTAIN:
				S10_baseMinWind=2.2f;S10_baseMaxWind=7.5f;S10_gustProbBase=0.045f;S10_gustStrengthMax=2.2f;S10_thermalFreqBase=0.028f;break;
			default:
				S10_baseMinWind=1.8f;S10_baseMaxWind=5.5f;S10_gustProbBase=0.04f;S10_gustStrengthMax=2.1f;S10_thermalFreqBase=0.022f;break;
		}

		S10_phase = EN_A10_WEATHER_PHASE_NORMAL;
		S10_phaseStartSec = millis() / 1000.0f;
		S10_phaseDurationSec = 120.0f;
		S10_active = true;
		CL_D10_Logger::log(EN_L10_LOG_INFO,"Preset:%s(%.1f~%.1f)",S10_presetName,S10_baseMinWind,S10_baseMaxWind);
	}

	// =====================================================
	// Von Kármán 난류 합성
	// =====================================================
	void _S10_calcTurb(float p_dt) {
		if (!S10_active) return;
		float v_L = g_A10_config_root.sim->turbulence_length_scale;
		float v_sigma = g_A10_config_root.sim->turbulence_intensity_sigma;
		float v_U = max(S10_currentWindSpeed, 0.1f);
		float v_sum = 0.0f;
		for (int i=1;i<=12;i++){
			float v_n=i*0.1f;
			float v_f=v_n*v_U/v_L;
			float v_term=70.8f*v_f*v_f;
			float v_S=4.0f*v_sigma*v_sigma*(v_L/v_U)/(powf(1.0f+v_term,5.0f/6.0f));
			float v_phase_rate=2.0f*M_PI*v_f;
			float v_phase=v_phase_rate*p_dt*i+S10_spectralPhaseAcc;
			v_sum+=sqrtf(2.0f*v_S*0.083f)*sinf(v_phase);
		}
		S10_spectralPhaseAcc+=p_dt*0.5f;
		S10_spectralBuf=0.95f*S10_spectralBuf+0.05f*v_sum;
	}

	// =====================================================
	// 돌풍 업데이트
	// =====================================================
	void _S10_updateGust() {
		float v_now = millis()/1000.0f;
		if (S10_gustActive){
			float v_age=v_now-S10_gustStartSec;
			if(v_age>=S10_gustDurSec){S10_gustActive=false;S10_gustIntensity=1.0f;return;}
			float v_prog=v_age/S10_gustDurSec;
			float v_env=(v_prog<0.25f)?(v_prog/0.25f):(v_prog<0.7f?1.0f:1.0f-powf((v_prog-0.7f)/0.3f,1.3f));
			S10_gustIntensity=1.0f+(S10_gustStrengthMax-1.0f)*v_env;
			return;
		}
		if (millis()-S10_lastGustCheck<1000) return;
		S10_lastGustCheck=millis();
		float v_p=S10_gustProbBase*(g_A10_config_root.sim->gust_frequency/100.0f);
		if (A10_getRandom01()<v_p){
			S10_gustActive=true;
			S10_gustStartSec=v_now;
			S10_gustDurSec=A10_randRange(1.5f,5.0f);
			S10_gustIntensity=A10_randRange(1.1f,S10_gustStrengthMax);
		}
	}

	// =====================================================
	// 열기포 업데이트
	// =====================================================
	void _S10_updateThermal() {
		float v_now=millis()/1000.0f;
		if (S10_thermalActive){
			float v_age=v_now-S10_thermalStartSec;
			if(v_age>=S10_thermalDurSec){S10_thermalActive=false;S10_thermalContrib=0.0f;return;}
			float v_prog=v_age/S10_thermalDurSec;
			float v_env=(v_prog<0.2f)?(v_prog/0.2f):(v_prog<0.8f?1.0f:1.0f-powf((v_prog-0.8f)/0.2f,1.5f));
			S10_thermalContrib=(v_env-0.5f)*2.0f*0.1f;
			return;
		}
		if (millis()-S10_lastThermalCheck<2000)return;
		S10_lastThermalCheck=millis();
		float v_p=S10_thermalFreqBase*(g_A10_config_root.sim->thermal_frequency/100.0f);
		if(A10_getRandom01()<v_p){
			S10_thermalActive=true;
			S10_thermalStartSec=v_now;
			S10_thermalDurSec=A10_randRange(6.0f,12.0f);
		}
	}

	// =====================================================
	// Phase 자동 전환
	// =====================================================
	void _S10_updatePhase() {
		float v_now=millis()/1000.0f;
		if (v_now-S10_phaseStartSec<S10_phaseDurationSec) return;
		float v_r=A10_getRandom01();
		if(S10_phase==EN_A10_WEATHER_PHASE_CALM)
			S10_phase=(v_r<0.7f)?EN_A10_WEATHER_PHASE_NORMAL:EN_A10_WEATHER_PHASE_STRONG;
		else if(S10_phase==EN_A10_WEATHER_PHASE_STRONG)
			S10_phase=(v_r<0.7f)?EN_A10_WEATHER_PHASE_NORMAL:EN_A10_WEATHER_PHASE_CALM;
		else{
			if(v_r<0.4f)S10_phase=EN_A10_WEATHER_PHASE_CALM;
			else if(v_r<0.8f)S10_phase=EN_A10_WEATHER_PHASE_NORMAL;
			else S10_phase=EN_A10_WEATHER_PHASE_STRONG;
		}
		S10_phaseStartSec=v_now;
		float v_span=S10_baseMaxWind-S10_baseMinWind;
		if(S10_phase==EN_A10_WEATHER_PHASE_CALM){
			S10_phaseDurationSec=A10_randRange(90.0f,210.0f);
			S10_phaseMinWind=S10_baseMinWind;
			S10_phaseMaxWind=S10_baseMinWind+v_span*0.6f;
		}else if(S10_phase==EN_A10_WEATHER_PHASE_NORMAL){
			S10_phaseDurationSec=A10_randRange(120.0f,300.0f);
			S10_phaseMinWind=S10_baseMinWind+v_span*0.15f;
			S10_phaseMaxWind=S10_baseMinWind+v_span*0.85f;
		}else{
			S10_phaseDurationSec=A10_randRange(60.0f,150.0f);
			S10_phaseMinWind=S10_baseMinWind+v_span*0.4f;
			S10_phaseMaxWind=S10_baseMaxWind;
		}
		S10_phaseMinWind=max(0.2f,S10_phaseMinWind);
		S10_phaseMaxWind=min(11.0f,S10_phaseMaxWind);
		_S10_newTarget();
	}

	// =====================================================
	// 새 목표 풍속 생성
	// =====================================================
	void _S10_newTarget() {
		float v_rng=S10_phaseMaxWind-S10_phaseMinWind;
		float v_new=S10_phaseMinWind+A10_getRandom01()*v_rng;
		float v_mid=(S10_phaseMinWind+S10_phaseMaxWind)*0.5f;
		v_new=(v_new+v_mid*A10_randRange(0.0f,1.0f))/2.0f;
		S10_targetWindSpeed=v_new;
	}

	// =====================================================
	// 메인 tick
	// =====================================================
	void S10_tick() {
		if (!S10_active || !_p_pwm || !g_A10_config_root.sim) return;
		unsigned long v_nowMs=millis();
		float v_dt=(v_nowMs-S10_lastTickMs)/1000.0f;
		if(v_dt<0.05f) return;
		S10_lastTickMs=v_nowMs;

		_S10_updatePhase();
		_S10_calcTurb(v_dt);
		_S10_updateGust();
		_S10_updateThermal();

		float v_diff=S10_targetWindSpeed-S10_currentWindSpeed;
		S10_windMomentum=S10_windMomentum*0.85f+v_diff*0.15f;
		S10_currentWindSpeed+=S10_windMomentum+S10_spectralBuf;
		S10_currentWindSpeed=constrain(S10_currentWindSpeed,0.2f,11.0f);

		float v_pwm=(S10_currentWindSpeed*9.0f)+10.0f;
		v_pwm*=S10_gustIntensity;
		v_pwm+=S10_thermalContrib*5.0f;
		v_pwm=constrain(v_pwm,0.0f,100.0f);
		_p_pwm->P10_setDutyPercent(v_pwm);

		// Chart log
		if (millis()-s_lastChartLogMs>1000){
			ST_S10_ChartEntry e;
			e.timestamp=millis();
			e.wind_speed=S10_currentWindSpeed;
			e.pwm_duty=v_pwm;
			e.intensity=g_A10_config_root.sim->wind_intensity;
			e.variability=g_A10_config_root.sim->wind_variability;
			e.turbulence=g_A10_config_root.sim->turbulence_intensity_sigma;
			e.preset_id=(uint8_t)A10_getPresetIndex(S10_presetName);
			e.gust_active=S10_gustActive;
			e.thermal_active=S10_thermalActive;
			s_chart.push_back(e);
			if(s_chart.size()>120)s_chart.pop_front();
			s_lastChartLogMs=millis();
		}
		yield();
	}

	// =====================================================
	// 상태 JSON
	// =====================================================
	void S10_toJson(JsonDocument& p_doc) {
		JsonObject o = p_doc["sim"].to<JsonObject>();
		o["active"]     = S10_active;
		o["preset"]     = S10_presetName;
		o["phase"]      = A10_getPhaseName(S10_phase);
		o["wind_speed"] = S10_currentWindSpeed;
		o["target"]     = S10_targetWindSpeed;
		o["gust_active"]= S10_gustActive;
		o["thermal_active"] = S10_thermalActive;
		o["gust_intensity"] = S10_gustIntensity;
		o["thermal_contrib"] = S10_thermalContrib;
		o["pwm_duty"]   = _p_pwm ? _p_pwm->P10_getDutyPercent() : 0.0f;
		o["turbulence"] = g_A10_config_root.sim ? g_A10_config_root.sim->turbulence_intensity_sigma : 0.0f;
		o["variability"]= g_A10_config_root.sim ? g_A10_config_root.sim->wind_variability : 0.0f;
		o["intensity"]  = g_A10_config_root.sim ? g_A10_config_root.sim->wind_intensity : 0.0f;
		o["phase_min"]  = S10_phaseMinWind;
		o["phase_max"]  = S10_phaseMaxWind;
		o["timestamp"]  = millis();
	}

	// =====================================================
	// Chart 로그 JSON 직렬화
	// =====================================================
	void S10_toChartJson(JsonDocument& p_doc) {
		JsonArray arr = p_doc["chart"].to<JsonArray>();
		for (auto& e : s_chart) {
			JsonObject o = arr.add<JsonObject>();
			o["t"] = e.timestamp;
			o["w"] = e.wind_speed;
			o["p"] = e.pwm_duty;
			o["i"] = e.intensity;
			o["v"] = e.variability;
			o["tb"]= e.turbulence;
			o["pr"]= e.preset_id;
			o["g"] = e.gust_active;
			o["h"] = e.thermal_active;
		}
	}

	// =====================================================
	// 시뮬 강제 중지
	// =====================================================
	void S10_stop() {
		S10_active = false;
		if (_p_pwm) _p_pwm->P10_setDutyPercent(0.0f);
		CL_D10_Logger::log(EN_L10_LOG_INFO, "Simulation stopped");
	}

	// =====================================================
	// 상태 요약 출력 (디버그용)
	// =====================================================
	void S10_debugPrint() {
		CL_D10_Logger::log(EN_L10_LOG_DEBUG,
			"Wind=%.2f→%.2f | Phase=%s | Gust=%d Thermal=%d | PWM=%.1f%%",
			S10_currentWindSpeed, S10_targetWindSpeed,
			A10_getPhaseName(S10_phase),
			S10_gustActive, S10_thermalActive,
			_p_pwm ? _p_pwm->P10_getDutyPercent() : 0.0f
		);
	}
};

// ------------------------------------------------------
// 정적 멤버 정의
// ------------------------------------------------------
std::deque<CL_S10_Simulation::ST_S10_ChartEntry> CL_S10_Simulation::s_chart;
unsigned long CL_S10_Simulation::s_lastChartLogMs = 0;
