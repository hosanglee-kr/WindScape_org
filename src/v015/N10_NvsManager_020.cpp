/*
 * ------------------------------------------------------
 * 소스명 : N10_NvsManager_020.cpp
 * 모듈약어 : N10
 * 모듈명 : Smart Nature Wind NVS Runtime Manager (v017)
 * ------------------------------------------------------
 * 기능 요약:
 * - CL_N10_NvsManager 클래스의 구현부
 * - 정적 멤버 변수 정의 및 모든 멤버 함수 구현
 * ------------------------------------------------------
 */

#include "N10_NvsManager_020.h"

// ------------------------------------------------------
// 정적 멤버 변수 정의
// ------------------------------------------------------
Preferences			  CL_N10_NvsManager::s_prefs;
bool				  CL_N10_NvsManager::s_initialized = false;
ST_N10_RuntimeState_t CL_N10_NvsManager::s_state;
ST_N10_DirtyFlags_t	  CL_N10_NvsManager::s_dirty	  = { false };
uint32_t			  CL_N10_NvsManager::s_lastSaveMs = 0;

// ==================================================
// 초기화 / 종료
// ==================================================
bool CL_N10_NvsManager::begin() {
	if (s_initialized)
		return true;

	if (!s_prefs.begin(G_N10_NS_RUNTIME, false)) {
		s_initialized = false;
		CL_D10_Logger::log(EN_L10_LOG_ERROR, "[N10] NVS begin failed (ns=%s)", G_N10_NS_RUNTIME);
		return false;
	}

	memset(&s_state, 0, sizeof(s_state));
	memset(&s_dirty, 0, sizeof(s_dirty));

	s_state.lastScheduleNo	  = -1;
	s_state.lastUserProfileNo = -1;

	loadRuntimeFromNvs();

	s_lastSaveMs  = millis();
	s_initialized = true;
	CL_D10_Logger::log(EN_L10_LOG_INFO, "[N10] Runtime Manager initialized");
	return true;
}

void CL_N10_NvsManager::end() {
	flush(true);
	s_prefs.end();
	s_initialized = false;
}

void CL_N10_NvsManager::clearAll() {
	s_prefs.begin("SNW_RUN", false);
	s_prefs.clear();  // 모든 key 삭제
	s_prefs.end();

	CL_D10_Logger::log(EN_L10_LOG_WARN, "[N10] NVS cleared (factory reset)");
}

// ==================================================
// Dirty 플래그 관리
// ==================================================
void CL_N10_NvsManager::markDirty(const char* p_key, bool p_flag) {
	if (!p_key || !p_key[0])
		return;

	if (strcasecmp(p_key, "runtime") == 0)
		s_dirty.runtime = p_flag;
}

void CL_N10_NvsManager::flushIfNeeded() {
	if (!s_initialized)
		return;

	uint32_t v_now = millis();
	if (v_now - s_lastSaveMs < G_N10_SAVE_INTERVAL_MS)
		return;

	bool v_saved = false;

	// 1) 런타임 상태 저장
	if (s_dirty.runtime) {
		flush(false);
		v_saved = true;
	}

	if (v_saved) {
		s_lastSaveMs = v_now;
		CL_D10_Logger::log(EN_L10_LOG_INFO, "[N10] Dirty flushed ");
	}
}

// ==================================================
// Getter / JSON Export
// ==================================================
ST_N10_RuntimeState_t CL_N10_NvsManager::getState() {
	return s_state;
}

void CL_N10_NvsManager::toJson(JsonDocument& p_doc) {
	JsonObject o		   = p_doc["runtime"].to<JsonObject>();
	o["runMode"]		   = s_state.runMode;
	o["runSource"]		   = s_state.runSource;
	o["lastScheduleNo"]	   = s_state.lastScheduleNo;
	o["lastUserProfileNo"] = s_state.lastUserProfileNo;

	JsonObject ao		   = o["autoOff"].to<JsonObject>();
	ao["enabled"]		   = s_state.autoOffEnabled;
	ao["minutes"]		   = s_state.autoOffMinutes;

	JsonObject ov		   = o["override"].to<JsonObject>();
	ov["enabled"]		   = s_state.overrideEnabled;
	ov["mode"]			   = s_state.overrideMode;
	ov["fixedPercent"]	   = s_state.overrideFixedPercent;
	ov["presetCode"]	   = s_state.overridePresetCode;
	ov["styleCode"]		   = s_state.overrideStyleCode;
}

// ==================================================
// 주기 Flush (loop용)
// ==================================================
void CL_N10_NvsManager::tick() {
	if (!s_initialized)
		return;
	if (!s_dirty.runtime)
		return;

	uint32_t v_now = millis();
	if (v_now - s_lastSaveMs < G_N10_SAVE_INTERVAL_MS)
		return;

	flush(false);
}

// ==================================================
// Setter API (CT10 등에서 호출)
// ==================================================
void CL_N10_NvsManager::setRunMode(uint8_t p_mode, uint8_t p_source) {
	if (!s_initialized)
		begin();
	if (s_state.runMode == p_mode && s_state.runSource == p_source)
		return;
	s_state.runMode	  = p_mode;
	s_state.runSource = p_source;
	s_dirty.runtime	  = true;
}

void CL_N10_NvsManager::setLastSchedule(int16_t p_schNo) {
	if (!s_initialized)
		begin();
	if (s_state.lastScheduleNo == p_schNo)
		return;
	s_state.lastScheduleNo = p_schNo;
	s_dirty.runtime		   = true;
}

void CL_N10_NvsManager::setLastUserProfile(int16_t p_profileNo) {
	if (!s_initialized)
		begin();
	if (s_state.lastUserProfileNo == p_profileNo)
		return;
	s_state.lastUserProfileNo = p_profileNo;
	s_dirty.runtime			  = true;
}

void CL_N10_NvsManager::setAutoOff(bool p_enabled, uint32_t p_minutes) {
	if (!s_initialized)
		begin();
	if (s_state.autoOffEnabled == p_enabled && s_state.autoOffMinutes == p_minutes)
		return;
	s_state.autoOffEnabled = p_enabled;
	s_state.autoOffMinutes = p_minutes;
	s_dirty.runtime		   = true;
}

void CL_N10_NvsManager::setOverrideFixed(bool p_enabled, float p_percent) {
	if (!s_initialized)
		begin();
	s_state.overrideEnabled		  = p_enabled;
	s_state.overrideMode		  = p_enabled ? 1 : 0;
	s_state.overrideFixedPercent  = p_enabled ? p_percent : 0.0f;
	s_state.overridePresetCode[0] = '\0';
	s_state.overrideStyleCode[0]  = '\0';
	s_dirty.runtime				  = true;
}

void CL_N10_NvsManager::setOverridePreset(bool p_enabled, const char* p_presetCode, const char* p_styleCode) {
	if (!s_initialized)
		begin();
	s_state.overrideEnabled		 = p_enabled;
	s_state.overrideMode		 = p_enabled ? 2 : 0;
	s_state.overrideFixedPercent = 0.0f;

	if (p_enabled) {
		strlcpy(s_state.overridePresetCode, p_presetCode ? p_presetCode : "", sizeof(s_state.overridePresetCode));
		strlcpy(s_state.overrideStyleCode, p_styleCode ? p_styleCode : "", sizeof(s_state.overrideStyleCode));
	} else {
		s_state.overridePresetCode[0] = '\0';
		s_state.overrideStyleCode[0]  = '\0';
	}
	s_dirty.runtime = true;
}

void CL_N10_NvsManager::clearOverride() {
	if (!s_initialized)
		begin();
	if (!s_state.overrideEnabled && s_state.overrideMode == 0)
		return;

	s_state.overrideEnabled		  = false;
	s_state.overrideMode		  = 0;
	s_state.overrideFixedPercent  = 0.0f;
	s_state.overridePresetCode[0] = '\0';
	s_state.overrideStyleCode[0]  = '\0';
	s_dirty.runtime				  = true;
}

void CL_N10_NvsManager::resetRuntime() {
	if (!s_initialized)
		begin();
	memset(&s_state, 0, sizeof(s_state));
	s_state.lastScheduleNo	  = -1;
	s_state.lastUserProfileNo = -1;
	s_dirty.runtime			  = true;
	flush(true);

	CL_D10_Logger::log(EN_L10_LOG_WARN, "[N10] Runtime state reset");
}

// --------------------------------------------------
// 내부: NVS 로드/저장
// --------------------------------------------------
void CL_N10_NvsManager::loadRuntimeFromNvs() {
	s_state.runMode				 = s_prefs.getUChar("run_mode", 0);
	s_state.runSource			 = s_prefs.getUChar("run_src", 0);
	s_state.lastScheduleNo		 = s_prefs.getShort("sched_no", -1);
	s_state.lastUserProfileNo	 = s_prefs.getShort("uprofile_no", -1);

	s_state.autoOffEnabled		 = s_prefs.getUChar("autoOff_en", 0) != 0;
	s_state.autoOffMinutes		 = s_prefs.getULong("autoOff_min", 0);

	s_state.overrideEnabled		 = s_prefs.getUChar("ovr_en", 0) != 0;
	s_state.overrideMode		 = s_prefs.getUChar("ovr_mode", 0);
	s_state.overrideFixedPercent = s_prefs.getFloat("ovr_fixed", 0.0f);

	char v_buf[24];
	memset(v_buf, 0, sizeof(v_buf));
	s_prefs.getString("ovr_preset", v_buf, sizeof(v_buf));
	strlcpy(s_state.overridePresetCode, v_buf, sizeof(s_state.overridePresetCode));
	memset(v_buf, 0, sizeof(v_buf));
	s_prefs.getString("ovr_style", v_buf, sizeof(v_buf));
	strlcpy(s_state.overrideStyleCode, v_buf, sizeof(s_state.overrideStyleCode));

	s_dirty.runtime = false;
	CL_D10_Logger::log(EN_L10_LOG_INFO, "[N10] Load runtime: mode=%u src=%u sch=%d up=%d autoOff(%d,%lu) ovr_en=%d", s_state.runMode, s_state.runSource, (int)s_state.lastScheduleNo, (int)s_state.lastUserProfileNo, (int)s_state.autoOffEnabled, (unsigned long)s_state.autoOffMinutes, (int)s_state.overrideEnabled);
}

void CL_N10_NvsManager::flush(bool p_force) {
	if (!s_initialized)
		return;
	if (!p_force && !s_dirty.runtime)
		return;

	s_prefs.putUChar("run_mode", s_state.runMode);
	s_prefs.putUChar("run_src", s_state.runSource);
	s_prefs.putShort("sched_no", s_state.lastScheduleNo);
	s_prefs.putShort("uprofile_no", s_state.lastUserProfileNo);
	s_prefs.putUChar("autoOff_en", s_state.autoOffEnabled ? 1 : 0);
	s_prefs.putULong("autoOff_min", s_state.autoOffMinutes);
	s_prefs.putUChar("ovr_en", s_state.overrideEnabled ? 1 : 0);
	s_prefs.putUChar("ovr_mode", s_state.overrideMode);
	s_prefs.putFloat("ovr_fixed", s_state.overrideFixedPercent);
	s_prefs.putString("ovr_preset", s_state.overridePresetCode);
	s_prefs.putString("ovr_style", s_state.overrideStyleCode);

	s_dirty.runtime = false;
	s_lastSaveMs	= millis();

	CL_D10_Logger::log(EN_L10_LOG_DEBUG, "[N10] Runtime flushed to NVS");
}
