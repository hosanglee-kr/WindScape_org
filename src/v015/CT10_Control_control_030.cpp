/*
 * ------------------------------------------------------
 * 소스명 : CT10_Control_control_030.cpp
 * 모듈약어 : CT10
 * 모듈명 : Smart Nature Wind 제어 통합 Manager (v026, Control)
 * ------------------------------------------------------
 * 기능 요약:
 * - begin/tick 및 Override/Profile/Schedule 제어 루프 구현
 * - Segment 시퀀스 오버로드 구현(템플릿 제거)
 * - applySegmentOn 로그 포맷 개선(이름 출력)
 * ------------------------------------------------------
 */

#include "CT10_Control_030.h"

// 외부 전역(프로젝트 기존 전역 PWM 가정)
extern CL_P10_PWM g_P10_pwm;

bool CL_CT10_ControlManager::begin() {
	instance().begin(g_P10_pwm);
	return true;
}

void CL_CT10_ControlManager::tick() {
	instance().tickLoop();
}

void CL_CT10_ControlManager::setMode(bool p_profileMode) {
	instance().setProfileMode(p_profileMode);
}

bool CL_CT10_ControlManager::setActiveUserProfile(uint8_t p_profileNo) {
	return instance().startUserProfileByNo(p_profileNo);
}

void CL_CT10_ControlManager::applyManual(const ST_A20_ResolvedWind_t& p_wind) {
	instance().applyManualResolved(p_wind, 0);
}

void CL_CT10_ControlManager::clearManual() {
	instance().stopOverride();
}

bool CL_CT10_ControlManager::reloadAll() {
	bool v_ok = CL_C10_ConfigManager::loadAll(g_A20_config_root);
	if (!v_ok)
		return false;

	CL_CT10_ControlManager& v_inst = instance();

	v_inst.runSource			   = EN_CT10_RUN_NONE;
	v_inst.curScheduleIndex		   = -1;
	v_inst.curProfileIndex		   = -1;
	v_inst.useProfileMode		   = false;

	memset(&v_inst.overrideState, 0, sizeof(v_inst.overrideState));
	memset(&v_inst.autoOffRt, 0, sizeof(v_inst.autoOffRt));
	memset(&v_inst.scheduleSegRt, 0, sizeof(v_inst.scheduleSegRt));
	memset(&v_inst.profileSegRt, 0, sizeof(v_inst.profileSegRt));
	v_inst.scheduleSegRt.index = -1;
	v_inst.profileSegRt.index  = -1;

	v_inst.sim.stop();

	v_inst.markDirty("state");
	v_inst.markDirty("metrics");

	CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] reloadAll done");
	return true;
}

// --------------------------------------------------
// begin / motion
// --------------------------------------------------
void CL_CT10_ControlManager::begin(CL_P10_PWM& p_pwm) {
	pwm = &p_pwm;

	memset(&overrideState, 0, sizeof(overrideState));
	memset(&scheduleSegRt, 0, sizeof(scheduleSegRt));
	memset(&profileSegRt, 0, sizeof(profileSegRt));
	memset(&autoOffRt, 0, sizeof(autoOffRt));

	scheduleSegRt.index = -1;
	profileSegRt.index	= -1;

	useProfileMode		= false;
	runSource			= EN_CT10_RUN_NONE;
	lastTickMs			= 0;
	lastMetricsPushMs	= 0;

	sim.begin(p_pwm);

	active = true;

	markDirty("state");
	markDirty("metrics");

	CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] begin()");
}

void CL_CT10_ControlManager::setMotion(CL_M10_MotionLogic* p_motion) {
	motion = p_motion;
}

// --------------------------------------------------
// mode/profile
// --------------------------------------------------
void CL_CT10_ControlManager::setProfileMode(bool p_profileMode) {
	useProfileMode = p_profileMode;

	if (!p_profileMode) {
		stopUserProfile();
	} else {
		curScheduleIndex = -1;
		if (runSource == EN_CT10_RUN_SCHEDULE) {
			runSource = EN_CT10_RUN_NONE;
		}
	}

	markDirty("state");
	markDirty("metrics");

	CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] setMode(profileMode=%d)", p_profileMode ? 1 : 0);
}

bool CL_CT10_ControlManager::startUserProfileByNo(uint8_t p_profileNo) {
	if (!g_A20_config_root.userProfiles)
		return false;

	ST_A20_UserProfilesRoot_t& v_cfg = *g_A20_config_root.userProfiles;

	for (uint8_t v_i = 0; v_i < v_cfg.count; v_i++) {
		const ST_A20_UserProfileItem_t& v_p = v_cfg.items[v_i];
		if (!v_p.enabled)
			continue;

		if (v_p.profileNo == p_profileNo) {
			runSource				  = EN_CT10_RUN_USER_PROFILE;
			curProfileIndex			  = (int8_t)v_i;
			profileSegRt.index		  = -1;
			profileSegRt.onPhase	  = true;
			profileSegRt.phaseStartMs = millis();
			profileSegRt.loopCount	  = 0;

			initAutoOffFromUserProfile(v_p);

			CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] Start UserProfile #%u (%s)", (unsigned)p_profileNo, v_p.name);

			markDirty("state");
			markDirty("metrics");
			return true;
		}
	}

	return false;
}

void CL_CT10_ControlManager::stopUserProfile() {
	if (runSource != EN_CT10_RUN_USER_PROFILE)
		return;

	runSource		   = EN_CT10_RUN_NONE;
	curProfileIndex	   = -1;
	profileSegRt.index = -1;

	sim.stop();

	markDirty("state");
	markDirty("metrics");

	CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] UserProfile stopped");
}

// --------------------------------------------------
// override
// --------------------------------------------------
void CL_CT10_ControlManager::startOverrideFixed(float p_percent, uint32_t p_seconds) {
	memset(&overrideState, 0, sizeof(overrideState));
	overrideState.active	   = true;
	overrideState.useFixed	   = true;
	overrideState.fixedPercent = constrain(p_percent, 0.0f, 100.0f);
	overrideState.endMs		   = (p_seconds > 0) ? (millis() + (p_seconds * 1000UL)) : 0;

	markDirty("state");
	markDirty("metrics");

	CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] Override FIXED %.1f%% (sec=%lu)", overrideState.fixedPercent, (unsigned long)p_seconds);
}

void CL_CT10_ControlManager::startOverridePreset(const char* p_presetCode, const char* p_styleCode, const ST_A20_AdjustDelta_t* p_adj, uint32_t p_seconds) {
	if (!g_A20_config_root.windDict)
		return;

	ST_A20_ResolvedWind_t v_resolved;
	memset(&v_resolved, 0, sizeof(v_resolved));

	bool v_ok = S20_resolveWindParams(*g_A20_config_root.windDict, p_presetCode, p_styleCode, p_adj, v_resolved);

	if (!v_ok || !v_resolved.valid) {
		CL_D10_Logger::log(EN_L10_LOG_WARN, "[CT10] startOverridePreset resolve failed (%s,%s)", p_presetCode ? p_presetCode : "", p_styleCode ? p_styleCode : "");
		return;
	}

	applyManualResolved(v_resolved, p_seconds);
}

void CL_CT10_ControlManager::applyManualResolved(const ST_A20_ResolvedWind_t& p_wind, uint32_t p_seconds) {
	if (!p_wind.valid) {
		CL_D10_Logger::log(EN_L10_LOG_WARN, "[CT10] applyManual: invalid ResolvedWind");
		return;
	}

	if (p_wind.fixedMode) {
		startOverrideFixed(p_wind.fixedSpeed, p_seconds);
		return;
	}

	memset(&overrideState, 0, sizeof(overrideState));
	overrideState.active		  = true;
	overrideState.useFixed		  = false;
	overrideState.resolvedApplied = false;
	overrideState.fixedPercent	  = 0.0f;
	overrideState.resolved		  = p_wind;
	overrideState.endMs			  = (p_seconds > 0) ? (millis() + (p_seconds * 1000UL)) : 0;

	markDirty("state");
	markDirty("metrics");
	markDirty("chart");

	CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] applyManual: preset=%s style=%s (sec=%lu)", p_wind.presetCode, p_wind.styleCode, (unsigned long)p_seconds);
}

void CL_CT10_ControlManager::stopOverride() {
	if (!overrideState.active)
		return;

	memset(&overrideState, 0, sizeof(overrideState));

	markDirty("state");
	markDirty("metrics");

	CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] Override cleared");
}

// --------------------------------------------------
// tick loop
// --------------------------------------------------
void CL_CT10_ControlManager::tickLoop() {
	if (!active || !pwm)
		return;

	unsigned long v_nowMs = millis();
	if (v_nowMs - lastTickMs < S_TICK_MIN_INTERVAL_MS)
		return;
	lastTickMs = v_nowMs;

	// 1) Override
	if (tickOverride()) {
		sim.tick();
		maybePushMetricsDirty();
		return;
	}

	// 2) Profile 전용 모드
	if (useProfileMode) {
		if (runSource == EN_CT10_RUN_USER_PROFILE && tickUserProfile()) {
			sim.tick();
			maybePushMetricsDirty();
		} else if (sim.active) {
			sim.stop();
			maybePushMetricsDirty();
		}
		return;
	}

	// 3) UserProfile(스케줄과 별개로 실행)
	if (runSource == EN_CT10_RUN_USER_PROFILE && tickUserProfile()) {
		sim.tick();
		maybePushMetricsDirty();
		return;
	}

	// 4) Schedule
	if (tickSchedule()) {
		sim.tick();
		markDirty("chart");
		maybePushMetricsDirty();
		return;
	}

	// 5) 아무 소스도 없는데 sim이 켜져 있으면 정지
	if (sim.active) {
		sim.stop();
		maybePushMetricsDirty();
		markDirty("state");
	}
}

// --------------------------------------------------
// override tick
// --------------------------------------------------
bool CL_CT10_ControlManager::tickOverride() {
	if (!overrideState.active)
		return false;

	unsigned long v_nowMs = millis();

	// timeout
	if (overrideState.endMs != 0 && v_nowMs >= overrideState.endMs) {
		CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] Override timeout");
		memset(&overrideState, 0, sizeof(overrideState));
		markDirty("state");
		markDirty("metrics");
		return false;
	}

	// fixed
	if (overrideState.useFixed) {
		sim.stop();
		if (pwm) {
			pwm->P10_setDutyPercent(overrideState.fixedPercent);
		}
		return true;
	}

	// resolved
	if (!overrideState.resolved.valid) {
		CL_D10_Logger::log(EN_L10_LOG_WARN, "[CT10] Override resolved invalid, clear");
		memset(&overrideState, 0, sizeof(overrideState));
		markDirty("state");
		markDirty("metrics");
		return false;
	}

	if (!overrideState.resolvedApplied) {
		sim.applyResolvedWind(overrideState.resolved);
		overrideState.resolvedApplied = true;
		markDirty("chart");
	}

	return true;
}

// --------------------------------------------------
// userProfile tick
// --------------------------------------------------
bool CL_CT10_ControlManager::tickUserProfile() {
	if (!g_A20_config_root.userProfiles)
		return false;
	if (curProfileIndex < 0)
		return false;

	ST_A20_UserProfilesRoot_t& v_cfg = *g_A20_config_root.userProfiles;
	if ((uint8_t)curProfileIndex >= v_cfg.count)
		return false;

	ST_A20_UserProfileItem_t& v_profile = v_cfg.items[(uint8_t)curProfileIndex];
	if (!v_profile.enabled || v_profile.seg_count == 0)
		return false;

	if (checkAutoOff()) {
		sim.stop();
		runSource		= EN_CT10_RUN_NONE;
		curProfileIndex = -1;

		CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] UserProfile AutoOff stop");
		markDirty("state");
		markDirty("metrics");
		return true;
	}

	if (isMotionBlocked(v_profile.motion)) {
		sim.stop();
		return true;
	}

	return tickSegmentSequence(v_profile.repeatSegments, v_profile.repeatCount, v_profile.segments, v_profile.seg_count, profileSegRt);
}

// --------------------------------------------------
// schedule tick
// --------------------------------------------------
bool CL_CT10_ControlManager::tickSchedule() {
	if (!g_A20_config_root.schedules)
		return false;

	ST_A20_SchedulesRoot_t& v_cfg		= *g_A20_config_root.schedules;

	int						v_activeIdx = findActiveScheduleIndex(v_cfg);
	if (v_activeIdx < 0) {
		curScheduleIndex = -1;
		return false;
	}

	if (curScheduleIndex != (int8_t)v_activeIdx) {
		curScheduleIndex		   = (int8_t)v_activeIdx;
		runSource				   = EN_CT10_RUN_SCHEDULE;
		scheduleSegRt.index		   = -1;
		scheduleSegRt.onPhase	   = true;
		scheduleSegRt.phaseStartMs = millis();
		scheduleSegRt.loopCount	   = 0;

		initAutoOffFromSchedule(v_cfg.items[(uint8_t)curScheduleIndex]);

		CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] Active Schedule idx=%d", v_activeIdx);

		markDirty("state");
		markDirty("metrics");
	}

	ST_A20_ScheduleItem_t& v_schedule = v_cfg.items[(uint8_t)curScheduleIndex];
	if (!v_schedule.enabled || v_schedule.seg_count == 0)
		return false;

	if (checkAutoOff()) {
		sim.stop();
		runSource		 = EN_CT10_RUN_NONE;
		curScheduleIndex = -1;

		CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] Schedule AutoOff stop");
		markDirty("state");
		markDirty("metrics");
		return true;
	}

	if (isMotionBlocked(v_schedule.motion)) {
		sim.stop();
		return true;
	}

	return tickSegmentSequence(v_schedule.repeatSegments, v_schedule.repeatCount, v_schedule.segments, v_schedule.seg_count, scheduleSegRt);
}

// --------------------------------------------------
// segment sequence (schedule)
// --------------------------------------------------
bool CL_CT10_ControlManager::tickSegmentSequence(bool p_repeat, uint8_t p_repeatCount, ST_A20_ScheduleSegment_t* p_segs, uint8_t p_count, ST_CT10_SegmentRuntime_t& p_rt) {
	unsigned long v_nowMs = millis();

	if (p_count == 0 || !p_segs) {
		sim.stop();
		return false;
	}

	if (p_rt.index < 0) {
		p_rt.index		  = 0;
		p_rt.onPhase	  = true;
		p_rt.phaseStartMs = v_nowMs;
		p_rt.loopCount	  = 0;
		applySegmentOn(p_segs[0]);
		return true;
	}

	if ((uint8_t)p_rt.index >= p_count) {
		sim.stop();
		return false;
	}

	ST_A20_ScheduleSegment_t& v_seg	  = p_segs[(uint8_t)p_rt.index];

	uint32_t				  v_onMs  = (uint32_t)v_seg.on_minutes * 60000UL;
	uint32_t				  v_offMs = (uint32_t)v_seg.off_minutes * 60000UL;

	if (p_rt.onPhase && v_onMs > 0 && v_nowMs - p_rt.phaseStartMs >= v_onMs) {
		p_rt.onPhase	  = false;
		p_rt.phaseStartMs = v_nowMs;
		applySegmentOff();
	} else if (!p_rt.onPhase && v_offMs > 0 && v_nowMs - p_rt.phaseStartMs >= v_offMs) {
		p_rt.index++;

		if ((uint8_t)p_rt.index >= p_count) {
			if (!p_repeat) {
				sim.stop();
				return true;
			}

			if (p_repeatCount > 0) {
				if (p_rt.loopCount + 1 >= p_repeatCount) {
					sim.stop();
					return true;
				}
				p_rt.loopCount++;
			}

			p_rt.index = 0;
		}

		p_rt.onPhase	  = true;
		p_rt.phaseStartMs = v_nowMs;
		applySegmentOn(p_segs[(uint8_t)p_rt.index]);
	}

	return true;
}

// --------------------------------------------------
// segment sequence (profile)
// --------------------------------------------------
bool CL_CT10_ControlManager::tickSegmentSequence(bool p_repeat, uint8_t p_repeatCount, ST_A20_UserProfileSegment_t* p_segs, uint8_t p_count, ST_CT10_SegmentRuntime_t& p_rt) {
	unsigned long v_nowMs = millis();

	if (p_count == 0 || !p_segs) {
		sim.stop();
		return false;
	}

	if (p_rt.index < 0) {
		p_rt.index		  = 0;
		p_rt.onPhase	  = true;
		p_rt.phaseStartMs = v_nowMs;
		p_rt.loopCount	  = 0;
		applySegmentOn(p_segs[0]);
		return true;
	}

	if ((uint8_t)p_rt.index >= p_count) {
		sim.stop();
		return false;
	}

	ST_A20_UserProfileSegment_t& v_seg	 = p_segs[(uint8_t)p_rt.index];

	uint32_t					 v_onMs	 = (uint32_t)v_seg.on_minutes * 60000UL;
	uint32_t					 v_offMs = (uint32_t)v_seg.off_minutes * 60000UL;

	if (p_rt.onPhase && v_onMs > 0 && v_nowMs - p_rt.phaseStartMs >= v_onMs) {
		p_rt.onPhase	  = false;
		p_rt.phaseStartMs = v_nowMs;
		applySegmentOff();
	} else if (!p_rt.onPhase && v_offMs > 0 && v_nowMs - p_rt.phaseStartMs >= v_offMs) {
		p_rt.index++;

		if ((uint8_t)p_rt.index >= p_count) {
			if (!p_repeat) {
				sim.stop();
				return true;
			}

			if (p_repeatCount > 0) {
				if (p_rt.loopCount + 1 >= p_repeatCount) {
					sim.stop();
					return true;
				}
				p_rt.loopCount++;
			}

			p_rt.index = 0;
		}

		p_rt.onPhase	  = true;
		p_rt.phaseStartMs = v_nowMs;
		applySegmentOn(p_segs[(uint8_t)p_rt.index]);
	}

	return true;
}

// --------------------------------------------------
// apply segment on/off + 로그 개선(이름 출력)
// --------------------------------------------------
void CL_CT10_ControlManager::applySegmentOn(const ST_A20_ScheduleSegment_t& p_seg) {
	if (!g_A20_config_root.windDict)
		return;

	if (p_seg.mode == EN_A20_SEG_MODE_FIXED) {
		sim.stop();
		if (pwm) {
			pwm->P10_setDutyPercent(p_seg.fixed_speed);
		}

		markDirty("state");
		markDirty("chart");

		CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] SegmentOn(SCH) FIXED duty=%.1f%%", p_seg.fixed_speed);
		return;
	}

	ST_A20_ResolvedWind_t v_resolved;
	memset(&v_resolved, 0, sizeof(v_resolved));

	bool v_ok = S20_resolveWindParams(*g_A20_config_root.windDict, p_seg.presetCode, p_seg.styleCode, &p_seg.adjust, v_resolved);

	if (v_ok && v_resolved.valid) {
		sim.applyResolvedWind(v_resolved);

		markDirty("state");
		markDirty("chart");

		const char* v_presetName = findPresetNameByCode(p_seg.presetCode);
		const char* v_styleName	 = findStyleNameByCode(p_seg.styleCode);

		// (요청사항) applySegmentOn 로그포맷: 이름 출력
		CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] SegmentOn(SCH) PRESET=%s(%s) STYLE=%s(%s) on=%u off=%u", p_seg.presetCode, v_presetName, p_seg.styleCode, v_styleName, (unsigned)p_seg.on_minutes, (unsigned)p_seg.off_minutes);
	} else {
		CL_D10_Logger::log(EN_L10_LOG_WARN, "[CT10] SegmentOn(SCH) resolve failed preset=%s style=%s", p_seg.presetCode, p_seg.styleCode);
	}
}

void CL_CT10_ControlManager::applySegmentOn(const ST_A20_UserProfileSegment_t& p_seg) {
	if (!g_A20_config_root.windDict)
		return;

	if (p_seg.mode == EN_A20_SEG_MODE_FIXED) {
		sim.stop();
		if (pwm) {
			pwm->P10_setDutyPercent(p_seg.fixed_speed);
		}

		markDirty("state");
		markDirty("chart");

		CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] SegmentOn(PROFILE) FIXED duty=%.1f%%", p_seg.fixed_speed);
		return;
	}

	ST_A20_ResolvedWind_t v_resolved;
	memset(&v_resolved, 0, sizeof(v_resolved));

	bool v_ok = S20_resolveWindParams(*g_A20_config_root.windDict, p_seg.presetCode, p_seg.styleCode, &p_seg.adjust, v_resolved);

	if (v_ok && v_resolved.valid) {
		sim.applyResolvedWind(v_resolved);

		markDirty("state");
		markDirty("chart");

		const char* v_presetName = findPresetNameByCode(p_seg.presetCode);
		const char* v_styleName	 = findStyleNameByCode(p_seg.styleCode);

		CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] SegmentOn(PROFILE) PRESET=%s(%s) STYLE=%s(%s) on=%u off=%u", p_seg.presetCode, v_presetName, p_seg.styleCode, v_styleName, (unsigned)p_seg.on_minutes, (unsigned)p_seg.off_minutes);
	} else {
		CL_D10_Logger::log(EN_L10_LOG_WARN, "[CT10] SegmentOn(PROFILE) resolve failed preset=%s style=%s", p_seg.presetCode, p_seg.styleCode);
	}
}

void CL_CT10_ControlManager::applySegmentOff() {
	sim.stop();
	markDirty("state");
	markDirty("chart");
}
