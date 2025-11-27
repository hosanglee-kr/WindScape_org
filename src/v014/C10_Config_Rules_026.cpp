#include "C10_Config_026.h"

extern ST_A10_ConfigRoot_t g_A10_config_root; 

// A10_modeFromString 함수가 외부 모듈에 있다고 가정
extern EN_A10_MODE_t A10_modeFromString(const char* p_mode) {
    if (strcmp(p_mode, "PRESET") == 0) return (EN_A10_MODE_t)1;
    if (strcmp(p_mode, "FIXED") == 0) return (EN_A10_MODE_t)2;
    return (EN_A10_MODE_t)0; // Default
}

// 임시 상수 정의 (A10_Const_015.h에 정의되어야 함)
namespace A10_Const {
    constexpr uint8_t MAX_SEGMENTS_PER_SCHEDULE = 4;
    constexpr uint8_t MAX_SEGMENTS_PER_PROFILE = 8;
    constexpr uint8_t MAX_WIND_PROFILES = 10;
}


// =====================================================
// 5. JSON Patch (Schedules, UserProfiles) 구현
// (로직은 요청에 따라 이전 응답과 동일하게 유지)
// =====================================================

bool CL_C10_ConfigManager::patchSchedulesFromJson(ST_A10_SchedulesRoot_t& p_cfg,
                                   const JsonDocument&	 p_patch) {

    if (xSemaphoreTake(s_configMutex, MUTEX_TIMEOUT) != pdTRUE) {
        CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] patchSchedulesFromJson() Mutex timeout!");
        return false;
    }
    
    JsonArrayConst arr = p_patch["schedules"].as<JsonArrayConst>();
    if (arr.isNull()) {
        xSemaphoreGive(s_configMutex);
        return false;
    }

    bool v_changed = false;

    for (JsonObjectConst j_patch : arr) {
        if (!j_patch["schNo"].is<uint16_t>()) continue;
        uint16_t v_schNo = j_patch["schNo"];

        ST_A10_ScheduleItem_t* v_item = nullptr;
        for (uint8_t i = 0; i < p_cfg.count; i++) {
            if (p_cfg.items[i].schNo == v_schNo) {
                v_item = &p_cfg.items[i];
                break;
            }
        }

        if (!v_item) continue;
        
        // --- 필드별 덮어쓰기 (PATCH 로직) ---
        // A. 기본 속성
        if (j_patch["name"].is<const char*>()) { /* ... name patch logic ... */ v_changed = true; }
        if (j_patch["enabled"].is<bool>()) { /* ... enabled patch logic ... */ v_changed = true; }

        // B. period
        JsonObjectConst j_per = j_patch["period"];
        if (!j_per.isNull()) {
            if (j_per["enabled"].is<bool>()) { /* ... period.enabled patch logic ... */ v_changed = true; }
            // ... (나머지 period 필드 패치 로직 생략)
            
            // days 배열 패치
            JsonArrayConst j_days = j_per["days"].as<JsonArrayConst>();
            if (!j_days.isNull()) { v_changed = true; /* ... days array patch logic ... */ }
        }
        
        // C. segments (배열 전체 덮어쓰기 - PUT 방식)
        JsonArrayConst j_segs = j_patch["segments"].as<JsonArrayConst>();
        if (!j_segs.isNull()) {
            v_item->seg_count = 0; 
            for (JsonObjectConst jseg : j_segs) {
                if (v_item->seg_count >= A10_Const::MAX_SEGMENTS_PER_SCHEDULE) break;
                // ... (세그먼트 복사 로직 생략)
                v_item->seg_count++;
            }
            v_changed = true;
        }
        
        // D. autoOff 필드 PATCH
        JsonObjectConst j_autoOff = j_patch["autoOff"];
        if (!j_autoOff.isNull()) { /* ... autoOff patch logic ... */ v_changed = true; }

        // E. motion 필드 PATCH
        JsonObjectConst j_motion = j_patch["motion"];
        if (!j_motion.isNull()) { /* ... motion patch logic ... */ v_changed = true; }


        if (v_changed) {
            CL_D10_Logger::log(EN_L10_LOG_DEBUG, "[C10] Schedule %u patched.", v_schNo);
        }
    }

    if (v_changed) {
        _dirty_schedules = true;
        CL_D10_Logger::log(EN_L10_LOG_INFO, "[C10] schedules config patched (Memory Only). Dirty=true");
    }

    xSemaphoreGive(s_configMutex); 
    return v_changed;
}


bool CL_C10_ConfigManager::patchUserProfilesFromJson(ST_A10_UserProfilesRoot_t& p_cfg,
                                          const JsonDocument&		 p_patch) {

    if (xSemaphoreTake(s_configMutex, MUTEX_TIMEOUT) != pdTRUE) {
        CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] patchUserProfilesFromJson() Mutex timeout!");
        return false;
    }
    
    JsonArrayConst arr = p_patch["userProfiles"]["profiles"].as<JsonArrayConst>();
    if (arr.isNull()) {
        xSemaphoreGive(s_configMutex);
        return false;
    }

    bool v_changed = false;

    for (JsonObjectConst j_patch : arr) {
        if (!j_patch["profileNo"].is<uint16_t>()) continue;
        uint16_t v_profileNo = j_patch["profileNo"];

        ST_A10_UserProfileItem_t* v_item = nullptr;
        for (uint8_t i = 0; i < p_cfg.count; i++) {
            if (p_cfg.items[i].profileNo == v_profileNo) {
                v_item = &p_cfg.items[i];
                break;
            }
        }

        if (!v_item) continue;
        
        // --- 필드별 덮어쓰기 (PATCH 로직) ---
        // A. 기본 속성
        if (j_patch["name"].is<const char*>()) { /* ... name patch logic ... */ v_changed = true; }
        if (j_patch["enabled"].is<bool>()) { /* ... enabled patch logic ... */ v_changed = true; }
        if (j_patch["repeatSegments"].is<bool>()) { /* ... repeatSegments patch logic ... */ v_changed = true; }


        // B. segments (배열 전체 덮어쓰기 - PUT 방식)
        JsonArrayConst j_segs = j_patch["segments"].as<JsonArrayConst>();
        if (!j_segs.isNull()) {
            v_item->seg_count = 0; 
            for (JsonObjectConst jseg : j_segs) {
                if (v_item->seg_count >= A10_Const::MAX_SEGMENTS_PER_PROFILE) break;
                // ... (세그먼트 복사 로직 생략)
                v_item->seg_count++;
            }
            v_changed = true;
        }
        
        // C. autoOff 필드 PATCH 
        JsonObjectConst j_autoOff = j_patch["autoOff"];
        if (!j_autoOff.isNull()) { /* ... autoOff patch logic ... */ v_changed = true; }

        // D. motion 필드 PATCH
        JsonObjectConst j_motion = j_patch["motion"];
        if (!j_motion.isNull()) { /* ... motion patch logic ... */ v_changed = true; }

        if (v_changed) {
            CL_D10_Logger::log(EN_L10_LOG_DEBUG, "[C10] UserProfile %u patched.", v_profileNo);
        }
    }

    if (v_changed) {
        _dirty_userProfiles = true;
        CL_D10_Logger::log(EN_L10_LOG_INFO, "[C10] userProfiles config patched (Memory Only). Dirty=true");
    }

    xSemaphoreGive(s_configMutex); 
    return v_changed;
}

// ===================================================== 
// 6. Wind Profile CRUD 구현
// ===================================================== 

int CL_C10_ConfigManager::addWindProfileFromJson(const JsonDocument& p_doc) {
    if (xSemaphoreTake(s_configMutex, MUTEX_TIMEOUT) != pdTRUE) { return 0; }

    ST_A10_WindProfileDict_t* v_root = g_A10_config_root.windDict;
    if (!v_root || v_root->count >= A10_Const::MAX_WIND_PROFILES) { 
        xSemaphoreGive(s_configMutex); return 0; 
    }

    uint16_t v_new_id = 0;
    for (uint8_t i = 0; i < v_root->count; i++) {
        if (v_root->items[i].wpNo > v_new_id) { v_new_id = v_root->items[i].wpNo; }
    }
    v_new_id++;
    
    ST_A10_WindProfileItem_t& v_new_item = v_root->items[v_root->count];
    memset(&v_new_item, 0, sizeof(v_new_item));
    
    v_new_item.wpNo = v_new_id;
    JsonObjectConst j_patch = p_doc.as<JsonObjectConst>();
    
    if (j_patch["name"].is<const char*>()) {
        strlcpy(v_new_item.name, j_patch["name"], sizeof(v_new_item.name));
    }
    // ... (나머지 필드 복사 로직 생략)

    v_root->count++;
    _dirty_windProfile = true;
    CL_D10_Logger::log(EN_L10_LOG_INFO, "[C10] New Wind Profile ID %u added. Dirty=true", v_new_id);
    
    xSemaphoreGive(s_configMutex);
    return v_new_id;
}


bool CL_C10_ConfigManager::updateWindProfileFromJson(uint16_t p_id, const JsonDocument& p_patch) {
    if (xSemaphoreTake(s_configMutex, MUTEX_TIMEOUT) != pdTRUE) { return false; }

    ST_A10_WindProfileDict_t* v_root = g_A10_config_root.windDict;
    if (!v_root) { xSemaphoreGive(s_configMutex); return false; }

    ST_A10_WindProfileItem_t* v_item = nullptr;
    for (uint8_t i = 0; i < v_root->count; i++) {
        if (v_root->items[i].wpNo == p_id) { v_item = &v_root->items[i]; break; }
    }

    if (!v_item) { xSemaphoreGive(s_configMutex); return false; }

    bool v_changed = false;
    JsonObjectConst j_patch = p_patch.as<JsonObjectConst>();
    
    // name
    if (j_patch["name"].is<const char*>()) { /* ... name patch logic ... */ v_changed = true; }
    // speed (Float)
    if (j_patch["speed"].is<float>() || j_patch["speed"].is<int>()) { /* ... speed patch logic ... */ v_changed = true; }
    // ... (나머지 필드 패치 로직 생략)
    
    if (v_changed) {
        _dirty_windProfile = true;
        CL_D10_Logger::log(EN_L10_LOG_INFO, "[C10] Wind Profile ID %u patched. Dirty=true", p_id);
    }

    xSemaphoreGive(s_configMutex);
    return v_changed;
}


bool CL_C10_ConfigManager::deleteWindProfile(uint16_t p_id) {
    if (xSemaphoreTake(s_configMutex, MUTEX_TIMEOUT) != pdTRUE) { return false; }

    ST_A10_WindProfileDict_t* v_root = g_A10_config_root.windDict;
    if (!v_root) { xSemaphoreGive(s_configMutex); return false; }

    int v_del_idx = -1;
    for (uint8_t i = 0; i < v_root->count; i++) {
        if (v_root->items[i].wpNo == p_id) { v_del_idx = i; break; }
    }

    if (v_del_idx == -1) { xSemaphoreGive(s_configMutex); return false; }

    // 배열에서 항목 제거 (덮어쓰기)
    for (uint8_t i = v_del_idx; i < v_root->count - 1; i++) {
        v_root->items[i] = v_root->items[i + 1];
    }

    v_root->count--; 

    _dirty_windProfile = true;
    CL_D10_Logger::log(EN_L10_LOG_INFO, "[C10] Wind Profile ID %u deleted. count=%u. Dirty=true", p_id, v_root->count);

    xSemaphoreGive(s_configMutex);
    return true;
}

// ===================================================== 
// 7. Schedules CRUD 구현
// ===================================================== 

bool CL_C10_ConfigManager::updateScheduleFromJson(uint16_t p_id, const JsonDocument& p_patch) {
    // Note: patchSchedulesFromJson의 로직을 그대로 사용하거나, 
    // patchSchedulesFromJson을 ID 기반으로 필터링하도록 수정하여 재활용하는 것이 일반적입니다.

    if (xSemaphoreTake(s_configMutex, MUTEX_TIMEOUT) != pdTRUE) { return false; }

    ST_A10_SchedulesRoot_t* v_root = g_A10_config_root.schedules;
    if (!v_root) { xSemaphoreGive(s_configMutex); return false; }

    ST_A10_ScheduleItem_t* v_item = nullptr;
    for (uint8_t i = 0; i < v_root->count; i++) {
        if (v_root->items[i].schNo == p_id) { v_item = &v_root->items[i]; break; }
    }

    if (!v_item) { xSemaphoreGive(s_configMutex); return false; }

    // 임시 로직: name 필드만 패치하여 변경 확인
    bool v_changed = false;
    JsonObjectConst j_patch = p_patch.as<JsonObjectConst>();
    if (j_patch["name"].is<const char*>()) { 
        const char* v_new = j_patch["name"];
        if (strcmp(v_new, v_item->name) != 0) {
            strlcpy(v_item->name, v_new, sizeof(v_item->name));
            v_changed = true;
        }
    }

    if (v_changed) {
        _dirty_schedules = true;
        CL_D10_Logger::log(EN_L10_LOG_INFO, "[C10] Schedule ID %u patched (partial). Dirty=true", p_id);
    }
    
    xSemaphoreGive(s_configMutex);
    return v_changed;
}

bool CL_C10_ConfigManager::deleteSchedule(uint16_t p_id) {
    if (xSemaphoreTake(s_configMutex, MUTEX_TIMEOUT) != pdTRUE) { return false; }

    ST_A10_SchedulesRoot_t* v_root = g_A10_config_root.schedules;
    if (!v_root) { xSemaphoreGive(s_configMutex); return false; }

    int v_del_idx = -1;
    for (uint8_t i = 0; i < v_root->count; i++) {
        if (v_root->items[i].schNo == p_id) { v_del_idx = i; break; }
    }

    if (v_del_idx == -1) { xSemaphoreGive(s_configMutex); return false; }

    // 배열에서 항목 제거 (덮어쓰기)
    for (uint8_t i = v_del_idx; i < v_root->count - 1; i++) {
        v_root->items[i] = v_root->items[i + 1];
    }

    v_root->count--; 

    _dirty_schedules = true;
    CL_D10_Logger::log(EN_L10_LOG_INFO, "[C10] Schedule ID %u deleted. count=%u. Dirty=true", p_id, v_root->count);

    xSemaphoreGive(s_configMutex);
    return true;
}


// **[추가 구현]** WindProfileDict를 JSON으로 변환하는 함수
void CL_C10_ConfigManager::toJson_WindProfileDict(const ST_A10_WindProfileDict_t& p, JsonDocument& d) {
    // [Implementation required: WindProfileDict -> JSON]
    JsonArray j_arr = d.createNestedArray("windProfiles");
    
    for (uint8_t i = 0; i < p.count; i++) {
        JsonObject j_item = j_arr.createNestedObject();
        j_item["wpNo"] = p.items[i].wpNo;
        j_item["name"] = p.items[i].name;
        j_item["speed"] = p.items[i].speed;
        // ... (나머지 필드 복사 로직 생략)
    }
}

