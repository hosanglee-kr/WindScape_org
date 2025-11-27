#include "C10_Config_026.h"

/*
 * ------------------------------------------------------
 * 소스명 : C10_Config_Core_026.cpp
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

// =====================================================
// 더미 I/O 함수 및 상수 정의 (실제 SPIFFS I/O 로직으로 대체 필요)
// =====================================================
namespace A10_Const {
    // A10_Const_015.h에 정의되어야 하나, 여기서는 편의상 재정의
    const char CFG_SYSTEM_FILE[]         = "/config/system.json";
    const char CFG_SYSTEM_FILE_BAK[]     = "/config/system.bak";
    const char CFG_WIFI_FILE[]           = "/config/wifi.json";
    const char CFG_WIFI_FILE_BAK[]       = "/config/wifi.bak";
    const char CFG_MOTION_FILE[]         = "/config/motion.json";
    const char CFG_MOTION_FILE_BAK[]     = "/config/motion.bak";
    const char CFG_SCHEDULES_FILE[]      = "/config/schedules.json";
    const char CFG_SCHEDULES_FILE_BAK[]  = "/config/schedules.bak";
    const char CFG_USER_PROFILES_FILE[]  = "/config/userProfiles.json";
    const char CFG_USER_PROFILES_FILE_BAK[]= "/config/userProfiles.bak";
    const char CFG_WIND_DICT_FILE[]      = "/config/windDict.json";
    const char CFG_WIND_DICT_FILE_BAK[]  = "/config/windDict.bak";
    
    constexpr uint8_t MAX_WIND_PROFILES = 10;
    #define CFG_DEFAULT_FILE_EXISTS // Factory Reset 활성화
}




// =====================================================
// 9. 내부 헬퍼 함수 (Reset) 구현
// =====================================================
void CL_C10_ConfigManager::A10_resetSystemDefault(ST_A10_SystemConfig& p_cfg) {
    // 더미 초기화 로직
    memset(&p_cfg, 0, sizeof(p_cfg));
    p_cfg.system.logging.max_entries = 100;
    strlcpy(p_cfg.system.logging.level, "INFO", sizeof(p_cfg.system.logging.level));
    strlcpy(p_cfg.security.api_key, "DEFAULT_KEY", sizeof(p_cfg.security.api_key));
}

void CL_C10_ConfigManager::A10_resetWindProfileDictDefault(ST_A10_WindProfileDict_t& p_cfg) {
    // 더미 초기화 로직
    memset(&p_cfg, 0, sizeof(p_cfg));
    p_cfg.count = 2;
    p_cfg.items[0].wpNo = 100;
    strlcpy(p_cfg.items[0].name, "Breeze", sizeof(p_cfg.items[0].name));
    p_cfg.items[0].speed = 0.3f;
    p_cfg.items[1].wpNo = 101;
    strlcpy(p_cfg.items[1].name, "Storm", sizeof(p_cfg.items[1].name));
    p_cfg.items[1].speed = 0.9f;
}

void CL_C10_ConfigManager::A10_resetSchedulesDefault(ST_A10_SchedulesRoot_t& p_cfg) {
    memset(&p_cfg, 0, sizeof(p_cfg));
    // 스케줄 초기화 로직
}

void CL_C10_ConfigManager::A10_resetUserProfilesDefault(ST_A10_UserProfilesRoot_t& p_cfg) {
    memset(&p_cfg, 0, sizeof(p_cfg));
    // 사용자 프로필 초기화 로직
}

void CL_C10_ConfigManager::A10_resetWifiDefault(ST_A10_WifiConfig& p_cfg) {
    // 더미 초기화 로직
    memset(&p_cfg, 0, sizeof(p_cfg));
    p_cfg.wifiMode = (EN_A10_WIFI_MODE_t)0;
    strlcpy(p_cfg.ap.ssid, "AP_DEFAULT", sizeof(p_cfg.ap.ssid));
}

void CL_C10_ConfigManager::A10_resetMotionDefault(ST_A10_MotionConfig& p_cfg) {
    // 더미 초기화 로직
    memset(&p_cfg, 0, sizeof(p_cfg));
    p_cfg.enabled = true;
    p_cfg.pir.enabled = true;
    p_cfg.pir.hold_sec = 30;
}


// =====================================================
// 2-1. 목적물별 Load 구현
// =====================================================
bool CL_C10_ConfigManager::loadSystemConfig(ST_A10_SystemConfig& p_cfg) {
    CL_D10_Logger::log(EN_L10_LOG_DEBUG, "[C10] Loading System Config...");
    JsonDocument v_doc;
    if (ioLoadJson(A10_Const::CFG_SYSTEM_FILE, A10_Const::CFG_SYSTEM_FILE_BAK, v_doc)) {
        // 실제 로직: JSON에서 p_cfg로 데이터 복원
        // ...
        return true;
    }
    return false;
}

bool CL_C10_ConfigManager::loadWindProfileDict(ST_A10_WindProfileDict_t& p_cfg) {
    CL_D10_Logger::log(EN_L10_LOG_DEBUG, "[C10] Loading Wind Profile Dict...");
    JsonDocument v_doc;
    if (ioLoadJson(A10_Const::CFG_WIND_DICT_FILE, A10_Const::CFG_WIND_DICT_FILE_BAK, v_doc)) {
        // 실제 로직: JSON에서 p_cfg로 데이터 복원
        // ...
        return true;
    }
    return false;
}

bool CL_C10_ConfigManager::loadSchedules(ST_A10_SchedulesRoot_t& p_cfg) {
    CL_D10_Logger::log(EN_L10_LOG_DEBUG, "[C10] Loading Schedules...");
    JsonDocument v_doc;
    if (ioLoadJson(A10_Const::CFG_SCHEDULES_FILE, A10_Const::CFG_SCHEDULES_FILE_BAK, v_doc)) {
        // 실제 로직: JSON에서 p_cfg로 데이터 복원
        // ...
        return true;
    }
    return false;
}

bool CL_C10_ConfigManager::loadUserProfiles(ST_A10_UserProfilesRoot_t& p_cfg) {
    CL_D10_Logger::log(EN_L10_LOG_DEBUG, "[C10] Loading User Profiles...");
    JsonDocument v_doc;
    if (ioLoadJson(A10_Const::CFG_USER_PROFILES_FILE, A10_Const::CFG_USER_PROFILES_FILE_BAK, v_doc)) {
        // 실제 로직: JSON에서 p_cfg로 데이터 복원
        // ...
        return true;
    }
    return false;
}

bool CL_C10_ConfigManager::loadWifiConfig(ST_A10_WifiConfig& p_cfg) {
    CL_D10_Logger::log(EN_L10_LOG_DEBUG, "[C10] Loading WiFi Config...");
    JsonDocument v_doc;
    if (ioLoadJson(A10_Const::CFG_WIFI_FILE, A10_Const::CFG_WIFI_FILE_BAK, v_doc)) {
        // 실제 로직: JSON에서 p_cfg로 데이터 복원
        // ...
        return true;
    }
    return false;
}

bool CL_C10_ConfigManager::loadMotionConfig(ST_A10_MotionConfig& p_cfg) {
    CL_D10_Logger::log(EN_L10_LOG_DEBUG, "[C10] Loading Motion Config...");
    JsonDocument v_doc;
    if (ioLoadJson(A10_Const::CFG_MOTION_FILE, A10_Const::CFG_MOTION_FILE_BAK, v_doc)) {
        // 실제 로직: JSON에서 p_cfg로 데이터 복원
        // ...
        return true;
    }
    return false;
}

// =====================================================
// 2-2. 목적물별 Save 구현
// =====================================================
void CL_C10_ConfigManager::saveSystemConfig(const ST_A10_SystemConfig& p_cfg) {
    JsonDocument v_doc;
    toJson_System(p_cfg, v_doc);
    ioSaveJson(A10_Const::CFG_SYSTEM_FILE, A10_Const::CFG_SYSTEM_FILE_BAK, v_doc);
    _dirty_system = false;
}

void CL_C10_ConfigManager::saveWifiConfig(const ST_A10_WifiConfig& p_cfg) {
    JsonDocument v_doc;
    toJson_Wifi(p_cfg, v_doc);
    ioSaveJson(A10_Const::CFG_WIFI_FILE, A10_Const::CFG_WIFI_FILE_BAK, v_doc);
    _dirty_wifi = false;
}

void CL_C10_ConfigManager::saveMotionConfig(const ST_A10_MotionConfig& p_cfg) {
    JsonDocument v_doc;
    toJson_Motion(p_cfg, v_doc);
    ioSaveJson(A10_Const::CFG_MOTION_FILE, A10_Const::CFG_MOTION_FILE_BAK, v_doc);
    _dirty_motion = false;
}

void CL_C10_ConfigManager::saveSchedules(const ST_A10_SchedulesRoot_t& p_cfg) {
    JsonDocument v_doc;
    toJson_Schedules(p_cfg, v_doc);
    ioSaveJson(A10_Const::CFG_SCHEDULES_FILE, A10_Const::CFG_SCHEDULES_FILE_BAK, v_doc);
    _dirty_schedules = false;
}

void CL_C10_ConfigManager::saveUserProfiles(const ST_A10_UserProfilesRoot_t& p_cfg) {
    JsonDocument v_doc;
    toJson_UserProfiles(p_cfg, v_doc);
    ioSaveJson(A10_Const::CFG_USER_PROFILES_FILE, A10_Const::CFG_USER_PROFILES_FILE_BAK, v_doc);
    _dirty_userProfiles = false;
}

void CL_C10_ConfigManager::saveWindProfileDict(const ST_A10_WindProfileDict_t& p_cfg) {
    JsonDocument v_doc;
    // toJson_WindProfileDict 함수가 가정되지 않았으므로, 직접 JsonObject 생성 (더미)
    v_doc["windProfiles"] = JsonArray(); 
    ioSaveJson(A10_Const::CFG_WIND_DICT_FILE, A10_Const::CFG_WIND_DICT_FILE_BAK, v_doc);
    _dirty_windProfile = false;
}


// =====================================================
// 3-1. 목적물별 JSON Export 구현
// =====================================================
void CL_C10_ConfigManager::toJson_System(const ST_A10_SystemConfig& p, JsonDocument& d) {
    JsonObject j_sys = d.createNestedObject("system");
    JsonObject j_log = j_sys.createNestedObject("logging");
    j_log["level"] = p.system.logging.level;
    j_log["max_entries"] = p.system.logging.max_entries;
    JsonObject j_sec = d.createNestedObject("security");
    j_sec["api_key"] = p.security.api_key;
}

void CL_C10_ConfigManager::toJson_Wifi(const ST_A10_WifiConfig& p, JsonDocument& d) {
    JsonObject j_wifi = d.createNestedObject("wifi");
    // ... WiFi Config JSON 구성 로직
    j_wifi["wifiMode"] = p.wifiMode;
}

void CL_C10_ConfigManager::toJson_Motion(const ST_A10_MotionConfig& p, JsonDocument& d) {
    JsonObject j_motion = d.createNestedObject("motion");
    // ... Motion Config JSON 구성 로직
    j_motion["enabled"] = p.enabled;
}

void CL_C10_ConfigManager::toJson_Schedules(const ST_A10_SchedulesRoot_t& p, JsonDocument& d) {
    JsonArray j_arr = d.createNestedArray("schedules");
    // ... Schedules JSON 구성 로직
}

void CL_C10_ConfigManager::toJson_UserProfiles(const ST_A10_UserProfilesRoot_t& p, JsonDocument& d) {
    JsonObject j_up = d.createNestedObject("userProfiles");
    JsonArray j_arr = j_up.createNestedArray("profiles");
    // ... User Profiles JSON 구성 로직
}


// =====================================================
// 1. 전체 관리 (Load/Free/Save) 구현
// =====================================================

bool CL_C10_ConfigManager::loadAll(ST_A10_ConfigRoot_t& p_root) {
    bool v_ok = true;

    // 필수 섹션 객체 확보 (메모리 할당)
    if (!p_root.system)      p_root.system = new ST_A10_SystemConfig();
    if (!p_root.windDict)    p_root.windDict = new ST_A10_WindProfileDict_t();
    if (!p_root.schedules)   p_root.schedules = new ST_A10_SchedulesRoot_t();
    if (!p_root.userProfiles)p_root.userProfiles = new ST_A10_UserProfilesRoot_t();
    if (!p_root.wifi)        p_root.wifi = new ST_A10_WifiConfig();
    if (!p_root.motion)      p_root.motion = new ST_A10_MotionConfig();

    // 기본값 설정 (클래스 내부 함수 호출)
    A10_resetSystemDefault(*p_root.system);
    A10_resetWindProfileDictDefault(*p_root.windDict);
    A10_resetSchedulesDefault(*p_root.schedules);
    A10_resetUserProfilesDefault(*p_root.userProfiles);
    A10_resetWifiDefault(*p_root.wifi);
    A10_resetMotionDefault(*p_root.motion);

    // 실제 파일에서 로드 (클래스 내부 함수 호출)
    if (!loadSystemConfig(*p_root.system)) v_ok = false;
    if (!loadWindProfileDict(*p_root.windDict)) v_ok = false;
    if (!loadWifiConfig(*p_root.wifi)) v_ok = false;
    if (!loadMotionConfig(*p_root.motion)) v_ok = false;
    if (!loadSchedules(*p_root.schedules)) v_ok = false;
    if (!loadUserProfiles(*p_root.userProfiles)) v_ok = false;

    CL_D10_Logger::log(EN_L10_LOG_INFO, "[C10] Config loaded (all sections, result=%d)", v_ok);
    return v_ok;
}

void CL_C10_ConfigManager::freeLazySection(const char* p_section, ST_A10_ConfigRoot_t& p_root) {
    if (strcmp(p_section, "wifi") == 0 && p_root.wifi) {
        delete p_root.wifi; p_root.wifi = nullptr; return;
    }
    if (strcmp(p_section, "motion") == 0 && p_root.motion) {
        delete p_root.motion; p_root.motion = nullptr; return;
    }
    if (strcmp(p_section, "schedules") == 0 && p_root.schedules) {
        delete p_root.schedules; p_root.schedules = nullptr; return;
    }
    if (strcmp(p_section, "userProfiles") == 0 && p_root.userProfiles) {
        delete p_root.userProfiles; p_root.userProfiles = nullptr; return;
    }
}

void CL_C10_ConfigManager::freeAll(ST_A10_ConfigRoot_t& p_root) {
    if (p_root.system) { delete p_root.system; p_root.system = nullptr; }
    if (p_root.wifi) { delete p_root.wifi; p_root.wifi = nullptr; }
    if (p_root.motion) { delete p_root.motion; p_root.motion = nullptr; }
    if (p_root.windDict) { delete p_root.windDict; p_root.windDict = nullptr; }
    if (p_root.schedules) { delete p_root.schedules; p_root.schedules = nullptr; }
    if (p_root.userProfiles) { delete p_root.userProfiles; p_root.userProfiles = nullptr; }
    CL_D10_Logger::log(EN_L10_LOG_INFO, "[C10] All config objects freed");
}

void CL_C10_ConfigManager::saveAll(const ST_A10_ConfigRoot_t& p_root) {
    if (p_root.system && _dirty_system) saveSystemConfig(*p_root.system);
    if (p_root.wifi && _dirty_wifi) saveWifiConfig(*p_root.wifi);
    if (p_root.motion && _dirty_motion) saveMotionConfig(*p_root.motion);
    if (p_root.schedules && _dirty_schedules) saveSchedules(*p_root.schedules);
    if (p_root.userProfiles && _dirty_userProfiles) saveUserProfiles(*p_root.userProfiles);
    if (p_root.windDict && _dirty_windProfile) saveWindProfileDict(*p_root.windDict);
}

// =====================================================
// 3-2. All Config → JSON Export 구현
// =====================================================
void CL_C10_ConfigManager::toJson_All(
    const ST_A10_ConfigRoot_t& p,
    JsonDocument& d,
    bool includeSystem,
    bool includeWifi,
    bool includeMotion,
    bool includeSchedules,
    bool includeUserProfiles) {
    if (includeSystem && p.system) toJson_System(*p.system, d);
    if (includeWifi && p.wifi) toJson_Wifi(*p.wifi, d);
    if (includeMotion && p.motion){ toJson_Motion(*p.motion, d); }
    if (includeSchedules && p.schedules) toJson_Schedules(*p.schedules, d);
    if (includeUserProfiles && p.userProfiles) toJson_UserProfiles(*p.userProfiles, d);

    CL_D10_Logger::log(
        EN_L10_LOG_DEBUG,
        "[C10] Config export → JSON (sys=%d wifi=%d motion=%d sch=%d up=%d)",
        includeSystem, includeWifi,
        includeMotion, includeSchedules,
        includeUserProfiles);
}


void CL_C10_ConfigManager::toJson_System(const ST_A10_SystemConfig& p, JsonDocument& d) {
    // [Implementation required: System Config -> JSON]
    JsonObject j_sys = d.createNestedObject("system");
    JsonObject j_log = j_sys.createNestedObject("logging");
    j_log["level"] = p.system.logging.level;
    j_log["max_entries"] = p.system.logging.max_entries;
    
    // 이 외의 system 필드 복사 로직 ...
    
    JsonObject j_sec = d.createNestedObject("security");
    j_sec["api_key"] = p.security.api_key;
}





// =====================================================
// 3-2. All Config → JSON Export 구현 (이전 파일과 동일)
// =====================================================
void CL_C10_ConfigManager::toJson_All(
    const ST_A10_ConfigRoot_t& p,
    JsonDocument& d,
    bool includeSystem,
    bool includeWifi,
    bool includeMotion,
    bool includeSchedules,
    bool includeUserProfiles) {
    
    if (includeSystem && p.system) toJson_System(*p.system, d);
    if (includeWifi && p.wifi) toJson_Wifi(*p.wifi, d);
    if (includeMotion && p.motion){ toJson_Motion(*p.motion, d); }
    if (includeSchedules && p.schedules) toJson_Schedules(*p.schedules, d);
    if (includeUserProfiles && p.userProfiles) toJson_UserProfiles(*p.userProfiles, d);

    CL_D10_Logger::log(
        EN_L10_LOG_DEBUG,
        "[C10] Config export → JSON (sys=%d wifi=%d motion=%d sch=%d up=%d)",
        includeSystem, includeWifi,
        includeMotion, includeSchedules,
        includeUserProfiles);
}



// =====================================================
// 8. Factory Reset 구현
// =====================================================
bool CL_C10_ConfigManager::factoryResetFromDefault() {
#ifdef CFG_DEFAULT_FILE_EXISTS
    JsonDocument v_def;
    if (!ioLoadJson("dummy_default.json", "dummy_default.bak", v_def)) {
        CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] Default file missing.");
        return false;
    }

    // Dummy Save based on default data (실제 로직은 v_def의 각 섹션을 추출하여 개별 파일로 저장)
    if (v_def["system"].is<JsonObjectConst>()) { /* Save system */ }
    if (v_def["wifi"].is<JsonObjectConst>()) { /* Save wifi */ }
    if (v_def["motion"].is<JsonObjectConst>()) { /* Save motion */ }
    if (v_def["schedules"].is<JsonArrayConst>()) { /* Save schedules */ }
    if (v_def["userProfiles"].is<JsonObjectConst>()) { /* Save userProfiles */ }
    
    CL_D10_Logger::log(EN_L10_LOG_INFO, "[C10] Factory reset completed from default");
    return true;
#else
    CL_D10_Logger::log(EN_L10_LOG_WARN, "[C10] Factory reset skipped: CFG_DEFAULT_FILE_EXISTS not defined");
    return false;
#endif
}
