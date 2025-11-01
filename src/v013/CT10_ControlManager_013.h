#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : CT10_ControlManager_013.h
 * 모듈약어 : CT10
 * 모듈명 : Smart Nature Wind 통합 제어 Manager
 * ------------------------------------------------------
 * 기능 요약
 *  - cfg_control_022.json 기반 통합 바람 제어 엔진
 *  - Continuous / Schedule 모드 운영
 *  - Segment 단위 ON/OFF 사이클
 *  - Motion 게이팅(PIR/BLE)
 *  - Override (강제 풍속/프리셋 제어)
 *  - Simulation / PWM 연동
 *  - Localtime 기반 스케줄
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
 * ------------------------------------------------------
 * Motion 게이팅 개념
 *  - 사람 감지 시 동작 유지
 *  - 미감지 일정 시간 후 풍속 낮추거나 중지
 * ------------------------------------------------------
 * Override 개념
 *  - 최우선 제어권
 *  - 일정 시간 동안 프리셋/고정 속도 강제
 *  - 종료 시 기존 모드 복귀
 * ------------------------------------------------------
 */

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ctime>

#include "A10_Const_012.h"
#include "C10_ConfigManager_012.h"
#include "D10_Logger_011.h"
#include "P10_PWM_ctrl_012.h"
#include "S10_Simulation_012.h"

// forward
class CL_M10_MotionLogic;

// ------------------------------------------------------
// Override enum & 구조체
// ------------------------------------------------------
typedef enum : uint8_t {
	EN_CT10_OVERRIDE_NONE   = 0,
	EN_CT10_OVERRIDE_FIXED  = 1,
	EN_CT10_OVERRIDE_PRESET = 2
} EN_CT10_override_mode_t;

typedef struct {
	bool					active		   = false;
	unsigned long			until_sec	   = 0;
	EN_CT10_override_mode_t overrideMode   = EN_CT10_OVERRIDE_NONE;
	float					fixedPercent   = 0.0f;
	char					preset[24]	   = {0};
	int						adjIntensity   = 0;
	int						adjVariability = 0;
} ST_CT10_overrideState_t;


// ------------------------------------------------------
// Control Manager
// ------------------------------------------------------
class CL_CT10_ControlManager {
public:
	bool			active			= false;
	uint8_t 		runMode 		= 0;

	int			  	curSchedule	  	= -1;
	int			  	curSegment	  	= -1;
	bool		  	segOnPhase	  	= false;
	unsigned long 	segPhaseStartMs = 0;

	ST_CT10_overrideState_t overrideState;

public:

	// --------------------------------------------------
	// 시작
	// --------------------------------------------------
	void begin(CL_S10_Simulation& p_sim, CL_P10_PWM& p_pwm) {
		sim	 = &p_sim;
		pwm	 = &p_pwm;
		active = true;
		runMode = _getRunMode();

		CL_D10_Logger::log(EN_L10_LOG_INFO,"CT10 begin (mode=%u)", runMode);

		if (!sim || !pwm) {
			CL_D10_Logger::log(EN_L10_LOG_ERROR,"CT10 begin failed: null sim/pwm");
			return;
		}

		if (runMode == 0) _applyContinuousPreset();
	}

	void setMotion(CL_M10_MotionLogic* p_motion) { motion = p_motion; }

	// --------------------------------------------------
	// 주기 호출
	// --------------------------------------------------
	void tick() {
		if (!active || !pwm) return;

		// ------------------
		// Override 최우선
		// ------------------
		if (overrideState.active) {
			unsigned long v_now = millis()/1000UL;
			if (v_now >= overrideState.until_sec) {
				overrideState = ST_CT10_overrideState_t{};
				CL_D10_Logger::log(EN_L10_LOG_INFO,"CT10 Override expired");
				_reapplyMode();
			} else {
				_applyOverride();
				return;
			}
		}

		runMode = _getRunMode();
		if (runMode == 0) _tickContinuous();
		else              _tickSchedule();
	}

	// --------------------------------------------------
	// Override API
	// --------------------------------------------------
	void overrideFixed(float p_percent, uint32_t p_seconds) {
		overrideState.active       = true;
		overrideState.overrideMode = EN_CT10_OVERRIDE_FIXED;
		overrideState.fixedPercent = constrain(p_percent, 0.0f, 100.0f);
		overrideState.until_sec    = (millis()/1000UL) + p_seconds;

		CL_D10_Logger::log(EN_L10_LOG_INFO,
			"CT10 Override FIXED %.1f%% %lu sec", p_percent,(unsigned long)p_seconds);

		_applyOverride();
	}

	void overridePreset(const char* p_preset,int p_adjInt,int p_adjVar,uint32_t p_seconds) {
		overrideState.active       = true;
		overrideState.overrideMode = EN_CT10_OVERRIDE_PRESET;
		strlcpy(overrideState.preset, p_preset?p_preset:"OCEAN", sizeof(overrideState.preset));
		overrideState.adjIntensity   = p_adjInt;
		overrideState.adjVariability = p_adjVar;
		overrideState.until_sec      = (millis()/1000UL) + p_seconds;

		CL_D10_Logger::log(EN_L10_LOG_INFO,
			"CT10 Override PRESET %s (+%d,+%d) %lu sec",
			overrideState.preset,p_adjInt,p_adjVar,(unsigned long)p_seconds);

		_applyOverride();
	}

	void releaseOverride() {
		if (!overrideState.active) return;
		overrideState = ST_CT10_overrideState_t{};
		CL_D10_Logger::log(EN_L10_LOG_INFO,"CT10 Override released");
		_reapplyMode();
	}

	// --------------------------------------------------
	// JSON Export
	// --------------------------------------------------
	void toJson(JsonDocument& p_doc) {
		JsonObject o = p_doc["control"].to<JsonObject>();
		o["active"]  = active;
		o["runMode"] = runMode;

		JsonObject ov = o["override"].to<JsonObject>();
		ov["active"] = overrideState.active;
		ov["mode"]   = (int)overrideState.overrideMode;
		ov["remainSec"] =
			overrideState.active ? (int32_t)(overrideState.until_sec - (millis()/1000UL)) : 0;

		if (overrideState.overrideMode == EN_CT10_OVERRIDE_FIXED)
			ov["fixedPercent"] = overrideState.fixedPercent;
		else if (overrideState.overrideMode == EN_CT10_OVERRIDE_PRESET) {
			ov["preset"]        = overrideState.preset;
			ov["adjIntensity"]  = overrideState.adjIntensity;
			ov["adjVariability"]= overrideState.adjVariability;
		}

		JsonObject sch = o["schedule"].to<JsonObject>();
		sch["currentSchedule"] = curSchedule;
		sch["currentSegment"]  = curSegment;
		sch["onPhase"]		  = segOnPhase;
	}

private:
	CL_S10_Simulation*	sim	   = nullptr;
	CL_P10_PWM*			pwm	   = nullptr;
	CL_M10_MotionLogic* motion = nullptr;

	// --------------------------------------------------
	// Continuous Mode
	// --------------------------------------------------
	void _tickContinuous() {
		if (!sim) return;

		if (_motionGateActive()) {
			pwm->P10_setDutyPercent(0.0f);
			sim->stop();
			return;
		}

		if (!sim->S10_active) _applyContinuousPreset();
	}

	void _applyContinuousPreset() {
		if (!sim || !g_A10_config_root.control) return;

		auto& v_w = g_A10_config_root.control->Continuous.wind;
		if (!v_w.enabled) {
			sim->stop();
			pwm->P10_setDutyPercent(0.0f);
			return;
		}

		sim->S10_setParams(v_w.wind_intensity, v_w.wind_variability, v_w.gust_frequency,
						   v_w.min_fan, v_w.fan_limit, v_w.preset);

		sim->S10_applyPreset(v_w.preset);
		if (!sim->S10_active) sim->begin(*pwm);
	}

	// --------------------------------------------------
	// Schedule Mode
	// --------------------------------------------------
	void _tickSchedule() {
		if (!g_A10_config_root.control) return;

		int v_idx = _findActiveSchedule(g_A10_config_root.control->schedules);
		if (v_idx < 0) {
			sim->stop();
			pwm->P10_setDutyPercent(0.0f);
			curSchedule = -1;
			curSegment  = -1;
			return;
		}

		if (curSchedule != v_idx) {
			curSchedule = v_idx;
			curSegment  = -1;
			segOnPhase = false;
			segPhaseStartMs = 0;
			CL_D10_Logger::log(EN_L10_LOG_INFO,"CT10 Schedule #%d",v_idx);
		}

		if (_motionGateActiveForSchedule(g_A10_config_root.control->schedules[v_idx])) {
			sim->stop();
			pwm->P10_setDutyPercent(0.0f);
			return;
		}

		_applyScheduleSegment(g_A10_config_root.control->schedules[v_idx]);
	}

	void _applyScheduleSegment(const ST_A10_ScheduleItem& p_sch) {		
		if (p_sch.seg_count==0) {
		//if (p_sch.segments.size()==0) {
			sim->stop();
			pwm->P10_setDutyPercent(0.0f);
			return;
		}

		if (curSegment < 0) {
			curSegment = 0;
			segOnPhase = true;
			segPhaseStartMs = millis();
			_applySegmentOn(p_sch.segments[curSegment],p_sch);
			return;
		}

		const auto& v_seg = p_sch.segments[curSegment];
		uint32_t v_onMs  = max(1, v_seg.on_minutes)*60000UL;
		uint32_t v_offMs = max(0, v_seg.off_minutes)*60000UL;

		if (segOnPhase) {
			if (millis()-segPhaseStartMs >= v_onMs) {
				segOnPhase=false;
				segPhaseStartMs=millis();
				_applySegmentOff();
			}
		} else {
			if (millis()-segPhaseStartMs >= v_offMs) {
				
				curSegment=(curSegment+1) % (int)p_sch.seg_count;
				//curSegment=(curSegment+1) % (int)p_sch.segments.size();
				segOnPhase=true;
				segPhaseStartMs=millis();
				_applySegmentOn(p_sch.segments[curSegment],p_sch);
			}
		}
	}

	void _applySegmentOn(const auto& p_seg, const auto&) {
		if (p_seg.mode == "fixed") {
			sim->stop();
			pwm->P10_setDutyPercent(constrain(p_seg.fixed_speed,0.0f,100.0f));
		} else {
			int v_adjInt = p_seg.preset_adjust.valid ? p_seg.preset_adjust.intensity   : 0;
			int v_adjVar = p_seg.preset_adjust.valid ? p_seg.preset_adjust.variability: 0;
			_applyPresetWithAdjust(p_seg.preset_name,v_adjInt,v_adjVar);
		}
	}

	void _applySegmentOff() {
		sim->stop();
		pwm->P10_setDutyPercent(0.0f);
	}

	// --------------------------------------------------
	// Override
	// --------------------------------------------------
	void _applyOverride() {
		if (!pwm||!sim||!overrideState.active) return;

		if (overrideState.overrideMode==EN_CT10_OVERRIDE_FIXED) {
			sim->stop();
			pwm->P10_setDutyPercent(constrain(overrideState.fixedPercent,0.0f,100.0f));
			return;
		}

		if (overrideState.overrideMode==EN_CT10_OVERRIDE_PRESET) {
			_applyPresetWithAdjust(
				overrideState.preset,
				overrideState.adjIntensity,
				overrideState.adjVariability
			);
		}
	}

	void _applyPresetWithAdjust(const char* p_preset,int p_adjInt,int p_adjVar) {
		auto* v_s = g_A10_config_root.sim;
		if (!v_s || !sim) return;

		strlcpy(v_s->preset, p_preset?p_preset:"OCEAN", sizeof(v_s->preset));
		float v_int = constrain(v_s->wind_intensity + p_adjInt,0.0f,100.0f);
		float v_var = constrain(v_s->wind_variability+p_adjVar,0.0f,100.0f);

		sim->S10_setParams(v_int, v_var, v_s->gust_frequency, v_s->min_fan, v_s->fan_limit, v_s->preset);
		sim->S10_applyPreset(v_s->preset);
		if (!sim->S10_active) sim->begin(*pwm);
	}

	// --------------------------------------------------
	// Motion
	// --------------------------------------------------
	bool _motionGateActive() {
		if (!motion||!g_A10_config_root.control) return false;
		const auto& v_m = g_A10_config_root.control->Continuous.motion;
		if (!v_m.pir.enabled && !v_m.ble.enabled) return false;
		return !motion->M10_isMotionPresent();
	}

	bool _motionGateActiveForSchedule(const auto& p_sch) {
		if (!motion) return false;
		if (!p_sch.motion.pir.enabled && !p_sch.motion.ble.enabled) return false;
		return !motion->M10_isMotionPresent();
	}

	// --------------------------------------------------
	// Helper
	// --------------------------------------------------
	void _reapplyMode() {
		if (runMode==0) {
			_applyContinuousPreset();
		} else {
			curSchedule=-1;
			curSegment=-1;
			_tickSchedule();
		}
	}

	int _findActiveSchedule(const ST_A10_ScheduleItem& p_arr) {
		if (p_arr.size()==0) return -1;

		time_t v_t = time(nullptr);
		struct tm* v_tm = localtime(&v_t);
		if (!v_tm) return -1;

		int v_wday   = v_tm->tm_wday;
		int v_nowMin = v_tm->tm_hour*60 + v_tm->tm_min;

		for (size_t i=0;i<p_arr.size();++i) {
			const auto& s = p_arr[i];
			if (!s.enabled) continue;
			if (s.days[v_wday]==0) continue;

			int st=_parseHHMM(s.start_time);
			int ed=_parseHHMM(s.end_time);
			if (st<0||ed<0) continue;

			if (st<=v_nowMin && v_nowMin<ed) return (int)i;
			if (ed<st && (v_nowMin>=st || v_nowMin<ed)) return (int)i;
		}
		return -1;
	}

	static int _parseHHMM(const String& p) {
		int c=p.indexOf(':'); if (c<0) return -1;
		int h=p.substring(0,c).toInt();
		int m=p.substring(c+1).toInt();
		if (h<0||h>23||m<0||m>59) return -1;
		return h*60+m;
	}

	uint8_t _getRunMode() const {
		if (!g_A10_config_root.control) return 0;
		return (uint8_t)g_A10_config_root.control->runMode;
	}
};
