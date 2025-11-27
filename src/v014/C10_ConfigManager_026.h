#ifndef CL_C10_CONFIGMANAGER_V026_H
#define CL_C10_CONFIGMANAGER_V026_H

#include <ArduinoJson.h>
#include "A10_Const_015.h"        // 사용자 요청 반영

//#include <ConfigStructs.h>        // ST_A10_ConfigRoot_t, ST_A10_SystemConfig 등 구조체 정의 가정
#include "D10_Logger_016.h"       // 사용자 요청 반영
#include <freertos/semphr.h>      
#include <string.h>               
#include <stdlib.h>               


// 외부에서 정의된 헬퍼 함수 (리셋, 모드 변환, 파일 I/O) 선언 가정
// ConfigManager의 책임 분리를 위해 최소한의 외부 I/O와 기본값만 extern으로 남깁니다.
extern bool ioLoadJson(const char* p_path, const char* p_bak_path, JsonDocument& p_doc);
extern bool ioSaveJson(const char* p_path, const char* p_bak_path, const JsonDocument& p_doc);
extern EN_A10_MODE_t A10_modeFromString(const char* p_mode);

// Mutex Timeout 정의 (A10_Const_015.h에 정의되지 않았을 경우를 대비)
#ifndef MUTEX_TIMEOUT
#define MUTEX_TIMEOUT pdMS_TO_TICKS(100)
#endif


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

    static void saveSystemConfig(const ST_A10_SystemConfig& p_cfg);
    static void saveWifiConfig(const ST_A10_WifiConfig& p_cfg);
    static void saveMotionConfig(const ST_A10_MotionConfig& p_cfg);
    static void saveSchedules(const ST_A10_SchedulesRoot_t& p_cfg);
    static void saveUserProfiles(const ST_A10_UserProfilesRoot_t& p_cfg);


	// =====================================================
	// 3. JSON Export (새로운 Static Member 함수)
	// =====================================================
	static void toJson_All(
		const ST_A10_ConfigRoot_t& p,
		JsonDocument& d,
		bool includeSystem = true,
		bool includeWifi = true,
		bool includeMotion = true,
		bool includeSchedules = true,
		bool includeUserProfiles = true);
        
    static void toJson_System(const ST_A10_SystemConfig& p, JsonDocument& d);
    static void toJson_Wifi(const ST_A10_WifiConfig& p, JsonDocument& d);
    static void toJson_Motion(const ST_A10_MotionConfig& p, JsonDocument& d);
    static void toJson_Schedules(const ST_A10_SchedulesRoot_t& p, JsonDocument& d);
    static void toJson_UserProfiles(const ST_A10_UserProfilesRoot_t& p, JsonDocument& d);


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

};

// 전역 Config Root (포인터 보관용)
inline ST_A10_ConfigRoot_t g_A10_config_root;

#endif // CL_C10_CONFIGMANAGER_V026_H
