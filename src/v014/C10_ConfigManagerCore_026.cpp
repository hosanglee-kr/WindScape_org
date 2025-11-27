#include "C10_ConfigManager_V026.h"

// =====================================================
// Static Private: Reset Default 구현
// (별도의 extern 함수 대신 클래스 내부 구현으로 변경)
// =====================================================
void CL_C10_ConfigManager::resetSystemDefault(ST_A10_SystemConfig& p_cfg) {
    // 실제 로직 구현 (예시)
    p_cfg.system.logging.max_entries = 100;
    strlcpy(p_cfg.system.logging.level, "INFO", sizeof(p_cfg.system.logging.level));
    strlcpy(p_cfg.security.api_key, "DEFAULT_KEY", sizeof(p_cfg.security.api_key));
}
void CL_C10_ConfigManager::resetWindProfileDictDefault(ST_A10_WindProfileDict_t& p_cfg) {
    // 실제 로직 구현 (예시)
    p_cfg.count = 0; // 초기화
}
void CL_C10_ConfigManager::resetSchedulesDefault(ST_A10_SchedulesRoot_t& p_cfg) {
    // 실제 로직 구현 (예시)
    p_cfg.count = 0; // 초기화
}
void CL_C10_ConfigManager::resetUserProfilesDefault(ST_A10_UserProfilesRoot_t& p_cfg) {
    // 실제 로직 구현 (예시)
    p_cfg.count = 0; // 초기화
}
void CL_C10_ConfigManager::resetWifiDefault(ST_A10_WifiConfig& p_cfg) {
    // 실제 로직 구현 (예시)
    p_cfg.wifiMode = (EN_A10_WIFI_MODE_t)0; // AP
    strlcpy(p_cfg.ap.ssid, "WIFI_AP", sizeof(p_cfg.ap.ssid));
    p_cfg.sta_count = 0;
}
void CL_C10_ConfigManager::resetMotionDefault(ST_A10_MotionConfig& p_cfg) {
    // 실제 로직 구현 (예시)
    p_cfg.enabled = true;
    p_cfg.pir.enabled = true;
    p_cfg.ble.enabled = false;
}

// =====================================================
// 2. 목적물별 Load/Save 구현 (새로운 Static Member 함수)
// =====================================================

bool CL_C10_ConfigManager::loadSystemConfig(ST_A10_SystemConfig& p_cfg) {
    DynamicJsonDocument v_doc(A10_Const::JSON_DOC_SIZE_SYSTEM); // 크기 가정
    if (!ioLoadJson(A10_Const::CFG_SYSTEM_FILE, A10_Const::CFG_SYSTEM_FILE_BAK, v_doc)) {
        D10_Logger::log(EN_L10_LOG_WARN, "[C10] Sys file not found. Using default.");
        resetSystemDefault(p_cfg); // 로드 실패 시 기본값 적용
        return false;
    }
    // TODO: JSON -> ST_A10_SystemConfig 역직렬화 로직 구현
    D10_Logger::log(EN_L10_LOG_INFO, "[C10] System config loaded.");
    return true;
}

void CL_C10_ConfigManager::saveSystemConfig(const ST_A10_SystemConfig& p_cfg) {
    if (!_dirty_system) return;

    DynamicJsonDocument v_doc(A10_Const::JSON_DOC_SIZE_SYSTEM);
    toJson_System(p_cfg, v_doc); // 구조체를 JSON으로 변환
    if (ioSaveJson(A10_Const::CFG_SYSTEM_FILE, A10_Const::CFG_SYSTEM_FILE_BAK, v_doc)) {
        _dirty_system = false;
        D10_Logger::log(EN_L10_LOG_INFO, "[C10] System config saved.");
    }
}

// 나머지 load/save 함수들은 각 cpp 파일의 역할에 맞게 분산 구현해야 하지만,
// loadSystemConfig의 로직을 Core에 남기기 위해 Core에 배치합니다.

bool CL_C10_ConfigManager::loadWindProfileDict(ST_A10_WindProfileDict_t& p_cfg) {
    // Wind Profile은 Embedded/Fixed-Data일 가능성이 높으므로, 파일 로드 대신 Reset Default만 호출
    resetWindProfileDictDefault(p_cfg);
    D10_Logger::log(EN_L10_LOG_INFO, "[C10] WindProfile dict reset to default.");
    return true;
}
// saveWindProfileDict 함수는 saveAll에서 호출되지 않으므로 구현 생략

bool CL_C10_ConfigManager::loadSchedules(ST_A10_SchedulesRoot_t& p_cfg) {
    DynamicJsonDocument v_doc(A10_Const::JSON_DOC_SIZE_SCHEDULES);
    if (!ioLoadJson(A10_Const::CFG_SCHEDULES_FILE, A10_Const::CFG_SCHEDULES_FILE_BAK, v_doc)) {
        D10_Logger::log(EN_L10_LOG_WARN, "[C10] Schedules file not found. Using default.");
        resetSchedulesDefault(p_cfg);
        return false;
    }
    // TODO: JSON -> ST_A10_SchedulesRoot_t 역직렬화 로직 구현
    D10_Logger::log(EN_L10_LOG_INFO, "[C10] Schedules config loaded.");
    return true;
}
void CL_C10_ConfigManager::saveSchedules(const ST_A10_SchedulesRoot_t& p_cfg) {
    if (!_dirty_schedules) return;

    DynamicJsonDocument v_doc(A10_Const::JSON_DOC_SIZE_SCHEDULES);
    toJson_Schedules(p_cfg, v_doc);
    if (ioSaveJson(A10_Const::CFG_SCHEDULES_FILE, A10_Const::CFG_SCHEDULES_FILE_BAK, v_doc)) {
        _dirty_schedules = false;
        D10_Logger::log(EN_L10_LOG_INFO, "[C10] Schedules config saved.");
    }
}

bool CL_C10_ConfigManager::loadUserProfiles(ST_A10_UserProfilesRoot_t& p_cfg) {
    DynamicJsonDocument v_doc(A10_Const::JSON_DOC_SIZE_USER_PROFILES);
    if (!ioLoadJson(A10_Const::CFG_USER_PROFILES_FILE, A10_Const::CFG_USER_PROFILES_FILE_BAK, v_doc)) {
        D10_Logger::log(EN_L10_LOG_WARN, "[C10] UserProfiles file not found. Using default.");
        resetUserProfilesDefault(p_cfg);
        return false;
    }
    // TODO: JSON -> ST_A10_UserProfilesRoot_t 역직렬화 로직 구현
    D10_Logger::log(EN_L10_LOG_INFO, "[C10] UserProfiles config loaded.");
    return true;
}
void CL_C10_ConfigManager::saveUserProfiles(const ST_A10_UserProfilesRoot_t& p_cfg) {
    if (!_dirty_userProfiles) return;

    DynamicJsonDocument v_doc(A10_Const::JSON_DOC_SIZE_USER_PROFILES);
    toJson_UserProfiles(p_cfg, v_doc);
    if (ioSaveJson(A10_Const::CFG_USER_PROFILES_FILE, A10_Const::CFG_USER_PROFILES_FILE_BAK, v_doc)) {
        _dirty_userProfiles = false;
        D10_Logger::log(EN_L10_LOG_INFO, "[C10] UserProfiles config saved.");
    }
}

// =====================================================
// 1. 전체 관리 (Load/Free/Save)
// (loadAll에서는 위에서 구현한 static load 함수들을 호출)
// =====================================================

bool CL_C10_ConfigManager::loadAll(ST_A10_ConfigRoot_t& p_root) {
    bool v_ok = true;

    // ... (메모리 할당 로직 생략, 기존 코드 참조) ...

    // 실제 파일에서 로드 (클래스 멤버 함수 호출로 변경)
    if (!loadSystemConfig(*p_root.system)) v_ok = false;
    if (!loadWindProfileDict(*p_root.windDict)) v_ok = false;
    if (!loadWifiConfig(*p_root.wifi)) v_ok = false;
    if (!loadMotionConfig(*p_root.motion)) v_ok = false;
    if (!loadSchedules(*p_root.schedules)) v_ok = false;
    if (!loadUserProfiles(*p_root.userProfiles)) v_ok = false;

    D10_Logger::log(
        EN_L10_LOG_INFO,
        "[C10] Config loaded (all sections, result=%d)",
        v_ok);
    return v_ok;
}

// freeLazySection, freeAll, saveAll (기존 로직 유지, save* 함수만 변경)
// ... (freeLazySection, freeAll 로직 생략) ...

void CL_C10_ConfigManager::saveAll(const ST_A10_ConfigRoot_t& p_root) {
    if (p_root.system) saveSystemConfig(*p_root.system);
    if (p_root.wifi) saveWifiConfig(*p_root.wifi);
    if (p_root.motion) saveMotionConfig(*p_root.motion);
    if (p_root.schedules) saveSchedules(*p_root.schedules);
    if (p_root.userProfiles) saveUserProfiles(*p_root.userProfiles);
}


// =====================================================
// 3. JSON Export (toJson_All 및 개별 toJson_* 구현)
// =====================================================
void CL_C10_ConfigManager::toJson_All(
    const ST_A10_ConfigRoot_t& p,
    JsonDocument& d,
    bool includeSystem,
    bool includeWifi,
    bool includeMotion,
    bool includeSchedules,
    bool includeUserProfiles) {
    
    // (기존 로직 유지, 개별 toJson_* 함수 호출)
    if (includeSystem && p.system) toJson_System(*p.system, d);
    if (includeWifi && p.wifi) toJson_Wifi(*p.wifi, d);
    if (includeMotion && p.motion) toJson_Motion(*p.motion, d);
    if (includeSchedules && p.schedules) toJson_Schedules(*p.schedules, d);
    if (includeUserProfiles && p.userProfiles) toJson_UserProfiles(*p.userProfiles, d);

    D10_Logger::log(EN_L10_LOG_DEBUG, "[C10] Config export → JSON...");
}

void CL_C10_ConfigManager::toJson_System(const ST_A10_SystemConfig& p, JsonDocument& d) {
    // TODO: ST_A10_SystemConfig -> JSON 직렬화 로직 구현
}
void CL_C10_ConfigManager::toJson_Schedules(const ST_A10_SchedulesRoot_t& p, JsonDocument& d) {
    // TODO: ST_A10_SchedulesRoot_t -> JSON 직렬화 로직 구현
}
void CL_C10_ConfigManager::toJson_UserProfiles(const ST_A10_UserProfilesRoot_t& p, JsonDocument& d) {
    // TODO: ST_A10_UserProfilesRoot_t -> JSON 직렬화 로직 구현
}


// =====================================================
// 7. Factory Reset (ConfigManager_Core_V026.cpp에 위치)
// =====================================================
bool CL_C10_ConfigManager::factoryResetFromDefault() {
#ifdef A10_Const_CFG_DEFAULT_FILE_EXISTS
    // (기존 로직 유지)
    // ...
    D10_Logger::log(EN_L10_LOG_INFO, "[C10] Factory reset completed from default");
    return true;
#else
    D10_Logger::log(EN_L10_LOG_WARN, "[C10] Factory reset skipped...");
    return false;
#endif
}
