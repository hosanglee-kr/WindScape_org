#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : CT10_ControlManager_018.h
 * 모듈약어 : CT10
 * 모듈명 : Smart Nature Wind 제어 통합 Manager (v018)
 * ------------------------------------------------------
 * 기능 요약:
 *  - Schedules / UserProfiles / Override 기반 풍량 제어
 *  - WindProfile(preset/style) 사전 + C10_resolveWindParams 연동
 *  - S10_Simulation_017 / P10_PWM_ctrl_016 통합 제어
 *  - Motion( PIR / BLE ) 및 AutoOff 조건 훅 제공
 *  - Web UI / 버튼에서 Profile/Override 선택시 즉시 반영
 *  - JSON 상태 Export (control / sim / chart 등과 연동용)
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
#include <time.h>

#include "A10_Const_014.h"
#include "C10_ConfigManager_020.h"
#include "S10_Simulation_017.h"
#include "P10_PWM_ctrl_016.h"
#include "D10_Logger_011.h"

// Motion 모듈은 별도 헤더에서 제공된다고 가정 (여기서는 forward 선언)
class CL_M10_MotionLogic;

// ------------------------------------------------------
// 런타임 제어 관련 enum / 구조체
// ------------------------------------------------------
typedef enum : uint8_t {
	EN_CT10_RUN_NONE          = 0,
	EN_CT10_RUN_SCHEDULE      = 1,
	EN_CT10_RUN_USER_PROFILE  = 2
} EN_CT10_run_source_t;

// Override 상태 (NVS 저장 안함, 런타임 전용)
typedef struct {
	bool					active;
	unsigned long			endMs;
	bool					useFixed;
	float					fixedPercent;
	char					presetCode[24];
	char					styleCode[24];
	ST_A10_AdjustDelta_t	adjust;
} ST_CT10_Override_t;

// 세그먼트 실행 상태
typedef struct {
	int8_t			index;
	bool			onPhase;
	unsigned long	phaseStartMs;
} ST_CT10_SegmentRuntime_t;

// AutoOff 런타임 상태 (타이머/오프타임/온도 조건 훅용)
typedef struct {
	bool			timerArmed;
	unsigned long	timerStartMs;
	uint32_t		timerMinutes;

	bool			offTimeEnabled;
	uint16_t		offTimeMinutes;		// 0~1439 (HH*60+MM)

	bool			offTempEnabled;
	float			offTemp;
} ST_CT10_AutoOffRuntime_t;


// ======================================================
// CT10 제어 Manager
// ======================================================
class CL_CT10_ControlManager {
public:
	bool						active				= false;

	EN_CT10_run_source_t		runSource			= EN_CT10_RUN_NONE;

	int8_t						curScheduleIndex	= -1;
	ST_CT10_SegmentRuntime_t	scheduleSegRt;

	int8_t						curProfileIndex		= -1;
	ST_CT10_SegmentRuntime_t	profileSegRt;

	ST_CT10_Override_t			overrideState;
	ST_CT10_AutoOffRuntime_t	autoOffRt;

	CL_P10_PWM*					pwm					= nullptr;
	CL_M10_MotionLogic*			motion				= nullptr;

	CL_S10_Simulation			sim;

	unsigned long				lastTickMs			= 0;

public:
	// ==================================================
	// 초기화
	// ==================================================
	void begin(CL_P10_PWM& p_pwm) {
		pwm = &p_pwm;
		memset(&overrideState, 0, sizeof(overrideState));
		memset(&scheduleSegRt, 0, sizeof(scheduleSegRt));
		memset(&profileSegRt, 0, sizeof(profileSegRt));
		memset(&autoOffRt, 0, sizeof(autoOffRt));

		scheduleSegRt.index = -1;
		profileSegRt.index  = -1;

		sim.begin(p_pwm);
		active = true;

		CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] begin()");
	}

	void setMotion(CL_M10_MotionLogic* p_motion) {
		motion = p_motion;
	}

	// ==================================================
	// 사용자 API: 유저 프로파일 시작 / 정지
	// ==================================================
	bool startUserProfileByNo(uint8_t p_profileNo) {
		if (!g_A10_config_root.userProfiles) return false;

		const ST_A10_UserProfileConfig_t& v_cfg = *g_A10_config_root.userProfiles;
		for (uint8_t v_i=0; v_i<v_cfg.count; v_i++) {
			const ST_A10_UserProfile_t& v_p = v_cfg.items[v_i];
			if (!v_p.enabled) continue;
			if (v_p.profileNo == p_profileNo) {
				runSource = EN_CT10_RUN_USER_PROFILE;
				curProfileIndex = (int8_t)v_i;
				profileSegRt.index = -1;
				profileSegRt.onPhase = true;
				profileSegRt.phaseStartMs = 0;
				_initAutoOffFromUserProfile(v_p);
				CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] Start UserProfile #%u (%s)",
								   (unsigned)p_profileNo, v_p.name);
				return true;
			}
		}
		return false;
	}

	void stopUserProfile() {
		if (runSource == EN_CT10_RUN_USER_PROFILE) {
			runSource = EN_CT10_RUN_NONE;
			curProfileIndex = -1;
			profileSegRt.index = -1;
			sim.stop();
			CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] UserProfile stopped");
		}
	}

	// ==================================================
	// Override API (런타임 전용)
	// ==================================================
	void startOverrideFixed(float p_percent, uint32_t p_seconds) {
		if (p_seconds == 0) return;

		memset(&overrideState, 0, sizeof(overrideState));
		overrideState.active       = true;
		overrideState.useFixed     = true;
		overrideState.fixedPercent = constrain(p_percent, 0.0f, 100.0f);
		overrideState.endMs        = millis() + (p_seconds * 1000UL);

		CL_D10_Logger::log(EN_L10_LOG_INFO,
			"[CT10] Override FIXED %.1f%% for %lu sec",
			overrideState.fixedPercent, (unsigned long)p_seconds);
	}

	void startOverridePreset(const char* p_presetCode,
							 const char* p_styleCode,
							 const ST_A10_AdjustDelta_t* p_adj,
							 uint32_t p_seconds) {
		if (p_seconds == 0) return;

		memset(&overrideState, 0, sizeof(overrideState));
		overrideState.active   = true;
		overrideState.useFixed = false;
		overrideState.endMs    = millis() + (p_seconds * 1000UL);

		if (p_presetCode)
			strlcpy(overrideState.presetCode, p_presetCode, sizeof(overrideState.presetCode));
		if (p_styleCode)
			strlcpy(overrideState.styleCode, p_styleCode, sizeof(overrideState.styleCode));

		if (p_adj)
			overrideState.adjust = *p_adj;

		CL_D10_Logger::log(EN_L10_LOG_INFO,
			"[CT10] Override PRESET preset=%s style=%s dur=%lu",
			overrideState.presetCode, overrideState.styleCode, (unsigned long)p_seconds);
	}

	void stopOverride() {
		if (!overrideState.active) return;
		memset(&overrideState, 0, sizeof(overrideState));
		CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] Override cleared");
		// 원래 동작 모드로 복귀: tick에서 schedule/profile 로직이 다시 적용됨
	}

	// ==================================================
	// 메인 Tick
	// ==================================================
	void tick() {
		if (!active || !pwm) return;

		unsigned long v_now = millis();
		if (v_now - lastTickMs < 40UL) {
			// S10_Simulation_017 내부에서 자체 지터 처리, 여기서는 최소 호출 주기만 보장
		}
		lastTickMs = v_now;

		// 1) Override 최우선
		if (_tickOverride()) {
			sim.tick();
			return;
		}

		// 2) UserProfile 모드 우선 (사용자가 명시 선택한 경우)
		if (runSource == EN_CT10_RUN_USER_PROFILE && _tickUserProfile()) {
			sim.tick();
			return;
		}

		// 3) 스케줄 자동 모드
		if (_tickSchedule()) {
			sim.tick();
			return;
		}

		// 4) 아무 것도 없으면 정지
		if (sim.active) {
			sim.stop();
		}
	}

	// ==================================================
	// JSON Export
	// ==================================================
	void toJson(JsonDocument& p_doc) {
		JsonObject o = p_doc["control"].to<JsonObject>();
		o["active"]     = active;
		o["runSource"]  = (int)runSource;
		o["scheduleIdx"]= curScheduleIndex;
		o["profileIdx"] = curProfileIndex;

		JsonObject ov = o["override"].to<JsonObject>();
		ov["active"] = overrideState.active;
		if (overrideState.active) {
			unsigned long v_now = millis();
			uint32_t v_remain = (overrideState.endMs > v_now)
				? (uint32_t)((overrideState.endMs - v_now)/1000UL)
				: 0;
			ov["remainSec"] = v_remain;
			ov["useFixed"]  = overrideState.useFixed;
			if (overrideState.useFixed) {
				ov["fixedPercent"] = overrideState.fixedPercent;
			} else {
				ov["presetCode"] = overrideState.presetCode;
				ov["styleCode"]  = overrideState.styleCode;
			}
		}

		JsonObject ao = o["autoOff"].to<JsonObject>();
		ao["timerArmed"] = autoOffRt.timerArmed;
		ao["timerMinutes"] = autoOffRt.timerMinutes;
		ao["offTimeEnabled"] = autoOffRt.offTimeEnabled;
		ao["offTimeMinutes"] = autoOffRt.offTimeMinutes;
		ao["offTempEnabled"] = autoOffRt.offTempEnabled;
		ao["offTemp"]        = autoOffRt.offTemp;

		if (pwm) {
			o["pwmDuty"] = pwm->P10_getDutyPercent();
		}

		sim.toJson(p_doc);
	}

private:
	// ==================================================
	// Override 처리
	// ==================================================
	bool _tickOverride() {
		if (!overrideState.active) return false;

		unsigned long v_now = millis();
		if (v_now >= overrideState.endMs) {
			// 만료
			memset(&overrideState, 0, sizeof(overrideState));
			CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] Override expired");
			return false;
		}

		if (overrideState.useFixed) {
			// 고정 풍속: 시뮬레이션 중지 + PWM 직접 제어
			if (sim.active) sim.stop();
			float v_pct = constrain(overrideState.fixedPercent, 0.0f, 100.0f);
			if (pwm) pwm->P10_setDutyPercent(v_pct);
			return true;
		}

		// preset/style 기반 Override → C10_resolveWindParams → sim.applyResolvedWind
		if (!g_A10_config_root.windDict) return false;

		ST_A10_ResolvedWind_t v_resolved;
		bool v_ok = CL_C10_ConfigManager::C10_resolveWindParams(
			*g_A10_config_root.windDict,
			overrideState.presetCode,
			overrideState.styleCode,
			&overrideState.adjust,
			v_resolved
		);
		if (!v_ok) return false;

		sim.applyResolvedWind(v_resolved);
		return true;
	}

	// ==================================================
	// UserProfile 모드 처리
	// ==================================================
	bool _tickUserProfile() {
		if (!g_A10_config_root.userProfiles) return false;
		if (curProfileIndex < 0) return false;

		ST_A10_UserProfileConfig_t& v_cfg = *g_A10_config_root.userProfiles;
		if ((uint8_t)curProfileIndex >= v_cfg.count) return false;

		ST_A10_UserProfile_t& v_p = v_cfg.items[curProfileIndex];
		if (!v_p.enabled || v_p.segCount == 0) return false;

		// AutoOff 조건 체크 (타이머/오프타임/온도 -> 실제 온도연동은 외부에서 훅)
		if (_checkAutoOff()) {
			sim.stop();
			runSource = EN_CT10_RUN_NONE;
			curProfileIndex = -1;
			return true;
		}

		// 세그먼트 시퀀스 처리
		return _tickSegmentSequence(v_p.repeatSegments,
									v_p.segments,
									v_p.segCount,
									profileSegRt);
	}

	// ==================================================
	// 스케줄 모드 처리
	// ==================================================
	bool _tickSchedule() {
		if (!g_A10_config_root.schedules) return false;

		ST_A10_ScheduleConfig& v_cfg = *g_A10_config_root.schedules;
		int v_idx = _findActiveScheduleIndex(v_cfg);
		if (v_idx < 0) {
			curScheduleIndex   = -1;
			scheduleSegRt.index = -1;
			return false;
		}

		if (curScheduleIndex != v_idx) {
			curScheduleIndex    = v_idx;
			scheduleSegRt.index = -1;
			scheduleSegRt.onPhase = true;
			scheduleSegRt.phaseStartMs = 0;
			_initAutoOffFromSchedule(v_cfg.items[v_idx]);
			runSource = EN_CT10_RUN_SCHEDULE;
			CL_D10_Logger::log(EN_L10_LOG_INFO,
							   "[CT10] Active Schedule idx=%d name=%s",
							   v_idx, v_cfg.items[v_idx].name);
		}

		ST_A10_ScheduleItem_t& v_s = v_cfg.items[curScheduleIndex];
		if (!v_s.enabled || v_s.segCount == 0) return false;

		// AutoOff 조건
		if (_checkAutoOff()) {
			sim.stop();
			runSource = EN_CT10_RUN_NONE;
			curScheduleIndex = -1;
			return true;
		}

		// Motion 게이팅
		if (_isMotionBlocked(v_s.motion)) {
			sim.stop();
			return true;
		}

		// 세그먼트 시퀀스 처리
		return _tickSegmentSequence(true,
									v_s.segments,
									v_s.segCount,
									scheduleSegRt);
	}

	// ==================================================
	// 공통: 세그먼트 (on/off 사이클) 처리
	// ==================================================
	bool _tickSegmentSequence(bool p_repeat,
							  ST_A10_OpSegment_t* p_segments,
							  uint8_t p_segCount,
							  ST_CT10_SegmentRuntime_t& p_rt) {
		unsigned long v_now = millis();

		if (p_rt.index < 0) {
			p_rt.index = 0;
			p_rt.onPhase = true;
			p_rt.phaseStartMs = v_now;
			_applySegmentOn(p_segments[p_rt.index]);
			return true;
		}

		if (p_rt.index >= p_segCount) {
			if (!p_repeat) {
				sim.stop();
				return true;
			}
			p_rt.index = 0;
			p_rt.onPhase = true;
			p_rt.phaseStartMs = v_now;
			_applySegmentOn(p_segments[p_rt.index]);
			return true;
		}

		ST_A10_OpSegment_t& v_seg = p_segments[p_rt.index];
		uint32_t v_onMs  = (uint32_t)v_seg.on_minutes  * 60000UL;
		uint32_t v_offMs = (uint32_t)v_seg.off_minutes * 60000UL;

		if (p_rt.onPhase) {
			if (v_onMs == 0) {
				// 즉시 OFF phase로
				p_rt.onPhase = false;
				p_rt.phaseStartMs = v_now;
				_applySegmentOff();
				return true;
			}
			if (v_now - p_rt.phaseStartMs >= v_onMs) {
				p_rt.onPhase = false;
				p_rt.phaseStartMs = v_now;
				_applySegmentOff();
			}
		} else {
			if (v_offMs == 0 || v_now - p_rt.phaseStartMs >= v_offMs) {
				p_rt.index++;
				if (p_rt.index >= p_segCount && p_repeat) {
					p_rt.index = 0;
				}
				p_rt.onPhase = true;
				p_rt.phaseStartMs = v_now;
				if (p_rt.index < p_segCount) {
					_applySegmentOn(p_segments[p_rt.index]);
				} else {
					sim.stop();
				}
			}
		}
		return true;
	}

	void _applySegmentOn(const ST_A10_OpSegment_t& p_seg) {
		if (!g_A10_config_root.windDict) {
			// windDict 없으면 fixed만 처리
			if (strcasecmp(p_seg.mode, "FIXED") == 0 && pwm) {
				float v_pct = constrain(p_seg.fixed_speed, 0.0f, 100.0f);
				sim.stop();
				pwm->P10_setDutyPercent(v_pct);
			}
			return;
		}

		if (strcasecmp(p_seg.mode, "FIXED") == 0) {
			if (pwm) {
				float v_pct = constrain(p_seg.fixed_speed, 0.0f, 100.0f);
				sim.stop();
				pwm->P10_setDutyPercent(v_pct);
			}
			return;
		}

		// PRESET 모드: presetCode + styleCode + adjust → resolve → sim.applyResolvedWind
		ST_A10_ResolvedWind_t v_resolved;
		bool v_ok = CL_C10_ConfigManager::C10_resolveWindParams(
			*g_A10_config_root.windDict,
			p_seg.presetCode,
			p_seg.styleCode,
			&p_seg.adjust,
			v_resolved
		);
		if (!v_ok) {
			CL_D10_Logger::log(EN_L10_LOG_WARN,
				"[CT10] resolve failed segNo=%d preset=%s style=%s",
				(int)p_seg.segNo, p_seg.presetCode, p_seg.styleCode);
			return;
		}

		sim.applyResolvedWind(v_resolved);
	}

	void _applySegmentOff() {
		sim.stop();
	}

	// ==================================================
	// AutoOff 설정 초기화
	// ==================================================
	void _initAutoOffFromUserProfile(const ST_A10_UserProfile_t& p_up) {
		memset(&autoOffRt, 0, sizeof(autoOffRt));
		if (p_up.autoOff.timer.enabled && p_up.autoOff.timer.minutes > 0) {
			autoOffRt.timerArmed   = true;
			autoOffRt.timerStartMs = millis();
			autoOffRt.timerMinutes = p_up.autoOff.timer.minutes;
		}
		if (p_up.autoOff.offTime.enabled) {
			autoOffRt.offTimeEnabled = true;
			autoOffRt.offTimeMinutes = _parseHHMMtoMin(p_up.autoOff.offTime.time);
		}
		if (p_up.autoOff.offTemp.enabled) {
			autoOffRt.offTempEnabled = true;
			autoOffRt.offTemp        = p_up.autoOff.offTemp.temp;
		}
	}

	void _initAutoOffFromSchedule(const ST_A10_ScheduleItem_t& p_s) {
		memset(&autoOffRt, 0, sizeof(autoOffRt));
		if (p_s.autoOff.timer.enabled && p_s.autoOff.timer.minutes > 0) {
			autoOffRt.timerArmed   = true;
			autoOffRt.timerStartMs = millis();
			autoOffRt.timerMinutes = p_s.autoOff.timer.minutes;
		}
		if (p_s.autoOff.offTime.enabled) {
			autoOffRt.offTimeEnabled = true;
			autoOffRt.offTimeMinutes = _parseHHMMtoMin(p_s.autoOff.offTime.time);
		}
		if (p_s.autoOff.offTemp.enabled) {
			autoOffRt.offTempEnabled = true;
			autoOffRt.offTemp        = p_s.autoOff.offTemp.temp;
		}
	}

	// ==================================================
	// AutoOff 조건 체크 (온도는 외부에서 값 전달해야 함)
	//  - 여기서는 Timer/OffTime만 처리 훅 구조
	// ==================================================
	bool _checkAutoOff() {
		unsigned long v_now = millis();

		// 타이머 기반
		if (autoOffRt.timerArmed && autoOffRt.timerMinutes > 0) {
			unsigned long v_elapsedMin =
				(v_now - autoOffRt.timerStartMs) / 60000UL;
			if (v_elapsedMin >= autoOffRt.timerMinutes) {
				CL_D10_Logger::log(EN_L10_LOG_INFO,
					"[CT10] AutoOff by timer (%lu min)",
					(unsigned long)autoOffRt.timerMinutes);
				return true;
			}
		}

		// OffTime 기반 (현지시간 필요)
		if (autoOffRt.offTimeEnabled && autoOffRt.offTimeMinutes < 1440) {
			time_t v_t = time(nullptr);
			struct tm* v_tm = localtime(&v_t);
			if (v_tm) {
				uint16_t v_minNow = (uint16_t)(v_tm->tm_hour * 60 + v_tm->tm_min);
				if (v_minNow >= autoOffRt.offTimeMinutes) {
					CL_D10_Logger::log(EN_L10_LOG_INFO,
						"[CT10] AutoOff by offTime");
					return true;
				}
			}
		}

		// 온도조건은 외부에서 호출 시 파라미터로 평가 권장
		return false;
	}

	// ==================================================
	// Motion 게이팅
	// ==================================================
	bool _isMotionBlocked(const ST_A10_MotionBinding_t& p_motionCfg) {
		if (!motion) return false;
		// 여기서는 단순히 enabled && !present 이면 차단으로 가정
		bool v_needPir = p_motionCfg.pir.enabled;
		bool v_needBle = p_motionCfg.ble.enabled;

		// 실제 구현은 M10 모듈 스펙에 따라 수정
		// 예시: PIR 또는 BLE 중 하나라도 활성 감지면 OK
		bool v_present = false;
		if (!v_needPir && !v_needBle) {
			return false;
		}

		// 외부 CL_M10_MotionLogic 인터페이스 가정:
		//  - bool M10_isMotionPresent();
		//  - bool M10_isBlePresentThreshold(int rssi);
		// 여기선 최소 훅만:
		if (motion) {
			// 전체 presence만 사용 (상세 조건은 M10 내부)
			// true => 사람 있음 => 차단 안 함
			// false => 사람 없음 => 게이팅(차단)
			extern bool M10_isMotionPresentProxy(CL_M10_MotionLogic*); // 필요 시 구현
			if (M10_isMotionPresentProxy) {
				v_present = M10_isMotionPresentProxy(motion);
			}
		}

		return !v_present;
	}

	// ==================================================
	// 스케줄 활성 인덱스 찾기
	// ==================================================
	int _findActiveScheduleIndex(const ST_A10_ScheduleConfig& p_cfg) {
		time_t v_t = time(nullptr);
		struct tm* v_tm = localtime(&v_t);
		if (!v_tm) return -1;

		uint8_t v_wday = (v_tm->tm_wday + 6) % 7; // 필요에 따라 조정
		uint16_t v_minNow = (uint16_t)(v_tm->tm_hour * 60 + v_tm->tm_min);

		for (uint8_t v_i=0; v_i<p_cfg.count; v_i++) {
			const ST_A10_ScheduleItem_t& v_s = p_cfg.items[v_i];
			if (!v_s.enabled) continue;
			if (!v_s.period.enabled) continue;

			if (v_s.period.days[v_wday] == 0) continue;

			int v_st = _parseHHMMtoMin(v_s.period.start_time);
			int v_ed = _parseHHMMtoMin(v_s.period.end_time);
			if (v_st < 0 || v_ed < 0) continue;

			if (v_st <= v_ed) {
				if (v_minNow >= v_st && v_minNow < v_ed) return (int)v_i;
			} else {
				// 야간 랩어라운드 (예: 23:00~07:00)
				if (v_minNow >= v_st || v_minNow < v_ed) return (int)v_i;
			}
		}
		return -1;
	}

	static int _parseHHMMtoMin(const char* p_str) {
		if (!p_str || !p_str[0]) return -1;
		int v_h = 0;
		int v_m = 0;
		const char* v_c = strchr(p_str, ':');
		if (!v_c) return -1;
		v_h = atoi(p_str);
		v_m = atoi(v_c + 1);
		if (v_h < 0 || v_h > 23 || v_m < 0 || v_m > 59) return -1;
		return v_h * 60 + v_m;
	}
};

