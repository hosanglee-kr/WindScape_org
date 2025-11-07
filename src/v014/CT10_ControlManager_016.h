#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : CT10_ControlManager_016.h
 * 모듈 약어 : CT10
 * 모듈명 : Smart Nature Wind Control Manager (v016 Extended)
 * ------------------------------------------------------
 * 기능 요약:
 *  - Schedule / UserProfile / Motion / AutoOff / Simulation Chart 통합 제어
 *  - Preset × Style × Adjust → Wind Profile 계산 및 PWM/Simulation 반영
 *  - WebAPI/Manual Override/NVS 상태관리 포함
 * ------------------------------------------------------
 * [구현 규칙]
 *  - ArduinoJson v7.x.x 사용
 *  - JsonDocument 단일 타입만 사용
 *  - createNestedArray/Object/containsKey 사용 금지
 *  - memset + strlcpy 기반 안전 초기화
 *  - 모듈별 단일 헤더(h)파일 구성 (cpp 없음)
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
 * ------------------------------------------------------
 */

#include <Arduino.h>
#include "A10_Const_014.h"
#include "C10_ConfigManager_015.h"
#include "S10_Simulation_014.h"
#include "P10_PWM_ctrl_014.h"
#include "M10_MotionLogic_014.h"
#include "N10_NvsManager_014.h"
#include "D10_Logger_011.h"

class CL_CT10_ControlManager {
private:
	static ST_A10_WindProfileDict_t    s_windDict;
	static ST_A10_SchedulesConfig_t    s_schedCfg;
	static ST_A10_UserProfilesConfig_t s_userCfg;

	static ST_A10_ScheduleSegment_t    s_activeSeg;
	static ST_A10_ResolvedWind_t       s_currentWind;
	static ST_A10_ResolvedWind_t       s_prevWind;

	static uint32_t                    s_lastApplyMs;
	static uint32_t                    s_autoOffStartMs;
	static bool                        s_autoOffTriggered;
	static bool                        s_overrideActive;
	static bool                        s_motionHold;
	static uint8_t                     s_mode; // 0:schedule / 1:userProfile

public:
	// ==================================================
	// 초기화
	// ==================================================
	static bool begin() {
		memset(&s_windDict, 0, sizeof(s_windDict));
		memset(&s_schedCfg, 0, sizeof(s_schedCfg));
		memset(&s_userCfg, 0, sizeof(s_userCfg));
		memset(&s_activeSeg, 0, sizeof(s_activeSeg));
		memset(&s_currentWind, 0, sizeof(s_currentWind));
		memset(&s_prevWind, 0, sizeof(s_prevWind));

		bool ok = true;
		ok &= CL_C10_ConfigManager::loadWindProfileDict(s_windDict);
		ok &= CL_C10_ConfigManager::loadSchedules(s_schedCfg);
		ok &= CL_C10_ConfigManager::loadUserProfiles(s_userCfg);

		s_lastApplyMs      = millis();
		s_autoOffStartMs   = millis();
		s_autoOffTriggered = false;
		s_overrideActive   = false;
		s_motionHold       = false;
		s_mode             = 0;

		CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] Initialized (v016 extended)");
		return ok;
	}

	// ==================================================
	// 주기 실행
	// ==================================================
	static void tick(uint32_t nowMs, const char* timeStr, float tempC) {
		if (s_overrideActive) return;

		// motion check
		bool motionActive = CL_M10_MotionLogic::isMotionActive();
		if (!motionActive && !s_motionHold) {
			P10_PWM_ctrl::setDuty(0);
			return;
		}
		else if (motionActive) {
			s_motionHold = true;
		}

		// autoOff check
		if (checkAutoOff(nowMs, timeStr, tempC)) {
			P10_PWM_ctrl::setDuty(0);
			return;
		}

		// schedule or user profile 기반 운전
		if (s_mode == 0) runScheduleMode(nowMs, timeStr);
		else             runUserProfileMode(nowMs);
	}

	// ==================================================
	// 모드 선택
	// ==================================================
	static void setMode(bool useProfile) {
		s_mode = useProfile ? 1 : 0;
		CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] Mode set: %s",
			s_mode ? "UserProfile" : "Schedule");
	}

	// ==================================================
	// Manual Override
	// ==================================================
	static void applyManual(const ST_A10_ResolvedWind_t& w) {
		memcpy(&s_currentWind, &w, sizeof(w));
		s_overrideActive = true;
		applyResolvedWind(s_currentWind);
		N10_NvsManager::markDirty("override", true);
		CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] Manual override applied");
	}

	static void clearManual() {
		s_overrideActive = false;
		N10_NvsManager::markDirty("override", false);
	}

private:
	// ==================================================
	// Schedule 모드
	// ==================================================
	static void runScheduleMode(uint32_t nowMs, const char* timeStr) {
		const ST_A10_ScheduleItem_t* active = findActiveSchedule(timeStr);
		if (!active) return;

		const ST_A10_ScheduleSegment_t* seg = selectSegment(*active, nowMs);
		if (!seg) return;

		if (memcmp(&s_activeSeg, seg, sizeof(s_activeSeg)) != 0) {
			memcpy(&s_activeSeg, seg, sizeof(s_activeSeg));
			applySegment(s_activeSeg);
		}

		if (nowMs - s_lastApplyMs > 30000) {
			s_lastApplyMs = nowMs;
			applySegment(s_activeSeg);
		}
	}

	static const ST_A10_ScheduleItem_t* findActiveSchedule(const char* timeStr) {
		for (uint8_t i = 0; i < s_schedCfg.count; i++) {
			const auto& v = s_schedCfg.items[i];
			if (!v.enabled) continue;
			if (isWithinPeriod(v.period, timeStr)) return &v;
		}
		return nullptr;
	}

	static bool isWithinPeriod(const ST_A10_Period_t& p, const char* timeStr) {
		if (!p.enabled) return false;
		uint16_t nowMin = parseTimeToMin(timeStr);
		uint16_t s = parseTimeToMin(p.start_time);
		uint16_t e = parseTimeToMin(p.end_time);
		if (s <= e) return (nowMin >= s && nowMin <= e);
		return (nowMin >= s || nowMin <= e);
	}

	static uint16_t parseTimeToMin(const char* t) {
		uint16_t h=0,m=0;
		if (t && sscanf(t,"%2hu:%2hu",&h,&m)==2) return h*60+m;
		return 0;
	}

	static const ST_A10_ScheduleSegment_t* selectSegment(const ST_A10_ScheduleItem_t& p, uint32_t nowMs) {
		if (p.segmentCount==0) return nullptr;
		uint32_t total=0;
		for (uint8_t i=0;i<p.segmentCount;i++)
			total += (p.segments[i].on_minutes+p.segments[i].off_minutes)*60000UL;

		uint32_t pos = nowMs % total;
		uint32_t acc=0;
		for (uint8_t i=0;i<p.segmentCount;i++) {
			const auto& s=p.segments[i];
			acc+=s.on_minutes*60000UL;
			if (pos<acc) return &s;
			acc+=s.off_minutes*60000UL;
		}
		return &p.segments[0];
	}

	// ==================================================
	// UserProfile 모드
	// ==================================================
	static void runUserProfileMode(uint32_t nowMs) {
		for (uint8_t i=0;i<s_userCfg.count;i++) {
			const auto& profile = s_userCfg.items[i];
			if (!profile.enabled) continue;

			const ST_A10_ScheduleSegment_t* seg = selectUserSegment(profile, nowMs);
			if (!seg) continue;

			if (memcmp(&s_activeSeg, seg, sizeof(s_activeSeg)) != 0) {
				memcpy(&s_activeSeg, seg, sizeof(s_activeSeg));
				applySegment(s_activeSeg);
			}
			break;
		}
	}

	static const ST_A10_ScheduleSegment_t* selectUserSegment(const ST_A10_UserProfile_t& p, uint32_t nowMs) {
		if (p.segmentCount==0) return nullptr;
		uint32_t total=0;
		for (uint8_t i=0;i<p.segmentCount;i++)
			total += (p.segments[i].on_minutes+p.segments[i].off_minutes)*60000UL;

		uint32_t pos = nowMs % total;
		uint32_t acc=0;
		for (uint8_t i=0;i<p.segmentCount;i++) {
			const auto& s=p.segments[i];
			acc+=s.on_minutes*60000UL;
			if (pos<acc) return &s;
			acc+=s.off_minutes*60000UL;
		}
		return &p.segments[0];
	}

	// ==================================================
	// AutoOff 체크
	// ==================================================
	static bool checkAutoOff(uint32_t nowMs, const char* timeStr, float tempC) {
		const auto& ao = s_activeSeg.autoOff;
		if (ao.timer.enabled && !s_autoOffTriggered) {
			if ((nowMs - s_autoOffStartMs) > (ao.timer.minutes*60000UL)) {
				s_autoOffTriggered=true;
				P10_PWM_ctrl::setDuty(0);
				CL_D10_Logger::log(EN_L10_LOG_INFO,"[CT10] AutoOff(timer) triggered");
				return true;
			}
		}
		if (ao.offTime.enabled && !s_autoOffTriggered) {
			uint16_t cur=parseTimeToMin(timeStr);
			uint16_t off=parseTimeToMin(ao.offTime.time);
			if (cur>=off) {
				s_autoOffTriggered=true;
				CL_D10_Logger::log(EN_L10_LOG_INFO,"[CT10] AutoOff(time) triggered");
				return true;
			}
		}
		if (ao.offTemp.enabled && !s_autoOffTriggered && tempC>=ao.offTemp.temp) {
			s_autoOffTriggered=true;
			CL_D10_Logger::log(EN_L10_LOG_INFO,"[CT10] AutoOff(temp) triggered");
			return true;
		}
		return false;
	}

	// ==================================================
	// Segment → Wind 계산 및 적용
	// ==================================================
	static void applySegment(const ST_A10_ScheduleSegment_t& seg) {
		if (strcasecmp(seg.mode,"OFF")==0) { P10_PWM_ctrl::setDuty(0); return; }
		else if (strcasecmp(seg.mode,"FIXED")==0) { P10_PWM_ctrl::setDuty(seg.fixed_speed); return; }

		ST_A10_ResolvedWind_t w;
		if (!CL_C10_ConfigManager::resolveWindParams(s_windDict,
				seg.presetCode,
				seg.styleCode,
				seg.adjust.valid ? &seg.adjust : nullptr,
				w)) return;

		applyResolvedWind(w);
	}

	// ==================================================
	// Wind 적용 (Simulation + PWM + Chart)
	// ==================================================
	static void applyResolvedWind(const ST_A10_ResolvedWind_t& w) {
		if (memcmp(&s_prevWind,&w,sizeof(w))==0) return;
		memcpy(&s_currentWind,&w,sizeof(w));
		memcpy(&s_prevWind,&w,sizeof(w));

		S10_Simulation_014::setWind(w);
		S10_Simulation_014::updateChart(w);  // chart feedback 추가
		P10_PWM_ctrl::applyWind(w);

		N10_NvsManager::markDirty("wind_state",true);
		CL_D10_Logger::log(EN_L10_LOG_INFO,"[CT10] Applied wind (preset:%s style:%s)",
			w.presetCode, w.styleCode);
	}
};

// ------------------------------------------------------
// 정적 멤버 정의
// ------------------------------------------------------
ST_A10_WindProfileDict_t    CL_CT10_ControlManager::s_windDict;
ST_A10_SchedulesConfig_t    CL_CT10_ControlManager::s_schedCfg;
ST_A10_UserProfilesConfig_t CL_CT10_ControlManager::s_userCfg;
ST_A10_ScheduleSegment_t    CL_CT10_ControlManager::s_activeSeg;
ST_A10_ResolvedWind_t       CL_CT10_ControlManager::s_currentWind;
ST_A10_ResolvedWind_t       CL_CT10_ControlManager::s_prevWind;
uint32_t                    CL_CT10_ControlManager::s_lastApplyMs;
uint32_t                    CL_CT10_ControlManager::s_autoOffStartMs;
bool                        CL_CT10_ControlManager::s_autoOffTriggered;
bool                        CL_CT10_ControlManager::s_overrideActive;
bool                        CL_CT10_ControlManager::s_motionHold;
uint8_t                     CL_CT10_ControlManager::s_mode;
