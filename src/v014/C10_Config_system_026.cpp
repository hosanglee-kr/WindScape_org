

#include "C10_Config_026.h"
// g_A10_config_root 전역 변수 사용을 위해 extern 선언
extern ST_A10_ConfigRoot_t g_A10_config_root; 

// =====================================================
// 4. JSON Patch (System, Wifi, Motion) 구현
// (로직은 요청에 따라 이전 응답과 동일하게 유지)
// =====================================================

bool CL_C10_ConfigManager::patchSystemFromJson(ST_A10_SystemConfig& p_config,
                                const JsonDocument&	 p_patch) {
    bool v_changed = false;

    if (xSemaphoreTake(s_configMutex, MUTEX_TIMEOUT) != pdTRUE) {
        CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] patchSystem() Mutex timeout!");
        return false; 
    }
    
    JsonObjectConst j_sys = p_patch["system"];
    JsonObjectConst j_sec_root = p_patch["security"];

    if (j_sys.isNull() && j_sec_root.isNull()) {
        xSemaphoreGive(s_configMutex);
        return false; 
    }

    // 1. system.logging 객체 처리
    if (!j_sys.isNull()) {
        JsonObjectConst j_log = j_sys["logging"];
        if (!j_log.isNull()) {
            const char* v_lv = j_log["level"] | "";
            if (j_log["max_entries"].is<uint16_t>()) {
                uint16_t v_max = j_log["max_entries"];
                if (v_max != p_config.system.logging.max_entries) {
                    p_config.system.logging.max_entries = v_max;
                    v_changed = true;
                }
            }
            if (strlen(v_lv) > 0 && strcmp(v_lv, p_config.system.logging.level) != 0) { 
                strlcpy(p_config.system.logging.level, v_lv, sizeof(p_config.system.logging.level));
                v_changed = true;
            }
        }
    }

    // 2. security 객체 처리
    if (!j_sec_root.isNull()) {
        const char* v_key = j_sec_root["api_key"] | "";
        if (strlen(v_key) > 0 && strcmp(v_key, p_config.security.api_key) != 0) {
            strlcpy(p_config.security.api_key, v_key, sizeof(p_config.security.api_key));
            v_changed = true;
        }
    }
    
    if (v_changed) {
        _dirty_system = true;
        CL_D10_Logger::log(EN_L10_LOG_INFO, "[C10] System config patched (Memory Only). Dirty=true");
    }

    xSemaphoreGive(s_configMutex); 
    return v_changed;
}


bool CL_C10_ConfigManager::patchWifiFromJson(ST_A10_WifiConfig& p_config,
                                  const JsonDocument&	 p_patch) {
    bool v_changed = false;

    if (xSemaphoreTake(s_configMutex, MUTEX_TIMEOUT) != pdTRUE) {
        CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] patchWifiFromJson() Mutex timeout!");
        return false; 
    }
    
    JsonObjectConst j_wifi = p_patch["wifi"];
    if (j_wifi.isNull()) {
        xSemaphoreGive(s_configMutex);
        return false;
    }

    // wifiMode 처리
    if (j_wifi["wifiMode"].is<uint8_t>()) {
        uint8_t v_mode = j_wifi["wifiMode"];
        constexpr uint8_t EN_A10_WIFI_MODE_AP = 0;
        constexpr uint8_t EN_A10_WIFI_MODE_AP_STA = 2;
        
        if (v_mode != p_config.wifiMode) {
            if (v_mode >= EN_A10_WIFI_MODE_AP && v_mode <= EN_A10_WIFI_MODE_AP_STA) { 
                p_config.wifiMode = (EN_A10_WIFI_MODE_t)v_mode;
                v_changed = true;
            } else {
                CL_D10_Logger::log(EN_L10_LOG_WARN, "[C10] Invalid wifiMode value: %d", v_mode);
            }
        }
    }

    // 2. ap 객체 처리
    JsonObjectConst j_ap = j_wifi["ap"];
    if (!j_ap.isNull()) {
        const char* v_ssid = j_ap["ssid"] | "";
        if (strlen(v_ssid) > 0 && strcmp(v_ssid, p_config.ap.ssid) != 0) {
            strlcpy(p_config.ap.ssid, v_ssid, sizeof(p_config.ap.ssid));
            v_changed = true;
        }
        const char* v_pwd = j_ap["password"] | "";
        if (strlen(v_pwd) > 0 && strcmp(v_pwd, p_config.ap.password) != 0) {
            strlcpy(p_config.ap.password, v_pwd, sizeof(p_config.ap.password));
            v_changed = true;
        }
    }
    
    // 3. sta 배열 전체 덮어쓰기 (PUT 방식)
    JsonArrayConst j_sta = j_wifi["sta"].as<JsonArrayConst>();
    if (!j_sta.isNull()) {
        constexpr uint8_t MAX_STA_NETWORKS = 5; 

        p_config.sta_count = 0; 

        for (JsonObjectConst v_js : j_sta) {
            if (p_config.sta_count >= MAX_STA_NETWORKS)
                break;
            
            strlcpy(p_config.sta[p_config.sta_count].ssid,
                    v_js["ssid"] | "",
                    sizeof(p_config.sta[p_config.sta_count].ssid));
            strlcpy(p_config.sta[p_config.sta_count].pass,
                    v_js["pass"] | "",
                    sizeof(p_config.sta[p_config.sta_count].pass));
            p_config.sta_count++;
        }
        v_changed = true;
        CL_D10_Logger::log(EN_L10_LOG_DEBUG, "[C10] WiFi STA array fully replaced.");
    }

    if (v_changed) {
        _dirty_wifi = true;
        CL_D10_Logger::log(EN_L10_LOG_INFO, "[C10] WiFi config patched (Memory Only). Dirty=true");
    }

    xSemaphoreGive(s_configMutex); 
    return v_changed;
}


bool CL_C10_ConfigManager::patchMotionFromJson(ST_A10_MotionConfig& p_config,
                                  const JsonDocument&	 p_patch) {
    if (xSemaphoreTake(s_configMutex, MUTEX_TIMEOUT) != pdTRUE) {
        CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] patchMotionFromJson() Mutex timeout!");
        return false; 
    }
    
    bool v_changed = false;
    JsonObjectConst j_motion = p_patch["motion"];

    if (j_motion.isNull()) {
        xSemaphoreGive(s_configMutex);
        return false; 
    }

    // 1. 최상위 enabled 필드
    if (j_motion["enabled"].is<bool>() && 
        j_motion["enabled"].as<bool>() != p_config.enabled) {
        p_config.enabled = j_motion["enabled"];
        v_changed = true;
    }

    // 2. pir 객체 처리
    JsonObjectConst j_pir = j_motion["pir"];
    if (!j_pir.isNull()) {
        if (j_pir["enabled"].is<bool>() &&
            j_pir["enabled"].as<bool>() != p_config.pir.enabled) {
            p_config.pir.enabled = j_pir["enabled"];
            v_changed = true;
        }
        if (j_pir["hold_sec"].is<uint16_t>()) { 
            if (j_pir["hold_sec"].as<uint16_t>() != p_config.pir.hold_sec) {
                p_config.pir.hold_sec = j_pir["hold_sec"];
                v_changed = true;
            }
        }
    }
    
    // 3. ble 객체 및 중첩된 rssi 객체 처리
    JsonObjectConst j_ble = j_motion["ble"];
    if (!j_ble.isNull()) {
        if (j_ble["enabled"].is<bool>() &&
            j_ble["enabled"].as<bool>() != p_config.ble.enabled) {
            p_config.ble.enabled = j_ble["enabled"];
            v_changed = true;
        }
        
        JsonObjectConst j_rssi = j_ble["rssi"];
        if (!j_rssi.isNull()) {
            if (j_rssi["on"].is<int8_t>() &&
                j_rssi["on"].as<int8_t>() != p_config.ble.rssi.on) {
                p_config.ble.rssi.on = j_rssi["on"];
                v_changed = true;
            }
            if (j_rssi["off"].is<int8_t>() &&
                j_rssi["off"].as<int8_t>() != p_config.ble.rssi.off) {
                p_config.ble.rssi.off = j_rssi["off"];
                v_changed = true;
            }
            // ... (나머지 rssi 필드 패치 로직 생략)
        }

        // trusted_devices 배열 전체 덮어쓰기(PUT)
        JsonArrayConst j_devices = j_ble["trusted_devices"].as<JsonArrayConst>();
        if (!j_devices.isNull()) {
            constexpr uint8_t MAX_BLE_DEVICES = 5; 

            p_config.ble.trusted_count = 0; 
            for (JsonObjectConst j_dev : j_devices) {
                if (p_config.ble.trusted_count >= MAX_BLE_DEVICES)
                    break;
                
                strlcpy(p_config.ble.trusted_devices[p_config.ble.trusted_count].alias, 
                        j_dev["alias"] | "", 
                        sizeof(p_config.ble.trusted_devices[p_config.ble.trusted_count].alias));
                // ... (나머지 필드 복사 로직 생략)
                
                p_config.ble.trusted_count++;
            }
            v_changed = true;
            CL_D10_Logger::log(EN_L10_LOG_DEBUG, "[C10] Motion Trusted Devices array fully replaced.");
        }
    }

    if (v_changed) {
        _dirty_motion = true;
        CL_D10_Logger::log(EN_L10_LOG_INFO, "[C10] motion config patched (Memory Only). Dirty=true");
    }

    xSemaphoreGive(s_configMutex); 
    return v_changed;
}
