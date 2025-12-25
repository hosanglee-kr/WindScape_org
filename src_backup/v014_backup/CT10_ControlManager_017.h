#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : CT10_ControlManager_017.h
 * 모듈약어 : CT10
 * 모듈명 : Smart Nature Wind Control Core (v017)
 * ------------------------------------------------------
 * 기능 요약
 *  - WindProfile (Preset+Style) × Schedules × UserProfiles 통합 제어
 *  - 구동 모드
 *    - Schedule 기반 자동 운전
 *    - User Profile 기반 운전
 *    - Override (Fixed / Preset+Style 일시 강제)
 *  - Segment 단위 ON/OFF 사이클 및 반복 실행
 *  - Motion (PIR / BLE) 기반 게이팅
 *  - AutoOff (Timer / OffTime / OffTemp) 조건 처리
 *  - S10 Simulation / P10 PWM / N10 NVS / C10 Config 연동
 *  - Web UI 조회용 JSON 상태 Export
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
#include <string.h>
#include <time.h>

#include "A10_Const_014.h"
#include "C10_ConfigManager_020.h"
#include "D10_Logger_011.h"
#include "P10_PWM_ctrl_016.h"
#include "S10_Simulation_016.h"
#include "N10_NvsManager_016.h"

// forward
class CL_M10_MotionLogic;

// ------------------------------------------------------
// 런모드 / 오버라이드 정의
// ------------------------------------------------------
typedef enum : uint8_t {
	EN_CT10_RUNMODE_OFF       = 0,
	EN_CT10_RUNMODE_SCHEDULE  = 1,
	EN_CT10_RUNMODE_PROFILE   = 2
} EN_CT10_runMode_t;

typedef enum : uint8_t {
	EN_CT10_OVERRIDE_NONE   = 0,
	EN_CT10_OVERRIDE_FIXED  = 1,
	EN_CT10_OVERRIDE_PRESET = 2
} EN_CT10_overrideMode_t;

// ------------------------------------------------------
// AutoOff 상태
// ------------------------------------------------------
typedef struct {
	bool			timerEnabled	   = false;
	uint32_t		timerMinutes	   = 0;
	unsigned long	timerStartMs	   = 0;

	bool			offTimeEnabled	   = false;
	char			offTime[6]		   = {0}; // "HH:MM"

	bool			offTempEnabled	   = false;
	float			offTempC		   = 0.0f;
} ST_CT10_autoOffState_t;

// ------------------------------------------------------
// Override 상태
// ------------------------------------------------------
typedef struct {
	bool					active			= false;
	EN_CT10_overrideMode_t	mode			= EN_CT10_OVERRIDE_NONE;
	unsigned long			untilMs			= 0;

	// FIXED
	float					fixedPercent	= 0.0f;

	// PRESET
	char					presetCode[24]	= {0};
	char					styleCode[24]	= {0};
	ST_A10_AdjustDelta_t	adjust;
} ST_CT10_overrideState_t;

// ------------------------------------------------------
// 현재 실행 컨텍스트 (Schedule / Profile 공용)
// ------------------------------------------------------
typedef struct {
	EN_CT10_runMode_t	runMode			= EN_CT10_RUNMODE_OFF;

	// Schedule 모드
	int16_t				activeScheduleIndex = -1;

	// UserProfile 모드
	int16_t				activeProfileIndex  = -1;

	// Segment 실행 상태
	int16_t				curSegmentIndex	= -1;
	bool				segOnPhase		= false;
	unsigned long		segPhaseStartMs	= 0;

	// 반복 여부 (Profile 에서 사용)
	bool				repeatSegments	= true;

	// AutoOff
	ST_CT10_autoOffState_t autoOff;

	// Motion Gate
	bool				motionBlock		= false;

	// 시작 시각 (상태 참고용)
	unsigned long		startMs			= 0;
} ST_CT10_runState_t;


// ======================================================
// CL_CT10_ControlManager
// ======================================================
class CL_CT10_ControlManager {
public:
	bool					active		= false;

public:
	// --------------------------------------------------
	// 초기화
	// --------------------------------------------------
	bool begin(CL_S10_Simulation& p_sim,
			   CL_P10_PWM&		  p_pwm,
			   CL_N10_NvsManager& p_nvs,
			   CL_M10_MotionLogic* p_motion = nullptr) {
		_sim	= &p_sim;
		_pwm	= &p_pwm;
		_nvs	= &p_nvs;
		_motion = p_motion;

		memset(&_state, 0, sizeof(_state));
		memset(&_overrideState, 0, sizeof(_overrideState));

		if (!_sim || !_pwm || !_nvs) {
			CL_D10_Logger::log(EN_L10_LOG_ERROR,
				"[CT10] begin failed: null dep (sim=%p,pwm=%p,nvs=%p)", _sim, _pwm, _nvs);
			active = false;
			return false;
		}

		// 기본 상태: OFF
		_state.runMode = EN_CT10_RUNMODE_OFF;
		_state.startMs = millis();

		// NVS 에 저장된 마지막 모드/선택 복구(필요 시)
		_restoreFromNvs();

		active = true;
		CL_D10_Logger::log(EN_L10_LOG_INFO,
			"[CT10] begin ok (runMode=%u)", (unsigned)_state.runMode);
		return true;
	}

	// --------------------------------------------------
	// 의존성 주입 보정 (옵션)
	// --------------------------------------------------
	void setMotion(CL_M10_MotionLogic* p_motion) {
		_motion = p_motion;
	}

	// --------------------------------------------------
	// 주기 호출 (loop에서 100~500ms 주기 권장)
	// --------------------------------------------------
	void tick() {
		if (!active || !_pwm || !_sim) return;

		unsigned long v_now = millis();

		// 1) Override 최우선 처리
		if (_overrideState.active) {
			if (v_now >= _overrideState.untilMs) {
				_clearOverride();
				_reapplyCurrentContext();
			} else {
				_applyOverride();
				return;
			}
		}

		// 2) 현재 모드에 따른 실행
		switch (_state.runMode) {
			case EN_CT10_RUNMODE_OFF:
				_applyOff();
				break;

			case EN_CT10_RUNMODE_SCHEDULE:
				_tickSchedule();
				break;

			case EN_CT10_RUNMODE_PROFILE:
				_tickUserProfile();
				break;

			default:
				_applyOff();
				break;
		}
	}

	// --------------------------------------------------
	// 모드 전환 API
	// --------------------------------------------------

	// 전체 정지
	void stopAll() {
		_state.runMode = EN_CT10_RUNMODE_OFF;
		_state.activeScheduleIndex = -1;
		_state.activeProfileIndex  = -1;
		_state.curSegmentIndex     = -1;
		_state.segOnPhase          = false;
		_state.motionBlock         = false;
		memset(&_state.autoOff, 0, sizeof(_state.autoOff));

		_clearOverride();
		if (_sim) _sim->S10_stop();
		if (_pwm) _pwm->P10_setDutyPercent(0.0f);

		if (_nvs) {
			_nvs->N10_setRunMode((uint8_t)EN_CT10_RUNMODE_OFF);
			_nvs->N10_setActiveSchedule(-1);
			_nvs->N10_setActiveUserProfile(-1);
		}

		CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] stopAll");
	}

	// Schedule 번호 기반 시작
	bool startSchedule(int16_t p_schNo) {
		if (!g_A10_config_root.schedules) return false;

		int16_t v_idx = _findScheduleIndexByNo(*g_A10_config_root.schedules, p_schNo);
		if (v_idx < 0) {
			CL_D10_Logger::log(EN_L10_LOG_WARN,
				"[CT10] startSchedule: not found schNo=%d", (int)p_schNo);
			return false;
		}
		return _startScheduleByIndex(v_idx);
	}

	// User Profile 번호 기반 시작
	bool startUserProfile(int16_t p_profileNo) {
		if (!g_A10_config_root.userProfiles) return false;

		int16_t v_idx = _findUserProfileIndexByNo(*g_A10_config_root.userProfiles, p_profileNo);
		if (v_idx < 0) {
			CL_D10_Logger::log(EN_L10_LOG_WARN,
				"[CT10] startUserProfile: not found profileNo=%d", (int)p_profileNo);
			return false;
		}
		return _startUserProfileByIndex(v_idx);
	}

	// --------------------------------------------------
	// Override API (NVS 비저장, 일시적 상태)
	// --------------------------------------------------
	void overrideFixed(float p_percent, uint32_t p_seconds) {
		if (!_pwm) return;

		_clearOverride();
		_overrideState.active       = true;
		_overrideState.mode         = EN_CT10_OVERRIDE_FIXED;
		_overrideState.fixedPercent = constrain(p_percent, 0.0f, 100.0f);
		_overrideState.untilMs      = millis() + (p_seconds * 1000UL);

		CL_D10_Logger::log(EN_L10_LOG_INFO,
			"[CT10] Override FIXED %.1f%% %lus",
			_overrideState.fixedPercent, (unsigned long)p_seconds);

		_applyOverride();
	}

	void overridePreset(const char* p_presetCode,
						const char* p_styleCode,
						const ST_A10_AdjustDelta_t* p_adj,
						uint32_t p_seconds) {
		if (!p_presetCode || !p_presetCode[0]) return;

		_clearOverride();
		_overrideState.active = true;
		_overrideState.mode   = EN_CT10_OVERRIDE_PRESET;
		strlcpy(_overrideState.presetCode, p_presetCode,
				sizeof(_overrideState.presetCode));
		if (p_styleCode) {
			strlcpy(_overrideState.styleCode, p_styleCode,
					sizeof(_overrideState.styleCode));
		}
		memset(&_overrideState.adjust, 0, sizeof(_overrideState.adjust));
		if (p_adj) {
			_overrideState.adjust = *p_adj;
		}
		_overrideState.untilMs = millis() + (p_seconds * 1000UL);

		CL_D10_Logger::log(EN_L10_LOG_INFO,
			"[CT10] Override PRESET %s/%s %lus",
			_overrideState.presetCode,
			_overrideState.styleCode,
			(unsigned long)p_seconds);

		_applyOverride();
	}

	void releaseOverride() {
		if (!_overrideState.active) return;
		_clearOverride();
		_reapplyCurrentContext();
		CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] Override released");
	}

	// --------------------------------------------------
	// JSON Export (Web UI용)
	// --------------------------------------------------
	void toJson(JsonDocument& p_doc) {
		JsonObject o = p_doc["ct10"].to<JsonObject>();

		o["active"]  = active;
		o["runMode"] = (uint8_t)_state.runMode;

		JsonObject cur = o["current"].to<JsonObject>();
		cur["scheduleIndex"] = _state.activeScheduleIndex;
		cur["profileIndex"]  = _state.activeProfileIndex;
		cur["segmentIndex"]  = _state.curSegmentIndex;
		cur["segOnPhase"]    = _state.segOnPhase;

		JsonObject ov = o["override"].to<JsonObject>();
		ov["active"] = _overrideState.active;
		ov["mode"]   = (uint8_t)_overrideState.mode;
		if (_overrideState.active) {
			unsigned long v_now = millis();
			uint32_t v_remain = (_overrideState.untilMs > v_now)
				? (uint32_t)((_overrideState.untilMs - v_now)/1000UL)
				: 0;
			ov["remainSec"] = (int32_t)v_remain;
		} else {
			ov["remainSec"] = 0;
		}
		if (_overrideState.mode == EN_CT10_OVERRIDE_FIXED) {
			ov["fixedPercent"] = _overrideState.fixedPercent;
		} else if (_overrideState.mode == EN_CT10_OVERRIDE_PRESET) {
			ov["presetCode"] = _overrideState.presetCode;
			ov["styleCode"]  = _overrideState.styleCode;
		}

		JsonObject ao = o["autoOff"].to<JsonObject>();
		ao["timerEnabled"] = _state.autoOff.timerEnabled;
		ao["timerMinutes"] = _state.autoOff.timerMinutes;
		ao["offTimeEnabled"] = _state.autoOff.offTimeEnabled;
		ao["offTime"]        = _state.autoOff.offTime;
		ao["offTempEnabled"] = _state.autoOff.offTempEnabled;
		ao["offTempC"]       = _state.autoOff.offTempC;

		o["motionBlock"] = _state.motionBlock;
	}

private:
	CL_S10_Simulation*	_sim	= nullptr;
	CL_P10_PWM*			_pwm	= nullptr;
	CL_N10_NvsManager*	_nvs	= nullptr;
	CL_M10_MotionLogic*	_motion = nullptr;

	ST_CT10_runState_t		_state;
	ST_CT10_overrideState_t	_overrideState;

	// --------------------------------------------------
	// 내부: NVS 복구
	// --------------------------------------------------
	void _restoreFromNvs() {
		if (!_nvs) return;

		uint8_t v_run = _nvs->N10_getRunMode();
		int16_t v_sch = _nvs->N10_getActiveSchedule();
		int16_t v_prf = _nvs->N10_getActiveUserProfile();

		if (v_run == EN_CT10_RUNMODE_SCHEDULE && g_A10_config_root.schedules) {
			int16_t v_idx = _findScheduleIndexByNo(*g_A10_config_root.schedules, v_sch);
			if (v_idx >= 0) {
				_startScheduleByIndex(v_idx, false);
				CL_D10_Logger::log(EN_L10_LOG_INFO,
					"[CT10] restored schedule #%d", (int)v_sch);
				return;
			}
		}

		if (v_run == EN_CT10_RUNMODE_PROFILE && g_A10_config_root.userProfiles) {
			int16_t v_idx = _findUserProfileIndexByNo(*g_A10_config_root.userProfiles, v_prf);
			if (v_idx >= 0) {
				_startUserProfileByIndex(v_idx, false);
				CL_D10_Logger::log(EN_L10_LOG_INFO,
					"[CT10] restored profile #%d", (int)v_prf);
				return;
			}
		}

		// 유효하지 않으면 OFF
		_nvs->N10_setRunMode((uint8_t)EN_CT10_RUNMODE_OFF);
		_nvs->N10_setActiveSchedule(-1);
		_nvs->N10_setActiveUserProfile(-1);
	}

	// --------------------------------------------------
	// 내부: Schedule 모드 처리
	// --------------------------------------------------
	void _tickSchedule() {
		if (!g_A10_config_root.schedules) {
			_applyOff();
			return;
		}

		ST_A10_ScheduleConfig& v_cfg = *g_A10_config_root.schedules;

		// 활성 스케줄 유효성 확인/선정
		if (_state.activeScheduleIndex < 0 ||
			_state.activeScheduleIndex >= v_cfg.count ||
			!v_cfg.items[_state.activeScheduleIndex].enabled) {
			int16_t v_idx = _selectActiveScheduleNow(v_cfg);
			if (v_idx < 0) {
				_applyOff();
				return;
			}
			_startScheduleByIndex(v_idx, true);
		}

		ST_A10_ScheduleItem_t& v_s = v_cfg.items[_state.activeScheduleIndex];

		// 현재 시간 기준 period 안에 있는지 재확인
		if (!_isNowInPeriod(v_s.period)) {
			int16_t v_next = _selectActiveScheduleNow(v_cfg);
			if (v_next >= 0 && v_next != _state.activeScheduleIndex) {
				_startScheduleByIndex(v_next, true);
				return;
			}
			_applyOff();
			return;
		}

		// Motion gate
		if (_checkMotionBlocked(v_s.motion)) {
			_applyOffFanOnly();
			return;
		}

		// AutoOff
		if (_checkAutoOff()) {
			_applyOff();
			return;
		}

		// Segment 실행
		_execCurrentSegment(v_s.segments, v_s.segCount, false);
	}

	// 현재 시각에 활성화 가능한 스케줄 선택
	int16_t _selectActiveScheduleNow(const ST_A10_ScheduleConfig& p_cfg) {
		for (uint8_t v_i=0; v_i<p_cfg.count; v_i++) {
			const ST_A10_ScheduleItem_t& v_s = p_cfg.items[v_i];
			if (!v_s.enabled) continue;
			if (!_isNowInPeriod(v_s.period)) continue;
			return (int16_t)v_i;
		}
		return -1;
	}

	// --------------------------------------------------
	// 내부: UserProfile 모드 처리
	// --------------------------------------------------
	void _tickUserProfile() {
		if (!g_A10_config_root.userProfiles) {
			_applyOff();
			return;
		}

		ST_A10_UserProfileConfig_t& v_cfg = *g_A10_config_root.userProfiles;

		if (_state.activeProfileIndex < 0 ||
			_state.activeProfileIndex >= v_cfg.count ||
			!v_cfg.items[_state.activeProfileIndex].enabled) {
			_applyOff();
			return;
		}

		ST_A10_UserProfile_t& v_p = v_cfg.items[_state.activeProfileIndex];

		// Motion gate
		if (_checkMotionBlocked(v_p.motion)) {
			_applyOffFanOnly();
			return;
		}

		// AutoOff
		if (_checkAutoOff()) {
			_applyOff();
			return;
		}

		// Segment 실행 (repeatSegments 지원)
		_execCurrentSegment(v_p.segments, v_p.segCount, v_p.repeatSegments);
	}

	// --------------------------------------------------
	// Segment 실행 공통 처리
	// --------------------------------------------------
	void _execCurrentSegment(ST_A10_OpSegment_t* p_segments,
							 uint8_t			 p_segCount,
							 bool				 p_repeat) {
		if (!_sim || !_pwm || !p_segments || p_segCount == 0) {
			_applyOff();
			return;
		}

		unsigned long v_now = millis();

		// 초기 진입
		if (_state.curSegmentIndex < 0 || _state.curSegmentIndex >= p_segCount) {
			_state.curSegmentIndex = 0;
			_state.segOnPhase      = true;
			_state.segPhaseStartMs = v_now;
			_applySegmentOn(p_segments[_state.curSegmentIndex]);
			return;
		}

		ST_A10_OpSegment_t& v_seg = p_segments[_state.curSegmentIndex];

		uint32_t v_onMs  = (uint32_t)max(1, (int)v_seg.on_minutes)  * 60000UL;
		uint32_t v_offMs = (uint32_t)max(0, (int)v_seg.off_minutes) * 60000UL;

		if (_state.segOnPhase) {
			if (v_now - _state.segPhaseStartMs >= v_onMs) {
				_state.segOnPhase      = false;
				_state.segPhaseStartMs = v_now;
				_applySegmentOff();
			}
		} else {
			if (v_now - _state.segPhaseStartMs >= v_offMs) {
				// 다음 segment
				int16_t v_next = _state.curSegmentIndex + 1;
				if (v_next >= (int16_t)p_segCount) {
					if (p_repeat) {
						v_next = 0;
					} else {
						_applyOff();
						return;
					}
				}
				_state.curSegmentIndex = v_next;
				_state.segOnPhase      = true;
				_state.segPhaseStartMs = v_now;
				_applySegmentOn(p_segments[_state.curSegmentIndex]);
			}
		}
	}

	void _applySegmentOn(const ST_A10_OpSegment_t& p_seg) {
		if (!_sim || !_pwm) return;

		if (p_seg.mode[0] == 'F') { // "FIXED"
			_sim->S10_stop();
			float v_pct = constrain(p_seg.fixed_speed, 0.0f, 100.0f);
			_pwm->P10_setDutyPercent(v_pct);
			return;
		}

		// PRESET 모드
		if (!g_A10_config_root.windDict) {
			CL_D10_Logger::log(EN_L10_LOG_WARN,
				"[CT10] segment PRESET but windDict missing");
			return;
		}

		ST_A10_ResolvedWind_t v_res;
		if (!CL_C10_ConfigManager::C10_resolveWindParams(
				*g_A10_config_root.windDict,
				p_seg.presetCode,
				p_seg.styleCode,
				&p_seg.adjust,
				v_res)) {
			CL_D10_Logger::log(EN_L10_LOG_WARN,
				"[CT10] resolveWind failed preset=%s style=%s",
				p_seg.presetCode, p_seg.styleCode);
			return;
		}

		_applyResolvedWind(v_res);
	}

	void _applySegmentOff() {
		if (!_sim || !_pwm) return;
		_sim->S10_stop();
		_pwm->P10_setDutyPercent(0.0f);
	}

	// --------------------------------------------------
	// AutoOff 체크
	// --------------------------------------------------
	bool _checkAutoOff() {
		if (!_state.autoOff.timerEnabled &&
			!_state.autoOff.offTimeEnabled &&
			!_state.autoOff.offTempEnabled) {
			return false;
		}

		unsigned long v_nowMs = millis();

		// Timer
		if (_state.autoOff.timerEnabled &&
			_state.autoOff.timerMinutes > 0 &&
			_state.autoOff.timerStartMs > 0) {
			uint32_t v_ms = _state.autoOff.timerMinutes * 60000UL;
			if (v_nowMs - _state.autoOff.timerStartMs >= v_ms) {
				CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] AutoOff: timer");
				return true;
			}
		}

		// OffTime (로컬타임 기반)
		if (_state.autoOff.offTimeEnabled &&
			_state.autoOff.offTime[0]) {
			if (_isNowPastOffTime(_state.autoOff.offTime)) {
				CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] AutoOff: offTime %s",
					_state.autoOff.offTime);
				return true;
			}
		}

		// OffTemp (온도 센서 연동 가정: A10_getCurrentTempC())
		if (_state.autoOff.offTempEnabled) {
			float v_cur = A10_getCurrentTempC();
			if (v_cur <= _state.autoOff.offTempC) {
				CL_D10_Logger::log(EN_L10_LOG_INFO,
					"[CT10] AutoOff: offTemp %.2f <= %.2f",
					(double)v_cur, (double)_state.autoOff.offTempC);
				return true;
			}
		}

		return false;
	}

	// --------------------------------------------------
	// Motion Gate
	// --------------------------------------------------
	bool _checkMotionBlocked(const ST_A10_OpMotionConfig_t& p_m) {
		if (!_motion) return false;

		// 단순 정책:
		//  - pir/ble 둘 중 하나라도 enabled이고
		//  - M10_isMotionPresent() == false 이면 차단
		if (!p_m.pir.enabled && !p_m.ble.enabled)
			return false;

		bool v_present = _motion->M10_isMotionPresent();
		_state.motionBlock = !v_present;
		return _state.motionBlock;
	}

	// 팬만 끄고 상태/모드는 유지
	void _applyOffFanOnly() {
		if (_pwm) _pwm->P10_setDutyPercent(0.0f);
		if (_sim) _sim->S10_stop();
	}

	// 완전 OFF
	void _applyOff() {
		if (_sim) _sim->S10_stop();
		if (_pwm) _pwm->P10_setDutyPercent(0.0f);
	}

	// --------------------------------------------------
	// Override 처리
	// --------------------------------------------------
	void _applyOverride() {
		if (!_overrideState.active || !_pwm) return;

		if (_overrideState.mode == EN_CT10_OVERRIDE_FIXED) {
			if (_sim) _sim->S10_stop();
			float v_pct = constrain(_overrideState.fixedPercent, 0.0f, 100.0f);
			_pwm->P10_setDutyPercent(v_pct);
			return;
		}

		if (_overrideState.mode == EN_CT10_OVERRIDE_PRESET) {
			if (!g_A10_config_root.windDict || !_sim) return;

			ST_A10_ResolvedWind_t v_res;
			if (!CL_C10_ConfigManager::C10_resolveWindParams(
					*g_A10_config_root.windDict,
					_overrideState.presetCode,
					_overrideState.styleCode,
					&_overrideState.adjust,
					v_res)) {
				return;
			}
			_applyResolvedWind(v_res);
		}
	}

	void _clearOverride() {
		if (!_overrideState.active) {
			memset(&_overrideState, 0, sizeof(_overrideState));
			return;
		}
		memset(&_overrideState, 0, sizeof(_overrideState));
	}

	// --------------------------------------------------
	// ResolvedWind → Simulation/PWM 적용
	// --------------------------------------------------
	void _applyResolvedWind(const ST_A10_ResolvedWind_t& p_wind) {
		if (!_sim || !_pwm) return;

		_sim->S10_applyResolvedWind(p_wind, *_pwm);
	}

	// --------------------------------------------------
	// 현재 컨텍스트 재적용 (Override 해제 시 등)
	// --------------------------------------------------
	void _reapplyCurrentContext() {
		if (_state.runMode == EN_CT10_RUNMODE_SCHEDULE) {
			if (g_A10_config_root.schedules &&
				_state.activeScheduleIndex >= 0 &&
				_state.activeScheduleIndex < g_A10_config_root.schedules->count) {
				// 현재 segment 기준 재적용
				ST_A10_ScheduleItem_t& v_s =
					g_A10_config_root.schedules->items[_state.activeScheduleIndex];
				if (_state.curSegmentIndex >= 0 &&
					_state.curSegmentIndex < v_s.segCount &&
					_state.segOnPhase) {
					_applySegmentOn(v_s.segments[_state.curSegmentIndex]);
				} else {
					_applySegmentOff();
				}
			}
		} else if (_state.runMode == EN_CT10_RUNMODE_PROFILE) {
			if (g_A10_config_root.userProfiles &&
				_state.activeProfileIndex >= 0 &&
				_state.activeProfileIndex < g_A10_config_root.userProfiles->count) {

				ST_A10_UserProfile_t& v_p =
					g_A10_config_root.userProfiles->items[_state.activeProfileIndex];
				if (_state.curSegmentIndex >= 0 &&
					_state.curSegmentIndex < v_p.segCount &&
					_state.segOnPhase) {
					_applySegmentOn(v_p.segments[_state.curSegmentIndex]);
				} else {
					_applySegmentOff();
				}
			}
		} else {
			_applyOff();
		}
	}

	// --------------------------------------------------
	// 시작 함수 (내부용): Schedule
	// --------------------------------------------------
	bool _startScheduleByIndex(int16_t p_idx, bool p_resetState=true) {
		if (!g_A10_config_root.schedules) return false;
		if (p_idx < 0 || p_idx >= g_A10_config_root.schedules->count) return false;

		if (p_resetState) {
			memset(&_state, 0, sizeof(_state));
		}

		ST_A10_ScheduleItem_t& v_s = g_A10_config_root.schedules->items[p_idx];

		_state.runMode				= EN_CT10_RUNMODE_SCHEDULE;
		_state.activeScheduleIndex	= p_idx;
		_state.activeProfileIndex	= -1;
		_state.curSegmentIndex		= -1;
		_state.segOnPhase			= false;
		_state.segPhaseStartMs		= 0;
		_state.startMs				= millis();
		_state.repeatSegments		= true; // schedule는 항상 순환 (period 내)

		// AutoOff 초기화
		memset(&_state.autoOff, 0, sizeof(_state.autoOff));
		_state.autoOff.timerEnabled   = v_s.autoOff.timer.enabled;
		_state.autoOff.timerMinutes   = v_s.autoOff.timer.minutes;
		_state.autoOff.offTimeEnabled = v_s.autoOff.offTime.enabled;
		strlcpy(_state.autoOff.offTime, v_s.autoOff.offTime.time,
			sizeof(_state.autoOff.offTime));
		_state.autoOff.offTempEnabled = v_s.autoOff.offTemp.enabled;
		_state.autoOff.offTempC       = v_s.autoOff.offTemp.temp;
		if (_state.autoOff.timerEnabled && _state.autoOff.timerMinutes > 0) {
			_state.autoOff.timerStartMs = millis();
		}

		if (_nvs) {
			_nvs->N10_setRunMode((uint8_t)EN_CT10_RUNMODE_SCHEDULE);
			_nvs->N10_setActiveSchedule(v_s.schNo);
			_nvs->N10_setActiveUserProfile(-1);
		}

		CL_D10_Logger::log(EN_L10_LOG_INFO,
			"[CT10] startSchedule idx=%d schNo=%d", (int)p_idx, (int)v_s.schNo);

		return true;
	}

	// --------------------------------------------------
	// 시작 함수 (내부용): UserProfile
	// --------------------------------------------------
	bool _startUserProfileByIndex(int16_t p_idx, bool p_resetState=true) {
		if (!g_A10_config_root.userProfiles) return false;
		if (p_idx < 0 || p_idx >= g_A10_config_root.userProfiles->count) return false;

		if (p_resetState) {
			memset(&_state, 0, sizeof(_state));
		}

		ST_A10_UserProfile_t& v_p = g_A10_config_root.userProfiles->items[p_idx];

		_state.runMode				= EN_CT10_RUNMODE_PROFILE;
		_state.activeScheduleIndex	= -1;
		_state.activeProfileIndex	= p_idx;
		_state.curSegmentIndex		= -1;
		_state.segOnPhase			= false;
		_state.segPhaseStartMs		= 0;
		_state.startMs				= millis();
		_state.repeatSegments		= v_p.repeatSegments;

		memset(&_state.autoOff, 0, sizeof(_state.autoOff));
		_state.autoOff.timerEnabled   = v_p.autoOff.timer.enabled;
		_state.autoOff.timerMinutes   = v_p.autoOff.timer.minutes;
		_state.autoOff.offTimeEnabled = v_p.autoOff.offTime.enabled;
		strlcpy(_state.autoOff.offTime, v_p.autoOff.offTime.time,
			sizeof(_state.autoOff.offTime));
		_state.autoOff.offTempEnabled = v_p.autoOff.offTemp.enabled;
		_state.autoOff.offTempC       = v_p.autoOff.offTemp.temp;
		if (_state.autoOff.timerEnabled && _state.autoOff.timerMinutes > 0) {
			_state.autoOff.timerStartMs = millis();
		}

		if (_nvs) {
			_nvs->N10_setRunMode((uint8_t)EN_CT10_RUNMODE_PROFILE);
			_nvs->N10_setActiveSchedule(-1);
			_nvs->N10_setActiveUserProfile(v_p.profileNo);
		}

		CL_D10_Logger::log(EN_L10_LOG_INFO,
			"[CT10] startUserProfile idx=%d profileNo=%d",
			(int)p_idx, (int)v_p.profileNo);

		return true;
	}

	// --------------------------------------------------
	// Helper: Schedule/UserProfile index 찾기
	// --------------------------------------------------
	int16_t _findScheduleIndexByNo(const ST_A10_ScheduleConfig& p_cfg, int16_t p_no) {
		for (uint8_t v_i=0; v_i<p_cfg.count; v_i++) {
			if ((int16_t)p_cfg.items[v_i].schNo == p_no) return (int16_t)v_i;
		}
		return -1;
	}

	int16_t _findUserProfileIndexByNo(const ST_A10_UserProfileConfig_t& p_cfg, int16_t p_no) {
		for (uint8_t v_i=0; v_i<p_cfg.count; v_i++) {
			if ((int16_t)p_cfg.items[v_i].profileNo == p_no) return (int16_t)v_i;
		}
		return -1;
	}

	// --------------------------------------------------
	// Helper: Period, OffTime
	// --------------------------------------------------
	bool _isNowInPeriod(const ST_A10_SchedulePeriod_t& p) {
		if (!p.enabled) return true; // period 미사용 시 항상 허용

		time_t v_t = time(nullptr);
		struct tm* v_tm = localtime(&v_t);
		if (!v_tm) return false;

		int v_wday   = v_tm->tm_wday; // 0=Sun
		if (v_wday < 0 || v_wday > 6) return false;
		if (p.days[v_wday] == 0) return false;

		int v_nowMin = v_tm->tm_hour*60 + v_tm->tm_min;
		int v_st     = _parseHHMM(p.start_time);
		int v_ed     = _parseHHMM(p.end_time);
		if (v_st < 0 || v_ed < 0) return false;

		if (v_st <= v_ed) {
			return (v_nowMin >= v_st && v_nowMin < v_ed);
		} else {
			// over-midnight
			return (v_nowMin >= v_st || v_nowMin < v_ed);
		}
	}

	bool _isNowPastOffTime(const char* p_hhmm) {
		if (!p_hhmm || !p_hhmm[0]) return false;

		int v_off = _parseHHMM(p_hhmm);
		if (v_off < 0) return false;

		time_t v_t = time(nullptr);
		struct tm* v_tm = localtime(&v_t);
		if (!v_tm) return false;

		int v_nowMin = v_tm->tm_hour*60 + v_tm->tm_min;
		return (v_nowMin >= v_off);
	}

	int _parseHHMM(const char* p) {
		if (!p) return -1;
		int v_len = (int)strlen(p);
		if (v_len < 4 || v_len > 5) return -1;

		int v_col = -1;
		for (int v_i=0; v_i<v_len; v_i++) {
			if (p[v_i] == ':') { v_col = v_i; break; }
		}
		if (v_col <= 0) return -1;

		int v_h = atoi(String(p).substring(0, v_col).c_str());
		int v_m = atoi(String(p).substring(v_col+1).c_str());
		if (v_h < 0 || v_h > 23 || v_m < 0 || v_m > 59) return -1;
		return v_h*60 + v_m;
	}
};

// ------------------------------------------------------
// 외부 참조용 config root
// ------------------------------------------------------
inline ST_A10_ConfigRoot g_A10_config_root;

