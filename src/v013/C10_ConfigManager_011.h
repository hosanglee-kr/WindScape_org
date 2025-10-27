#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : C10_ConfigManager_011.h
 * 모듈 약어 : C10
 * 모듈명 : Smart Nature Wind Configuration Manager
 * ------------------------------------------------------
 * 기능 요약:
 *  - JSON 설정 로드/저장/백업/복구/기본생성(loadAll/resetAll/saveAll)
 *  - 구조체 ↔ JSON 직렬화(toJson/parseJson)
 *  - /api/config 패치(부분 갱신) 반영(patchFromJson)
 *  - JSON 파일 분리 관리(system / wifi / motion / control)
 *  - ArduinoJson v7 사용: JsonDocument 단일 타입만 사용
  *  - CONTROL : Continuous / Schedule 모드 기반 제어 설정
 *  - PATCH   : 부분 JSON 패치 적용 (/api/config)
 *  - LOADALL : 모든 JSON 파일 로드
 *  - SAVEALL : 모든 JSON 파일 저장
 *  - RESETALL: 기본값 초기화
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
 * 		- 전역 상수,매크로      : G_모듈약어_ 접두사
 * 		- 전역 변수             : g_모듈약어_ 접두사
 * 		- 전역 함수             : 모듈약어_ 접두사
 * 		- type                  : T_모듈약어_ 접두사
 * 		- enum 상수             : EN_모듈약어_ 접두사
 * 		- 구조체                : ST_모듈약어_ 접두사
 * 		- 클래스명              : CL_모듈약어_ 접두사
 * 		- 클래스 private 멤버   : _ 접두사,
 * 		- 클래스 정적 멤버      : s_ 접두사
 * 		- 로컬 변수             : v_ 접두사
 * 		- 함수 인자             : p_ 접두사
 * ------------------------------------------------------
 */

#include <ArduinoJson.h>
#include <LittleFS.h>
#include "A10_Const_011.h"
#include "D10_Logger_010.h"

// ======================================================
// 클래스 정의 : CL_C10_ConfigManager
// ------------------------------------------------------
// 역할 : Smart Nature Wind의 설정 파일 로드/저장/복구 관리
// JSON 파일별 관리 항목
//   ① system : 하드웨어/웹/시간/보안
//   ② wifi   : 무선 네트워크 (AP/STA 목록)
//   ③ motion : PIR/ BLE 감지
//   ④ control: Continuous/Schedule 기반 작동
// ======================================================
class CL_C10_ConfigManager {
public:
    // ======================================================
    // 공통 JSON 입출력 유틸
    // ======================================================
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
        auto v_err = deserializeJson(p_doc, v_file);
        v_file.close();
        if (v_err) {
            CL_D10_Logger::log(EN_L10_LOG_ERROR, "Parse failed: %s", v_err.c_str());
            return false;
        }
        return true;
    }

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

    static bool restoreBackupFile(const char* p_path_bak, const char* p_path_target) {
        if (!LittleFS.exists(p_path_bak)) return false;
        LittleFS.remove(p_path_target);
        bool v_ok = LittleFS.rename(p_path_bak, p_path_target);
        CL_D10_Logger::log(v_ok ? EN_L10_LOG_INFO : EN_L10_LOG_ERROR,
                           v_ok ? "Restored: %s" : "Restore failed: %s", p_path_target);
        return v_ok;
    }

    // ======================================================
    // SYSTEM 설정 로드/저장
    // ======================================================
    static bool loadSystem(ST_A10_SystemConfig& p_cfg) {
        JsonDocument v_doc;
        if (!_loadJsonFile(A10_Const::CFG_SYSTEM_FILE, v_doc)) return false;
        JsonObjectConst v_root = v_doc.as<JsonObjectConst>();

        // meta
        strlcpy(p_cfg.meta.version, v_root["meta"]["version"] | A10_Const::FW_VERSION, sizeof(p_cfg.meta.version));
        strlcpy(p_cfg.meta.device_name, v_root["meta"]["device_name"] | "WindScape_XY-SK10", sizeof(p_cfg.meta.device_name));
        strlcpy(p_cfg.meta.last_update, v_root["meta"]["last_update"] | "", sizeof(p_cfg.meta.last_update));

        // system.web
        strlcpy(p_cfg.system.web.html, v_root["system"]["web"]["html"] | "/html/SC10_main_021.html", sizeof(p_cfg.system.web.html));
        strlcpy(p_cfg.system.web.css,  v_root["system"]["web"]["css"]  | "/html/SC10_main_021.css", sizeof(p_cfg.system.web.css));
        strlcpy(p_cfg.system.web.js,   v_root["system"]["web"]["js"]   | "/html/SC10_main_021.js", sizeof(p_cfg.system.web.js));

        // logging
        strlcpy(p_cfg.system.logging.level, v_root["system"]["logging"]["level"] | "INFO", sizeof(p_cfg.system.logging.level));
        p_cfg.system.logging.max_entries = v_root["system"]["logging"]["max_entries"] | 300;

        // hw.fan_pwm
        p_cfg.hw.fan_pwm.pin     = v_root["hw"]["fan_pwm"]["pin"]     | 6;
        p_cfg.hw.fan_pwm.channel = v_root["hw"]["fan_pwm"]["channel"] | 0;
        p_cfg.hw.fan_pwm.freq    = v_root["hw"]["fan_pwm"]["freq"]    | 25000;
        p_cfg.hw.fan_pwm.res     = v_root["hw"]["fan_pwm"]["res"]     | 10;

        // hw.pir
        p_cfg.hw.sensors.pir.enabled      = v_root["hw"]["pir"]["enabled"]      | true;
        p_cfg.hw.sensors.pir.pin          = v_root["hw"]["pir"]["pin"]          | 13;
        p_cfg.hw.sensors.pir.debounce_sec = v_root["hw"]["pir"]["debounce_sec"] | 5;

        // hw.tempHum
        p_cfg.hw.sensors.tempHum.enabled      = v_root["hw"]["tempHum"]["enabled"] | true;
        strlcpy(p_cfg.hw.sensors.tempHum.type, v_root["hw"]["tempHum"]["type"] | "DHT22", sizeof(p_cfg.hw.sensors.tempHum.type));
        p_cfg.hw.sensors.tempHum.pin          = v_root["hw"]["tempHum"]["pin"] | 23;
        p_cfg.hw.sensors.tempHum.interval_sec = v_root["hw"]["tempHum"]["interval_sec"] | 30;

        // hw.ble
        p_cfg.hw.sensors.ble.enabled       = v_root["hw"]["ble"]["enabled"] | true;
        p_cfg.hw.sensors.ble.scan_interval = v_root["hw"]["ble"]["scan_interval"] | 5;

        // security
        strlcpy(p_cfg.security.api_key, v_root["security"]["api_key"] | "my_api_key_12345", sizeof(p_cfg.security.api_key));

        // time
        strlcpy(p_cfg.time.ntp_server, v_root["time"]["ntp_server"] | "pool.ntp.org", sizeof(p_cfg.time.ntp_server));
        strlcpy(p_cfg.time.timezone,   v_root["time"]["timezone"] | "Asia/Seoul", sizeof(p_cfg.time.timezone));
        p_cfg.time.sync_interval_min = v_root["time"]["sync_interval_min"] | 60;
        return true;
    }

    static bool saveSystem(const ST_A10_SystemConfig& p_cfg) {
        JsonDocument v_doc;
        v_doc["meta"]["version"] = p_cfg.meta.version;
        v_doc["meta"]["device_name"] = p_cfg.meta.device_name;
        v_doc["meta"]["last_update"] = p_cfg.meta.last_update;

        v_doc["system"]["web"]["html"] = p_cfg.system.web.html;
        v_doc["system"]["web"]["css"]  = p_cfg.system.web.css;
        v_doc["system"]["web"]["js"]   = p_cfg.system.web.js;
        v_doc["system"]["logging"]["level"] = p_cfg.system.logging.level;
        v_doc["system"]["logging"]["max_entries"] = p_cfg.system.logging.max_entries;

        v_doc["hw"]["fan_pwm"]["pin"]     = p_cfg.hw.fan_pwm.pin;
        v_doc["hw"]["fan_pwm"]["channel"] = p_cfg.hw.fan_pwm.channel;
        v_doc["hw"]["fan_pwm"]["freq"]    = p_cfg.hw.fan_pwm.freq;
        v_doc["hw"]["fan_pwm"]["res"]     = p_cfg.hw.fan_pwm.res;

        v_doc["hw"]["pir"]["enabled"]      = p_cfg.hw.sensors.pir.enabled;
        v_doc["hw"]["pir"]["pin"]          = p_cfg.hw.sensors.pir.pin;
        v_doc["hw"]["pir"]["debounce_sec"] = p_cfg.hw.sensors.pir.debounce_sec;

        v_doc["hw"]["tempHum"]["enabled"]      = p_cfg.hw.sensors.tempHum.enabled;
        v_doc["hw"]["tempHum"]["type"]         = p_cfg.hw.sensors.tempHum.type;
        v_doc["hw"]["tempHum"]["pin"]          = p_cfg.hw.sensors.tempHum.pin;
        v_doc["hw"]["tempHum"]["interval_sec"] = p_cfg.hw.sensors.tempHum.interval_sec;

        v_doc["hw"]["ble"]["enabled"]       = p_cfg.hw.sensors.ble.enabled;
        v_doc["hw"]["ble"]["scan_interval"] = p_cfg.hw.sensors.ble.scan_interval;

        v_doc["security"]["api_key"] = p_cfg.security.api_key;
        v_doc["time"]["ntp_server"] = p_cfg.time.ntp_server;
        v_doc["time"]["timezone"] = p_cfg.time.timezone;
        v_doc["time"]["sync_interval_min"] = p_cfg.time.sync_interval_min;

        return _saveJsonFile(A10_Const::CFG_SYSTEM_FILE, A10_Const::CFG_SYSTEM_FILE_BAK, v_doc);
    }

    // ======================================================
    // WIFI 설정 로드/저장
    // ======================================================
    static bool loadWifi(ST_A10_WifiConfig& p_cfg) {
        JsonDocument v_doc;
        if (!_loadJsonFile(A10_Const::CFG_WIFI_FILE, v_doc)) return false;
        JsonObjectConst jw = v_doc["wifi"];

        p_cfg.wifiMode = jw["wifiMode"] | 2;
        strlcpy(p_cfg.wifiModeDesc, jw["wifiModeDesc"] | "0:AP,1:STA,2:AP+STA", sizeof(p_cfg.wifiModeDesc));
        strlcpy(p_cfg.ap.ssid, jw["ap"]["ssid"] | "NatureWind", sizeof(p_cfg.ap.ssid));
        strlcpy(p_cfg.ap.password, jw["ap"]["password"] | "2540", sizeof(p_cfg.ap.password));

        p_cfg.sta_count = 0;
        for (JsonObjectConst s : jw["sta"].as<JsonArrayConst>()) {
            if (p_cfg.sta_count >= A10_Const::MAX_STA_NETWORKS) break;
            strlcpy(p_cfg.sta[p_cfg.sta_count].ssid, s["ssid"] | "", sizeof(p_cfg.sta[0].ssid));
            strlcpy(p_cfg.sta[p_cfg.sta_count].pass, s["pass"] | "", sizeof(p_cfg.sta[0].pass));
            p_cfg.sta_count++;
        }
        return true;
    }

    static bool saveWifi(const ST_A10_WifiConfig& p_cfg) {
        JsonDocument v_doc;
        v_doc["wifi"]["wifiMode"] = p_cfg.wifiMode;
        v_doc["wifi"]["wifiModeDesc"] = p_cfg.wifiModeDesc;
        v_doc["wifi"]["ap"]["ssid"] = p_cfg.ap.ssid;
        v_doc["wifi"]["ap"]["password"] = p_cfg.ap.password;
        for (uint8_t i = 0; i < p_cfg.sta_count; i++) {
            JsonObject v = v_doc["wifi"]["sta"].add<JsonObject>();
            v["ssid"] = p_cfg.sta[i].ssid;
            v["pass"] = p_cfg.sta[i].pass;
        }
        return _saveJsonFile(A10_Const::CFG_WIFI_FILE, A10_Const::CFG_WIFI_FILE_BAK, v_doc);
    }

    // ======================================================
    // MOTION 설정 로드/저장
    // ======================================================
    static bool loadMotion(ST_A10_MotionConfig& p_cfg) {
        JsonDocument v_doc;
        if (!_loadJsonFile(A10_Const::CFG_MOTION_FILE, v_doc)) return false;
        JsonObjectConst jm = v_doc["motion"];

        p_cfg.enabled = jm["enabled"] | true;
        p_cfg.pir.enabled = jm["pir"]["enabled"] | true;
        p_cfg.pir.hold_sec = jm["pir"]["hold_sec"] | 120;
        p_cfg.pir.debounce_sec = jm["pir"]["debounce_sec"] | 5;
        p_cfg.ble.enabled = jm["ble"]["enabled"] | true;
        p_cfg.ble.rssi_threshold = jm["ble"]["rssi_threshold"] | -70;
        p_cfg.ble.hold_sec = jm["ble"]["hold_sec"] | 120;

        p_cfg.ble.device_count = 0;
        for (JsonObjectConst d : jm["ble"]["devices"].as<JsonArrayConst>()) {
            if (p_cfg.ble.device_count >= A10_Const::MAX_BLE_DEVICES) break;
            auto& dev = p_cfg.ble.devices[p_cfg.ble.device_count++];
            strlcpy(dev.mac, d["mac"] | "", sizeof(dev.mac));
            strlcpy(dev.alias, d["alias"] | "", sizeof(dev.alias));
            dev.enabled = d["enabled"] | false;
        }
        return true;
    }

    static bool saveMotion(const ST_A10_MotionConfig& p_cfg) {
        JsonDocument v_doc;
        v_doc["motion"]["enabled"] = p_cfg.enabled;
        v_doc["motion"]["pir"]["enabled"] = p_cfg.pir.enabled;
        v_doc["motion"]["pir"]["hold_sec"] = p_cfg.pir.hold_sec;
        v_doc["motion"]["pir"]["debounce_sec"] = p_cfg.pir.debounce_sec;
        v_doc["motion"]["ble"]["enabled"] = p_cfg.ble.enabled;
        v_doc["motion"]["ble"]["rssi_threshold"] = p_cfg.ble.rssi_threshold;
        v_doc["motion"]["ble"]["hold_sec"] = p_cfg.ble.hold_sec;
        for (uint8_t i = 0; i < p_cfg.ble.device_count; i++) {
            JsonObject v = v_doc["motion"]["ble"]["devices"].add<JsonObject>();
            v["mac"] = p_cfg.ble.devices[i].mac;
            v["alias"] = p_cfg.ble.devices[i].alias;
            v["enabled"] = p_cfg.ble.devices[i].enabled;
        }
        return _saveJsonFile(A10_Const::CFG_MOTION_FILE, A10_Const::CFG_MOTION_FILE_BAK, v_doc);
    }


    // ======================================================
    // CONTROL 설정 로드/저장
    // ======================================================
    static bool loadControl(ST_A10_ControlConfig& p_cfg) {
        JsonDocument v_doc;
        if (!_loadJsonFile(A10_Const::CFG_CONTROL_FILE, v_doc)) return false;
        JsonObjectConst jc = v_doc["control"];

        p_cfg.runMode = jc["runMode"] | 0;
        strlcpy(p_cfg.runModeDesc, jc["runModeDesc"] | "0=Continuous,1=Schedule", sizeof(p_cfg.runModeDesc));

        // Continuous wind
        JsonObjectConst cw = jc["Continuous"]["wind"];
        p_cfg.continuous.wind.enabled = cw["enabled"] | true;
        strlcpy(p_cfg.continuous.wind.preset, cw["preset"] | "COUNTRY_BREEZE", sizeof(p_cfg.continuous.wind.preset));
        p_cfg.continuous.wind.wind_intensity = cw["wind_intensity"] | 70.0f;
        p_cfg.continuous.wind.gust_frequency = cw["gust_frequency"] | 45.0f;
        p_cfg.continuous.wind.wind_variability = cw["wind_variability"] | 50.0f;
        p_cfg.continuous.wind.fan_limit = cw["fan_limit"] | 90.0f;
        p_cfg.continuous.wind.min_fan = cw["min_fan"] | 10.0f;
        p_cfg.continuous.wind.turbulence_length_scale = cw["turbulence_length_scale"] | 40.0f;
        p_cfg.continuous.wind.turbulence_intensity_sigma = cw["turbulence_intensity_sigma"] | 0.5f;
        p_cfg.continuous.wind.thermal_bubble_strength = cw["thermal_bubble_strength"] | 2.0f;
        p_cfg.continuous.wind.thermal_bubble_radius = cw["thermal_bubble_radius"] | 18.0f;

        // Continuous motion
        JsonObjectConst cm = jc["Continuous"]["motion"];
        p_cfg.continuous.motion.pir.enabled = cm["pir"]["enabled"] | true;
        p_cfg.continuous.motion.pir.hold_sec = cm["pir"]["hold_sec"] | 120;
        p_cfg.continuous.motion.ble.enabled = cm["ble"]["enabled"] | true;
        p_cfg.continuous.motion.ble.rssi_threshold = cm["ble"]["rssi_threshold"] | -70;
        p_cfg.continuous.motion.ble.hold_sec = cm["ble"]["hold_sec"] | 120;

        // Schedule list
        p_cfg.schedule_count = 0;
        for (JsonObjectConst js : jc["schedules"].as<JsonArrayConst>()) {
            if (p_cfg.schedule_count >= A10_Const::MAX_SCHEDULES) break;
            auto& sch = p_cfg.schedules[p_cfg.schedule_count++];
            sch.schNo = js["schNo"] | 0;
            strlcpy(sch.schName, js["schName"] | "", sizeof(sch.schName));
            sch.enabled = js["enabled"] | true;
            JsonArrayConst days = js["days"].as<JsonArrayConst>();
            for (uint8_t i = 0; i < 7 && i < days.size(); i++) sch.days[i] = days[i] | 1;
            strlcpy(sch.start_time, js["start_time"] | "08:00", sizeof(sch.start_time));
            strlcpy(sch.end_time, js["end_time"] | "12:00", sizeof(sch.end_time));

            sch.seg_count = 0;
            for (JsonObjectConst seg : js["segments"].as<JsonArrayConst>()) {
                if (sch.seg_count >= A10_Const::MAX_SEGMENTS_PER_SCHEDULE) break;
                auto& s = sch.segments[sch.seg_count++];
                s.segNo = seg["segNo"] | 0;
                s.on_minutes = seg["on_minutes"] | 10;
                s.off_minutes = seg["off_minutes"] | 5;
                strlcpy(s.mode, seg["mode"] | "preset", sizeof(s.mode));
                strlcpy(s.preset_name, seg["preset_name"] | "COUNTRY_BREEZE", sizeof(s.preset_name));
                s.preset_adjust.intensity = seg["preset_adjust"]["intensity"] | 0.0f;
                s.preset_adjust.variability = seg["preset_adjust"]["variability"] | 0.0f;
                s.fixed_speed = seg["fixed_speed"] | 0.0f;
            }

            sch.motion.pir.enabled = js["motion"]["pir"]["enabled"] | true;
            sch.motion.pir.hold_sec = js["motion"]["pir"]["hold_sec"] | 120;
            sch.motion.ble.enabled = js["motion"]["ble"]["enabled"] | true;
            sch.motion.ble.rssi_threshold = js["motion"]["ble"]["rssi_threshold"] | -70;
            sch.motion.ble.hold_sec = js["motion"]["ble"]["hold_sec"] | 120;
        }
        return true;
    }

    static bool saveControl(const ST_A10_ControlConfig& p_cfg) {
        JsonDocument v_doc;
        JsonObject j = v_doc["control"];
        j["runMode"] = p_cfg.runMode;
        j["runModeDesc"] = p_cfg.runModeDesc;

        // Continuous
        JsonObject cw = j["Continuous"]["wind"];
        cw["enabled"] = p_cfg.continuous.wind.enabled;
        cw["preset"] = p_cfg.continuous.wind.preset;
        cw["wind_intensity"] = p_cfg.continuous.wind.wind_intensity;
        cw["gust_frequency"] = p_cfg.continuous.wind.gust_frequency;
        cw["wind_variability"] = p_cfg.continuous.wind.wind_variability;
        cw["fan_limit"] = p_cfg.continuous.wind.fan_limit;
        cw["min_fan"] = p_cfg.continuous.wind.min_fan;
        cw["turbulence_length_scale"] = p_cfg.continuous.wind.turbulence_length_scale;
        cw["turbulence_intensity_sigma"] = p_cfg.continuous.wind.turbulence_intensity_sigma;
        cw["thermal_bubble_strength"] = p_cfg.continuous.wind.thermal_bubble_strength;
        cw["thermal_bubble_radius"] = p_cfg.continuous.wind.thermal_bubble_radius;

        j["Continuous"]["motion"]["pir"]["enabled"] = p_cfg.continuous.motion.pir.enabled;
        j["Continuous"]["motion"]["pir"]["hold_sec"] = p_cfg.continuous.motion.pir.hold_sec;
        j["Continuous"]["motion"]["ble"]["enabled"] = p_cfg.continuous.motion.ble.enabled;
        j["Continuous"]["motion"]["ble"]["rssi_threshold"] = p_cfg.continuous.motion.ble.rssi_threshold;
        j["Continuous"]["motion"]["ble"]["hold_sec"] = p_cfg.continuous.motion.ble.hold_sec;

        // Schedule list
        for (uint8_t i = 0; i < p_cfg.schedule_count; i++) {
            const auto& sch = p_cfg.schedules[i];
            JsonObject sj = j["schedules"].add<JsonObject>();
            sj["schNo"] = sch.schNo;
            sj["schName"] = sch.schName;
            sj["enabled"] = sch.enabled;
            JsonArray days = sj["days"];
            for (uint8_t d = 0; d < 7; d++) days.add(sch.days[d]);
            sj["start_time"] = sch.start_time;
            sj["end_time"] = sch.end_time;

            for (uint8_t s = 0; s < sch.seg_count; s++) {
                const auto& seg = sch.segments[s];
                JsonObject sg = sj["segments"].add<JsonObject>();
                sg["segNo"] = seg.segNo;
                sg["on_minutes"] = seg.on_minutes;
                sg["off_minutes"] = seg.off_minutes;
                sg["mode"] = seg.mode;
                sg["preset_name"] = seg.preset_name;
                sg["preset_adjust"]["intensity"] = seg.preset_adjust.intensity;
                sg["preset_adjust"]["variability"] = seg.preset_adjust.variability;
                sg["fixed_speed"] = seg.fixed_speed;
            }

            sj["motion"]["pir"]["enabled"] = sch.motion.pir.enabled;
            sj["motion"]["pir"]["hold_sec"] = sch.motion.pir.hold_sec;
            sj["motion"]["ble"]["enabled"] = sch.motion.ble.enabled;
            sj["motion"]["ble"]["rssi_threshold"] = sch.motion.ble.rssi_threshold;
            sj["motion"]["ble"]["hold_sec"] = sch.motion.ble.hold_sec;
        }

        return _saveJsonFile(A10_Const::CFG_CONTROL_FILE, A10_Const::CFG_CONTROL_FILE_BAK, v_doc);
    }

    // ======================================================
    // PATCH / LOADALL / SAVEALL / RESETALL / RESTOREALL
    // ======================================================
    static bool patchFromJson(ST_A10_ConfigRoot& p_root, const JsonDocument& p_patch, bool& p_wifiReinit) {
        p_wifiReinit = false;
        if (p_patch.containsKey("wifi")) {
            if (!p_root.wifi) p_root.wifi = new ST_A10_WifiConfig();
            loadWifi(*p_root.wifi);
            p_wifiReinit = true;
        }
        if (p_patch.containsKey("motion")) {
            if (!p_root.motion) p_root.motion = new ST_A10_MotionConfig();
            loadMotion(*p_root.motion);
        }
        if (p_patch.containsKey("control")) {
            if (!p_root.control) p_root.control = new ST_A10_ControlConfig();
            loadControl(*p_root.control);
        }
        saveAll(p_root);
        return true;
    }

    static void loadAll(ST_A10_ConfigRoot& p_root) {
        A10_resetToDefault(p_root);
        loadSystem(p_root.system);
        p_root.wifi = new ST_A10_WifiConfig();
        loadWifi(*p_root.wifi);
        p_root.motion = new ST_A10_MotionConfig();
        loadMotion(*p_root.motion);
        p_root.control = new ST_A10_ControlConfig();
        loadControl(*p_root.control);
    }

    static void saveAll(const ST_A10_ConfigRoot& p_root) {
        saveSystem(p_root.system);
        if (p_root.wifi) saveWifi(*p_root.wifi);
        if (p_root.motion) saveMotion(*p_root.motion);
        if (p_root.control) saveControl(*p_root.control);
    }

    static void resetAll(ST_A10_ConfigRoot& p_root) {
        A10_resetToDefault(p_root);
        saveAll(p_root);
    }

    static void restoreAllFromBackup() {
        restoreBackupFile(A10_Const::CFG_SYSTEM_FILE_BAK,  A10_Const::CFG_SYSTEM_FILE);
        restoreBackupFile(A10_Const::CFG_WIFI_FILE_BAK,    A10_Const::CFG_WIFI_FILE);
        restoreBackupFile(A10_Const::CFG_MOTION_FILE_BAK,  A10_Const::CFG_MOTION_FILE);
        restoreBackupFile(A10_Const::CFG_CONTROL_FILE_BAK, A10_Const::CFG_CONTROL_FILE);
    }

};

