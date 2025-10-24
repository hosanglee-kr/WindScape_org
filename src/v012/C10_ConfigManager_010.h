#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : C10_ConfigManager_010.h
 * 모듈명 : Smart Nature Wind Configuration Manager
 * ------------------------------------------------------
 * 기능 요약:
 *  - JSON 설정 로드/저장/백업/복구/기본생성(loadOrCreate)
 *  - 구조체 ↔ JSON 직렬화(toJson/parseJson)
 *  - /api/config 패치(부분 갱신) 반영(patchFromJson)
 *  - Wi-Fi / Sim / Schedule / Motion 설정 분리 관리
 * ------------------------------------------------------
 * [코드 네이밍 규칙]
 * 		- 현재 파일 모듈약어    : C10
 * 		- 전역 상수,매크로      : G_모듈약어_ 접두사
 * 		- 전역 변수             : g_모듈약어_ 접두사
 * 		- 전역 함수             : 모듈약어_ 접두사
 * 		- type                  : T_모듈약어_ 접두사
 * 		- enum 상수             : EN_모듈약어_ 접두사
 * 		- 구조체                : ST_모듈약어_ 접두사
 * 		- 클래스명              : CL_모듈약어_ 접두사
 * 		- 클래스 private 멤버   : _ 접두사,
 * 		- 클래스 정적 멤버      : s_접두사
 * 		- 로컬 변수             : v_접두사
 * 		- 함수 인자             : p_접두사
 */

#include <ArduinoJson.h>
#include <LittleFS.h>
#include "A10_Const_010.h"
#include "D10_Logger_010.h"

class CL_C10_ConfigManager {
public:
    // ------------------------------------------------------
    // 공통 유틸 함수군
    // ------------------------------------------------------

    /** @brief JSON 파일 로드 */
    static bool _loadJsonFile(const char* p_path, JsonDocument& p_doc) {
        if (!LittleFS.exists(p_path)) {
            CL_D10_Logger::log(EN_L10_LOG_WARN, "Config not found: %s", p_path);
            return false;
        }
        File v_file = LittleFS.open(p_path, "r");
        if (!v_file) {
            CL_D10_Logger::log(EN_L10_LOG_ERROR, "Open failed: %s", p_path);
            return false;
        }
        DeserializationError v_err = deserializeJson(p_doc, v_file);
        v_file.close();
        if (v_err) {
            CL_D10_Logger::log(EN_L10_LOG_ERROR, "Parse failed: %s", v_err.c_str());
            return false;
        }
        return true;
    }

    /** @brief JSON 파일 저장 (백업 포함) */
    static bool _saveJsonFile(const char* p_path, const char* p_path_bak, const JsonDocument& p_doc) {
        if (LittleFS.exists(p_path)) {
            LittleFS.remove(p_path_bak);
            LittleFS.rename(p_path, p_path_bak);
        }
        File v_file = LittleFS.open(p_path, "w");
        if (!v_file) {
            CL_D10_Logger::log(EN_L10_LOG_ERROR, "Write open failed: %s", p_path);
            return false;
        }
        if (serializeJsonPretty(p_doc, v_file) == 0) {
            v_file.close();
            CL_D10_Logger::log(EN_L10_LOG_ERROR, "Write failed: %s", p_path);
            return false;
        }
        v_file.close();
        CL_D10_Logger::log(EN_L10_LOG_INFO, "Saved: %s", p_path);
        return true;
    }

    // ------------------------------------------------------
    // CORE 설정
    // ------------------------------------------------------
    static bool loadCore(ST_A10_CoreConfig& p_cfg) {
        JsonDocument v_doc;
        if (!_loadJsonFile(A10_Const::CFG_CORE_FILE, v_doc)) return false;
        return parseCoreJson(p_cfg, v_doc);
    }

    static bool saveCore(const ST_A10_CoreConfig& p_cfg) {
        JsonDocument v_doc;
        toCoreJson(p_cfg, v_doc);
        return _saveJsonFile(A10_Const::CFG_CORE_FILE, A10_Const::CFG_CORE_FILE_BAK, v_doc);
    }

    static bool parseCoreJson(ST_A10_CoreConfig& p_cfg, JsonDocument& p_doc) {
        JsonObjectConst v_root = p_doc.as<JsonObjectConst>();

        // meta
        strlcpy(p_cfg.meta.version, v_root["meta"]["version"] | A10_Const::FW_VERSION, sizeof(p_cfg.meta.version));
        strlcpy(p_cfg.meta.device_name, v_root["meta"]["device_name"] | "WindScape_XY-SK10", sizeof(p_cfg.meta.device_name));
        strlcpy(p_cfg.meta.last_update, v_root["meta"]["last_update"] | "", sizeof(p_cfg.meta.last_update));

        // system
        strlcpy(p_cfg.system.web.html, v_root["system"]["web"]["html"] | "/html/SC10_main_021.html", sizeof(p_cfg.system.web.html));
        strlcpy(p_cfg.system.web.css,  v_root["system"]["web"]["css"]  | "/html/SC10_main_021.css",  sizeof(p_cfg.system.web.css));
        strlcpy(p_cfg.system.web.js,   v_root["system"]["web"]["js"]   | "/html/SC10_main_021.js",   sizeof(p_cfg.system.web.js));
        strlcpy(p_cfg.system.logging.level, v_root["system"]["logging"]["level"] | "INFO", sizeof(p_cfg.system.logging.level));
        p_cfg.system.logging.max_entries = v_root["system"]["logging"]["max_entries"] | 300;

        // hw.fan_pwm
        p_cfg.hw.fan_pwm.pin = v_root["hw"]["fan_pwm"]["pin"] | 6;
        p_cfg.hw.fan_pwm.channel = v_root["hw"]["fan_pwm"]["channel"] | 0;
        p_cfg.hw.fan_pwm.freq = v_root["hw"]["fan_pwm"]["freq"] | 25000;
        p_cfg.hw.fan_pwm.res = v_root["hw"]["fan_pwm"]["res"] | 10;

        // hw.sensors
        p_cfg.hw.sensors.pir.enabled = v_root["hw"]["pir"]["enabled"] | true;
        p_cfg.hw.sensors.pir.pin = v_root["hw"]["pir"]["pin"] | 13;
        p_cfg.hw.sensors.pir.debounce_sec = v_root["hw"]["pir"]["debounce_sec"] | 5;

        p_cfg.hw.sensors.tempHum.enabled = v_root["hw"]["tempHum"]["enabled"] | true;
        strlcpy(p_cfg.hw.sensors.tempHum.type, v_root["hw"]["tempHum"]["type"] | "DHT22", sizeof(p_cfg.hw.sensors.tempHum.type));
        p_cfg.hw.sensors.tempHum.pin = v_root["hw"]["tempHum"]["pin"] | 23;
        p_cfg.hw.sensors.tempHum.interval_sec = v_root["hw"]["tempHum"]["interval_sec"] | 30;

        p_cfg.hw.sensors.ble.enabled = v_root["hw"]["ble"]["enabled"] | true;
        p_cfg.hw.sensors.ble.scan_interval = v_root["hw"]["ble"]["scan_interval"] | 5;

        // security
        strlcpy(p_cfg.security.api_key, v_root["security"]["api_key"] | "", sizeof(p_cfg.security.api_key));

        // time
        strlcpy(p_cfg.time.ntp_server, v_root["time"]["ntp_server"] | "pool.ntp.org", sizeof(p_cfg.time.ntp_server));
        strlcpy(p_cfg.time.timezone, v_root["time"]["timezone"] | "Asia/Seoul", sizeof(p_cfg.time.timezone));
        p_cfg.time.sync_interval_min = v_root["time"]["sync_interval_min"] | 60;
        return true;
    }

    static void toCoreJson(const ST_A10_CoreConfig& p_cfg, JsonDocument& p_doc) {
        JsonObject v_root = p_doc.as<JsonObject>();
        v_root["meta"]["version"] = p_cfg.meta.version;
        v_root["meta"]["device_name"] = p_cfg.meta.device_name;
        v_root["meta"]["last_update"] = p_cfg.meta.last_update;
        v_root["system"]["web"]["html"] = p_cfg.system.web.html;
        v_root["system"]["web"]["css"] = p_cfg.system.web.css;
        v_root["system"]["web"]["js"] = p_cfg.system.web.js;
        v_root["system"]["logging"]["level"] = p_cfg.system.logging.level;
        v_root["system"]["logging"]["max_entries"] = p_cfg.system.logging.max_entries;
        v_root["hw"]["fan_pwm"]["pin"] = p_cfg.hw.fan_pwm.pin;
        v_root["hw"]["fan_pwm"]["channel"] = p_cfg.hw.fan_pwm.channel;
        v_root["hw"]["fan_pwm"]["freq"] = p_cfg.hw.fan_pwm.freq;
        v_root["hw"]["fan_pwm"]["res"] = p_cfg.hw.fan_pwm.res;
        v_root["security"]["api_key"] = p_cfg.security.api_key;
        v_root["time"]["ntp_server"] = p_cfg.time.ntp_server;
        v_root["time"]["timezone"] = p_cfg.time.timezone;
        v_root["time"]["sync_interval_min"] = p_cfg.time.sync_interval_min;
    }

    // ------------------------------------------------------
    // Wi-Fi 설정
    // ------------------------------------------------------
    static bool loadWifi(ST_A10_WifiConfig& p_cfg) {
        JsonDocument v_doc;
        if (!_loadJsonFile(A10_Const::CFG_WIFI_FILE, v_doc)) return false;
        JsonObjectConst jwifi = v_doc["wifi"];
        p_cfg.mode = jwifi["mode"] | EN_A10_WIFI_MODE_AP_STA;
        strlcpy(p_cfg.ap.ssid, jwifi["ap"]["ssid"] | "NatureWind", sizeof(p_cfg.ap.ssid));
        strlcpy(p_cfg.ap.password, jwifi["ap"]["password"] | "2540", sizeof(p_cfg.ap.password));
        p_cfg.sta_count = 0;
        JsonArrayConst v_sta = jwifi["sta"].as<JsonArrayConst>();
        for (JsonObjectConst v_net : v_sta) {
            if (p_cfg.sta_count >= A10_Const::MAX_STA_NETWORKS) break;
            strlcpy(p_cfg.sta[p_cfg.sta_count].ssid, v_net["ssid"] | "", sizeof(p_cfg.sta[0].ssid));
            strlcpy(p_cfg.sta[p_cfg.sta_count].pass, v_net["pass"] | "", sizeof(p_cfg.sta[0].pass));
            p_cfg.sta_count++;
        }
        return true;
    }

    static bool saveWifi(const ST_A10_WifiConfig& p_cfg) {
        JsonDocument v_doc;
        v_doc["wifi"]["mode"] = p_cfg.mode;
        v_doc["wifi"]["ap"]["ssid"] = p_cfg.ap.ssid;
        v_doc["wifi"]["ap"]["password"] = p_cfg.ap.password;
        for (uint8_t v_i = 0; v_i < p_cfg.sta_count; v_i++) {
            JsonObject v_net = v_doc["wifi"]["sta"].add<JsonObject>();
            v_net["ssid"] = p_cfg.sta[v_i].ssid;
            v_net["pass"] = p_cfg.sta[v_i].pass;
        }
        return _saveJsonFile(A10_Const::CFG_WIFI_FILE, A10_Const::CFG_WIFI_FILE_BAK, v_doc);
    }

    // ------------------------------------------------------
    // Sim 설정
    // ------------------------------------------------------
    static bool loadSim(ST_A10_SimConfig& p_cfg) {
        JsonDocument v_doc;
        if (!_loadJsonFile(A10_Const::CFG_SIM_FILE, v_doc)) return false;
        JsonObjectConst jsim = v_doc["sim"];
        strlcpy(p_cfg.preset, jsim["preset"] | "COUNTRY_BREEZE", sizeof(p_cfg.preset));
        p_cfg.wind_intensity = jsim["wind_intensity"] | 70.0f;
        p_cfg.gust_frequency = jsim["gust_frequency"] | 45.0f;
        p_cfg.wind_variability = jsim["wind_variability"] | 50.0f;
        p_cfg.fan_limit = jsim["fan_limit"] | 90.0f;
        p_cfg.min_fan = jsim["min_fan"] | 10.0f;
        p_cfg.turbulence.length_scale = jsim["turbulence"]["length_scale"] | 40.0f;
        p_cfg.turbulence.intensity_sigma = jsim["turbulence"]["intensity_sigma"] | 0.5f;
        p_cfg.thermal.bubble_strength = jsim["thermal"]["bubble_strength"] | 2.0f;
        p_cfg.thermal.bubble_radius = jsim["thermal"]["bubble_radius"] | 18.0f;
        return true;
    }

    static bool saveSim(const ST_A10_SimConfig& p_cfg) {
        JsonDocument v_doc;
        v_doc["sim"]["preset"] = p_cfg.preset;
        v_doc["sim"]["wind_intensity"] = p_cfg.wind_intensity;
        v_doc["sim"]["gust_frequency"] = p_cfg.gust_frequency;
        v_doc["sim"]["wind_variability"] = p_cfg.wind_variability;
        v_doc["sim"]["fan_limit"] = p_cfg.fan_limit;
        v_doc["sim"]["min_fan"] = p_cfg.min_fan;
        v_doc["sim"]["turbulence"]["length_scale"] = p_cfg.turbulence.length_scale;
        v_doc["sim"]["turbulence"]["intensity_sigma"] = p_cfg.turbulence.intensity_sigma;
        v_doc["sim"]["thermal"]["bubble_strength"] = p_cfg.thermal.bubble_strength;
        v_doc["sim"]["thermal"]["bubble_radius"] = p_cfg.thermal.bubble_radius;
        return _saveJsonFile(A10_Const::CFG_SIM_FILE, A10_Const::CFG_SIM_FILE_BAK, v_doc);
    }

    // ------------------------------------------------------
    // Schedule 설정
    // ------------------------------------------------------
    static bool loadSchedule(ST_A10_ScheduleConfig& p_cfg) {
        JsonDocument v_doc;
        if (!_loadJsonFile(A10_Const::CFG_SCHEDULE_FILE, v_doc)) return false;
        JsonArrayConst v_schedules = v_doc["schedules"].as<JsonArrayConst>();
        p_cfg.count = 0;
        for (JsonObjectConst v_item : v_schedules) {
            if (p_cfg.count >= A10_Const::MAX_SCHEDULES) break;
            ST_A10_ScheduleItem& v_sc = p_cfg.items[p_cfg.count];
            v_sc.no = v_item["no"] | 0;
            strlcpy(v_sc.name, v_item["name"] | "", sizeof(v_sc.name));
            v_sc.enabled = v_item["enabled"] | true;
            JsonArrayConst v_days = v_item["days"].as<JsonArrayConst>();
            for (uint8_t i = 0; i < 7; i++) v_sc.days[i] = v_days[i] | 0;
            strlcpy(v_sc.start_time, v_item["start_time"] | "00:00", sizeof(v_sc.start_time));
            strlcpy(v_sc.end_time, v_item["end_time"] | "00:00", sizeof(v_sc.end_time));
            v_sc.seg_count = 0;
            JsonArrayConst v_seg = v_item["segments"].as<JsonArrayConst>();
            for (JsonObjectConst v_s : v_seg) {
                if (v_sc.seg_count >= A10_Const::MAX_SEGMENTS_PER_SCHEDULE) break;
                auto& s = v_sc.seg[v_sc.seg_count];
                s.no = v_s["no"] | 0;
                s.on_minutes = v_s["on_minutes"] | 10;
                s.off_minutes = v_s["off_minutes"] | 5;
                strlcpy(s.mode, v_s["mode"] | "preset", sizeof(s.mode));
                strlcpy(s.preset_name, v_s["preset_name"] | "", sizeof(s.preset_name));
                s.fixed_speed = v_s["fixed_speed"] | 0.0f;
                s.adj_intensity = v_s["preset_adjust"]["intensity"] | 0.0f;
                s.adj_variability = v_s["preset_adjust"]["variability"] | 0.0f;
                v_sc.seg_count++;
            }
            p_cfg.count++;
        }
        return true;
    }

    static bool saveSchedule(const ST_A10_ScheduleConfig& p_cfg) {
        JsonDocument v_doc;
        for (uint8_t v_i = 0; v_i < p_cfg.count; v_i++) {
            JsonObject v_item = v_doc["schedules"].add<JsonObject>();
            const auto& sc = p_cfg.items[v_i];
            v_item["no"] = sc.no;
            v_item["name"] = sc.name;
            v_item["enabled"] = sc.enabled;
            JsonArray v_days = v_item["days"].to<JsonArray>();
            for (uint8_t d = 0; d < 7; d++) v_days.add(sc.days[d]);
            v_item["start_time"] = sc.start_time;
            v_item["end_time"] = sc.end_time;
            for (uint8_t s = 0; s < sc.seg_count; s++) {
                JsonObject v_seg = v_item["segments"].add<JsonObject>();
                v_seg["no"] = sc.seg[s].no;
                v_seg["on_minutes"] = sc.seg[s].on_minutes;
                v_seg["off_minutes"] = sc.seg[s].off_minutes;
                v_seg["mode"] = sc.seg[s].mode;
                v_seg["preset_name"] = sc.seg[s].preset_name;
                v_seg["fixed_speed"] = sc.seg[s].fixed_speed
///////////
                v_seg["preset_adjust"]["intensity"]  = sc.seg[s].adj_intensity;
                v_seg["preset_adjust"]["variability"] = sc.seg[s].adj_variability;
            }
        }
        return _saveJsonFile(A10_Const::CFG_SCHEDULE_FILE, A10_Const::CFG_SCHEDULE_FILE_BAK, v_doc);
    }

    // ------------------------------------------------------
    // Motion 설정
    // ------------------------------------------------------
    static bool loadMotion(ST_A10_MotionConfig& p_cfg) {
        JsonDocument v_doc;
        if (!_loadJsonFile(A10_Const::CFG_MOTION_FILE, v_doc)) return false;
        JsonObjectConst v_motion = v_doc["motion"];

        p_cfg.enabled = v_motion["enabled"] | true;

        // PIR 설정
        p_cfg.pir.enabled  = v_motion["pir"]["enabled"] | true;
        p_cfg.pir.hold_sec = v_motion["pir"]["hold_sec"] | 120;

        // BLE 설정
        p_cfg.ble.enabled        = v_motion["ble"]["enabled"] | true;
        p_cfg.ble.rssi_threshold = v_motion["ble"]["rssi_threshold"] | -70;
        p_cfg.ble.hold_sec       = v_motion["ble"]["hold_sec"] | 120;
        p_cfg.ble.device_count   = 0;

        JsonArrayConst v_devices = v_motion["ble"]["devices"].as<JsonArrayConst>();
        for (JsonObjectConst v_d : v_devices) {
            if (p_cfg.ble.device_count >= A10_Const::MAX_BLE_DEVICES) break;
            auto& d = p_cfg.ble.devices[p_cfg.ble.device_count];
            strlcpy(d.mac, v_d["mac"] | "", sizeof(d.mac));
            strlcpy(d.alias, v_d["alias"] | "", sizeof(d.alias));
            d.enabled = v_d["enabled"] | false;
            p_cfg.ble.device_count++;
        }
        return true;
    }

    static bool saveMotion(const ST_A10_MotionConfig& p_cfg) {
        JsonDocument v_doc;
        v_doc["motion"]["enabled"] = p_cfg.enabled;

        // PIR
        v_doc["motion"]["pir"]["enabled"]  = p_cfg.pir.enabled;
        v_doc["motion"]["pir"]["hold_sec"] = p_cfg.pir.hold_sec;

        // BLE
        v_doc["motion"]["ble"]["enabled"]        = p_cfg.ble.enabled;
        v_doc["motion"]["ble"]["rssi_threshold"] = p_cfg.ble.rssi_threshold;
        v_doc["motion"]["ble"]["hold_sec"]       = p_cfg.ble.hold_sec;

        for (uint8_t i = 0; i < p_cfg.ble.device_count; i++) {
            JsonObject v_dev = v_doc["motion"]["ble"]["devices"].add<JsonObject>();
            v_dev["mac"]    = p_cfg.ble.devices[i].mac;
            v_dev["alias"]  = p_cfg.ble.devices[i].alias;
            v_dev["enabled"] = p_cfg.ble.devices[i].enabled;
        }
        return _saveJsonFile(A10_Const::CFG_MOTION_FILE, A10_Const::CFG_MOTION_FILE_BAK, v_doc);
    }

    // ------------------------------------------------------
    // 전체 로드 / 저장 / 기본값 초기화
    // ------------------------------------------------------

    /** 
     * @brief 모든 설정 로드 (core + wifi + sim + schedule + motion)
     * @note  로드 실패 시 각 섹션은 nullptr 또는 기본값 유지
     */
    static void loadAll(ST_A10_ConfigRoot& p_root) {
        // Core
        loadCore(p_root.core);

        // WiFi
        if (!p_root.wifi) p_root.wifi = new ST_A10_WifiConfig();
        if (!loadWifi(*p_root.wifi)) {
            A10_resetWifiDefault(*p_root.wifi);
            saveWifi(*p_root.wifi);
        }

        // Sim
        if (!p_root.sim) p_root.sim = new ST_A10_SimConfig();
        if (!loadSim(*p_root.sim)) {
            A10_resetSimDefault(*p_root.sim);
            saveSim(*p_root.sim);
        }

        // Schedule
        if (!p_root.schedule) p_root.schedule = new ST_A10_ScheduleConfig();
        if (!loadSchedule(*p_root.schedule)) {
            A10_resetScheduleDefault(*p_root.schedule);
            saveSchedule(*p_root.schedule);
        }

        // Motion
        if (!p_root.motion) p_root.motion = new ST_A10_MotionConfig();
        if (!loadMotion(*p_root.motion)) {
            A10_resetMotionDefault(*p_root.motion);
            saveMotion(*p_root.motion);
        }
    }

    /**
     * @brief 모든 설정 저장
     */
    static void saveAll(const ST_A10_ConfigRoot& p_root) {
        saveCore(p_root.core);
        if (p_root.wifi)     saveWifi(*p_root.wifi);
        if (p_root.sim)      saveSim(*p_root.sim);
        if (p_root.schedule) saveSchedule(*p_root.schedule);
        if (p_root.motion)   saveMotion(*p_root.motion);
    }

    /**
     * @brief 공장 초기화 후 저장
     */
    static void resetAll(ST_A10_ConfigRoot& p_root) {
        A10_resetToDefault(p_root);
        saveCore(p_root.core);
        if (p_root.wifi)     saveWifi(*p_root.wifi);
        if (p_root.sim)      saveSim(*p_root.sim);
        if (p_root.schedule) saveSchedule(*p_root.schedule);
        if (p_root.motion)   saveMotion(*p_root.motion);
        CL_D10_Logger::log(EN_L10_LOG_INFO, "Factory reset completed");
    }

    /**
     * @brief 개별 파일 백업 복구
     */
    static bool restoreBackupFile(const char* p_path_bak, const char* p_path_target) {
        if (!LittleFS.exists(p_path_bak)) return false;
        LittleFS.remove(p_path_target);
        return LittleFS.rename(p_path_bak, p_path_target);
    }

    /**
     * @brief 모든 설정 백업 복구
     */
    static void restoreAllFromBackup() {
        restoreBackupFile(A10_Const::CFG_CORE_FILE_BAK, A10_Const::CFG_CORE_FILE);
        restoreBackupFile(A10_Const::CFG_WIFI_FILE_BAK, A10_Const::CFG_WIFI_FILE);
        restoreBackupFile(A10_Const::CFG_SIM_FILE_BAK,  A10_Const::CFG_SIM_FILE);
        restoreBackupFile(A10_Const::CFG_SCHEDULE_FILE_BAK, A10_Const::CFG_SCHEDULE_FILE);
        restoreBackupFile(A10_Const::CFG_MOTION_FILE_BAK,   A10_Const::CFG_MOTION_FILE);
        CL_D10_Logger::log(EN_L10_LOG_INFO, "Restored from backup");
    }

private:
    // ------------------------------------------------------
    // 내부 공용 보조 함수
    // ------------------------------------------------------
    static void _safeStr(JsonObjectConst p_obj, const char* p_key, char* p_dest, size_t p_size, const char* p_def = "") {
        strlcpy(p_dest, p_obj[p_key] | p_def, p_size);
    }

    static void _safeInt(JsonObjectConst p_obj, const char* p_key, int32_t& p_dest, int32_t p_def = 0) {
        p_dest = p_obj[p_key] | p_def;
    }

    static void _safeFloat(JsonObjectConst p_obj, const char* p_key, float& p_dest, float p_def = 0.0f) {
        p_dest = p_obj[p_key] | p_def;
    }
};
                    
