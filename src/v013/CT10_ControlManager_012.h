#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : CT10_ControlManager_012.h
 * 모듈약어 : CT10
 * 모듈명 : Smart Nature Wind 통합 제어 Manager
 * ------------------------------------------------------
 * 기능 요약:
 *  - cfg_control_022.json 기반 통합 제어 엔진
 *  - Continuous / Schedule 모드 자동 전환
 *  - Segment 단위 on/off 제어
 *  - Motion(모션센서) 게이팅 지원 (PIR/BLE)
 *  - Override(수동강제) 기능 지원
 *  - Simulation / PWM 연동
 * ------------------------------------------------------
 * ------------------------------------------------------
 * 구현 규칙:
 *  - ArduinoJson v7.x.x 사용 (v6 이하 금지), JsonDocument 단일 타입만 사용
 *  - createNestedArray/Object/containsKey 사용 금지
 *  - 암묵적 생성 / 인덱스 기반 대입으로 배열/오브젝트 구성
 *  - memset + strlcpy 기반 안전 초기화
 *  - 주석/필드명은 JSON 구조와 동일하게 유지
 *  - 단일 헤더(h) 파일로 구성 (cpp 없음)
 * ------------------------------------------------------
 * [코드 네이밍 규칙]
 * 		- 전역 상수,매크로      : G_모듈약어_ 접두사
 * 		- 전역 변수             : g_모듈약어_ 접두사
 * 		- 전역 함수             : 모듈약어_ 접두사
 * 		- type                  : T_모듈약어_ 접두사
 *      - typedef               : _t  접미사
 * 		- enum 상수             : EN_모듈약어_ 접두사
 * 		- 구조체                : ST_모듈약어_ 접두사
 * 		- 클래스명              : CL_모듈약어_ 접두사
 * 		- 클래스 private 멤버   : _ 접두사
 *      - 클래스 멤버 함수,변수   : 모듈약어 접두사 미시용
 * 		- 클래스 정적 멤버      : s_ 접두사
 * 		- 함수 로컬 변수             : v_ 접두사
 * 		- 함수 인자             : p_ 접두사
 * ------------------------------------------------------
 Override 기능 (수동 강제 제어)

Override는 사용자나 외부 시스템이 일정 시간 동안 강제로 바람을 제어할 수 있는 기능이에요.
즉, 스케줄이나 모션 상태와 관계없이 우선순위 1위로 작동합니다.
이 기능은 예를 들어 다음 상황에 쓰입니다:
“지금 방이 덥다, 10분간 강풍으로 돌려줘.”
“30분 동안 COUNTRY_BREEZE 프리셋으로 고정해줘.”
“회의 중이라 조용히, 바람 꺼둬.”

Motion 게이팅 기능 (자동 일시 정지/대기)

💡 개념

**모션 게이팅(Motion Gating)**은
PIR 센서나 BLE 근접센서를 이용해 “사람이 근처에 있는지”를 감지하고,
사람이 없을 때 바람을 자동으로 멈추거나 저속으로 유지하는 기능이에요.

즉,

> "사람이 나가면 바람 멈춤"
"다시 들어오면 부드럽게 재개"

 */


#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : CT10_ControlManager_012.h
 * 모듈명 : Smart Nature Wind 통합 제어 Manager
 * ------------------------------------------------------
 * 기능 요약:
 *  - cfg_control_022.json 기반 Continuous / Schedule 모드 제어
 *  - Segment on/off 주기 제어
 *  - Motion 게이팅(PIR/BLE)
 *  - Override 기능 (초 단위)
 *  - Simulation / PWM 연동
 * ------------------------------------------------------
 */

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ctime>

#include "A10_Const_011.h"
#include "C10_ConfigManager_011.h"
#include "D10_Logger_010.h"
#include "S10_Simulation_010.h"
#include "P10_PWM_ctrl_010.h"

// ------------------------------------------------------
// Override 타입 정의
// ------------------------------------------------------
typedef enum : uint8_t  {
    EN_CT10_OVERRIDE_NONE   = 0,
    EN_CT10_OVERRIDE_FIXED  = 1,
    EN_CT10_OVERRIDE_PRESET = 2
} EN_CT10_override_mode_t;

typedef struct {
	bool active = false;
	unsigned long until_sec = 0;     // 만료 시각(초 단위)
	EN_CT10_override_mode_t overrideMode = EN_CT10_OVERRIDE_NONE;
	float fixedPercent = 0.0f;
	char preset[24] = {0};
	int adjIntensity = 0;
	int adjVariability = 0;
} ST_CT10_overrideState_t;

// ------------------------------------------------------
// 모션 로직 전방선언
// ------------------------------------------------------
class CL_M10_MotionLogic;

// ------------------------------------------------------
// CT10 ControlManager 클래스
// ------------------------------------------------------
class CL_CT10_ControlManager {
public:
	bool active = false;
	uint8_t runMode = 0;  // 0: Continuous, 1: Schedule

	int curSchedule = -1;
	int curSegment = -1;
	bool segOnPhase = false;
	unsigned long segPhaseStartMs = 0;

	ST_CT10_overrideState_t overrideState; // 오버라이드 상태

public:
	// ==================================================
	// 초기화
	// ==================================================
	void begin(CL_S10_Simulation& p_sim, CL_P10_PWM& p_pwm) {
		sim = &p_sim;
		pwm = &p_pwm;
		active = true;

		runMode = _getRunMode();
		CL_D10_Logger::log(EN_L10_LOG_INFO, "CT10 ControlManager begin (mode=%u)", runMode);

		if (runMode == 0) _applyContinuousPreset();
	}

	void setMotion(CL_M10_MotionLogic* p_motion) { motion = p_motion; }

	// ==================================================
	// 주기 Tick
	// ==================================================
	void tick() {
		if (!active || !pwm) return;

		// 오버라이드 우선
		if (overrideState.active) {
			unsigned long v_nowSec = millis() / 1000UL;
			if (v_nowSec >= overrideState.until_sec) {
				overrideState.active = false;
				overrideState.overrideMode = EN_CT10_OVERRIDE_NONE;
				CL_D10_Logger::log(EN_L10_LOG_INFO, "CT10 Override expired");
				_reapplyMode();
			} else {
				_applyOverride();
				return;
			}
		}

		runMode = _getRunMode();
		if (runMode == 0) _tickContinuous();
		else               _tickSchedule();
	}

	// ==================================================
	// Override (초 단위)
	// ==================================================
	void overrideFixed(float p_percent, uint32_t p_seconds) {
		overrideState.active = true;
		overrideState.overrideMode = EN_CT10_OVERRIDE_FIXED;
		overrideState.fixedPercent = constrain(p_percent, 0.0f, 100.0f);
		overrideState.until_sec = (millis() / 1000UL) + p_seconds;

		CL_D10_Logger::log(EN_L10_LOG_INFO, "CT10 Override FIXED %.1f%% for %lu sec",
			p_percent, (unsigned long)p_seconds);
		_applyOverride();
	}

	void overridePreset(const char* p_preset, int p_adjInt, int p_adjVar, uint32_t p_seconds) {
		overrideState.active = true;
		overrideState.overrideMode = EN_CT10_OVERRIDE_PRESET;
		strlcpy(overrideState.preset, p_preset ? p_preset : "OCEAN", sizeof(overrideState.preset));
		overrideState.adjIntensity = p_adjInt;
		overrideState.adjVariability = p_adjVar;
		overrideState.until_sec = (millis() / 1000UL) + p_seconds;

		CL_D10_Logger::log(EN_L10_LOG_INFO, "CT10 Override PRESET %s (+%d,+%d) %lu sec",
			p_preset, p_adjInt, p_adjVar, (unsigned long)p_seconds);
		_applyOverride();
	}

	void releaseOverride() {
		if (!overrideState.active) return;
		overrideState = ST_CT10_overrideState_t{};
		CL_D10_Logger::log(EN_L10_LOG_INFO, "CT10 Override released");
		_reapplyMode();
	}

	// ==================================================
	// 상태 JSON
	// ==================================================
	void toJson(JsonDocument& p_doc) {
		JsonObject o = p_doc["control"].to<JsonObject>();
		o["active"] = active;
		o["runMode"] = runMode;

		JsonObject ov = o["override"].to<JsonObject>();
		ov["active"] = overrideState.active;
		ov["mode"] = (int)overrideState.overrideMode;
		ov["remainSec"] = overrideState.active ?
			(int32_t)(overrideState.until_sec - (millis() / 1000UL)) : 0;
		if (overrideState.overrideMode == EN_CT10_OVERRIDE_FIXED)
			ov["fixedPercent"] = overrideState.fixedPercent;
		else if (overrideState.overrideMode == EN_CT10_OVERRIDE_PRESET) {
			ov["preset"] = overrideState.preset;
			ov["adjIntensity"] = overrideState.adjIntensity;
			ov["adjVariability"] = overrideState.adjVariability;
		}

		JsonObject sch = o["schedule"].to<JsonObject>();
		sch["currentSchedule"] = curSchedule;
		sch["currentSegment"] = curSegment;
		sch["onPhase"] = segOnPhase;
	}

private:
	CL_S10_Simulation* sim = nullptr;
	CL_P10_PWM* pwm = nullptr;
	CL_M10_MotionLogic* motion = nullptr;

	// ==================================================
	// Continuous
	// ==================================================
	void _tickContinuous() {
		if (!sim) return;
		if (_isMotionGateActive()) {
			pwm->P10_setDutyPercent(0.0f);
			return;
		}
		if (!sim->S10_active) _applyContinuousPreset();
	}

	void _applyContinuousPreset() {
		if (!sim || !g_A10_config_root.control) return;
		auto& v_ctl = *g_A10_config_root.control;
		if (v_ctl.Continuous.wind.enabled) {
			_setSimFromContinuous();
			sim->S10_applyPreset(v_ctl.Continuous.wind.preset);
			if (!sim->S10_active) sim->S10_begin(*pwm);
		} else {
			sim->S10_stop();
			pwm->P10_setDutyPercent(0.0f);
		}
	}

	// ==================================================
	// Schedule
	// ==================================================
	void _tickSchedule() {
		if (!g_A10_config_root.control) return;
		const auto& v_ctl = *g_A10_config_root.control;
		int v_idx = _findActiveSchedule(v_ctl);
		if (v_idx < 0) {
			sim->S10_stop(); pwm->P10_setDutyPercent(0.0f);
			curSchedule = -1; curSegment = -1;
			return;
		}

		if (curSchedule != v_idx) {
			curSchedule = v_idx; curSegment = -1;
			segOnPhase = false; segPhaseStartMs = 0;
			CL_D10_Logger::log(EN_L10_LOG_INFO, "CT10 Schedule #%d active", v_idx);
		}

		if (_isMotionGateActiveForSchedule(v_ctl.schedules[v_idx])) {
			pwm->P10_setDutyPercent(0.0f);
			return;
		}

		_applyScheduleSegment(v_ctl.schedules[v_idx]);
	}

	void _applyScheduleSegment(const decltype((*g_A10_config_root.control).schedules[0])& p_sch) {
		if (p_sch.segments.size() == 0) {
			sim->S10_stop(); pwm->P10_setDutyPercent(0.0f); return;
		}

		if (curSegment < 0) {
			curSegment = 0; segOnPhase = true;
			segPhaseStartMs = millis();
			_applySegmentOn(p_sch.segments[curSegment], p_sch);
			return;
		}

		const auto& v_seg = p_sch.segments[curSegment];
		uint32_t v_onMs  = (uint32_t)max(1, v_seg.on_minutes) * 60000UL;
		uint32_t v_offMs = (uint32_t)max(0, v_seg.off_minutes) * 60000UL;

		if (segOnPhase) {
			if (millis() - segPhaseStartMs >= v_onMs) {
				segOnPhase = false; segPhaseStartMs = millis();
				_applySegmentOff(v_seg);
			}
		} else {
			if (millis() - segPhaseStartMs >= v_offMs) {
				curSegment = (curSegment + 1) % (int)p_sch.segments.size();
				segOnPhase = true; segPhaseStartMs = millis();
				_applySegmentOn(p_sch.segments[curSegment], p_sch);
			}
		}
	}

	void _applySegmentOn(const decltype((*g_A10_config_root.control).schedules[0].segments[0])& p_seg,
	                     const decltype((*g_A10_config_root.control).schedules[0])& p_sch) {
		if (p_seg.mode == String("fixed")) {
			sim->S10_stop();
			pwm->P10_setDutyPercent(constrain(p_seg.fixed_speed, 0.0f, 100.0f));
		} else {
			_setSimFromSchedule(p_seg, p_sch);
			sim->S10_applyPreset(p_seg.preset_name);
			if (!sim->S10_active) sim->S10_begin(*pwm);
		}
	}

	void _applySegmentOff(const decltype((*g_A10_config_root.control).schedules[0].segments[0])& /*p_seg*/) {
		sim->S10_stop();
		pwm->P10_setDutyPercent(0.0f);
	}

	// ==================================================
	// Override
	// ==================================================
	void _applyOverride() {
		if (!pwm || !sim || !overrideState.active) return;
		if (overrideState.overrideMode == EN_CT10_OVERRIDE_FIXED) {
			sim->S10_stop();
			pwm->P10_setDutyPercent(constrain(overrideState.fixedPercent, 0.0f, 100.0f));
		} else if (overrideState.overrideMode == EN_CT10_OVERRIDE_PRESET) {
			_applyPresetWithAdjust(overrideState.preset,
				overrideState.adjIntensity, overrideState.adjVariability);
		}
	}

	// ==================================================
	// 내부 설정 반영
	// ==================================================
	void _setSimFromContinuous() {
		auto& v_w = g_A10_config_root.control->Continuous.wind;
		auto* v_s = g_A10_config_root.sim;
		v_s->wind_intensity = v_w.wind_intensity;
		v_s->gust_frequency = v_w.gust_frequency;
		v_s->wind_variability = v_w.wind_variability;
		v_s->fan_limit = v_w.fan_limit;
		v_s->min_fan = v_w.min_fan;
		strlcpy(v_s->preset, v_w.preset, sizeof(v_s->preset));
	}

	void _setSimFromSchedule(const decltype((*g_A10_config_root.control).schedules[0].segments[0])& p_seg,
	                         const decltype((*g_A10_config_root.control).schedules[0])& /*p_sch*/) {
		int v_adjInt = 0, v_adjVar = 0;
		if (p_seg.preset_adjust.valid) {
			v_adjInt = p_seg.preset_adjust.intensity;
			v_adjVar = p_seg.preset_adjust.variability;
		}
		_applyPresetWithAdjust(p_seg.preset_name, v_adjInt, v_adjVar);
	}

	void _applyPresetWithAdjust(const char* p_preset, int p_adjInt, int p_adjVar) {
		if (!g_A10_config_root.sim) return;
		auto* v_s = g_A10_config_root.sim;
		strlcpy(v_s->preset, p_preset ? p_preset : "OCEAN", sizeof(v_s->preset));
		v_s->wind_intensity = constrain(v_s->wind_intensity + p_adjInt, 0.0f, 100.0f);
		v_s->wind_variability = constrain(v_s->wind_variability + p_adjVar, 0.0f, 100.0f);
		sim->S10_applyPreset(v_s->preset);
		if (!sim->S10_active) sim->S10_begin(*pwm);
	}

	// ==================================================
	// 모션 게이팅
	// ==================================================
	bool _isMotionGateActive() {
		if (!motion || !g_A10_config_root.control) return false;
		const auto& v_m = g_A10_config_root.control->Continuous.motion;
		if (!v_m.pir.enabled && !v_m.ble.enabled) return false;
		extern bool M10_motionDetected();
		return !M10_motionDetected();
	}

	bool _isMotionGateActiveForSchedule(const decltype((*g_A10_config_root.control).schedules[0])& p_sch) {
		if (!motion) return false;
		if (!p_sch.motion.pir.enabled && !p_sch.motion.ble.enabled) return false;
		extern bool M10_motionDetected();
		return !M10_motionDetected();
	}

	// ==================================================
	// 보조함수
	// ==================================================
	void _reapplyMode() {
		if (runMode == 0) _applyContinuousPreset();
		else { curSchedule = -1; curSegment = -1; _tickSchedule(); }
	}

	int _findActiveSchedule(const decltype((*g_A10_config_root.control).schedules)& p_arr) {
		if (p_arr.size() == 0) return -1;
		time_t v_t = time(nullptr);
		struct tm* v_tm = localtime(&v_t);
		if (!v_tm) return -1;
		int v_wday = v_tm->tm_wday;
		int v_nowMin = v_tm->tm_hour * 60 + v_tm->tm_min;

		for (size_t i=0;i<p_arr.size();++i) {
			const auto& v_s = p_arr[i];
			if (!v_s.enabled) continue;
			if (v_s.days.size()==7 && v_s.days[v_wday]==0) continue;
			int v_st=_parseHHMM(v_s.start_time);
			int v_ed=_parseHHMM(v_s.end_time);
			if (v_st<0||v_ed<0) continue;
			if (v_st<=v_nowMin && v_nowMin<v_ed) return (int)i;
			if (v_ed<v_st && (v_nowMin>=v_st || v_nowMin<v_ed)) return (int)i;
		}
		return -1;
	}

	static int _parseHHMM(const String& p_hhmm) {
		int v_c = p_hhmm.indexOf(':');
		if (v_c<0) return -1;
		int v_h=p_hhmm.substring(0,v_c).toInt();
		int v_m=p_hhmm.substring(v_c+1).toInt();
		if (v_h<0||v_h>23||v_m<0||v_m>59) return -1;
		return v_h*60+v_m;
	}

	uint8_t _getRunMode() const {
		if (!g_A10_config_root.control) return 0;
		return (uint8_t)g_A10_config_root.control->runMode;
	}
};

