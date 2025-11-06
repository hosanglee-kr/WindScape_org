#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : CT10_ControlTask_014.h
 * 모듈약어 : CT10
 * 모듈명 : Smart Nature Wind 실행 엔진 (Schedules / UserProfiles)
 * ------------------------------------------------------
 * 기능 요약
 *  - Schedules 또는 UserProfiles 중 하나를 소스로 실행
 *  - Segment 단위 ON/OFF 사이클 (PRESET / FIXED)
 *  - Motion 게이팅(PIR/BLE)
 *  - AutoOff (timer / offTime / offTemp)
 *  - preset×style×adjust → resolved wind 파라미터 계산(C10 호출)
 *  - Lazy NVS 저장 트리거(N10 훅) 제공
 * ------------------------------------------------------
 * [구현 규칙]
 *  - 단일 헤더(h) 파일만 사용 (cpp 없음)
 *  - ArduinoJson v7.x.x
 *  - JsonDocument 단일 타입만 사용
 *  - createNestedArray/Object/containsKey 사용 금지
 *  - memset + strlcpy 기반 안전 초기화
 *  - 주석/필드명은 JSON 구조와 동일 유지
 * ------------------------------------------------------
 * [코드 네이밍 규칙]
 *   - 전역 상수/매크로 : G_모듈약어_
 *   - 전역 변수       : g_모듈약어_
 *   - 전역 함수       : 모듈약어_ 접두사
 *   - type            : T_모듈약어_
 *   - typedef         : _t 접미사
 *   - enum 상수       : EN_모듈약어_
 *   - 구조체          : ST_모듈약어_
 *   - 클래스명        : CL_모듈약어_
 *   - private 멤버    : _ 접두사
 *   - 멤버(함수/변수) : 모듈약어 접두사 미사용
 *   - 정적 멤버       : s_ 접두사
 *   - 로컬 변수       : v_ 접두사
 *   - 함수 인자       : p_ 접두사
 * ------------------------------------------------------
 */

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ctime>

#include "A10_Const_014.h"          // 타입/상수/글로벌 사전(dict) 선언
#include "C10_ConfigManager_014.h"  // Schedules / UserProfiles 로더 + C10_resolveWindParams()
#include "D10_Logger_011.h"
#include "P10_PWM_ctrl_012.h"
#include "S10_Simulation_014.h"

// 선택적: 모션 로직 전방 선언
class CL_M10_MotionLogic;

/* =======================================================
 * 내부 타입
 * =======================================================*/
typedef enum : uint8_t {
	EN_CT10_SRC_NONE     = 0,
	EN_CT10_SRC_SCHEDULE = 1,
	EN_CT10_SRC_PROFILE  = 2
} EN_CT10_source_type_t;

typedef struct {
	EN_CT10_source_type_t type = EN_CT10_SRC_NONE; // 실행 소스 타입
	int32_t               id   = -1;               // schNo or profileNo
} ST_CT10_source_t;

typedef struct {
	int32_t  index        = -1;  // 현재 segments[index]
	bool     onPhase      = false;
	uint32_t phaseStartMs = 0;   // 현 phase 시작 ms
} ST_CT10_seg_cursor_t;

typedef struct {
	bool     timerEnabled   = false;
	uint32_t startMs        = 0;
	uint32_t durationMs     = 0;
	bool     offTimeEnabled = false;
	uint16_t offHHMM        = 0;     // 0~1439
	bool     offTempEnabled = false;
	float    thresholdC     = 100.0; // 사용시 센서 연동
} ST_CT10_autoOff_rt_t;

typedef struct {
	bool     pirEn   = false;
	uint16_t pirHold = 0;
	bool     bleEn   = false;
	int16_t  rssiTh  = -70;
	uint16_t bleHold = 0;
} ST_CT10_motion_rt_t;

typedef struct {
	bool     enabled     = false;
	uint8_t  days[7]     = {1,1,1,1,1,1,1};
	uint16_t startHHMM   = 0;   // 0~1439
	uint16_t endHHMM     = 1440;
} ST_CT10_period_rt_t;

typedef struct {
	// 상위 상태
	bool                active  = false;
	ST_CT10_source_t    source;
	ST_CT10_seg_cursor_t cursor;

	// 정책 런타임
	ST_CT10_autoOff_rt_t autoOff;
	ST_CT10_motion_rt_t  motion;
	ST_CT10_period_rt_t  period;   // schedule 전용. profile에서는 enabled=false로 둠.

	// 반복 여부 (profile 전용: repeatSegments, schedule은 항상 루프)
	bool repeatSegments = true;
} ST_CT10_state_t;

/* =======================================================
 * 본체 클래스
 * =======================================================*/
class CL_CT10_ControlTask {
public:
	/* 외부 의존 주입 */
	void begin(CL_S10_Simulation& p_sim, CL_P10_PWM& p_pwm) {
		sim = &p_sim;
		pwm = &p_pwm;
		// 초기 안전 정지
		if (pwm) pwm->P10_setDutyPercent(0.0f);
		if (sim) sim->stop();
		state = ST_CT10_state_t{};
		CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] begin");
	}

	void attachMotion(CL_M10_MotionLogic* p_motion) {
		motion = p_motion;
	}

	/* 주기 호출 */
	void tick() {
		if (!pwm || !sim) return;

		// 활성 아니면 아무것도 안 함
		if (!state.active) {
			yield();
			return;
		}

		// AutoOff 체크 (세 가지)
		if (_autoOffTriggered()) {
			_stopAndIdle("autoOff");
			return;
		}

		// Schedule인 경우 Period 체크
		if (state.source.type == EN_CT10_SRC_SCHEDULE) {
			if (!_isNowInPeriodAndDay()) {
				// Period 밖: 팬/시뮬 정지, 세그 포인터 유지(대기)
				if (sim->S10_active) sim->stop();
				pwm->P10_setDutyPercent(0.0f);
				yield();
				return;
			}
		}

		// Motion 게이트
		if (_motionGateActive()) {
			if (sim->S10_active) sim->stop();
			pwm->P10_setDutyPercent(0.0f);
			yield();
			return;
		}

		// 세그먼트가 비어있는 경우 즉시 종료
		if (_segmentCount() == 0) {
			_stopAndIdle("no segments");
			return;
		}

		// 초기 진입 처리
		if (state.cursor.index < 0) {
			state.cursor.index = 0;
			state.cursor.onPhase = true;
			state.cursor.phaseStartMs = millis();
			_applySegmentOn(_segmentAt(state.cursor.index));
			_markDirty();
			yield();
			return;
		}

		// 진행 중인 세그먼트
		const auto& v_seg = _segmentAt(state.cursor.index);
		uint32_t v_now = millis();

		if (state.cursor.onPhase) {
			uint32_t v_onMs  = _clampMinMs(v_seg.on_minutes, 1);
			if (v_now - state.cursor.phaseStartMs >= v_onMs) {
				// ON → OFF
				state.cursor.onPhase = false;
				state.cursor.phaseStartMs = v_now;
				_applySegmentOff();
				_markDirty();
			}
		} else {
			uint32_t v_offMs = _minutesToMs(v_seg.off_minutes);
			if (v_offMs == 0 || v_now - state.cursor.phaseStartMs >= v_offMs) {
				// OFF → 다음 세그먼트 ON
				int32_t v_next = state.cursor.index + 1;
				if (state.source.type == EN_CT10_SRC_PROFILE && !state.repeatSegments) {
					if (v_next >= (int32_t)_segmentCount()) {
						_stopAndIdle("profile end");
						return;
					}
				}
				state.cursor.index = (v_next % (int32_t)_segmentCount());
				state.cursor.onPhase = true;
				state.cursor.phaseStartMs = v_now;
				_applySegmentOn(_segmentAt(state.cursor.index));
				_markDirty();
			}
		}

		yield();
	}

	/* 소스 시작/중지 */
	bool startSchedule(uint16_t p_schNo) {
		if (!g_A10_schedules_loaded) {
			CL_D10_Logger::log(EN_L10_LOG_WARN, "[CT10] schedules not loaded");
			return false;
		}
		int32_t v_idx = C10_findScheduleIndexByNo(g_A10_schedules, p_schNo);
		if (v_idx < 0) {
			CL_D10_Logger::log(EN_L10_LOG_WARN, "[CT10] schedule #%u not found", p_schNo);
			return false;
		}
		_stopSourceIfAny();

		state.active = true;
		state.source.type = EN_CT10_SRC_SCHEDULE;
		state.source.id   = p_schNo;
		state.cursor = ST_CT10_seg_cursor_t{};
		// Period / Motion / AutoOff 설정 캐시
		_cacheSchedulePolicies(g_A10_schedules.items[v_idx]);
		_initAutoOffTimer();

		_markDirty();
		CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] start schedule #%u", p_schNo);
		return true;
	}

	bool startUserProfile(uint16_t p_profileNo) {
		if (!g_A10_userProfiles_loaded) {
			CL_D10_Logger::log(EN_L10_LOG_WARN, "[CT10] userProfiles not loaded");
			return false;
		}
		int32_t v_idx = C10_findUserProfileIndexByNo(g_A10_userProfiles, p_profileNo);
		if (v_idx < 0) {
			CL_D10_Logger::log(EN_L10_LOG_WARN, "[CT10] profile #%u not found", p_profileNo);
			return false;
		}
		_stopSourceIfAny();

		state.active = true;
		state.source.type = EN_CT10_SRC_PROFILE;
		state.source.id   = p_profileNo;
		state.cursor = ST_CT10_seg_cursor_t{};
		_cacheProfilePolicies(g_A10_userProfiles.items[v_idx]);
		_initAutoOffTimer();

		_markDirty();
		CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] start profile #%u", p_profileNo);
		return true;
	}

	void stopActive() {
		if (!state.active) return;
		_stopAndIdle("manual stop");
	}

	/* 상태 질의 */
	bool isRunning() const { return state.active; }
	bool isScheduleActive(uint16_t* p_outNo=nullptr) const {
		if (state.active && state.source.type==EN_CT10_SRC_SCHEDULE) {
			if (p_outNo) *p_outNo = (uint16_t)state.source.id;
		 return true;
		}
		return false;
	}
	bool isUserProfileActive(uint16_t* p_outNo=nullptr) const {
		if (state.active && state.source.type==EN_CT10_SRC_PROFILE) {
			if (p_outNo) *p_outNo = (uint16_t)state.source.id;
		 return true;
		}
		return false;
	}

	/* NVS lazy save 훅 */
	void markStateDirtyExternal() { _markDirty(); }

	/* 상태 JSON (UI/디버깅) */
	void toJson(JsonDocument& p_doc) {
		JsonObject o = p_doc["ct10"].to<JsonObject>();
		o["active"] = state.active;

		JsonObject src = o["source"].to<JsonObject>();
		src["type"] = (int)state.source.type;
		src["id"]   = state.source.id;

		JsonObject cur = o["cursor"].to<JsonObject>();
		cur["index"]       = state.cursor.index;
		cur["onPhase"]     = state.cursor.onPhase;
		cur["phaseStartMs"]= (int32_t)state.cursor.phaseStartMs;

		JsonObject per = o["period"].to<JsonObject>();
		per["enabled"] = state.period.enabled;
		JsonArray days = per["days"].to<JsonArray>();
		for (int i=0;i<7;i++) days.add(state.period.days[i]);
		per["startHHMM"] = (int)state.period.startHHMM;
		per["endHHMM"]   = (int)state.period.endHHMM;

		JsonObject mo = o["motion"].to<JsonObject>();
		mo["pirEn"]   = state.motion.pirEn;
		mo["pirHold"] = state.motion.pirHold;
		mo["bleEn"]   = state.motion.bleEn;
		mo["rssiTh"]  = state.motion.rssiTh;
		mo["bleHold"] = state.motion.bleHold;

		JsonObject ao = o["autoOff"].to<JsonObject>();
		ao["timerEnabled"]   = state.autoOff.timerEnabled;
		ao["durationMs"]     = (int32_t)state.autoOff.durationMs;
		ao["offTimeEnabled"] = state.autoOff.offTimeEnabled;
		ao["offHHMM"]        = (int)state.autoOff.offHHMM;
		ao["offTempEnabled"] = state.autoOff.offTempEnabled;
		ao["thresholdC"]     = state.autoOff.thresholdC;
	}

private:
	/* 의존 객체 */
	CL_S10_Simulation*   sim    = nullptr;
	CL_P10_PWM*          pwm    = nullptr;
	CL_M10_MotionLogic*  motion = nullptr;

	/* 상태 */
	ST_CT10_state_t      state;
	bool                 _dirty = false;     // lazy nvs flag
	uint32_t             _lastCommitMs = 0;  // 외부 N10 타이머가 없을 때 대비(옵션)

	/* 유틸: 시간 변환 */
	static uint16_t _toHHMM(const char* p_hhmm) {
		if (!p_hhmm) return 0;
		int v_h=0, v_m=0;
		sscanf(p_hhmm, "%d:%d", &v_h, &v_m);
		if (v_h<0) v_h=0; if (v_h>23) v_h=23;
		if (v_m<0) v_m=0; if (v_m>59) v_m=59;
		return (uint16_t)(v_h*60 + v_m);
	}
	static uint32_t _minutesToMs(uint32_t p_min) {
		return p_min*60000UL;
	}
	static uint32_t _clampMinMs(uint32_t p_min, uint32_t p_minClamp) {
		uint32_t v = (p_min < p_minClamp)? p_minClamp : p_min;
		return v*60000UL;
	}

	/* 소스별 설정 캐시 */
	void _cacheSchedulePolicies(const ST_A10_Schedule_t& p) {
		// period
		state.period.enabled = p.period.enabled;
		for (int i=0;i<7;i++) state.period.days[i] = p.period.days[i];
		state.period.startHHMM = _toHHMM(p.period.start_time);
		state.period.endHHMM   = _toHHMM(p.period.end_time);

		// motion
		state.motion.pirEn   = p.motion.pir.enabled;
		state.motion.pirHold = p.motion.pir.hold_sec;
		state.motion.bleEn   = p.motion.ble.enabled;
		state.motion.rssiTh  = p.motion.ble.rssi_threshold;
		state.motion.bleHold = p.motion.ble.hold_sec;

		// autoOff
		state.autoOff.timerEnabled   = p.autoOff.timer.enabled;
		state.autoOff.durationMs     = _minutesToMs(p.autoOff.timer.minutes);
		state.autoOff.offTimeEnabled = p.autoOff.offTime.enabled;
		state.autoOff.offHHMM        = _toHHMM(p.autoOff.offTime.time);
		state.autoOff.offTempEnabled = p.autoOff.offTemp.enabled;
		state.autoOff.thresholdC     = p.autoOff.offTemp.temp;

		// repeatSegments: schedule은 항상 true로 루프
		state.repeatSegments = true;
	}

	void _cacheProfilePolicies(const ST_A10_UserProfile_t& p) {
		// profile 은 period 미사용
		state.period = ST_CT10_period_rt_t{};
		state.period.enabled = false;

		// motion
		state.motion.pirEn   = p.motion.pir.enabled;
		state.motion.pirHold = p.motion.pir.hold_sec;
		state.motion.bleEn   = p.motion.ble.enabled;
		state.motion.rssiTh  = p.motion.ble.rssi_threshold;
		state.motion.bleHold = p.motion.ble.hold_sec;

		// autoOff
		state.autoOff.timerEnabled   = p.autoOff.timer.enabled;
		state.autoOff.durationMs     = _minutesToMs(p.autoOff.timer.minutes);
		state.autoOff.offTimeEnabled = p.autoOff.offTime.enabled;
		state.autoOff.offHHMM        = _toHHMM(p.autoOff.offTime.time);
		state.autoOff.offTempEnabled = p.autoOff.offTemp.enabled;
		state.autoOff.thresholdC     = p.autoOff.offTemp.temp;

		// repeatSegments
		state.repeatSegments = p.repeatSegments;
	}

	void _initAutoOffTimer() {
		state.autoOff.startMs = millis();
	}

	/* 현재 소스의 segment 접근 */
	uint32_t _segmentCount() const {
		if (state.source.type == EN_CT10_SRC_SCHEDULE) {
			int32_t v_idx = C10_findScheduleIndexByNo(g_A10_schedules, (uint16_t)state.source.id);
			if (v_idx<0) return 0;
			return g_A10_schedules.items[v_idx].segments.count;
		} else if (state.source.type == EN_CT10_SRC_PROFILE) {
			int32_t v_idx = C10_findUserProfileIndexByNo(g_A10_userProfiles, (uint16_t)state.source.id);
			if (v_idx<0) return 0;
			return g_A10_userProfiles.items[v_idx].segments.count;
		}
		return 0;
	}

	const ST_A10_Segment_t& _segmentAt(uint32_t p_idx) const {
		if (state.source.type == EN_CT10_SRC_SCHEDULE) {
			int32_t v_idx = C10_findScheduleIndexByNo(g_A10_schedules, (uint16_t)state.source.id);
			return g_A10_schedules.items[v_idx].segments.arr[p_idx];
		}
		// PROFILE
		int32_t v_idx = C10_findUserProfileIndexByNo(g_A10_userProfiles, (uint16_t)state.source.id);
		return g_A10_userProfiles.items[v_idx].segments.arr[p_idx];
	}

	/* 세그먼트 적용 */
	void _applySegmentOn(const ST_A10_Segment_t& p_seg) {
		if (p_seg.mode == EN_A10_SEG_MODE_FIXED) {
			// 고정 풍속
			if (sim->S10_active) sim->stop();
			float v_pct = constrain(p_seg.fixed_speed, 0.0f, 100.0f);
			pwm->P10_setDutyPercent(v_pct);
			CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] SEG%d ON FIXED=%.1f%%",
			                   (int)p_seg.segNo, v_pct);
			return;
		}

		// PRESET 모드: resolve → S10에 적용
		ST_A10_ResolvedWind_t v_res{};
		bool v_ok = C10_resolveWindParams(
			g_A10_windDict,
			p_seg.presetCode,
			p_seg.styleCode,
			&p_seg.adjust,
			v_res
		);
		if (!v_ok) {
			// 안전 fallback: OCEAN × BALANCE × no adjust
			ST_A10_AdjustDelta_t v_nil{}; memset(&v_nil, 0, sizeof(v_nil));
			C10_resolveWindParams(g_A10_windDict, "OCEAN", "BALANCE", &v_nil, v_res);
			CL_D10_Logger::log(EN_L10_LOG_WARN, "[CT10] resolve failed, fallback OCEAN/BALANCE");
		}

		sim->S10_setParams(
			v_res.wind_intensity,
			v_res.wind_variability,
			v_res.gust_frequency,
			v_res.min_fan,
			v_res.fan_limit,
			v_res.presetCode
		);
		sim->S10_applyPreset(v_res.presetCode);
		if (!sim->S10_active) sim->begin(*pwm);

		CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] SEG%d ON PRESET=%s STYLE=%s (+adj)",
		                   (int)p_seg.segNo, v_res.presetCode, v_res.styleCode);
	}

	void _applySegmentOff() {
		if (sim->S10_active) sim->stop();
		pwm->P10_setDutyPercent(0.0f);
		CL_D10_Logger::log(EN_L10_LOG_DEBUG, "[CT10] SEG OFF");
	}

	/* Period / Motion / AutoOff */
	bool _isNowInPeriodAndDay() const {
		// period 사용 안하면 항상 true
		if (!state.period.enabled) return true;

		time_t v_t = time(nullptr);
		struct tm* v_tm = localtime(&v_t);
		if (!v_tm) return true;

		int v_wday = v_tm->tm_wday; // 0=일
		if (v_wday<0 || v_wday>6) v_wday=0;
		if (state.period.days[v_wday]==0) return false;

		int v_now = v_tm->tm_hour*60 + v_tm->tm_min;
		uint16_t st = state.period.startHHMM;
		uint16_t ed = state.period.endHHMM;

		if (st <= ed) return (st <= v_now && v_now < ed);
		// 자정 교차
		return (v_now >= st || v_now < ed);
	}

	bool _motionGateActive() const {
		if (!motion) return false;
		// 정책 사용 여부
		if (!state.motion.pirEn && !state.motion.bleEn) return false;
		// 모션 로직에 PIR/BLE hold 내장되어 있다고 가정
		return !motion->M10_isMotionPresent();
	}

	bool _autoOffTriggered() const {
		// timer
		if (state.autoOff.timerEnabled) {
			uint32_t v_now = millis();
			if (v_now - state.autoOff.startMs >= state.autoOff.durationMs) return true;
		}
		// offTime
		if (state.autoOff.offTimeEnabled) {
			time_t v_t = time(nullptr);
			struct tm* v_tm = localtime(&v_t);
			if (v_tm) {
				uint16_t v_now = (uint16_t)(v_tm->tm_hour*60 + v_tm->tm_min);
				if (v_now == state.autoOff.offHHMM) return true;
			}
		}
		// offTemp (센서 연동 시 사용)
		if (state.autoOff.offTempEnabled) {
			// TODO: 외부 센서값 조회 훅이 있다면 연결
			// float v_tempC = ...
			// if (v_tempC >= state.autoOff.thresholdC) return true;
		}
		return false;
	}

	/* 종료/정지 */
	void _stopAndIdle(const char* p_reason) {
		(void)p_reason;
		if (sim->S10_active) sim->stop();
		pwm->P10_setDutyPercent(0.0f);
		state = ST_CT10_state_t{};
		_markDirty();
		CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] stop (%s)", p_reason?p_reason:"");
	}

	void _stopSourceIfAny() {
		if (!state.active) return;
		_stopAndIdle("switch source");
	}

	/* Lazy NVS */
	void _markDirty() {
		_dirty = true;
		// 외부 N10이 폴링/콜백으로 커밋하도록 위임
		// 필요 시 내부 주기로 간단 커밋 트리거:
		uint32_t v_now = millis();
		if (v_now - _lastCommitMs > 3000 && _dirty) {
			_lastCommitMs = v_now;
			// N10_commitState(state) 같은 외부 API가 있다면 여기서 호출 가능 (현재는 훅만)
			// 예: CL_N10_Manager::get().commitIfDirty(state);
			_dirty = false;
		}
	}
};
