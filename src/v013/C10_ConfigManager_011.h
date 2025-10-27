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


#include <Arduino.h>
#include <ArduinoJson.h>   // v7.x.x 전용
#include <LittleFS.h>
#include "A10_Const_011.h"
#include "D10_Logger_010.h"

class CL_C10_ConfigManager {
public:
    // ======================================================
    // 공통 JSON I/O 유틸
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
    // SYSTEM: load/save
    // ======================================================
    static bool loadSystem(ST_A10_CoreConfig& p_cfg) {
        JsonDocument v_doc;
        if (!_loadJsonFile(A10_Const::CFG_SYSTEM_FILE, v_doc)) return false;
        JsonObjectConst v_root = v_doc.as<JsonObjectConst>();

        // meta
        strlcpy(p_cfg.meta.version,     v_root["meta"]["version"]     | A10_Const::FW_VERSION, sizeof(p_cfg.meta.version));
        strlcpy(p_cfg.meta.device_name, v_root["meta"]["device_name"] | "WindScape_XY-SK10",   sizeof(p_cfg.meta.device_name));
        strlcpy(p_cfg.meta.last_update, v_root["meta"]["last_update"] | "",                    sizeof(p_cfg.meta.last_update));

        // system.web
        strlcpy(p_cfg.system.web.html, v_root["system"]["web"]["html"] | "/html/SC10_main_021.html", sizeof(p_cfg.system.web.html));
        strlcpy(p_cfg.system.web.css,  v_root["system"]["web"]["css"]  | "/html/SC10_main_021.css",  sizeof(p_cfg.system.web.css));
        strlcpy(p_cfg.system.web.js,   v_root["system"]["web"]["js"]   | "/html/SC10_main_021.js",   sizeof(p_cfg.system.web.js));

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
        strlcpy(p_cfg.hw.sensors.tempHum.type, v_root["hw"]["tempHum"]["type"]     | "DHT22", sizeof(p_cfg.hw.sensors.tempHum.type));
        p_cfg.hw.sensors.tempHum.pin          = v_root["hw"]["tempHum"]["pin"]     | 23;
        p_cfg.hw.sensors.tempHum.interval_sec = v_root["hw"]["tempHum"]["interval_sec"] | 30;

        // hw.ble
        p_cfg.hw.sensors.ble.enabled       = v_root["hw"]["ble"]["enabled"]       | true;
        p_cfg.hw.sensors.ble.scan_interval = v_root["hw"]["ble"]["scan_interval"] | 5;

        // security
        strlcpy(p_cfg.security.api_key, v_root["security"]["api_key"] | "my_api_key_12345", sizeof(p_cfg.security.api_key));

        // time
        strlcpy(p_cfg.time.ntp_server, v_root["time"]["ntp_server"] | "pool.ntp.org", sizeof(p_cfg.time.ntp_server));
        strlcpy(p_cfg.time.timezone,   v_root["time"]["timezone"]   | "Asia/Seoul",   sizeof(p_cfg.time.timezone));
        p_cfg.time.sync_interval_min = v_root["time"]["sync_interval_min"] | 60;

        return true;
    }

    static bool saveSystem(const ST_A10_CoreConfig& p_cfg) {
        JsonDocument v_doc;

        v_doc["meta"]["version"]      = p_cfg.meta.version;
        v_doc["meta"]["device_name"]  = p_cfg.meta.device_name;
        v_doc["meta"]["last_update"]  = p_cfg.meta.last_update;

        v_doc["system"]["web"]["html"] = p_cfg.system.web.html;
        v_doc["system"]["web"]["css"]  = p_cfg.system.web.css;
        v_doc["system"]["web"]["js"]   = p_cfg.system.web.js;
        v_doc["system"]["logging"]["level"]       = p_cfg.system.logging.level;
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

        v_doc["time"]["ntp_server"]       = p_cfg.time.ntp_server;
        v_doc["time"]["timezone"]         = p_cfg.time.timezone;
        v_doc["time"]["sync_interval_min"]= p_cfg.time.sync_interval_min;

        // NOTE: System 파일의 백업 상수는 CFG_SYSTEM_FILE_BAK 사용(최신 상수 정의 준수)
        return _saveJsonFile(A10_Const::CFG_SYSTEM_FILE, A10_Const::CFG_SYSTEM_FILE_BAK, v_doc);
    }

    // ======================================================
    // WIFI: load/save  (wifiMode / mode 모두 수용)
    // ======================================================
    static bool loadWifi(ST_A10_WifiConfig& p_cfg) {
        JsonDocument v_doc;
        if (!_loadJsonFile(A10_Const::CFG_WIFI_FILE, v_doc)) return false;
        JsonObjectConst jw = v_doc["wifi"];

        // 입력 JSON 호환: wifiMode(권장) 또는 mode(레거시)
        uint8_t v_mode = jw["wifiMode"] | jw["mode"] | EN_A10_WIFI_MODE_AP_STA;
        p_cfg.mode = static_cast<EN_A10_WIFI_MODE_t>(v_mode);

        strlcpy(p_cfg.ap.ssid,     jw["ap"]["ssid"]     | "NatureWind", sizeof(p_cfg.ap.ssid));
        strlcpy(p_cfg.ap.password, jw["ap"]["password"] | "2540",       sizeof(p_cfg.ap.password));

        p_cfg.sta_count = 0;
        if (jw["sta"].is<JsonArrayConst>()) {
            for (JsonObjectConst s : jw["sta"].as<JsonArrayConst>()) {
                if (p_cfg.sta_count >= A10_Const::MAX_STA_NETWORKS) break;
                strlcpy(p_cfg.sta[p_cfg.sta_count].ssid, s["ssid"] | "", sizeof(p_cfg.sta[0].ssid));
                strlcpy(p_cfg.sta[p_cfg.sta_count].pass, s["pass"] | "", sizeof(p_cfg.sta[0].pass));
                p_cfg.sta_count++;
            }
        }
        return true;
    }

    static bool saveWifi(const ST_A10_WifiConfig& p_cfg) {
        JsonDocument v_doc;
        // 권장 필드 이름으로 기록 (호환을 위해 mode도 함께 기록)
        v_doc["wifi"]["wifiMode"] = static_cast<uint8_t>(p_cfg.mode);
        v_doc["wifi"]["mode"]     = static_cast<uint8_t>(p_cfg.mode);
        v_doc["wifi"]["ap"]["ssid"]     = p_cfg.ap.ssid;
        v_doc["wifi"]["ap"]["password"] = p_cfg.ap.password;

        for (uint8_t i = 0; i < p_cfg.sta_count; i++) {
            JsonObject v = v_doc["wifi"]["sta"].add<JsonObject>();
            v["ssid"] = p_cfg.sta[i].ssid;
            v["pass"] = p_cfg.sta[i].pass;
        }
        return _saveJsonFile(A10_Const::CFG_WIFI_FILE, A10_Const::CFG_WIFI_FILE_BAK, v_doc);
    }

    // ======================================================
    // MOTION: load/save  (PIR debounce는 SYSTEM.hw 소관)
    // ======================================================
    static bool loadMotion(ST_A10_MotionConfig& p_cfg) {
        JsonDocument v_doc;
        if (!_loadJsonFile(A10_Const::CFG_MOTION_FILE, v_doc)) return false;
        JsonObjectConst jm = v_doc["motion"];

        p_cfg.enabled       = jm["enabled"] | true;
        p_cfg.pir.enabled   = jm["pir"]["enabled"]  | true;
        p_cfg.pir.hold_sec  = jm["pir"]["hold_sec"] | 120;

        p_cfg.ble.enabled        = jm["ble"]["enabled"]        | true;
        p_cfg.ble.rssi_threshold = jm["ble"]["rssi_threshold"] | -70;
        p_cfg.ble.hold_sec       = jm["ble"]["hold_sec"]       | 120;

        p_cfg.ble.device_count = 0;
        if (jm["ble"]["devices"].is<JsonArrayConst>()) {
            for (JsonObjectConst d : jm["ble"]["devices"].as<JsonArrayConst>()) {
                if (p_cfg.ble.device_count >= A10_Const::MAX_BLE_DEVICES) break;
                auto& dev = p_cfg.ble.devices[p_cfg.ble.device_count++];
                strlcpy(dev.mac,   d["mac"]   | "", sizeof(dev.mac));
                strlcpy(dev.alias, d["alias"] | "", sizeof(dev.alias));
                dev.enabled = d["enabled"] | false;
            }
        }
        return true;
    }

    static bool saveMotion(const ST_A10_MotionConfig& p_cfg) {
        JsonDocument v_doc;
        v_doc["motion"]["enabled"]          = p_cfg.enabled;

        v_doc["motion"]["pir"]["enabled"]   = p_cfg.pir.enabled;
        v_doc["motion"]["pir"]["hold_sec"]  = p_cfg.pir.hold_sec;

        v_doc["motion"]["ble"]["enabled"]        = p_cfg.ble.enabled;
        v_doc["motion"]["ble"]["rssi_threshold"] = p_cfg.ble.rssi_threshold;
        v_doc["motion"]["ble"]["hold_sec"]       = p_cfg.ble.hold_sec;

        for (uint8_t i = 0; i < p_cfg.ble.device_count; i++) {
            JsonObject v = v_doc["motion"]["ble"]["devices"].add<JsonObject>();
            v["mac"]     = p_cfg.ble.devices[i].mac;
            v["alias"]   = p_cfg.ble.devices[i].alias;
            v["enabled"] = p_cfg.ble.devices[i].enabled;
        }
        return _saveJsonFile(A10_Const::CFG_MOTION_FILE, A10_Const::CFG_MOTION_FILE_BAK, v_doc);
    }

    // ======================================================
    // CONTROL: load/save (Continuous + Schedules)
    // ======================================================
    static bool loadControl(ST_A10_ControlConfig& p_cfg) {
        JsonDocument v_doc;
        if (!_loadJsonFile(A10_Const::CFG_CONTROL_FILE, v_doc)) return false;
        JsonObjectConst jc = v_doc["control"];

        p_cfg.runMode = jc["runMode"] | 0;
        strlcpy(p_cfg.runModeDesc, jc["runModeDesc"] | "0=Continuous Mode, 1=Schedule Mode", sizeof(p_cfg.runModeDesc));

        // Continuous.wind
        JsonObjectConst cw = jc["Continuous"]["wind"];
        p_cfg.continuous.wind.enabled                    = cw["enabled"]                    | true;
        strlcpy(p_cfg.continuous.wind.preset,              cw["preset"]                     | "COUNTRY_BREEZE", sizeof(p_cfg.continuous.wind.preset));
        p_cfg.continuous.wind.wind_intensity             = cw["wind_intensity"]             | 70.0f;
        p_cfg.continuous.wind.gust_frequency             = cw["gust_frequency"]             | 45.0f;
        p_cfg.continuous.wind.wind_variability           = cw["wind_variability"]           | 50.0f;
        p_cfg.continuous.wind.fan_limit                  = cw["fan_limit"]                  | 90.0f;
        p_cfg.continuous.wind.min_fan                    = cw["min_fan"]                    | 10.0f;
        p_cfg.continuous.wind.turbulence_length_scale    = cw["turbulence_length_scale"]    | 40.0f;
        p_cfg.continuous.wind.turbulence_intensity_sigma = cw["turbulence_intensity_sigma"] | 0.5f;
        p_cfg.continuous.wind.thermal_bubble_strength    = cw["thermal_bubble_strength"]    | 2.0f;
        p_cfg.continuous.wind.thermal_bubble_radius      = cw["thermal_bubble_radius"]      | 18.0f;

        // Continuous.motion
        JsonObjectConst cm = jc["Continuous"]["motion"];
        p_cfg.continuous.motion.pir.enabled        = cm["pir"]["enabled"]        | true;
        p_cfg.continuous.motion.pir.hold_sec       = cm["pir"]["hold_sec"]       | 120;
        p_cfg.continuous.motion.ble.enabled        = cm["ble"]["enabled"]        | true;
        p_cfg.continuous.motion.ble.rssi_threshold = cm["ble"]["rssi_threshold"] | -70;
        p_cfg.continuous.motion.ble.hold_sec       = cm["ble"]["hold_sec"]       | 120;

        // schedules
        p_cfg.schedule_count = 0;
        if (jc["schedules"].is<JsonArrayConst>()) {
            for (JsonObjectConst js : jc["schedules"].as<JsonArrayConst>()) {
                if (p_cfg.schedule_count >= A10_Const::MAX_SCHEDULES) break;
                auto& sch = p_cfg.schedules[p_cfg.schedule_count++];

                sch.schNo   = js["schNo"] | 0;
                strlcpy(sch.schName, js["schName"] | js["SchName"] | "", sizeof(sch.schName)); // 이름 키 대소/철자 호환
                sch.enabled = js["enabled"] | true;

                // days
                memset(sch.days, 1, sizeof(sch.days));
                if (js["days"].is<JsonArrayConst>()) {
                    JsonArrayConst days = js["days"].as<JsonArrayConst>();
                    for (uint8_t i = 0; i < 7 && i < days.size(); i++) sch.days[i] = days[i] | 1;
                }

                strlcpy(sch.start_time, js["start_time"] | "08:00", sizeof(sch.start_time));
                strlcpy(sch.end_time,   js["end_time"]   | "12:00", sizeof(sch.end_time));

                sch.seg_count = 0;
                if (js["segments"].is<JsonArrayConst>()) {
                    for (JsonObjectConst jseg : js["segments"].as<JsonArrayConst>()) {
                        if (sch.seg_count >= A10_Const::MAX_SEGMENTS_PER_SCHEDULE) break;
                        auto& s = sch.segments[sch.seg_count++];

                        s.segNo       = jseg["segNo"]       | jseg["no"] | 0;
                        s.on_minutes  = jseg["on_minutes"]  | 10;
                        s.off_minutes = jseg["off_minutes"] | 5;
                        strlcpy(s.mode,        jseg["mode"]         | "preset",          sizeof(s.mode));
                        strlcpy(s.preset_name, jseg["preset_name"]  | "COUNTRY_BREEZE",  sizeof(s.preset_name));
                        s.preset_adjust.intensity   = jseg["preset_adjust"]["intensity"]   | 0.0f;
                        s.preset_adjust.variability = jseg["preset_adjust"]["variability"] | 0.0f;
                        s.fixed_speed               = jseg["fixed_speed"]                  | 0.0f;
                    }
                }

                sch.motion.pir.enabled        = js["motion"]["pir"]["enabled"]        | true;
                sch.motion.pir.hold_sec       = js["motion"]["pir"]["hold_sec"]       | 120;
                sch.motion.ble.enabled        = js["motion"]["ble"]["enabled"]        | true;
                sch.motion.ble.rssi_threshold = js["motion"]["ble"]["rssi_threshold"] | -70;
                sch.motion.ble.hold_sec       = js["motion"]["ble"]["hold_sec"]       | 120;
            }
        }
        return true;
    }

    static bool saveControl(const ST_A10_ControlConfig& p_cfg) {
        JsonDocument v_doc;
        JsonObject j = v_doc["control"];

        j["runMode"]    = p_cfg.runMode;
        j["runModeDesc"]= p_cfg.runModeDesc;

        // Continuous.wind
        JsonObject cw = j["Continuous"]["wind"];
        cw["enabled"]                    = p_cfg.continuous.wind.enabled;
        cw["preset"]                     = p_cfg.continuous.wind.preset;
        cw["wind_intensity"]             = p_cfg.continuous.wind.wind_intensity;
        cw["gust_frequency"]             = p_cfg.continuous.wind.gust_frequency;
        cw["wind_variability"]           = p_cfg.continuous.wind.wind_variability;
        cw["fan_limit"]                  = p_cfg.continuous.wind.fan_limit;
        cw["min_fan"]                    = p_cfg.continuous.wind.min_fan;
        cw["turbulence_length_scale"]    = p_cfg.continuous.wind.turbulence_length_scale;
        cw["turbulence_intensity_sigma"] = p_cfg.continuous.wind.turbulence_intensity_sigma;
        cw["thermal_bubble_strength"]    = p_cfg.continuous.wind.thermal_bubble_strength;
        cw["thermal_bubble_radius"]      = p_cfg.continuous.wind.thermal_bubble_radius;

        // Continuous.motion
        j["Continuous"]["motion"]["pir"]["enabled"]        = p_cfg.continuous.motion.pir.enabled;
        j["Continuous"]["motion"]["pir"]["hold_sec"]       = p_cfg.continuous.motion.pir.hold_sec;
        j["Continuous"]["motion"]["ble"]["enabled"]        = p_cfg.continuous.motion.ble.enabled;
        j["Continuous"]["motion"]["ble"]["rssi_threshold"] = p_cfg.continuous.motion.ble.rssi_threshold;
        j["Continuous"]["motion"]["ble"]["hold_sec"]       = p_cfg.continuous.motion.ble.hold_sec;

        // schedules
        for (uint8_t i = 0; i < p_cfg.schedule_count; i++) {
            const auto& sch = p_cfg.schedules[i];
            JsonObject sj = j["schedules"].add<JsonObject>();

            sj["schNo"]   = sch.schNo;
            sj["schName"] = sch.schName;
            sj["enabled"] = sch.enabled;

            JsonArray days = sj["days"];
            for (uint8_t d = 0; d < 7; d++) days.add(sch.days[d]);

            sj["start_time"] = sch.start_time;
            sj["end_time"]   = sch.end_time;

            for (uint8_t s = 0; s < sch.seg_count; s++) {
                const auto& seg = sch.segments[s];
                JsonObject sg = sj["segments"].add<JsonObject>();
                sg["segNo"]        = seg.segNo;
                sg["on_minutes"]   = seg.on_minutes;
                sg["off_minutes"]  = seg.off_minutes;
                sg["mode"]         = seg.mode;
                sg["preset_name"]  = seg.preset_name;
                sg["preset_adjust"]["intensity"]   = seg.preset_adjust.intensity;
                sg["preset_adjust"]["variability"] = seg.preset_adjust.variability;
                sg["fixed_speed"]  = seg.fixed_speed;
            }

            sj["motion"]["pir"]["enabled"]        = sch.motion.pir.enabled;
            sj["motion"]["pir"]["hold_sec"]       = sch.motion.pir.hold_sec;
            sj["motion"]["ble"]["enabled"]        = sch.motion.ble.enabled;
            sj["motion"]["ble"]["rssi_threshold"] = sch.motion.ble.rssi_threshold;
            sj["motion"]["ble"]["hold_sec"]       = sch.motion.ble.hold_sec;
        }

        return _saveJsonFile(A10_Const::CFG_CONTROL_FILE, A10_Const::CFG_CONTROL_FILE_BAK, v_doc);
    }

    // ======================================================
    // PATCH: 부분 갱신
    //  - 입력 JSON에 존재하는 키만 반영
    //  - 변경 섹션만 개별 파일 저장
    //  - p_needWifiReinit: Wi-Fi 재초기화 필요 여부 반환
    // ======================================================
    static bool patchFromJson(ST_A10_ConfigRoot& p_root, const JsonDocument& p_patch, bool& p_needWifiReinit) {
        p_needWifiReinit = false;
        bool v_changed = false;
        JsonObjectConst v_root = p_patch.as<JsonObjectConst>();

        // ---- SYSTEM ----
        if (!v_root["system"].isNull() || !v_root["meta"].isNull() || !v_root["hw"].isNull()
            || !v_root["security"].isNull() || !v_root["time"].isNull()) {

            // 최신 내용을 로드 후 패치 (안전)
            loadSystem(p_root.core);

            // meta
            if (!v_root["meta"].isNull()) {
                if (!v_root["meta"]["version"].isNull())
                    strlcpy(p_root.core.meta.version, v_root["meta"]["version"], sizeof(p_root.core.meta.version));
                if (!v_root["meta"]["device_name"].isNull())
                    strlcpy(p_root.core.meta.device_name, v_root["meta"]["device_name"], sizeof(p_root.core.meta.device_name));
                if (!v_root["meta"]["last_update"].isNull())
                    strlcpy(p_root.core.meta.last_update, v_root["meta"]["last_update"], sizeof(p_root.core.meta.last_update));
            }

            // system
            if (!v_root["system"].isNull()) {
                if (!v_root["system"]["web"].isNull()) {
                    if (!v_root["system"]["web"]["html"].isNull()) strlcpy(p_root.core.system.web.html, v_root["system"]["web"]["html"], sizeof(p_root.core.system.web.html));
                    if (!v_root["system"]["web"]["css"].isNull())  strlcpy(p_root.core.system.web.css,  v_root["system"]["web"]["css"],  sizeof(p_root.core.system.web.css));
                    if (!v_root["system"]["web"]["js"].isNull())   strlcpy(p_root.core.system.web.js,   v_root["system"]["web"]["js"],   sizeof(p_root.core.system.web.js));
                }
                if (!v_root["system"]["logging"].isNull()) {
                    if (!v_root["system"]["logging"]["level"].isNull())
                        strlcpy(p_root.core.system.logging.level, v_root["system"]["logging"]["level"], sizeof(p_root.core.system.logging.level));
                    if (!v_root["system"]["logging"]["max_entries"].isNull())
                        p_root.core.system.logging.max_entries = v_root["system"]["logging"]["max_entries"] | p_root.core.system.logging.max_entries;
                }
            }

            // hw
            if (!v_root["hw"].isNull()) {
                if (!v_root["hw"]["fan_pwm"].isNull()) {
                    if (!v_root["hw"]["fan_pwm"]["pin"].isNull())     p_root.core.hw.fan_pwm.pin     = v_root["hw"]["fan_pwm"]["pin"]     | p_root.core.hw.fan_pwm.pin;
                    if (!v_root["hw"]["fan_pwm"]["channel"].isNull()) p_root.core.hw.fan_pwm.channel = v_root["hw"]["fan_pwm"]["channel"] | p_root.core.hw.fan_pwm.channel;
                    if (!v_root["hw"]["fan_pwm"]["freq"].isNull())    p_root.core.hw.fan_pwm.freq    = v_root["hw"]["fan_pwm"]["freq"]    | p_root.core.hw.fan_pwm.freq;
                    if (!v_root["hw"]["fan_pwm"]["res"].isNull())     p_root.core.hw.fan_pwm.res     = v_root["hw"]["fan_pwm"]["res"]     | p_root.core.hw.fan_pwm.res;
                }
                if (!v_root["hw"]["pir"].isNull()) {
                    if (!v_root["hw"]["pir"]["enabled"].isNull())      p_root.core.hw.sensors.pir.enabled      = v_root["hw"]["pir"]["enabled"]      | p_root.core.hw.sensors.pir.enabled;
                    if (!v_root["hw"]["pir"]["pin"].isNull())          p_root.core.hw.sensors.pir.pin          = v_root["hw"]["pir"]["pin"]          | p_root.core.hw.sensors.pir.pin;
                    if (!v_root["hw"]["pir"]["debounce_sec"].isNull()) p_root.core.hw.sensors.pir.debounce_sec = v_root["hw"]["pir"]["debounce_sec"] | p_root.core.hw.sensors.pir.debounce_sec;
                }
                if (!v_root["hw"]["tempHum"].isNull()) {
                    if (!v_root["hw"]["tempHum"]["enabled"].isNull())      p_root.core.hw.sensors.tempHum.enabled      = v_root["hw"]["tempHum"]["enabled"]      | p_root.core.hw.sensors.tempHum.enabled;
                    if (!v_root["hw"]["tempHum"]["type"].isNull())         strlcpy(p_root.core.hw.sensors.tempHum.type, v_root["hw"]["tempHum"]["type"], sizeof(p_root.core.hw.sensors.tempHum.type));
                    if (!v_root["hw"]["tempHum"]["pin"].isNull())          p_root.core.hw.sensors.tempHum.pin          = v_root["hw"]["tempHum"]["pin"]          | p_root.core.hw.sensors.tempHum.pin;
                    if (!v_root["hw"]["tempHum"]["interval_sec"].isNull()) p_root.core.hw.sensors.tempHum.interval_sec = v_root["hw"]["tempHum"]["interval_sec"] | p_root.core.hw.sensors.tempHum.interval_sec;
                }
                if (!v_root["hw"]["ble"].isNull()) {
                    if (!v_root["hw"]["ble"]["enabled"].isNull())       p_root.core.hw.sensors.ble.enabled       = v_root["hw"]["ble"]["enabled"]       | p_root.core.hw.sensors.ble.enabled;
                    if (!v_root["hw"]["ble"]["scan_interval"].isNull()) p_root.core.hw.sensors.ble.scan_interval = v_root["hw"]["ble"]["scan_interval"] | p_root.core.hw.sensors.ble.scan_interval;
                }
            }

            // security
            if (!v_root["security"].isNull()) {
                if (!v_root["security"]["api_key"].isNull())
                    strlcpy(p_root.core.security.api_key, v_root["security"]["api_key"], sizeof(p_root.core.security.api_key));
            }

            // time
            if (!v_root["time"].isNull()) {
                if (!v_root["time"]["ntp_server"].isNull())
                    strlcpy(p_root.core.time.ntp_server, v_root["time"]["ntp_server"], sizeof(p_root.core.time.ntp_server));
                if (!v_root["time"]["timezone"].isNull())
                    strlcpy(p_root.core.time.timezone,   v_root["time"]["timezone"],   sizeof(p_root.core.time.timezone));
                if (!v_root["time"]["sync_interval_min"].isNull())
                    p_root.core.time.sync_interval_min = v_root["time"]["sync_interval_min"] | p_root.core.time.sync_interval_min;
            }

            saveSystem(p_root.core);
            v_changed = true;
        }

        // ---- WIFI ----
        if (!v_root["wifi"].isNull()) {
            if (!p_root.wifi) p_root.wifi = new ST_A10_WifiConfig();
            // 최신 내용 로드 후 패치
            loadWifi(*p_root.wifi);

            JsonObjectConst jw = v_root["wifi"];
            // mode / wifiMode 둘 다 수용
            if (!jw["wifiMode"].isNull() || !jw["mode"].isNull()) {
                uint8_t v_mode = jw["wifiMode"] | jw["mode"] | static_cast<uint8_t>(p_root.wifi->mode);
                p_root.wifi->mode = static_cast<EN_A10_WIFI_MODE_t>(v_mode);
                p_needWifiReinit = true;
            }
            if (!jw["ap"].isNull()) {
                if (!jw["ap"]["ssid"].isNull())     strlcpy(p_root.wifi->ap.ssid,     jw["ap"]["ssid"],     sizeof(p_root.wifi->ap.ssid)), p_needWifiReinit = true;
                if (!jw["ap"]["password"].isNull()) strlcpy(p_root.wifi->ap.password, jw["ap"]["password"], sizeof(p_root.wifi->ap.password)), p_needWifiReinit = true;
            }
            if (jw["sta"].is<JsonArrayConst>()) {
                p_root.wifi->sta_count = 0;
                for (JsonObjectConst s : jw["sta"].as<JsonArrayConst>()) {
                    if (p_root.wifi->sta_count >= A10_Const::MAX_STA_NETWORKS) break;
                    strlcpy(p_root.wifi->sta[p_root.wifi->sta_count].ssid, s["ssid"] | "", sizeof(p_root.wifi->sta[0].ssid));
                    strlcpy(p_root.wifi->sta[p_root.wifi->sta_count].pass, s["pass"] | "", sizeof(p_root.wifi->sta[0].pass));
                    p_root.wifi->sta_count++;
                }
                p_needWifiReinit = true;
            }
            saveWifi(*p_root.wifi);
            v_changed = true;
        }

        // ---- MOTION ----
        if (!v_root["motion"].isNull()) {
            if (!p_root.motion) p_root.motion = new ST_A10_MotionConfig();
            loadMotion(*p_root.motion);

            JsonObjectConst jm = v_root["motion"];
            if (!jm["enabled"].isNull()) p_root.motion->enabled = jm["enabled"] | p_root.motion->enabled;

            if (!jm["pir"].isNull()) {
                if (!jm["pir"]["enabled"].isNull())  p_root.motion->pir.enabled  = jm["pir"]["enabled"]  | p_root.motion->pir.enabled;
                if (!jm["pir"]["hold_sec"].isNull()) p_root.motion->pir.hold_sec = jm["pir"]["hold_sec"] | p_root.motion->pir.hold_sec;
            }
            if (!jm["ble"].isNull()) {
                if (!jm["ble"]["enabled"].isNull())        p_root.motion->ble.enabled        = jm["ble"]["enabled"]        | p_root.motion->ble.enabled;
                if (!jm["ble"]["rssi_threshold"].isNull()) p_root.motion->ble.rssi_threshold = jm["ble"]["rssi_threshold"] | p_root.motion->ble.rssi_threshold;
                if (!jm["ble"]["hold_sec"].isNull())       p_root.motion->ble.hold_sec       = jm["ble"]["hold_sec"]       | p_root.motion->ble.hold_sec;

                if (jm["ble"]["devices"].is<JsonArrayConst>()) {
                    p_root.motion->ble.device_count = 0;
                    for (JsonObjectConst d : jm["ble"]["devices"].as<JsonArrayConst>()) {
                        if (p_root.motion->ble.device_count >= A10_Const::MAX_BLE_DEVICES) break;
                        auto& dev = p_root.motion->ble.devices[p_root.motion->ble.device_count++];
                        strlcpy(dev.mac,   d["mac"]   | "", sizeof(dev.mac));
                        strlcpy(dev.alias, d["alias"] | "", sizeof(dev.alias));
                        dev.enabled = d["enabled"] | false;
                    }
                }
            }
            saveMotion(*p_root.motion);
            v_changed = true;
        }

        // ---- CONTROL ----
        if (!v_root["control"].isNull()) {
            if (!p_root.control) p_root.control = new ST_A10_ControlConfig();
            loadControl(*p_root.control);

            JsonObjectConst jc = v_root["control"];
            if (!jc["runMode"].isNull()) p_root.control->runMode = jc["runMode"] | p_root.control->runMode;
            if (!jc["runModeDesc"].isNull()) strlcpy(p_root.control->runModeDesc, jc["runModeDesc"], sizeof(p_root.control->runModeDesc));

            // Continuous.wind
            if (!jc["Continuous"].isNull() && !jc["Continuous"]["wind"].isNull()) {
                JsonObjectConst cw = jc["Continuous"]["wind"];
                if (!cw["enabled"].isNull())                    p_root.control->continuous.wind.enabled = cw["enabled"] | p_root.control->continuous.wind.enabled;
                if (!cw["preset"].isNull())                     strlcpy(p_root.control->continuous.wind.preset, cw["preset"], sizeof(p_root.control->continuous.wind.preset));
                if (!cw["wind_intensity"].isNull())             p_root.control->continuous.wind.wind_intensity             = cw["wind_intensity"]             | p_root.control->continuous.wind.wind_intensity;
                if (!cw["gust_frequency"].isNull())             p_root.control->continuous.wind.gust_frequency             = cw["gust_frequency"]             | p_root.control->continuous.wind.gust_frequency;
                if (!cw["wind_variability"].isNull())           p_root.control->continuous.wind.wind_variability           = cw["wind_variability"]           | p_root.control->continuous.wind.wind_variability;
                if (!cw["fan_limit"].isNull())                  p_root.control->continuous.wind.fan_limit                  = cw["fan_limit"]                  | p_root.control->continuous.wind.fan_limit;
                if (!cw["min_fan"].isNull())                    p_root.control->continuous.wind.min_fan                    = cw["min_fan"]                    | p_root.control->continuous.wind.min_fan;
                if (!cw["turbulence_length_scale"].isNull())    p_root.control->continuous.wind.turbulence_length_scale    = cw["turbulence_length_scale"]    | p_root.control->continuous.wind.turbulence_length_scale;
                if (!cw["turbulence_intensity_sigma"].isNull()) p_root.control->continuous.wind.turbulence_intensity_sigma = cw["turbulence_intensity_sigma"] | p_root.control->continuous.wind.turbulence_intensity_sigma;
                if (!cw["thermal_bubble_strength"].isNull())    p_root.control->continuous.wind.thermal_bubble_strength    = cw["thermal_bubble_strength"]    | p_root.control->continuous.wind.thermal_bubble_strength;
                if (!cw["thermal_bubble_radius"].isNull())      p_root.control->continuous.wind.thermal_bubble_radius      = cw["thermal_bubble_radius"]      | p_root.control->continuous.wind.thermal_bubble_radius;
            }

            // Continuous.motion
            if (!jc["Continuous"].isNull() && !jc["Continuous"]["motion"].isNull()) {
                JsonObjectConst cm = jc["Continuous"]["motion"];
                if (!cm["pir"]["enabled"].isNull())        p_root.control->continuous.motion.pir.enabled        = cm["pir"]["enabled"]        | p_root.control->continuous.motion.pir.enabled;
                if (!cm["pir"]["hold_sec"].isNull())       p_root.control->continuous.motion.pir.hold_sec       = cm["pir"]["hold_sec"]       | p_root.control->continuous.motion.pir.hold_sec;
                if (!cm["ble"]["enabled"].isNull())        p_root.control->continuous.motion.ble.enabled        = cm["ble"]["enabled"]        | p_root.control->continuous.motion.ble.enabled;
                if (!cm["ble"]["rssi_threshold"].isNull()) p_root.control->continuous.motion.ble.rssi_threshold = cm["ble"]["rssi_threshold"] | p_root.control->continuous.motion.ble.rssi_threshold;
                if (!cm["ble"]["hold_sec"].isNull())       p_root.control->continuous.motion.ble.hold_sec       = cm["ble"]["hold_sec"]       | p_root.control->continuous.motion.ble.hold_sec;
            }

            // schedules: 전체 치환(입력 배열이 오면 대체)
            if (jc["schedules"].is<JsonArrayConst>()) {
                p_root.control->schedule_count = 0;
                for (JsonObjectConst js : jc["schedules"].as<JsonArrayConst>()) {
                    if (p_root.control->schedule_count >= A10_Const::MAX_SCHEDULES) break;
                    auto& sch = p_root.control->schedules[p_root.control->schedule_count++];

                    sch.schNo   = js["schNo"] | js["no"] | 0;
                    strlcpy(sch.schName, js["schName"] | js["SchName"] | "", sizeof(sch.schName));
                    sch.enabled = js["enabled"] | true;

                    // days
                    memset(sch.days, 1, sizeof(sch.days));
                    if (js["days"].is<JsonArrayConst>()) {
                        JsonArrayConst days = js["days"].as<JsonArrayConst>();
                        for (uint8_t i = 0; i < 7 && i < days.size(); i++) sch.days[i] = days[i] | 1;
                    }

                    strlcpy(sch.start_time, js["start_time"] | "08:00", sizeof(sch.start_time));
                    strlcpy(sch.end_time,   js["end_time"]   | "12:00", sizeof(sch.end_time));

                    sch.seg_count = 0;
                    if (js["segments"].is<JsonArrayConst>()) {
                        for (JsonObjectConst jseg : js["segments"].as<JsonArrayConst>()) {
                            if (sch.seg_count >= A10_Const::MAX_SEGMENTS_PER_SCHEDULE) break;
                            auto& s = sch.segments[sch.seg_count++];

                            s.segNo       = jseg["segNo"]       | jseg["no"] | 0;
                            s.on_minutes  = jseg["on_minutes"]  | 10;
                            s.off_minutes = jseg["off_minutes"] | 5;
                            strlcpy(s.mode,        jseg["mode"]        | "preset",         sizeof(s.mode));
                            strlcpy(s.preset_name, jseg["preset_name"] | "COUNTRY_BREEZE", sizeof(s.preset_name));
                            s.preset_adjust.intensity   = jseg["preset_adjust"]["intensity"]   | 0.0f;
                            s.preset_adjust.variability = jseg["preset_adjust"]["variability"] | 0.0f;
                            s.fixed_speed               = jseg["fixed_speed"]                  | 0.0f;
                        }
                    }

                    sch.motion.pir.enabled        = js["motion"]["pir"]["enabled"]        | true;
                    sch.motion.pir.hold_sec       = js["motion"]["pir"]["hold_sec"]       | 120;
                    sch.motion.ble.enabled        = js["motion"]["ble"]["enabled"]        | true;
                    sch.motion.ble.rssi_threshold = js["motion"]["ble"]["rssi_threshold"] | -70;
                    sch.motion.ble.hold_sec       = js["motion"]["ble"]["hold_sec"]       | 120;
                }
            }

            saveControl(*p_root.control);
            v_changed = true;
        }

        return v_changed;
    }

    // ======================================================
    // ALL: load/save/reset/restore
    // ======================================================
    static void loadAll(ST_A10_ConfigRoot& p_root) {
        // Core는 상시 상주 기본값으로 세팅 후 파일 로드
        A10_resetToDefault(p_root);
        if (!loadSystem(p_root.core)) {
            A10_resetCoreDefault(p_root.core);
            saveSystem(p_root.core);
        }

        // WiFi
        if (!p_root.wifi) p_root.wifi = new ST_A10_WifiConfig();
        if (!loadWifi(*p_root.wifi)) {
            A10_resetWifiDefault(*p_root.wifi);
            saveWifi(*p_root.wifi);
        }

        // Motion
        if (!p_root.motion) p_root.motion = new ST_A10_MotionConfig();
        if (!loadMotion(*p_root.motion)) {
            A10_resetMotionDefault(*p_root.motion);
            saveMotion(*p_root.motion);
        }

        // Control
        if (!p_root.control) p_root.control = new ST_A10_ControlConfig();
        if (!loadControl(*p_root.control)) {
            A10_resetControlDefault(*p_root.control);
            saveControl(*p_root.control);
        }
    }

    static void saveAll(const ST_A10_ConfigRoot& p_root) {
        saveSystem(p_root.core);
        if (p_root.wifi)    saveWifi(*p_root.wifi);
        if (p_root.motion)  saveMotion(*p_root.motion);
        if (p_root.control) saveControl(*p_root.control);
    }

// ======================================================
    // RESETALL (Default JSON 기반 공장초기화)
    // ------------------------------------------------------
    // - cfg_default_022.json 파일에서 기본값을 불러와 각 섹션 분리저장
    // - 파일이 없을 경우 내부 기본값 생성 후 cfg_default_022.json도 재생성
    // - 규칙: ArduinoJson v7, JsonDocument 단일, 안전한 strlcpy
    // ======================================================
    static void resetAll(ST_A10_ConfigRoot& p_root) {
        constexpr char* DEF_FILE = (char*)A10_Const::CFG_DEFAULT_FILE;
        JsonDocument v_doc;

        // 1️⃣ default json 존재 여부 확인
        if (!LittleFS.exists(DEF_FILE)) {
            CL_D10_Logger::log(EN_L10_LOG_WARN, "Default file not found: %s -> creating built-in defaults", DEF_FILE);

            // 내장 기본값 JSON 생성
            v_doc["meta"]["version"] = A10_Const::FW_VERSION;
            v_doc["meta"]["device_name"] = "WindScape_XY-SK10";
            v_doc["system"]["web"]["html"] = "/html/SC10_main_021.html";
            v_doc["system"]["web"]["css"] = "/html/SC10_main_021.css";
            v_doc["system"]["web"]["js"]  = "/html/SC10_main_021.js";
            v_doc["system"]["logging"]["level"] = "INFO";
            v_doc["system"]["logging"]["max_entries"] = 300;

            v_doc["hw"]["fan_pwm"]["pin"] = 6;
            v_doc["hw"]["fan_pwm"]["channel"] = 0;
            v_doc["hw"]["fan_pwm"]["freq"] = 25000;
            v_doc["hw"]["fan_pwm"]["res"] = 10;
            v_doc["hw"]["pir"]["enabled"] = true;
            v_doc["hw"]["pir"]["pin"] = 13;
            v_doc["hw"]["pir"]["debounce_sec"] = 5;
            v_doc["hw"]["tempHum"]["enabled"] = true;
            v_doc["hw"]["tempHum"]["type"] = "DHT22";
            v_doc["hw"]["tempHum"]["pin"] = 23;
            v_doc["hw"]["tempHum"]["interval_sec"] = 30;
            v_doc["hw"]["ble"]["enabled"] = true;
            v_doc["hw"]["ble"]["scan_interval"] = 5;

            v_doc["security"]["api_key"] = "my_api_key_12345";
            v_doc["time"]["ntp_server"] = "pool.ntp.org";
            v_doc["time"]["timezone"] = "Asia/Seoul";
            v_doc["time"]["sync_interval_min"] = 60;

            v_doc["wifi"]["mode"] = 2;
            v_doc["wifi"]["ap"]["ssid"] = "NatureWind";
            v_doc["wifi"]["ap"]["password"] = "2540";
            JsonObject s = v_doc["wifi"]["sta"].add<JsonObject>();
            s["ssid"] = "";
            s["pass"] = "";

            v_doc["motion"]["enabled"] = true;
            v_doc["motion"]["pir"]["enabled"] = true;
            v_doc["motion"]["pir"]["hold_sec"] = 120;
            v_doc["motion"]["ble"]["enabled"] = true;
            v_doc["motion"]["ble"]["rssi_threshold"] = -70;
            v_doc["motion"]["ble"]["hold_sec"] = 120;

            JsonObject def_ctrl = v_doc["control"];
            def_ctrl["runMode"] = 0;
            def_ctrl["runModeDesc"] = "0=Continuous,1=Schedule";
            def_ctrl["Continuous"]["wind"]["enabled"] = true;
            def_ctrl["Continuous"]["wind"]["preset"] = "COUNTRY_BREEZE";
            def_ctrl["Continuous"]["wind"]["wind_intensity"] = 70.0;
            def_ctrl["Continuous"]["wind"]["gust_frequency"] = 45.0;
            def_ctrl["Continuous"]["wind"]["wind_variability"] = 50.0;
            def_ctrl["Continuous"]["wind"]["fan_limit"] = 90.0;
            def_ctrl["Continuous"]["wind"]["min_fan"] = 10.0;
            def_ctrl["Continuous"]["wind"]["turbulence_length_scale"] = 40.0;
            def_ctrl["Continuous"]["wind"]["turbulence_intensity_sigma"] = 0.5;
            def_ctrl["Continuous"]["wind"]["thermal_bubble_strength"] = 2.0;
            def_ctrl["Continuous"]["wind"]["thermal_bubble_radius"] = 18.0;
            def_ctrl["Continuous"]["motion"]["pir"]["enabled"] = true;
            def_ctrl["Continuous"]["motion"]["pir"]["hold_sec"] = 120;
            def_ctrl["Continuous"]["motion"]["ble"]["enabled"] = true;
            def_ctrl["Continuous"]["motion"]["ble"]["rssi_threshold"] = -70;
            def_ctrl["Continuous"]["motion"]["ble"]["hold_sec"] = 120;

            JsonObject sch = def_ctrl["schedules"].add<JsonObject>();
            sch["schNo"] = 0;
            sch["schName"] = "Morning Breeze";
            sch["enabled"] = true;
            JsonArray days = sch["days"];
            for (int i = 0; i < 7; i++) days.add(i < 5 ? 1 : 0);
            sch["start_time"] = "08:00";
            sch["end_time"] = "12:00";
            JsonObject seg = sch["segments"].add<JsonObject>();
            seg["segNo"] = 1;
            seg["on_minutes"] = 10;
            seg["off_minutes"] = 5;
            seg["mode"] = "preset";
            seg["preset_name"] = "COUNTRY_BREEZE";
            seg["preset_adjust"]["intensity"] = 0.0;
            seg["preset_adjust"]["variability"] = 0.0;
            seg["fixed_speed"] = 0.0;
            sch["motion"]["pir"]["enabled"] = true;
            sch["motion"]["pir"]["hold_sec"] = 120;
            sch["motion"]["ble"]["enabled"] = true;
            sch["motion"]["ble"]["rssi_threshold"] = -70;
            sch["motion"]["ble"]["hold_sec"] = 120;

            // 기본 default.json 저장
            _saveJsonFile(DEF_FILE, "/json/cfg_default_022.json.bak", v_doc);
            CL_D10_Logger::log(EN_L10_LOG_INFO, "Created default file: %s", DEF_FILE);
        } 
        else {
            File f = LittleFS.open(DEF_FILE, "r");
            auto err = deserializeJson(v_doc, f);
            f.close();
            if (err) {
                CL_D10_Logger::log(EN_L10_LOG_ERROR, "Parse error in default.json: %s", err.c_str());
                return;
            }
            CL_D10_Logger::log(EN_L10_LOG_INFO, "Loaded defaults from %s", DEF_FILE);
        }

        // 2️⃣ JSON에서 각 섹션 분리 저장
        if (v_doc.containsKey("meta") || v_doc.containsKey("system"))
            _saveJsonFile(A10_Const::CFG_SYSTEM_FILE, A10_Const::CFG_CORE_FILE_BAK, v_doc);
        if (v_doc.containsKey("wifi"))
            _saveJsonFile(A10_Const::CFG_WIFI_FILE, A10_Const::CFG_WIFI_FILE_BAK, v_doc);
        if (v_doc.containsKey("motion"))
            _saveJsonFile(A10_Const::CFG_MOTION_FILE, A10_Const::CFG_MOTION_FILE_BAK, v_doc);
        if (v_doc.containsKey("control"))
            _saveJsonFile(A10_Const::CFG_CONTROL_FILE, A10_Const::CFG_CONTROL_FILE_BAK, v_doc);

        CL_D10_Logger::log(EN_L10_LOG_INFO, "Factory reset applied from %s", DEF_FILE);
    }



    static void resetAll_old_001(ST_A10_ConfigRoot& p_root) {
        // 공장초기화: 메모리 기본값 → 파일 저장
        A10_resetToDefault(p_root);
        saveSystem(p_root.core);
        // Lazy 포인터는 nullptr이므로 필요 시 모듈별 즉시 초기화·저장
        ST_A10_WifiConfig v_wifi;   A10_resetWifiDefault(v_wifi);     saveWifi(v_wifi);
        ST_A10_MotionConfig v_motion; A10_resetMotionDefault(v_motion); saveMotion(v_motion);
        ST_A10_ControlConfig v_control; A10_resetControlDefault(v_control); saveControl(v_control);
        CL_D10_Logger::log(EN_L10_LOG_INFO, "Factory reset done (all sections defaulted)");
    }

    static void restoreAllFromBackup() {
        restoreBackupFile(A10_Const::CFG_SYSTEM_FILE_BAK,     A10_Const::CFG_SYSTEM_FILE);
        restoreBackupFile(A10_Const::CFG_WIFI_FILE_BAK,     A10_Const::CFG_WIFI_FILE);
        restoreBackupFile(A10_Const::CFG_MOTION_FILE_BAK,   A10_Const::CFG_MOTION_FILE);
        restoreBackupFile(A10_Const::CFG_CONTROL_FILE_BAK,  A10_Const::CFG_CONTROL_FILE);
    }
};



