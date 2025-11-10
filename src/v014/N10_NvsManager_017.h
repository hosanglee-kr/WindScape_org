#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : N10_NvsManager_017.h
 * 모듈약어 : N10
 * 모듈명 : Smart Nature Wind NVS Runtime Manager (v017)
 * ------------------------------------------------------
 * 기능 요약:
 *  - 런타임 상태를 NVS(Flash)에 안전하게 저장/복원
 *  - 현재 실행 모드, 마지막 스케줄/프로파일, AutoOff, Override 메타 관리
 *  - Flash 수명 보호를 위한 Dirty Flag + 최소 주기(10초) Flush 정책
 *  - C10_ConfigManager 연계로 schedules/userProfiles/system 등 병렬 Dirty Flush 지원
 * ------------------------------------------------------
 * NVS Key 설계 (namespace: "SNW_RUN"):
 *   run_mode, run_src, sched_no, uprofile_no, autoOff_en, autoOff_min,
 *   ovr_en, ovr_mode, ovr_fixed, ovr_preset, ovr_style
 * ------------------------------------------------------
 * [구현 규칙]
 *  - 항상 소스 시작 주석 체계 유지
 *  - ArduinoJson v7.x.x 사용 (v6 이하 사용 금지)
 *  - JsonDocument 단일 타입만 사용
 *  - createNestedArray/Object/containsKey 사용 금지
 *  - memset + strlcpy 기반 안전 초기화
 *  - 주석/필드명은 JSON/NVS Key 의미와 동일하게 유지
 *  - 모듈별 단일 헤더(h)파일로만 구성 (cpp 없음)
 * ------------------------------------------------------
 * [코드 네이밍 규칙]
 *   - 전역 상수,매크로      : G_모듈약어_ 접두사
 *   - 전역 변수             : g_모듈약어_ 접두사
 *   - 전역 함수             : 모듈약어_ 접두사
 *   - type                  : T_모듈약어_ 접두사
 *   - typedef               : _t 접미사
 *   - enum 상수             : EN_모듈약어_ 접두사
 *   - 구조체                : ST_모듈약어_ 접두사
 *   - 클래스명              : CL_모듈약어_ 접두사
 *   - 클래스 private 멤버   : _ 접두사
 *   - 클래스 멤버(함수/변수): 모듈약어 접두사 미사용
 *   - 클래스 정적 멤버      : s_ 접두사
 *   - 함수 로컬 변수        : v_ 접두사
 *   - 함수 인자             : p_ 접두사
 * ------------------------------------------------------
 */

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <string.h>

#include "A10_Const_014.h"
#include "C10_ConfigManager_021.h"
#include "D10_Logger_016.h"

// ------------------------------------------------------
// N10 런타임 상태 구조체
// ------------------------------------------------------
typedef struct {
	uint8_t	 runMode;			 // 0=OFF,1=SCHEDULE,2=USER_PROFILE
	uint8_t	 runSource;			 // 0=UNKNOWN,1=BUTTON,2=WEB,3=API
	int16_t	 lastScheduleNo;	 // 마지막 선택 스케줄 번호
	int16_t	 lastUserProfileNo;	 // 마지막 선택 유저 프로파일 번호
	bool	 autoOffEnabled;
	uint32_t autoOffMinutes;
	bool	 overrideEnabled;
	uint8_t	 overrideMode;	// 0=NONE,1=FIXED,2=PRESET
	float	 overrideFixedPercent;
	char	 overridePresetCode[24];
	char	 overrideStyleCode[24];
} ST_N10_RuntimeState_t;

// ------------------------------------------------------
// Dirty Flag 구조체
// ------------------------------------------------------
typedef struct {
	bool runtime;
	bool schedules;
	bool userProfiles;
	bool motion;
	bool system;
	bool wifi;
	bool windDict;
} ST_N10_DirtyFlags_t;

// ------------------------------------------------------
// N10 NVS Manager
// ------------------------------------------------------
class CL_N10_NvsManager {
   public:
	static constexpr const char* G_N10_NS_RUNTIME		= "SNW_RUN";
	static constexpr uint32_t	 G_N10_SAVE_INTERVAL_MS = 10UL * 1000UL;  // 최소 저장 주기 10초

   public:
	// ==================================================
	// 초기화 / 종료
	// ==================================================
	static bool N10_begin() {
		if (s_initialized)
			return true;

		if (!s_prefs.begin(G_N10_NS_RUNTIME, false)) {
			CL_D10_Logger::log(EN_L10_LOG_ERROR, "[N10] NVS begin failed (ns=%s)", G_N10_NS_RUNTIME);
			return false;
		}

		memset(&s_state, 0, sizeof(s_state));
		memset(&s_dirty, 0, sizeof(s_dirty));

		s_state.lastScheduleNo	  = -1;
		s_state.lastUserProfileNo = -1;
		N10_loadRuntimeFromNvs();

		s_lastSaveMs  = millis();
		s_initialized = true;
		CL_D10_Logger::log(EN_L10_LOG_INFO, "[N10] Runtime Manager initialized");
		return true;
	}

	static void N10_end() {
		N10_flush(true);
		s_prefs.end();
		s_initialized = false;
	}

	// ==================================================
	// Dirty 플래그 관리
	// ==================================================
	static void markDirty(const char* p_key, bool p_flag) {
		if (!p_key || !p_key[0])
			return;

		if (strcasecmp(p_key, "runtime") == 0)
			s_dirty.runtime = p_flag;
		else if (strcasecmp(p_key, "schedules") == 0)
			s_dirty.schedules = p_flag;
		else if (strcasecmp(p_key, "userProfiles") == 0)
			s_dirty.userProfiles = p_flag;
		else if (strcasecmp(p_key, "motion") == 0)
			s_dirty.motion = p_flag;
		else if (strcasecmp(p_key, "system") == 0)
			s_dirty.system = p_flag;
		else if (strcasecmp(p_key, "wifi") == 0)
			s_dirty.wifi = p_flag;
		else if (strcasecmp(p_key, "windDict") == 0)
			s_dirty.windDict = p_flag;
	}

	static void flushIfNeeded() {
		if (!s_initialized)
			return;

		uint32_t v_now = millis();
		if (v_now - s_lastSaveMs < G_N10_SAVE_INTERVAL_MS)
			return;

		bool v_saved = false;

		// 1) 런타임 상태 저장
		if (s_dirty.runtime) {
			N10_flush(false);
			v_saved = true;
		}

		// 2) Config JSON 계열
		if (s_dirty.schedules && g_A10_config_root.schedules) {
			s_dirty.schedules = false;
			CL_C10_ConfigManager::saveSchedules();
			v_saved = true;
		}
		if (s_dirty.userProfiles && g_A10_config_root.userProfiles) {
			s_dirty.userProfiles = false;
			CL_C10_ConfigManager::saveUserProfiles();
			v_saved = true;
		}
		if (s_dirty.motion && g_A10_config_root.motion) {
			s_dirty.motion = false;
			CL_C10_ConfigManager::saveMotion();
			v_saved = true;
		}
		if (s_dirty.system && g_A10_config_root.system) {
			s_dirty.system = false;
			CL_C10_ConfigManager::saveSystem();
			v_saved = true;
		}
		if (s_dirty.wifi && g_A10_config_root.wifi) {
			s_dirty.wifi = false;
			CL_C10_ConfigManager::saveWifi();
			v_saved = true;
		}
		if (s_dirty.windDict && g_A10_config_root.windDict) {
			s_dirty.windDict = false;
			// TODO: WindProfileDict 저장 함수 필요 시 추가
			v_saved = true;
		}

		if (v_saved) {
			s_lastSaveMs = v_now;
			CL_D10_Logger::log(EN_L10_LOG_INFO, "[N10] Dirty flushed (runtime/config)");
		}
	}

	// ==================================================
	// Getter / JSON Export
	// ==================================================
	static ST_N10_RuntimeState_t N10_getState() {
		return s_state;
	}

	static void N10_toJson(JsonDocument& p_doc) {
		JsonObject o		   = p_doc["runtime"].to<JsonObject>();
		o["runMode"]		   = s_state.runMode;
		o["runSource"]		   = s_state.runSource;
		o["lastScheduleNo"]	   = s_state.lastScheduleNo;
		o["lastUserProfileNo"] = s_state.lastUserProfileNo;

		JsonObject ao = o["autoOff"].to<JsonObject>();
		ao["enabled"] = s_state.autoOffEnabled;
		ao["minutes"] = s_state.autoOffMinutes;

		JsonObject ov	   = o["override"].to<JsonObject>();
		ov["enabled"]	   = s_state.overrideEnabled;
		ov["mode"]		   = s_state.overrideMode;
		ov["fixedPercent"] = s_state.overrideFixedPercent;
		ov["presetCode"]   = s_state.overridePresetCode;
		ov["styleCode"]	   = s_state.overrideStyleCode;
	}

	// ==================================================
	// 주기 Flush (loop용)
	// ==================================================
	static void N10_tick() {
		if (!s_initialized)
			return;
		if (!s_dirty.runtime)
			return;

		uint32_t v_now = millis();
		if (v_now - s_lastSaveMs < G_N10_SAVE_INTERVAL_MS)
			return;

		N10_flush(false);
	}

	// ==================================================
	// Setter API (CT10 등에서 호출)
	// ==================================================
	static void N10_setRunMode(uint8_t p_mode, uint8_t p_source) {
		if (!s_initialized)
			N10_begin();
		if (s_state.runMode == p_mode && s_state.runSource == p_source)
			return;
		s_state.runMode	  = p_mode;
		s_state.runSource = p_source;
		s_dirty.runtime	  = true;
	}

	static void N10_setLastSchedule(int16_t p_schNo) {
		if (!s_initialized)
			N10_begin();
		if (s_state.lastScheduleNo == p_schNo)
			return;
		s_state.lastScheduleNo = p_schNo;
		s_dirty.runtime		   = true;
	}

	static void N10_setLastUserProfile(int16_t p_profileNo) {
		if (!s_initialized)
			N10_begin();
		if (s_state.lastUserProfileNo == p_profileNo)
			return;
		s_state.lastUserProfileNo = p_profileNo;
		s_dirty.runtime			  = true;
	}

	static void N10_setAutoOff(bool p_enabled, uint32_t p_minutes) {
		if (!s_initialized)
			N10_begin();
		if (s_state.autoOffEnabled == p_enabled && s_state.autoOffMinutes == p_minutes)
			return;
		s_state.autoOffEnabled = p_enabled;
		s_state.autoOffMinutes = p_minutes;
		s_dirty.runtime		   = true;
	}

	static void N10_setOverrideFixed(bool p_enabled, float p_percent) {
		if (!s_initialized)
			N10_begin();
		s_state.overrideEnabled		  = p_enabled;
		s_state.overrideMode		  = p_enabled ? 1 : 0;
		s_state.overrideFixedPercent  = p_enabled ? p_percent : 0.0f;
		s_state.overridePresetCode[0] = '\0';
		s_state.overrideStyleCode[0]  = '\0';
		s_dirty.runtime				  = true;
	}

	static void N10_setOverridePreset(bool		  p_enabled,
									  const char* p_presetCode,
									  const char* p_styleCode) {
		if (!s_initialized)
			N10_begin();
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

	static void N10_clearOverride() {
		if (!s_initialized)
			N10_begin();
		if (!s_state.overrideEnabled && s_state.overrideMode == 0)
			return;

		s_state.overrideEnabled		  = false;
		s_state.overrideMode		  = 0;
		s_state.overrideFixedPercent  = 0.0f;
		s_state.overridePresetCode[0] = '\0';
		s_state.overrideStyleCode[0]  = '\0';
		s_dirty.runtime				  = true;
	}

	static void N10_resetRuntime() {
		if (!s_initialized)
			N10_begin();
		memset(&s_state, 0, sizeof(s_state));
		s_state.lastScheduleNo	  = -1;
		s_state.lastUserProfileNo = -1;
		s_dirty.runtime			  = true;
		N10_flush(true);
	}

   private:
	static Preferences			 s_prefs;
	static bool					 s_initialized;
	static ST_N10_RuntimeState_t s_state;
	static ST_N10_DirtyFlags_t	 s_dirty;
	static uint32_t				 s_lastSaveMs;

	// --------------------------------------------------
	// 내부: NVS 로드/저장
	// --------------------------------------------------
	static void N10_loadRuntimeFromNvs() {
		s_state.runMode			  = s_prefs.getUChar("run_mode", 0);
		s_state.runSource		  = s_prefs.getUChar("run_src", 0);
		s_state.lastScheduleNo	  = s_prefs.getShort("sched_no", -1);
		s_state.lastUserProfileNo = s_prefs.getShort("uprofile_no", -1);

		s_state.autoOffEnabled = s_prefs.getUChar("autoOff_en", 0) != 0;
		s_state.autoOffMinutes = s_prefs.getULong("autoOff_min", 0);

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
		CL_D10_Logger::log(EN_L10_LOG_INFO,
						   "[N10] Load runtime: mode=%u src=%u sch=%d up=%d autoOff(%d,%lu) ovr_en=%d",
						   s_state.runMode, s_state.runSource,
						   (int)s_state.lastScheduleNo, (int)s_state.lastUserProfileNo,
						   (int)s_state.autoOffEnabled, (unsigned long)s_state.autoOffMinutes,
						   (int)s_state.overrideEnabled);
	}

	static void N10_flush(bool p_force) {
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
};

// ------------------------------------------------------
// 정적 멤버 정의
// ------------------------------------------------------
Preferences			  CL_N10_NvsManager::s_prefs;
bool				  CL_N10_NvsManager::s_initialized = false;
ST_N10_RuntimeState_t CL_N10_NvsManager::s_state;
ST_N10_DirtyFlags_t	  CL_N10_NvsManager::s_dirty = {
	  false, false, false, false, false, false, false};
uint32_t CL_N10_NvsManager::s_lastSaveMs = 0;
