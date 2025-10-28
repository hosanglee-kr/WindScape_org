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
 * 		- enum 상수             : EN_모듈약어_ 접두사
 * 		- 구조체                : ST_모듈약어_ 접두사
 * 		- 클래스명              : CL_모듈약어_ 접두사
 * 		- 클래스 private 멤버   : _ 접두사,
 * 		- 클래스 정적 멤버      : s_ 접두사
 * 		- 로컬 변수             : v_ 접두사
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

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ctime>

#include "A10_Const_011.h"
#include "C10_ConfigManager_011.h"
#include "D10_Logger_010.h"
#include "S10_Simulation_010.h"
#include "P10_PWM_ctrl_010.h"

// 전방 선언
class CL_M10_MotionLogic;

class CL_CT10_ControlManager {
public:
	bool active = false;
	uint8_t runMode = 0; // 0:Continuous, 1:Schedule

	// 현재 스케줄 상태
	int curSchedule = -1;
	int curSegment = -1;
	bool segOnPhase = false;
	unsigned long segPhaseStartMs = 0;

	// Override 상태
	struct ST_Override {
		bool active = false;
		unsigned long untilMs = 0;
		enum EN_Mode { NONE=0, FIXED=1, PRESET=2 } mode = NONE;
		float fixedPercent = 0.0f;
		char preset[24] = {0};
		int adjIntensity = 0;
		int adjVariability = 0;
	} overrideState;

public:
	// ==================================================
	// 초기화
	// ==================================================
	void begin(CL_S10_Simulation& sim, CL_P10_PWM& pwm) {
		_sim = &sim;
		_pwm = &pwm;
		active = true;

		runMode = _getRunMode();
		CL_D10_Logger::log(EN_L10_LOG_INFO, "CT10 ControlManager started (mode=%u)", runMode);

		if (runMode == 0) {
			_applyContinuousPreset();
		}
	}

	void setMotion(CL_M10_MotionLogic* motion) { 
		_motion = motion;
	}

	// ==================================================
	// 주기 동작
	// ==================================================
	void tick() {
		if (!active || !_pwm) return;

		// 1. 오버라이드 우선
		if (overrideState.active) {
			if (millis() >= overrideState.untilMs) {
				overrideState.active = false;
				overrideState.mode = ST_Override::NONE;
				CL_D10_Logger::log(EN_L10_LOG_INFO, "CT10 Override expired");
				_reapplyMode();
			} else {
				_applyOverride();
				return;
			}
		}

		// 2. 런모드 분기
		runMode = _getRunMode();
		if (runMode == 0) _tickContinuous();
		else               _tickSchedule();
	}

	// ==================================================
	// 오버라이드
	// ==================================================
	void overrideFixed(float percent, uint16_t minutes) {
		overrideState.active = true;
		overrideState.mode = ST_Override::FIXED;
		overrideState.fixedPercent = constrain(percent, 0.0f, 100.0f);
		overrideState.untilMs = millis() + (unsigned long)minutes * 60UL * 1000UL;

		CL_D10_Logger::log(EN_L10_LOG_INFO, "CT10 Override fixed %.1f%% for %u min", percent, minutes);
		_applyOverride();
	}

	void overridePreset(const char* preset, int adjInt, int adjVar, uint16_t minutes) {
		overrideState.active = true;
		overrideState.mode = ST_Override::PRESET;
		strlcpy(overrideState.preset, preset ? preset : "OCEAN", sizeof(overrideState.preset));
		overrideState.adjIntensity = adjInt;
		overrideState.adjVariability = adjVar;
		overrideState.untilMs = millis() + (unsigned long)minutes * 60UL * 1000UL;

		CL_D10_Logger::log(EN_L10_LOG_INFO, "CT10 Override preset %s (+%d,+%d) %u min",
			preset, adjInt, adjVar, minutes);
		_applyOverride();
	}

	void releaseOverride() {
		if (!overrideState.active) return;
		overrideState = ST_Override{};
		CL_D10_Logger::log(EN_L10_LOG_INFO, "CT10 Override released");
		_reapplyMode();
	}

	// ==================================================
	// 상태 JSON
	// ==================================================
	void toJson(JsonDocument& doc) {
		JsonObject o = doc["control"].to<JsonObject>();
		o["active"] = active;
		o["runMode"] = runMode;

		JsonObject ov = o["override"].to<JsonObject>();
		ov["active"] = overrideState.active;
		ov["mode"] = (int)overrideState.mode;
		ov["remainMs"] = overrideState.active ? (int32_t)(overrideState.untilMs - millis()) : 0;
		if (overrideState.mode == ST_Override::FIXED)
			ov["fixedPercent"] = overrideState.fixedPercent;
		else if (overrideState.mode == ST_Override::PRESET) {
			ov["preset"] = overrideState.preset;
			ov["adjIntensity"] = overrideState.adjIntensity;
			ov["adjVariability"] = overrideState.adjVariability;
		}

		JsonObject sch = o["schedule"].to<JsonObject>();
		sch["currentIndex"] = curSchedule;
		sch["segmentIndex"] = curSegment;
		sch["onPhase"] = segOnPhase;
	}

private:
	CL_S10_Simulation*  _sim = nullptr;
	CL_P10_PWM*         _pwm = nullptr;
	CL_M10_MotionLogic* _motion = nullptr;

	// ==================================================
	// Continuous
	// ==================================================
	void _tickContinuous() {
		if (!_sim) return;
		if (_isMotionGateActive()) {
			_pwm->P10_setDutyPercent(0.0f);
			return;
		}
		if (!_sim->S10_active) _applyContinuousPreset();
	}

	void _applyContinuousPreset() {
		if (!_sim || !g_A10_config_root.control) return;

		auto& c = *g_A10_config_root.control;
		if (c.Continuous.wind.enabled) {
			_setSimFromContinuous();
			_sim->S10_applyPreset(c.Continuous.wind.preset);
			if (!_sim->S10_active) _sim->S10_begin(*_pwm);
		} else {
			_sim->S10_stop();
			_pwm->P10_setDutyPercent(0.0f);
		}
	}

	// ==================================================
	// Schedule
	// ==================================================
	void _tickSchedule() {
		if (!g_A10_config_root.control) return;
		const auto& ctl = *g_A10_config_root.control;
		int idx = _findActiveSchedule(ctl);
		if (idx < 0) {
			_sim->S10_stop(); _pwm->P10_setDutyPercent(0.0f);
			curSchedule = -1; curSegment = -1;
			return;
		}

		if (curSchedule != idx) {
			curSchedule = idx; curSegment = -1;
			segOnPhase = false; segPhaseStartMs = 0;
			CL_D10_Logger::log(EN_L10_LOG_INFO, "CT10 Schedule #%d active", idx);
		}

		if (_isMotionGateActiveForSchedule(ctl.schedules[idx])) {
			_pwm->P10_setDutyPercent(0.0f);
			return;
		}

		_applyScheduleSegment(ctl.schedules[idx]);
	}

	void _applyScheduleSegment(const decltype((*g_A10_config_root.control).schedules[0])& sch) {
		if (sch.segments.size() == 0) {
			_sim->S10_stop(); _pwm->P10_setDutyPercent(0.0f); return;
		}

		if (curSegment < 0) {
			curSegment = 0; segOnPhase = true;
			segPhaseStartMs = millis();
			_applySegmentOn(sch.segments[curSegment], sch);
			return;
		}

		const auto& seg = sch.segments[curSegment];
		uint32_t onMs  = (uint32_t)max(1, seg.on_minutes) * 60000UL;
		uint32_t offMs = (uint32_t)max(0, seg.off_minutes) * 60000UL;

		if (segOnPhase) {
			if (millis() - segPhaseStartMs >= onMs) {
				segOnPhase = false; segPhaseStartMs = millis();
				_applySegmentOff(seg);
			}
		} else {
			if (millis() - segPhaseStartMs >= offMs) {
				curSegment = (curSegment + 1) % (int)sch.segments.size();
				segOnPhase = true; segPhaseStartMs = millis();
				_applySegmentOn(sch.segments[curSegment], sch);
			}
		}
	}

	void _applySegmentOn(const decltype((*g_A10_config_root.control).schedules[0].segments[0])& seg,
	                     const decltype((*g_A10_config_root.control).schedules[0])& sch) {
		if (seg.mode == String("fixed")) {
			_sim->S10_stop();
			_pwm->P10_setDutyPercent(constrain(seg.fixed_speed, 0.0f, 100.0f));
		} else {
			_setSimFromSchedule(seg, sch);
			_sim->S10_applyPreset(seg.preset_name);
			if (!_sim->S10_active) _sim->S10_begin(*_pwm);
		}
	}

	void _applySegmentOff(const decltype((*g_A10_config_root.control).schedules[0].segments[0])& /*seg*/) {
		_sim->S10_stop();
		_pwm->P10_setDutyPercent(0.0f);
	}

	// ==================================================
	// Override
	// ==================================================
	void _applyOverride() {
		if (!_pwm || !_sim || !overrideState.active) return;
		if (overrideState.mode == ST_Override::FIXED) {
			_sim->S10_stop();
			_pwm->P10_setDutyPercent(constrain(overrideState.fixedPercent, 0.0f, 100.0f));
		} else if (overrideState.mode == ST_Override::PRESET) {
			_applyPresetWithAdjust(overrideState.preset,
				overrideState.adjIntensity, overrideState.adjVariability);
		}
	}

	// ==================================================
	// 내부 설정 반영
	// ==================================================
	void _setSimFromContinuous() {
		auto& w = g_A10_config_root.control->Continuous.wind;
		auto* s = g_A10_config_root.sim;
		s->wind_intensity = w.wind_intensity;
		s->gust_frequency = w.gust_frequency;
		s->wind_variability = w.wind_variability;
		s->fan_limit = w.fan_limit;
		s->min_fan = w.min_fan;
		strlcpy(s->preset, w.preset, sizeof(s->preset));
	}

	void _setSimFromSchedule(const decltype((*g_A10_config_root.control).schedules[0].segments[0])& seg,
	                         const decltype((*g_A10_config_root.control).schedules[0])& /*sch*/) {
		int adjInt = 0, adjVar = 0;
		if (seg.preset_adjust.valid) {
			adjInt = seg.preset_adjust.intensity;
			adjVar = seg.preset_adjust.variability;
		}
		_applyPresetWithAdjust(seg.preset_name, adjInt, adjVar);
	}

	void _applyPresetWithAdjust(const char* preset, int adjInt, int adjVar) {
		if (!g_A10_config_root.sim) return;
		auto* s = g_A10_config_root.sim;
		strlcpy(s->preset, preset ? preset : "OCEAN", sizeof(s->preset));
		s->wind_intensity = constrain(s->wind_intensity + adjInt, 0.0f, 100.0f);
		s->wind_variability = constrain(s->wind_variability + adjVar, 0.0f, 100.0f);
		_sim->S10_applyPreset(s->preset);
		if (!_sim->S10_active) _sim->S10_begin(*_pwm);
	}

	// ==================================================
	// 모션 게이팅
	// ==================================================
	bool _isMotionGateActive() {
		if (!_motion || !g_A10_config_root.control) return false;
		const auto& m = g_A10_config_root.control->Continuous.motion;
		if (!m.pir.enabled && !m.ble.enabled) return false;
		extern bool M10_motionDetected();
		return !M10_motionDetected();
	}

	bool _isMotionGateActiveForSchedule(const decltype((*g_A10_config_root.control).schedules[0])& sch) {
		if (!_motion) return false;
		if (!sch.motion.pir.enabled && !sch.motion.ble.enabled) return false;
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

	int _findActiveSchedule(const decltype((*g_A10_config_root.control).schedules)& arr) {
		if (arr.size() == 0) return -1;
		time_t t = time(nullptr);
		struct tm* tmv = localtime(&t);
		if (!tmv) return -1;
		int wday = tmv->tm_wday;
		int nowMin = tmv->tm_hour * 60 + tmv->tm_min;

		for (size_t i=0;i<arr.size();++i) {
			const auto& s = arr[i];
			if (!s.enabled) continue;
			if (s.days.size()==7 && s.days[wday]==0) continue;
			int st=_parseHHMM(s.start_time); int ed=_parseHHMM(s.end_time);
			if (st<0||ed<0) continue;
			if (st<=nowMin && nowMin<ed) return (int)i;
			if (ed<st && (nowMin>=st || nowMin<ed)) return (int)i;
		}
		return -1;
	}

	static int _parseHHMM(const String& hhmm) {
		int c = hhmm.indexOf(':');
		if (c<0) return -1;
		int h=hhmm.substring(0,c).toInt();
		int m=hhmm.substring(c+1).toInt();
		if (h<0||h>23||m<0||m>59) return -1;
		return h*60+m;
	}

	uint8_t _getRunMode() const {
		if (!g_A10_config_root.control) return 0;
		return (uint8_t)g_A10_config_root.control->runMode;
	}
};
