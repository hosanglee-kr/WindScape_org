#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : CT10_ControlManager_015.h
 * 모듈약어 : CT10
 * 모듈명 : Smart Nature Wind 통합 제어 Manager (v014)
 * ------------------------------------------------------
 * 기능 요약
 *  - WindProfile Dict (preset/style) + Schedules + UserProfiles 기반 통합 제어
 *  - 스케줄 자동 운전:
 *      - period(요일+시간) 내에서 segments 순환
 *      - segment별 PRESET(FROM presetCode/styleCode/adjust) / FIXED 속도 / OFF
 *  - 유저 프로파일 운전:
 *      - 사전 정의 profileNo 실행
 *      - segments 시퀀스, repeatSegments, autoOff 적용
 *  - Override:
 *      - 고정 속도 강제
 *      - presetCode+styleCode+adjust 기반 바람 패턴 강제
 *      - 지정 시간 경과 시 자동 해제, 기존 모드 복귀
 *  - Motion 게이팅(PIR/BLE):
 *      - 스케줄/프로파일 각각에 대해 motion 설정 반영
 *  - S10_Simulation_014 + P10_PWM_ctrl_014 연동
 *  - Web / 버튼 등 외부에서 제어 가능한 단일 Control 엔진
 * ------------------------------------------------------
 * [구현 규칙]
 *  - 항상 소스 시작 주석 부분 체계 유지 및 내용 업데이트
 *  - 소스 시작 주석 부분 구현규칙, 코드네이밍규칙 내용 그대로 유지, 수정금지
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
#include <ctime>
#include <cstring>

#include "A10_Const_014.h"
#include "C10_ConfigManager_014.h"
#include "D10_Logger_011.h"
#include "P10_PWM_ctrl_014.h"
#include "S10_Simulation_014.h"
#include "M10_MotionLogic_014.h"  // 모션 게이트 (PIR+BLE)

// ------------------------------------------------------
// Override enum & 구조체
// ------------------------------------------------------
typedef enum : uint8_t {
	EN_CT10_OVERRIDE_NONE   = 0,
	EN_CT10_OVERRIDE_FIXED  = 1,
	EN_CT10_OVERRIDE_PRESET = 2
} EN_CT10_override_mode_t;

typedef struct {
	bool					active        = false;
	unsigned long			untilSec      = 0;     // epoch-sec 기준(또는 millis/1000 기준)
	EN_CT10_override_mode_t overrideMode  = EN_CT10_OVERRIDE_NONE;
	float					fixedPercent  = 0.0f;  // 0~100
	char					presetCode[24] = {0};
	char					styleCode[24]  = {0};
	ST_A10_AdjustDelta_t	adjust;
} ST_CT10_overrideState_t;

// ------------------------------------------------------
// 런 모드 (내부 상태용)
// ------------------------------------------------------
typedef enum : uint8_t {
	EN_CT10_RUN_NONE      = 0,
	EN_CT10_RUN_SCHEDULE  = 1,
	EN_CT10_RUN_PROFILE   = 2
} EN_CT10_run_mode_t;

// ------------------------------------------------------
// CT10 Control Manager
// ------------------------------------------------------
class CL_CT10_ControlManager {
public:
	// 외부에서 보는 상태
	bool				active     = false;
	EN_CT10_run_mode_t runMode    = EN_CT10_RUN_NONE;

	// 현재 실행중인 스케줄/프로파일 식별
	int16_t				curScheduleIndex = -1;
	int16_t				curProfileIndex  = -1;

	// 현재 세그먼트 상태
	int16_t				curSegmentIndex  = -1;
	bool				segOnPhase       = false;
	unsigned long		segPhaseStartMs  = 0;

	// AutoOff 기준 시작 시각 (profile/schedule 공통 사용)
	unsigned long		runStartMs       = 0;

	// Override 상태
	ST_CT10_overrideState_t overrideState;

public:
	// --------------------------------------------------
	// 초기화
	// --------------------------------------------------
	void begin(CL_S10_Simulation_014& p_sim,
			   CL_P10_PWM&             p_pwm,
			   CL_M10_MotionLogic*     p_motion = nullptr)
	{
		sim    = &p_sim;
		pwm    = &p_pwm;
		motion = p_motion;

		const ST_A10_WindProfileDict_t* v_dict = nullptr;
		const ST_A10_SchedulesConfig_t* v_sch  = nullptr;
		const ST_A10_UserProfilesConfig_t* v_prof = nullptr;

		CL_C10_ConfigManager_014::C10_getWindProfileDict(v_dict);
		CL_C10_ConfigManager_014::C10_getSchedulesConfig(v_sch);
		CL_C10_ConfigManager_014::C10_getUserProfilesConfig(v_prof);

		windDict     = v_dict;
		schedulesCfg = v_sch;
		profilesCfg  = v_prof;

		runMode          = EN_CT10_RUN_NONE;
		curScheduleIndex = -1;
		curProfileIndex  = -1;
		curSegmentIndex  = -1;
		segOnPhase       = false;
		segPhaseStartMs  = 0;
		runStartMs       = 0;
		overrideState    = ST_CT10_overrideState_t{};
		active           = true;

		if (!sim || !pwm || !windDict) {
			active = false;
			CL_D10_Logger::log(EN_L10_LOG_ERROR,
				"[CT10] begin failed (sim=%p pwm=%p dict=%p)",
				(void*)sim,(void*)pwm,(void*)windDict);
			return;
		}

		CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] begin ok");
	}

	void setMotion(CL_M10_MotionLogic* p_motion) {
		motion = p_motion;
	}

	// --------------------------------------------------
	// 주기 호출 (loop)
	// --------------------------------------------------
	void tick() {
		if (!active || !sim || !pwm) return;

		unsigned long v_nowMs  = millis();
		unsigned long v_nowSec = v_nowMs / 1000UL;

		// 1) Override 최우선
		if (overrideState.active) {
			if (overrideState.untilSec > 0 && v_nowSec >= overrideState.untilSec) {
				// Override 자동 해제
				overrideState = ST_CT10_overrideState_t{};
				CL_D10_Logger::log(EN_L10_LOG_INFO,"[CT10] override expired");
				// override 해제 후 기존 runMode 로직 재적용
				_resetSegmentState();
			} else {
				_applyOverride();
				sim->tick();
				return;
			}
		}

		// 2) Motion 게이트 체크 (schedule/profile별 설정은 각각 처리 내부에서)
		if (_motionGateStopRequired()) {
			_stopOutputOnly();
			sim->tick();
			return;
		}

		// 3) runMode에 따라 동작
		switch (runMode) {
			case EN_CT10_RUN_SCHEDULE:
				_tickSchedule(v_nowMs);
				break;
			case EN_CT10_RUN_PROFILE:
				_tickProfile(v_nowMs);
				break;
			case EN_CT10_RUN_NONE:
			default:
				_stopOutputOnly();
				break;
		}

		// 4) 시뮬레이션 tick
		sim->tick();
	}

	// --------------------------------------------------
	// 외부 제어 API
	// --------------------------------------------------

	// 스케줄 자동모드 시작 (시간/요일 기반 자동 선택)
	// - 기존 profile/override 상태 해제
	void startScheduleAuto() {
		if (!schedulesCfg || schedulesCfg->count == 0) return;

		CL_D10_Logger::log(EN_L10_LOG_INFO,"[CT10] startScheduleAuto");
		_clearOverride();
		runMode          = EN_CT10_RUN_SCHEDULE;
		curProfileIndex  = -1;
		_resetSegmentState();
		runStartMs       = millis();
	}

	// 특정 profileNo 실행 (유저 프로파일 모드)
	bool startUserProfile(uint16_t p_profileNo) {
		if (!profilesCfg || profilesCfg->count == 0) return false;

		int16_t v_idx = _findProfileIndexByNo(p_profileNo);
		if (v_idx < 0) return false;

		const ST_A10_UserProfile_t& v_prof = profilesCfg->profiles[v_idx];
		if (!v_prof.enabled || v_prof.segmentCount == 0) return false;

		CL_D10_Logger::log(EN_L10_LOG_INFO,"[CT10] startUserProfile #%u (%s)",
			(unsigned)p_profileNo, v_prof.name);

		_clearOverride();
		runMode          = EN_CT10_RUN_PROFILE;
		curProfileIndex  = v_idx;
		curScheduleIndex = -1;
		_resetSegmentState();
		runStartMs       = millis();

		return true;
	}

	// 모든 동작 정지 (Idle)
	void stopAll() {
		CL_D10_Logger::log(EN_L10_LOG_INFO,"[CT10] stopAll");
		runMode          = EN_CT10_RUN_NONE;
		curScheduleIndex = -1;
		curProfileIndex  = -1;
		_resetSegmentState();
		_clearOverride();
		if (sim) sim->stop();
		if (pwm) pwm->setDutyPercent(0.0f);
	}

	// Override: 고정 풍속 (0~100%), p_seconds 동안 유지
	void overrideFixed(float p_percent, uint32_t p_seconds) {
		if (!pwm || !sim) return;

		_clearOverride();
		overrideState.active       = true;
		overrideState.overrideMode = EN_CT10_OVERRIDE_FIXED;
		overrideState.fixedPercent = constrain(p_percent, 0.0f, 100.0f);
		overrideState.untilSec     = (p_seconds > 0)
			? (millis()/1000UL + p_seconds)
			: 0;

		CL_D10_Logger::log(EN_L10_LOG_INFO,
			"[CT10] override FIXED=%.1f%% dur=%lus",
			overrideState.fixedPercent,
			(unsigned long)p_seconds);

		_applyOverride();
	}

	// Override: presetCode + styleCode + adjust 기반 패턴 강제
	void overridePreset(const char* p_presetCode,
						const char* p_styleCode,
						const ST_A10_AdjustDelta_t* p_adj,
						uint32_t p_seconds)
	{
		if (!sim || !pwm || !windDict) return;

		ST_A10_ResolvedWind_t v_resolved;
		if (!CL_C10_ConfigManager_014::C10_resolveWindParams(
				*windDict,
				p_presetCode,
				p_styleCode,
				p_adj,
				v_resolved))
		{
			CL_D10_Logger::log(EN_L10_LOG_WARN,
				"[CT10] overridePreset resolve failed (%s,%s)",
				p_presetCode ? p_presetCode:"", p_styleCode?p_styleCode:"");
			return;
		}

		_clearOverride();
		overrideState.active       = true;
		overrideState.overrideMode = EN_CT10_OVERRIDE_PRESET;
		overrideState.untilSec     = (p_seconds > 0)
			? (millis()/1000UL + p_seconds)
			: 0;

		memset(&overrideState.adjust, 0, sizeof(overrideState.adjust));
		if (p_presetCode) strlcpy(overrideState.presetCode, p_presetCode, sizeof(overrideState.presetCode));
		if (p_styleCode)  strlcpy(overrideState.styleCode,  p_styleCode,  sizeof(overrideState.styleCode));
		if (p_adj)        overrideState.adjust = *p_adj;

		CL_D10_Logger::log(EN_L10_LOG_INFO,
			"[CT10] override PRESET (%s,%s) dur=%lus",
			overrideState.presetCode,
			overrideState.styleCode,
			(unsigned long)p_seconds);

		// 즉시 적용
		sim->applyResolvedWind(v_resolved);
	}

	// Override 해제 (기존 모드로 복귀)
	void releaseOverride() {
		if (!overrideState.active) return;
		CL_D10_Logger::log(EN_L10_LOG_INFO,"[CT10] override released");
		_clearOverride();
		_resetSegmentState();
	}

	// --------------------------------------------------
	// 상태 JSON Export (Web/API용)
	// --------------------------------------------------
	void toJson(JsonDocument& p_doc) {
		JsonObject o = p_doc["control"].to<JsonObject>();
		o["active"]  = active;
		o["runMode"] = (int)runMode;

		JsonObject ov = o["override"].to<JsonObject>();
		ov["active"] = overrideState.active;
		ov["mode"]   = (int)overrideState.overrideMode;
		if (overrideState.active && overrideState.untilSec > 0) {
			unsigned long v_now = millis()/1000UL;
			int32_t v_rem = (int32_t)(overrideState.untilSec - v_now);
			if (v_rem < 0) v_rem = 0;
			ov["remainSec"] = v_rem;
		} else {
			ov["remainSec"] = 0;
		}
		if (overrideState.overrideMode == EN_CT10_OVERRIDE_FIXED) {
			ov["fixedPercent"] = overrideState.fixedPercent;
		} else if (overrideState.overrideMode == EN_CT10_OVERRIDE_PRESET) {
			ov["presetCode"] = overrideState.presetCode;
			ov["styleCode"]  = overrideState.styleCode;
		}

		JsonObject run = o["run"].to<JsonObject>();
		run["scheduleIndex"] = curScheduleIndex;
		run["profileIndex"]  = curProfileIndex;
		run["segmentIndex"]  = curSegmentIndex;
		run["onPhase"]       = segOnPhase;
	}

private:
	// 의존 모듈 포인터
	CL_S10_Simulation_014*      sim         = nullptr;
	CL_P10_PWM*                 pwm         = nullptr;
	CL_M10_MotionLogic*         motion      = nullptr;
	const ST_A10_WindProfileDict_t*  windDict     = nullptr;
	const ST_A10_SchedulesConfig_t*  schedulesCfg = nullptr;
	const ST_A10_UserProfilesConfig_t* profilesCfg = nullptr;

	// --------------------------------------------------
	// 내부 헬퍼: 공통
	// --------------------------------------------------
	void _stopOutputOnly() {
		if (pwm) pwm->setDutyPercent(0.0f);
		if (sim) sim->setFanPowerEnabled(false);
	}

	void _resetSegmentState() {
		curSegmentIndex = -1;
		segOnPhase      = false;
		segPhaseStartMs = 0;
		runStartMs      = millis();
		if (sim) sim->setFanPowerEnabled(true);
	}

	void _clearOverride() {
		overrideState = ST_CT10_overrideState_t{};
	}

	bool _motionGateStopRequired() {
		if (!motion) return false;

		// runMode별로 motion 설정 선택
		if (runMode == EN_CT10_RUN_SCHEDULE && schedulesCfg && curScheduleIndex >= 0) {
			if ((uint16_t)curScheduleIndex < schedulesCfg->count) {
				const ST_A10_ScheduleItem_t& v_s =
					schedulesCfg->schedules[curScheduleIndex];
				if (!v_s.motion.pir.enabled && !v_s.motion.ble.enabled) {
					return false;
				}
				return !motion->M10_isMotionPresent();
			}
		}

		if (runMode == EN_CT10_RUN_PROFILE && profilesCfg && curProfileIndex >= 0) {
			if ((uint16_t)curProfileIndex < profilesCfg->count) {
				const ST_A10_UserProfile_t& v_p =
					profilesCfg->profiles[curProfileIndex];
				if (!v_p.motion.pir.enabled && !v_p.motion.ble.enabled) {
					return false;
				}
				return !motion->M10_isMotionPresent();
			}
		}

		return false;
	}

	// --------------------------------------------------
	// 스케줄 모드
	// --------------------------------------------------
	void _tickSchedule(unsigned long p_nowMs) {
		if (!schedulesCfg || schedulesCfg->count == 0 || !windDict) {
			stopAll();
			return;
		}

		// 활성 스케줄 선택 (period.enabled 기준)
		int16_t v_idx = _findActiveScheduleIndexByTime();
		if (v_idx < 0) {
			// 현재 유효 스케줄 없음 → 출력만 정지
			_stopOutputOnly();
			curScheduleIndex = -1;
			curSegmentIndex  = -1;
			return;
		}

		if (curScheduleIndex != v_idx) {
			curScheduleIndex = v_idx;
			curSegmentIndex  = -1;
			segOnPhase       = false;
			segPhaseStartMs  = p_nowMs;
			runStartMs       = p_nowMs;
			CL_D10_Logger::log(EN_L10_LOG_INFO,
				"[CT10] schedule #%d selected", (int)v_idx);
		}

		const ST_A10_ScheduleItem_t& v_s =
			schedulesCfg->schedules[curScheduleIndex];

		if (v_s.segmentCount == 0) {
			_stopOutputOnly();
			return;
		}

		// AutoOffTimer
		if (v_s.autoOffTimer.enabled && v_s.autoOffTimer.minutes > 0) {
			unsigned long v_elapsedMin =
				(p_nowMs - runStartMs) / 60000UL;
			if (v_elapsedMin >= v_s.autoOffTimer.minutes) {
				CL_D10_Logger::log(EN_L10_LOG_INFO,
					"[CT10] schedule autoOff");
				stopAll();
				return;
			}
		}

		// 세그먼트 진입 초기화
		if (curSegmentIndex < 0) {
			curSegmentIndex = 0;
			segOnPhase      = true;
			segPhaseStartMs = p_nowMs;
			_applyScheduleSegmentOn(v_s.segments[curSegmentIndex]);
			return;
		}

		const ST_A10_ScheduleSegment_t& v_seg =
			v_s.segments[curSegmentIndex];

		uint32_t v_onMs  = (v_seg.on_minutes  > 0 ? v_seg.on_minutes  : 1) * 60000UL;
		uint32_t v_offMs = (v_seg.off_minutes > 0 ? v_seg.off_minutes : 0) * 60000UL;

		if (segOnPhase) {
			if (p_nowMs - segPhaseStartMs >= v_onMs) {
				segOnPhase      = false;
				segPhaseStartMs = p_nowMs;
				_applySegmentOff();
			}
		} else {
			if (p_nowMs - segPhaseStartMs >= v_offMs) {
				// 다음 세그먼트
				curSegmentIndex++;
				if (curSegmentIndex >= (int16_t)v_s.segmentCount) {
					curSegmentIndex = 0; // 루프
				}
				segOnPhase      = true;
				segPhaseStartMs = p_nowMs;
				_applyScheduleSegmentOn(v_s.segments[curSegmentIndex]);
			}
		}
	}

	void _applyScheduleSegmentOn(const ST_A10_ScheduleSegment_t& p_seg) {
		if (!sim || !pwm || !windDict) return;

		// OFF 모드 지원 (mode == "OFF" 또는 on_minutes == 0 사용 가능)
		if (strcasecmp(p_seg.mode, "OFF") == 0) {
			_applySegmentOff();
			return;
		}

		if (strcasecmp(p_seg.mode, "FIXED") == 0) {
			sim->stop();
			float v = constrain(p_seg.fixed_speed, 0.0f, 100.0f);
			pwm->setDutyPercent(v);
			return;
		}

		// PRESET 모드
		if (strcasecmp(p_seg.mode, "PRESET") == 0) {
			ST_A10_AdjustDelta_t v_adj;
			memset(&v_adj, 0, sizeof(v_adj));
			if (p_seg.hasAdjust) {
				v_adj = p_seg.adjust;
			}

			ST_A10_ResolvedWind_t v_resolved;
			if (!CL_C10_ConfigManager_014::C10_resolveWindParams(
					*windDict,
					p_seg.presetCode,
					p_seg.styleCode,
					p_seg.hasAdjust ? &v_adj : nullptr,
					v_resolved))
			{
				CL_D10_Logger::log(EN_L10_LOG_WARN,
					"[CT10] schedule seg resolve failed (%s,%s)",
					p_seg.presetCode, p_seg.styleCode);
				_applySegmentOff();
				return;
			}

			sim->applyResolvedWind(v_resolved);
			return;
		}

		// 정의되지 않은 mode → 안전하게 OFF
		_applySegmentOff();
	}

	void _applySegmentOff() {
		if (sim) sim->stop();
		if (pwm) pwm->setDutyPercent(0.0f);
	}

	int16_t _findActiveScheduleIndexByTime() {
		if (!schedulesCfg || schedulesCfg->count == 0) return -1;

		time_t v_t = time(nullptr);
		struct tm* v_tm = localtime(&v_t);
		if (!v_tm) return -1;

		int v_wday   = v_tm->tm_wday;               // 0=Sun
		int v_nowMin = v_tm->tm_hour*60 + v_tm->tm_min;

		for (uint16_t i=0; i<schedulesCfg->count; i++) {
			const ST_A10_ScheduleItem_t& s = schedulesCfg->schedules[i];
			if (!s.enabled || !s.period.enabled) continue;

			if (v_wday < 0 || v_wday > 6) continue;
			if (s.period.days[v_wday] == 0) continue;

			int st = _parseHHMM(s.period.start_time);
			int ed = _parseHHMM(s.period.end_time);
			if (st < 0 || ed < 0) continue;

			// 일반 구간
			if (st <= ed) {
				if (v_nowMin >= st && v_nowMin < ed) return (int16_t)i;
			} else {
				// 자정 넘김 구간
				if (v_nowMin >= st || v_nowMin < ed) return (int16_t)i;
			}
		}
		return -1;
	}

	// --------------------------------------------------
	// 프로파일 모드
	// --------------------------------------------------
	void _tickProfile(unsigned long p_nowMs) {
		if (!profilesCfg || profilesCfg->count == 0 ||
			curProfileIndex < 0 ||
			(uint16_t)curProfileIndex >= profilesCfg->count ||
			!windDict)
		{
			stopAll();
			return;
		}

		const ST_A10_UserProfile_t& v_p =
			profilesCfg->profiles[curProfileIndex];

		if (!v_p.enabled || v_p.segmentCount == 0) {
			stopAll();
			return;
		}

		// autoOff.timer
		if (v_p.autoOff.timer.enabled &&
			v_p.autoOff.timer.minutes > 0)
		{
			unsigned long v_elapsedMin =
				(p_nowMs - runStartMs)/60000UL;
			if (v_elapsedMin >= v_p.autoOff.timer.minutes) {
				CL_D10_Logger::log(EN_L10_LOG_INFO,
					"[CT10] profile autoOff (timer)");
				stopAll();
				return;
			}
		}

		// (주의) autoOff.offTime / offTemp 는 필요 시 확장 처리

		// 세그먼트 초기 진입
		if (curSegmentIndex < 0) {
			curSegmentIndex = 0;
			segOnPhase      = true;
			segPhaseStartMs = p_nowMs;
			_applyProfileSegmentOn(v_p.segments[curSegmentIndex]);
			return;
		}

		const ST_A10_UserProfileSegment_t& v_seg =
			v_p.segments[curSegmentIndex];

		uint32_t v_onMs  = (v_seg.on_minutes  > 0 ? v_seg.on_minutes  : 1) * 60000UL;
		uint32_t v_offMs = (v_seg.off_minutes > 0 ? v_seg.off_minutes : 0) * 60000UL;

		if (segOnPhase) {
			if (p_nowMs - segPhaseStartMs >= v_onMs) {
				segOnPhase      = false;
				segPhaseStartMs = p_nowMs;
				_applySegmentOff();
			}
		} else {
			if (p_nowMs - segPhaseStartMs >= v_offMs) {
				// 다음 세그먼트
				curSegmentIndex++;
				if (curSegmentIndex >= (int16_t)v_p.segmentCount) {
					if (v_p.repeatSegments) {
						curSegmentIndex = 0;
					} else {
						CL_D10_Logger::log(EN_L10_LOG_INFO,
							"[CT10] profile complete");
						stopAll();
						return;
					}
				}
				segOnPhase      = true;
				segPhaseStartMs = p_nowMs;
				_applyProfileSegmentOn(v_p.segments[curSegmentIndex]);
			}
		}
	}

	void _applyProfileSegmentOn(const ST_A10_UserProfileSegment_t& p_seg) {
		if (!sim || !pwm || !windDict) return;

		if (strcasecmp(p_seg.mode, "OFF") == 0) {
			_applySegmentOff();
			return;
		}

		if (strcasecmp(p_seg.mode, "FIXED") == 0) {
			sim->stop();
			float v = constrain(p_seg.fixed_speed, 0.0f, 100.0f);
			pwm->setDutyPercent(v);
			return;
		}

		if (strcasecmp(p_seg.mode, "PRESET") == 0) {
			ST_A10_AdjustDelta_t v_adj;
			memset(&v_adj, 0, sizeof(v_adj));
			if (p_seg.hasAdjust) {
				v_adj = p_seg.adjust;
			}

			ST_A10_ResolvedWind_t v_resolved;
			if (!CL_C10_ConfigManager_014::C10_resolveWindParams(
					*windDict,
					p_seg.presetCode,
					p_seg.styleCode,
					p_seg.hasAdjust ? &v_adj : nullptr,
					v_resolved))
			{
				CL_D10_Logger::log(EN_L10_LOG_WARN,
					"[CT10] profile seg resolve failed (%s,%s)",
					p_seg.presetCode, p_seg.styleCode);
				_applySegmentOff();
				return;
			}

			sim->applyResolvedWind(v_resolved);
			return;
		}

		_applySegmentOff();
	}

	// --------------------------------------------------
	// Override 적용
	// --------------------------------------------------
	void _applyOverride() {
		if (!sim || !pwm || !overrideState.active) return;

		if (overrideState.overrideMode == EN_CT10_OVERRIDE_FIXED) {
			sim->stop();
			float v = constrain(overrideState.fixedPercent, 0.0f, 100.0f);
			pwm->setDutyPercent(v);
			return;
		}

		if (overrideState.overrideMode == EN_CT10_OVERRIDE_PRESET) {
			if (!windDict) return;

			ST_A10_ResolvedWind_t v_resolved;
			if (!CL_C10_ConfigManager_014::C10_resolveWindParams(
					*windDict,
					overrideState.presetCode,
					overrideState.styleCode,
					&overrideState.adjust,
					v_resolved))
			{
				return;
			}
			sim->applyResolvedWind(v_resolved);
		}
	}

	// --------------------------------------------------
	// 공통 유틸
	// --------------------------------------------------
	int16_t _findProfileIndexByNo(uint16_t p_no) const {
		if (!profilesCfg) return -1;
		for (uint16_t i=0; i<profilesCfg->count; i++) {
			if (profilesCfg->profiles[i].profileNo == p_no)
				return (int16_t)i;
		}
		return -1;
	}

	static int _parseHHMM(const char* p) {
		if (!p) return -1;
		int v_len = strlen(p);
		if (v_len < 3) return -1;
		const char* v_col = strchr(p, ':');
		if (!v_col) return -1;
		int v_h = atoi(p);
		int v_m = atoi(v_col+1);
		if (v_h < 0 || v_h > 23 || v_m < 0 || v_m > 59) return -1;
		return v_h*60 + v_m;
	}
};
