/*
 * ------------------------------------------------------
 * 소스명 : CT10_Control_Basic_040.cpp
 * 모듈약어 : CT10
 * 모듈명 : Smart Nature Wind 제어 통합 Manager (v026, Misc)
 * ------------------------------------------------------
 * 기능 요약:
 * - AutoOff 초기화/체크, Motion 체크, Schedule 활성 인덱스 계산
 * - Dirty 플래그 관리, override remain 계산, preset/style 이름 조회 유틸
 * ------------------------------------------------------
 */

#include "CT10_Control_040.h"

// --------------------------------------------------
// Dirty flags
// --------------------------------------------------
void CL_CT10_ControlManager::markDirty(const char* p_key) {
	if (!p_key)
		return;

	if (strcmp(p_key, "state") == 0)
		_dirtyState = true;
	else if (strcmp(p_key, "chart") == 0)
		_dirtyChart = true;
	else if (strcmp(p_key, "metrics") == 0)
		_dirtyMetrics = true;
	else if (strcmp(p_key, "summary") == 0)
		_dirtySummary = true;
}

bool CL_CT10_ControlManager::consumeDirtyState() {
	bool v_ret	= _dirtyState;
	_dirtyState = false;
	return v_ret;
}

bool CL_CT10_ControlManager::consumeDirtyMetrics() {
	bool v_ret	  = _dirtyMetrics;
	_dirtyMetrics = false;
	return v_ret;
}

bool CL_CT10_ControlManager::consumeDirtyChart() {
	bool v_ret	= _dirtyChart;
	_dirtyChart = false;
	return v_ret;
}

bool CL_CT10_ControlManager::consumeDirtySummary() {
	bool v_ret	  = _dirtySummary;
	_dirtySummary = false;
	return v_ret;
}

// --------------------------------------------------
// override remain sec
// --------------------------------------------------
uint32_t CL_CT10_ControlManager::calcOverrideRemainSec() const {
	if (!overrideState.active || overrideState.endMs == 0)
		return 0;

	unsigned long v_nowMs = millis();
	if (overrideState.endMs <= v_nowMs)
		return 0;

	return (uint32_t)((overrideState.endMs - v_nowMs) / 1000UL);
}

// --------------------------------------------------
// autoOff init
// --------------------------------------------------
void CL_CT10_ControlManager::initAutoOffFromUserProfile(const ST_A20_UserProfileItem_t& p_up) {
	memset(&autoOffRt, 0, sizeof(autoOffRt));

	if (p_up.autoOff.timer.enabled) {
		autoOffRt.timerArmed   = true;
		autoOffRt.timerStartMs = millis();
		autoOffRt.timerMinutes = p_up.autoOff.timer.minutes;
	}
	if (p_up.autoOff.offTime.enabled) {
		autoOffRt.offTimeEnabled = true;
		autoOffRt.offTimeMinutes = parseHHMMtoMin(p_up.autoOff.offTime.time);
	}
	if (p_up.autoOff.offTemp.enabled) {
		autoOffRt.offTempEnabled = true;
		autoOffRt.offTemp		 = p_up.autoOff.offTemp.temp;
	}
}

void CL_CT10_ControlManager::initAutoOffFromSchedule(const ST_A20_ScheduleItem_t& p_s) {
	memset(&autoOffRt, 0, sizeof(autoOffRt));

	if (p_s.autoOff.timer.enabled) {
		autoOffRt.timerArmed   = true;
		autoOffRt.timerStartMs = millis();
		autoOffRt.timerMinutes = p_s.autoOff.timer.minutes;
	}
	if (p_s.autoOff.offTime.enabled) {
		autoOffRt.offTimeEnabled = true;
		autoOffRt.offTimeMinutes = parseHHMMtoMin(p_s.autoOff.offTime.time);
	}
	if (p_s.autoOff.offTemp.enabled) {
		autoOffRt.offTempEnabled = true;
		autoOffRt.offTemp		 = p_s.autoOff.offTemp.temp;
	}
}

// --------------------------------------------------
// autoOff check
// --------------------------------------------------
bool CL_CT10_ControlManager::checkAutoOff() {
	if (!autoOffRt.timerArmed && !autoOffRt.offTimeEnabled && !autoOffRt.offTempEnabled)
		return false;

	unsigned long v_nowMs = millis();

	// 1) timer
	if (autoOffRt.timerArmed && autoOffRt.timerMinutes > 0) {
		uint32_t v_elapsedMin = (v_nowMs - autoOffRt.timerStartMs) / 60000UL;
		if (v_elapsedMin >= autoOffRt.timerMinutes) {
			CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] AutoOff(timer %lu min) triggered", (unsigned long)autoOffRt.timerMinutes);
			return true;
		}
	}

	// 2) offTime (RTC)
	if (autoOffRt.offTimeEnabled) {
		time_t	   v_t		 = time(nullptr);
		struct tm* v_localTm = localtime(&v_t);
		if (v_localTm) {
			uint16_t v_curMin = (uint16_t)v_localTm->tm_hour * 60 + (uint16_t)v_localTm->tm_min;
			if (v_curMin >= autoOffRt.offTimeMinutes) {
				CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] AutoOff(time %u) triggered", (unsigned)autoOffRt.offTimeMinutes);
				return true;
			}
		}
	}

	// 3) offTemp (센서 연동 전까지 mock)
	if (autoOffRt.offTempEnabled) {
		float v_curTemp = getCurrentTemperatureMock();
		if (v_curTemp >= autoOffRt.offTemp) {
			CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] AutoOff(temp %.1fC >= %.1fC) triggered", v_curTemp, autoOffRt.offTemp);
			return true;
		}
	}

	return false;
}

// --------------------------------------------------
// helpers
// --------------------------------------------------
uint16_t CL_CT10_ControlManager::parseHHMMtoMin(const char* p_time) {
	if (!p_time || strlen(p_time) < 4)
		return 0;

	int			v_hh	= atoi(p_time);
	const char* v_colon = strchr(p_time, ':');
	int			v_mm	= (v_colon) ? atoi(v_colon + 1) : 0;

	return (uint16_t)(v_hh * 60 + v_mm);
}

float CL_CT10_ControlManager::getCurrentTemperatureMock() {
	return 24.0f;
}

// --------------------------------------------------
// find active schedule
// --------------------------------------------------
int CL_CT10_ControlManager::findActiveScheduleIndex(const ST_A20_SchedulesRoot_t& p_cfg) {
	if (p_cfg.count == 0)
		return -1;

	time_t	   v_now	 = time(nullptr);
	struct tm* v_localTm = localtime(&v_now);
	if (!v_localTm)
		return -1;

	// Arduino: 0=Sun, Config: 0=Mon => 보정
	uint8_t	 v_wday	  = (v_localTm->tm_wday == 0) ? 6 : (uint8_t)(v_localTm->tm_wday - 1);
	uint16_t v_curMin = (uint16_t)v_localTm->tm_hour * 60 + (uint16_t)v_localTm->tm_min;

	for (int v_i = 0; v_i < (int)p_cfg.count; v_i++) {
		const ST_A20_ScheduleItem_t& v_s = p_cfg.items[v_i];

		if (!v_s.enabled)
			continue;

		// days 체크
		if (!v_s.period.days[v_wday])
			continue;

		uint16_t v_startMin = parseHHMMtoMin(v_s.period.startTime);
		uint16_t v_endMin	= parseHHMMtoMin(v_s.period.endTime);

		if (v_startMin <= v_endMin) {
			if (v_curMin >= v_startMin && v_curMin < v_endMin)
				return v_i;
		} else {
			// cross midnight
			if (v_curMin >= v_startMin || v_curMin < v_endMin)
				return v_i;
		}
	}

	return -1;
}

// --------------------------------------------------
// motion check
// --------------------------------------------------
bool CL_CT10_ControlManager::isMotionBlocked(const ST_A20_Motion_t& p_motionCfg) {
	if (!motion)
		return false;

	if (!p_motionCfg.pir.enabled && !p_motionCfg.ble.enabled)
		return false;

	bool v_presenceActive = motion->isActive();
	if (!v_presenceActive) {
		CL_D10_Logger::log(EN_L10_LOG_DEBUG, "[CT10] Motion blocked (no presence)");
		return true;
	}
	return false;
}

// --------------------------------------------------
// metrics dirty push
// --------------------------------------------------
void CL_CT10_ControlManager::maybePushMetricsDirty() {
	unsigned long v_nowMs = millis();
	if (v_nowMs - lastMetricsPushMs < S_METRICS_PUSH_INTERVAL_MS)
		return;

	lastMetricsPushMs = v_nowMs;
	markDirty("metrics");
}

// --------------------------------------------------
// preset/style name lookup (log 개선용)
// --------------------------------------------------
const char* CL_CT10_ControlManager::findPresetNameByCode(const char* p_code) const {
	if (!g_A20_config_root.windDict || !p_code || !p_code[0])
		return "";

	const ST_A20_WindProfileDict_t& v_dict = *g_A20_config_root.windDict;
	for (uint8_t v_i = 0; v_i < v_dict.presetCount; v_i++) {
		if (strcasecmp(v_dict.presets[v_i].code, p_code) == 0) {
			return v_dict.presets[v_i].name;
		}
	}
	return "";
}

const char* CL_CT10_ControlManager::findStyleNameByCode(const char* p_code) const {
	if (!g_A20_config_root.windDict || !p_code || !p_code[0])
		return "";

	const ST_A20_WindProfileDict_t& v_dict = *g_A20_config_root.windDict;
	for (uint8_t v_i = 0; v_i < v_dict.styleCount; v_i++) {
		if (strcasecmp(v_dict.styles[v_i].code, p_code) == 0) {
			return v_dict.styles[v_i].name;
		}
	}
	return "";
}
