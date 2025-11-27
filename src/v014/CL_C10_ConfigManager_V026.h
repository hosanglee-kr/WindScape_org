#ifndef CL_C10_CONFIGMANAGER_V026_H
#define CL_C10_CONFIGMANAGER_V026_H

#include <ArduinoJson.h>
#include <ConfigStructs.h>        // ST_A10_ConfigRoot_t, ST_A10_SystemConfig 등 구조체 정의 가정
#include <Logger.h>               // CL_D10_Logger, EN_L10_LOG_INFO 등 정의 가정
#include <freertos/semphr.h>      // Mutex 정의 가정
#include <string.h>               // strcmp, strlcpy 등을 위해 포함 가정
#include <stdlib.h>               // abs를 위해 포함 가정

// 외부에서 정의된 헬퍼 함수 및 상수 선언 가정
// 실제 프로젝트에 맞게 extern 선언 필요
extern void A10_resetSystemDefault(ST_A10_SystemConfig& p_cfg);
extern void A10_resetWindProfileDictDefault(ST_A10_WindProfileDict_t& p_cfg);
extern void A10_resetSchedulesDefault(ST_A10_SchedulesRoot_t& p_cfg);
extern void A10_resetUserProfilesDefault(ST_A10_UserProfilesRoot_t& p_cfg);
extern void A10_resetWifiDefault(ST_A10_WifiConfig& p_cfg);
extern void A10_resetMotionDefault(ST_A10_MotionConfig& p_cfg);

extern bool loadSystemConfig(ST_A10_SystemConfig& p_cfg);
extern bool loadWindProfileDict(ST_A10_WindProfileDict_t& p_cfg);
extern bool loadSchedules(ST_A10_SchedulesRoot_t& p_cfg);
extern bool loadUserProfiles(ST_A10_UserProfilesRoot_t& p_cfg);
extern bool loadWifiConfig(ST_A10_WifiConfig& p_cfg);
extern bool loadMotionConfig(ST_A10_MotionConfig& p_cfg);

extern void saveSystemConfig(const ST_A10_SystemConfig& p_cfg);
extern void saveWifiConfig(const ST_A10_WifiConfig& p_cfg);
extern void saveMotionConfig(const ST_A10_MotionConfig& p_cfg);
extern void saveSchedules(const ST_A10_SchedulesRoot_t& p_cfg);
extern void saveUserProfiles(const ST_A10_UserProfilesRoot_t& p_cfg);
// windDict는 saveAll에서 제외되었으므로 saveWindProfileDict 함수는 필요에 따라 추가

extern EN_A10_MODE_t A10_modeFromString(const char* p_mode);
extern void toJson_System(const ST_A10_SystemConfig& p, JsonDocument& d);
extern void toJson_Wifi(const ST_A10_WifiConfig& p, JsonDocument& d);
extern void toJson_Motion(const ST_A10_MotionConfig& p, JsonDocument& d);
extern void toJson_Schedules(const ST_A10_SchedulesRoot_t& p, JsonDocument& d);
extern void toJson_UserProfiles(const ST_A10_UserProfilesRoot_t& p, JsonDocument& d);

// Mutex Timeout 정의 (외부 정의 가정)
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

	// =====================================================
	// 2. All Config → JSON Export
	// =====================================================
	static void toJson_All(
		const ST_A10_ConfigRoot_t& p,
		JsonDocument& d,
		bool includeSystem = true,
		bool includeWifi = true,
		bool includeMotion = true,
		bool includeSchedules = true,
		bool includeUserProfiles = true);

    // =====================================================
	// 3. JSON Patch (System, Wifi, Motion)
	// =====================================================
    static bool patchSystemFromJson(ST_A10_SystemConfig& p_config, const JsonDocument& p_patch);
    static bool patchWifiFromJson(ST_A10_WifiConfig& p_config, const JsonDocument& p_patch);
    static bool patchMotionFromJson(ST_A10_MotionConfig& p_config, const JsonDocument& p_patch);
    
    // =====================================================
	// 4. JSON Patch (Schedules, UserProfiles)
	// =====================================================
	static bool patchSchedulesFromJson(ST_A10_SchedulesRoot_t& p_cfg, const JsonDocument& p_patch);
	static bool patchUserProfilesFromJson(ST_A10_UserProfilesRoot_t& p_cfg, const JsonDocument& p_patch);


    // ===================================================== 
    // 5. Wind Profile CRUD
    // (원본 코드에서 inline이었으나, 분리를 위해 일반 static 함수로 선언)
    // ===================================================== 
    static int addWindProfileFromJson(const JsonDocument& p_doc);
    static bool updateWindProfileFromJson(uint16_t p_id, const JsonDocument& p_patch);
    static bool deleteWindProfile(uint16_t p_id);

    // ===================================================== 
    // 6. Schedules CRUD
    // (원본 코드에서 inline이었으나, 분리를 위해 일반 static 함수로 선언)
    // ===================================================== 
    // 참고: addScheduleFromJson은 원본 코드에 없었으므로 제외
    static bool updateScheduleFromJson(uint16_t p_id, const JsonDocument& p_patch);
    static bool deleteSchedule(uint16_t p_id);
    
    // =====================================================
	// 7. Factory Reset
	// =====================================================
	static bool factoryResetFromDefault();

private:
    // 정적 Dirty Flag 선언 및 인라인 초기화
	inline static bool _dirty_system       = false;
	inline static bool _dirty_wifi         = false;
	inline static bool _dirty_motion       = false;
	inline static bool _dirty_schedules    = false;
	inline static bool _dirty_userProfiles = false;
  inline static bool _dirty_windProfile  = false; // Wind Profile CRUD에 사용

    // 정적 Mutex 선언 및 인라인 초기화
	inline static SemaphoreHandle_t s_configMutex = xSemaphoreCreateMutex();

};

// 전역 Config Root (포인터 보관용)
inline ST_A10_ConfigRoot_t g_A10_config_root;

#endif // CL_C10_CONFIGMANAGER_V026_H
