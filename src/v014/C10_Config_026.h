#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : C10_Config_026.h
 * 모듈 약어 : C10
 * 모듈명 : Smart Nature Wind Configuration Manager
 * ------------------------------------------------------
 * 기능 요약:
 *  - Smart Nature Wind 전체 설정(JSON 기반) 관리 매니저
 *  - 설정 파일 단위 분리 관리 (system / wifi / motion / schedules / userProfiles / windProfile)
 *  - 구조체 ↔ JSON 직렬화 및 역직렬화 (ArduinoJson v7 전용)
 *  - 파일 백업(.bak) / 복구 / 공장초기화(factoryResetFromDefault) 지원
 *  - PATCH 기반 부분 업데이트(patchConfigFromJson) 지원
 *  - Lazy-Load 하이브리드 구성 (필요 섹션만 동적 로드)
 *  - Wi-Fi 등 재초기화 판단 로직 확장 가능
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

#include <ArduinoJson.h>
#include <freertos/semphr.h>      
#include <string.h>               
#include <stdlib.h>               

#include "A10_Const_015.h"        // ST_A10_ConfigRoot_t, ST_A10_SystemConfig 등 구조체 정의 가정
#include "D10_Logger_016.h"       


// 외부에서 정의된 헬퍼 함수 (리셋, 모드 변환, 파일 I/O) 선언 가정
// ConfigManager의 책임 분리를 위해 최소한의 외부 I/O와 기본값만 extern으로 남깁니다.

extern EN_A10_segment_mode_t A10_modeFromString(const char* p_mode);

// Mutex Timeout 정의 (A10_Const_015.h에 정의되지 않았을 경우를 대비)
#ifndef G_C10_MUTEX_TIMEOUT
	#define G_C10_MUTEX_TIMEOUT pdMS_TO_TICKS(100)
#endif



// @brief 뮤텍스를 획득합니다.
#define C10_MUTEX_ACQUIRE() \
    if (!CL_C10_ConfigManager::_mutex_Acquire(__func__)) { \
        return false; \
    }

// @brief 설정 뮤텍스를 해제합니다.
#define C10_MUTEX_RELEASE() \
    CL_C10_ConfigManager::_mutex_Release();


/* =====================================================
	* 공용: JSON IO Helper
	* ===================================================== */
bool ioLoadJson(const char* p_path, const char* p_bak, JsonDocument& p_doc) {
	if (!LittleFS.exists(p_path)) {
		if (p_bak && LittleFS.exists(p_bak)) {
			LittleFS.rename(p_bak, p_path);
			CL_D10_Logger::log(EN_L10_LOG_WARN,
								"[C10] Restored from backup: %s -> %s",
								p_bak, p_path);
		} else {
			CL_D10_Logger::log(EN_L10_LOG_ERROR,
								"[C10] Missing config & no backup: %s",
								p_path);
			return false;
		}
	}

	File v_f = LittleFS.open(p_path, "r");
	if (!v_f) {
		CL_D10_Logger::log(EN_L10_LOG_ERROR,
							"[C10] Open failed: %s", p_path);
		return false;
	}

	auto v_e = deserializeJson(p_doc, v_f);
	v_f.close();
	if (v_e) {
		CL_D10_Logger::log(EN_L10_LOG_ERROR,
							"[C10] Parse error(%s): %s",
							p_path, v_e.c_str());
		return false;
	}
	return true;
}


bool ioSaveJson(const char*		   p_path,
						const char*		   p_bak,
						const JsonDocument& p_doc) {
	if (LittleFS.exists(p_path)) {
		if (p_bak && LittleFS.exists(p_bak)) {
			LittleFS.remove(p_bak);
		}
		if (p_bak) {
			LittleFS.rename(p_path, p_bak);
		}
	}

	File v_f = LittleFS.open(p_path, "w");
	if (!v_f) {
		CL_D10_Logger::log(EN_L10_LOG_ERROR,
							"[C10] Save open failed: %s", p_path);
		return false;
	}
	if (serializeJsonPretty(p_doc, v_f) == 0) {
		CL_D10_Logger::log(EN_L10_LOG_ERROR,
							"[C10] Save write failed: %s", p_path);
		v_f.close();
		return false;
	}
	v_f.close();
	return true;
}

class CL_C10_ConfigManager {
public:
    // =====================================================
	// 1. 전체 관리 (Load/Free/Save)
	// =====================================================
	static bool loadAll(ST_A10_ConfigRoot_t& p_root);
	static void freeLazySection(const char* p_section, ST_A10_ConfigRoot_t& p_root);
	static void freeAll(ST_A10_ConfigRoot_t& p_root);
	static void saveAll(const ST_A10_ConfigRoot_t& p_root);
    static bool factoryResetFromDefault();


    // =====================================================
	// 2. 목적물별 Load/Save 구현 (새로운 Static Member 함수)
	// =====================================================
    static bool loadSystemConfig(ST_A10_SystemConfig& p_cfg);
    static bool loadWindProfileDict(ST_A10_WindProfileDict_t& p_cfg);
    static bool loadSchedules(ST_A10_SchedulesRoot_t& p_cfg);
    static bool loadUserProfiles(ST_A10_UserProfilesRoot_t& p_cfg);
    static bool loadWifiConfig(ST_A10_WifiConfig& p_cfg);
    static bool loadMotionConfig(ST_A10_MotionConfig& p_cfg);

    static bool saveSystemConfig(const ST_A10_SystemConfig& p_cfg);
    static bool saveWifiConfig(const ST_A10_WifiConfig& p_cfg);
    static bool saveMotionConfig(const ST_A10_MotionConfig& p_cfg);
    static bool saveSchedules(const ST_A10_SchedulesRoot_t& p_cfg);
    static bool saveUserProfiles(const ST_A10_UserProfilesRoot_t& p_cfg);


	// =====================================================
	// 3. JSON Export (새로운 Static Member 함수)
	// =====================================================
	static void toJson_All(const ST_A10_ConfigRoot_t& p, JsonDocument& d, bool includeSystem = true, bool includeWifi = true, bool includeMotion = true, bool includeSchedules = true, bool includeUserProfiles = true);
        
    static void toJson_System(const ST_A10_SystemConfig& p, JsonDocument& d);
    static void toJson_Wifi(const ST_A10_WifiConfig& p, JsonDocument& d);
    static void toJson_Motion(const ST_A10_MotionConfig& p, JsonDocument& d);
    static void toJson_Schedules(const ST_A10_SchedulesRoot_t& p, JsonDocument& d);
    static void toJson_UserProfiles(const ST_A10_UserProfilesRoot_t& p, JsonDocument& d);
    static void toJson_WindProfileDict(const ST_A10_WindProfileDict_t& p, JsonDocument& d);


    // =====================================================
	// 4. JSON Patch 및 CRUD
	// =====================================================
    static bool patchSystemFromJson(ST_A10_SystemConfig& p_config, const JsonDocument& p_patch);
    static bool patchWifiFromJson(ST_A10_WifiConfig& p_config, const JsonDocument& p_patch);
    static bool patchMotionFromJson(ST_A10_MotionConfig& p_config, const JsonDocument& p_patch);
    
	static bool patchSchedulesFromJson(ST_A10_SchedulesRoot_t& p_cfg, const JsonDocument& p_patch);
	static bool patchUserProfilesFromJson(ST_A10_UserProfilesRoot_t& p_cfg, const JsonDocument& p_patch);


    // Wind Profile CRUD
    static int addWindProfileFromJson(const JsonDocument& p_doc);
    static bool updateWindProfileFromJson(uint16_t p_id, const JsonDocument& p_patch);
    static bool deleteWindProfile(uint16_t p_id);

    // Schedules CRUD
    static bool updateScheduleFromJson(uint16_t p_id, const JsonDocument& p_patch);
    static bool deleteSchedule(uint16_t p_id);
    

private:
    // 정적 Dirty Flag 선언 및 인라인 초기화
	inline static bool _dirty_system       = false;
	inline static bool _dirty_wifi         = false;
	inline static bool _dirty_motion       = false;
	inline static bool _dirty_schedules    = false;
	inline static bool _dirty_userProfiles = false;
    inline static bool _dirty_windProfile  = false; 

    // 정적 Mutex 선언 및 인라인 초기화
	inline static SemaphoreHandle_t s_configMutex = xSemaphoreCreateMutex();

    // Reset Default 함수들은 private static으로 분리하여 ConfigManager 내부에 구현
    static void resetSystemDefault(ST_A10_SystemConfig& p_cfg);
    static void resetWindProfileDictDefault(ST_A10_WindProfileDict_t& p_cfg);
    static void resetSchedulesDefault(ST_A10_SchedulesRoot_t& p_cfg);
    static void resetUserProfilesDefault(ST_A10_UserProfilesRoot_t& p_cfg);
    static void resetWifiDefault(ST_A10_WifiConfig& p_cfg);
    static void resetMotionDefault(ST_A10_MotionConfig& p_cfg);


        // 뮤텍스 관리 헬퍼 함수 정의                   
    static bool _mutex_Acquire(const char* p_funcName) {
        
        // 성공 시 디버그 로그
        // CL_D10_Logger::log(EN_L10_LOG_DEBUG, "[C10] Mutex attempt: %s", p_funcName);
        
        // 뮤텍스 획득 시도
        if (xSemaphoreTake(s_configMutex, G_C10_MUTEX_TIMEOUT) != pdTRUE) {
            // 실패 시 에러 로그
            CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] %s() Mutex timeout!", p_funcName);
            return false;
        }
        return true;
    }
    
    static void _mutex_Release() {
        xSemaphoreGive(s_configMutex);
    }

};

// 전역 Config Root (포인터 보관용)
inline ST_A10_ConfigRoot_t g_A10_config_root;

