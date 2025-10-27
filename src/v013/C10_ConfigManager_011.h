#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : C10_ConfigManager_011.h
 * 모듈 약어 : C10
 * 모듈명 : Smart Nature Wind Configuration Manager (v011)
 * ------------------------------------------------------
 * 기능 요약:
 *  - JSON 설정 분리 로드/저장/백업/복구 (system / wifi / motion / control)
 *  - 구조체 ↔ JSON 직렬화 (toJson / parseJson)
 *  - /api/config 패치(부분 갱신) 반영 (patchConfigFromJson)
 *  - 공장 초기화(factoryResetFromDefault): cfg_default_022.json → 각 파일 분리 저장
 *  - loadAllConfigs / saveAllConfigs / restoreAllFromBackups 제공
 * ------------------------------------------------------
 * 구현 규칙:
 *  - ArduinoJson v7.x.x 사용 (v6 이하 금지), JsonDocument 단일 타입만 사용
 *  - createNestedArray/Object/containsKey 사용 금지
 *  - 암묵적 생성 / 인덱스 기반 대입으로 배열/오브젝트 구성
 *  - memset + strlcpy 기반 안전 초기화
 *  - 주석/필드명은 JSON 구조와 동일하게 유지
 *  - 단일 헤더(h) 파일로 구성 (cpp 없음)
 * ------------------------------------------------------
 * 코드 네이밍 규칙:
 *  - 전역 상수/매크로 : G_모듈약어_
 *  - 전역 변수       : g_모듈약어_
 *  - 전역 함수       : 모듈약어_ 접두사
 *  - type            : T_모듈약어_
 *  - enum            : EN_모듈약어_
 *  - 구조체          : ST_모듈약어_
 *  - 클래스          : CL_모듈약어_
 *  - private 멤버    : _ 접두사
 *  - static 멤버     : s_ 접두사
 *  - 로컬 변수       : v_
 *  - 함수 인자       : p_
 * ------------------------------------------------------
 */

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
    static bool ioLoadJson(const char* p_path, JsonDocument& p_doc) {
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

    static bool ioSaveJson(const char* p_path, const char* p_path_bak, const JsonDocument& p_doc) {
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

    static bool ioRestoreBackup(const char* p_path_bak, const char* p_path_target) {
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
    static bool loadSystemConfig(ST_A10_SystemConfig& p_cfg) {
        JsonDocument v_doc;
        if (!ioLoadJson(A10_Const::CFG_SYSTEM_FILE, v_doc)) return false;
        JsonObjectConst v_root = v_doc.as<JsonObjectConst>();

        // meta
        strlcpy(p_cfg.meta.version,     v_root["meta"]["version"]     | A10_Const::FW_VERSION, sizeof(p_cfg.meta.version));
        strlcpy(p_cfg.meta.device_name, v_root["meta"]["device_name"] | "WindScape_XY-SK10",   sizeof(p_cfg.meta.device_name));
        strlcpy(p_cfg.meta.last_update, v_root["meta"]["last_update"] | "",                    sizeof(p_cfg.meta.last_update));

        // system.web
        strlcpy(p_cfg.system.web.html, v_root["system"]["web"]["html"] | "/html/SC10_main_021.html", sizeof(p_cfg.system.web.html));
        strlcpy(p_cfg.system.web.css,  v_root["system"]["web"]["css"]  | "/html/SC10_main_021.css",  sizeof(p_cfg.system.web.css));
        strlcpy(p_cfg.system.web.js,   v_root["system"]["web"]["js"]   | "/html/SC10_main_021.js",   sizeof(p_cfg.system.web.js));

        // system.logging
        strlcpy(p_cfg.system.logging.level, v_root["system"]["logging"]["level"] | "INFO", sizeof(p_cfg.system.logging.level));
        p_cfg.system.logging.max_entries = v_root["system"]["logging"]["max_entries"] | 300;

        // hw.fan_pwm
        p_cfg.hw.fan_pwm.pin     = v_root["hw"]["fan_pwm"]["pin"]     | 6;
        p_cfg.hw.fan_pwm.channel = v_root["hw"]["fan_pwm"]["channel"] | 0;
        p_cfg.hw.fan_pwm.freq    = v_root["hw"]["fan_pwm"]["freq"]    | 25000;
        p_cfg.hw.fan_pwm.res     = v_root["hw"]["fan_pwm"]["res"]     | 10;

        // hw.pir / tempHum / ble (sensors 계층 없음)
        p_cfg.hw.pir.enabled      = v_root["hw"]["pir"]["enabled"]      | true;
        p_cfg.hw.pir.pin          = v_root["hw"]["pir"]["pin"]          | 13;
        p_cfg.hw.pir.debounce_sec = v_root["hw"]["pir"]["debounce_sec"] | 5;

        p_cfg.hw.tempHum.enabled      = v_root["hw"]["tempHum"]["enabled"] | true;
        strlcpy(p_cfg.hw.tempHum.type, v_root["hw"]["tempHum"]["type"]     | "DHT22", sizeof(p_cfg.hw.tempHum.type));
        p_cfg.hw.tempHum.pin          = v_root["hw"]["tempHum"]["pin"]     | 23;
        p_cfg.hw.tempHum.interval_sec = v_root["hw"]["tempHum"]["interval_sec"] | 30;

        p_cfg.hw.ble.enabled       = v_root["hw"]["ble"]["enabled"]       | true;
        p_cfg.hw.ble.scan_interval = v_root["hw"]["ble"]["scan_interval"] | 5;

        // security
        strlcpy(p_cfg.security.api_key, v_root["security"]["api_key"] | "my_api_key_12345", sizeof(p_cfg.security.api_key));

        // time
        strlcpy(p_cfg.time.ntp_server, v_root["time"]["ntp_server"] | "pool.ntp.org", sizeof(p_cfg.time.ntp_server));
        strlcpy(p_cfg.time.timezone,   v_root["time"]["timezone"]   | "Asia/Seoul",   sizeof(p_cfg.time.timezone));
        p_cfg.time.sync_interval_min = v_root["time"]["sync_interval_min"] | 60;

        return true;
    }

    static bool saveSystemConfig(const ST_A10_SystemConfig& p_cfg) {
        JsonDocument v_doc;

        v_doc["meta"]["version"]     = p_cfg.meta.version;
        v_doc["meta"]["device_name"] = p_cfg.meta.device_name;
        v_doc["meta"]["last_update"] = p_cfg.meta.last_update;

        v_doc["system"]["web"]["html"] = p_cfg.system.web.html;
        v_doc["system"]["web"]["css"]  = p_cfg.system.web.css;
        v_doc["system"]["web"]["js"]   = p_cfg.system.web.js;
        v_doc["system"]["logging"]["level"]       = p_cfg.system.logging.level;
        v_doc["system"]["logging"]["max_entries"] = p_cfg.system.logging.max_entries;

        v_doc["hw"]["fan_pwm"]["pin"]     = p_cfg.hw.fan_pwm.pin;
        v_doc["hw"]["fan_pwm"]["channel"] = p_cfg.hw.fan_pwm.channel;
        v_doc["hw"]["fan_pwm"]["freq"]    = p_cfg.hw.fan_pwm.freq;
        v_doc["hw"]["fan_pwm"]["res"]     = p_cfg.hw.fan_pwm.res;

        v_doc["hw"]["pir"]["enabled"]      = p_cfg.hw.pir.enabled;
        v_doc["hw"]["pir"]["pin"]          = p_cfg.hw.pir.pin;
        v_doc["hw"]["pir"]["debounce_sec"] = p_cfg.hw.pir.debounce_sec;

        v_doc["hw"]["tempHum"]["enabled"]      = p_cfg.hw.tempHum.enabled;
        v_doc["hw"]["tempHum"]["type"]         = p_cfg.hw.tempHum.type;
        v_doc["hw"]["tempHum"]["pin"]          = p_cfg.hw.tempHum.pin;
        v_doc["hw"]["tempHum"]["interval_sec"] = p_cfg.hw.tempHum.interval_sec;

        v_doc["hw"]["ble"]["enabled"]       = p_cfg.hw.ble.enabled;
        v_doc["hw"]["ble"]["scan_interval"] = p_cfg.hw.ble.scan_interval;

        v_doc["security"]["api_key"] = p_cfg.security.api_key;

        v_doc["time"]["ntp_server"]        = p_cfg.time.ntp_server;
        v_doc["time"]["timezone"]          = p_cfg.time.timezone;
        v_doc["time"]["sync_interval_min"] = p_cfg.time.sync_interval_min;

        return ioSaveJson(A10_Const::CFG_SYSTEM_FILE, A10_Const::CFG_SYSTEM_FILE_BAK, v_doc);
    }

    // ======================================================
    // WIFI: load/save (wifi.wifiMode / wifiModeDesc)
    // ======================================================
    static bool loadWifiConfig(ST_A10_WifiConfig& p_cfg) {
        JsonDocument v_doc;
        if (!ioLoadJson(A10_Const::CFG_WIFI_FILE, v_doc)) return false;
        JsonObjectConst jw = v_doc["wifi"];

        uint8_t v_mode = jw["wifiMode"] | jw["mode"] | static_cast<uint8_t>(EN_A10_WIFI_MODE_AP_STA);
        p_cfg.wifiMode = static_cast<EN_A10_WIFI_MODE_t>(v_mode);
        strlcpy(p_cfg.wifiModeDesc, jw["wifiModeDesc"] | "0:AP, 1:STA, 2: AP+STA", sizeof(p_cfg.wifiModeDesc));

        strlcpy(p_cfg.ap.ssid,     jw["ap"]["ssid"]     | "NatureWind", sizeof(p_cfg.ap.ssid));
        strlcpy(p_cfg.ap.password, jw["ap"]["password"] | "2540",       sizeof(p_cfg.ap.password));

        p_cfg.sta_count = 0;
        if (jw["sta"].is<JsonArrayConst>()) {
            JsonArrayConst arr = jw["sta"].as<JsonArrayConst>();
            for (JsonObjectConst s : arr) {
                if (p_cfg.sta_count >= A10_Const::MAX_STA_NETWORKS) break;
                strlcpy(p_cfg.sta[p_cfg.sta_count].ssid, s["ssid"] | "", sizeof(p_cfg.sta[0].ssid));
                strlcpy(p_cfg.sta[p_cfg.sta_count].pass, s["pass"] | "", sizeof(p_cfg.sta[0].pass));
                p_cfg.sta_count++;
            }
        }
        return true;
    }

    static bool saveWifiConfig(const ST_A10_WifiConfig& p_cfg) {
        JsonDocument v_doc;

        v_doc["wifi"]["wifiMode"]    = static_cast<uint8_t>(p_cfg.wifiMode);
        v_doc["wifi"]["wifiModeDesc"]= p_cfg.wifiModeDesc;               // 설명도 함께 저장
        v_doc["wifi"]["mode"]        = static_cast<uint8_t>(p_cfg.wifiMode); // 레거시 호환
        v_doc["wifi"]["ap"]["ssid"]     = p_cfg.ap.ssid;
        v_doc["wifi"]["ap"]["password"] = p_cfg.ap.password;

        // 배열 인덱싱으로 암묵 생성 (createNested/add 미사용)
        for (uint8_t i = 0; i < p_cfg.sta_count; i++) {
            v_doc["wifi"]["sta"][i]["ssid"] = p_cfg.sta[i].ssid;
            v_doc["wifi"]["sta"][i]["pass"] = p_cfg.sta[i].pass;
        }
        return ioSaveJson(A10_Const::CFG_WIFI_FILE, A10_Const::CFG_WIFI_FILE_BAK, v_doc);
    }

    // ======================================================
    // MOTION: load/save  (PIR debounce는 system.hw 소관)
    // ======================================================
    static bool loadMotionConfig(ST_A10_MotionConfig& p_cfg) {
        JsonDocument v_doc;
        if (!ioLoadJson(A10_Const::CFG_MOTION_FILE, v_doc)) return false;
        JsonObjectConst jm = v_doc["motion"];

        p_cfg.enabled      = jm["enabled"] | true;

        p_cfg.pir.enabled  = jm["pir"]["enabled"]  | true;
        p_cfg.pir.hold_sec = jm["pir"]["hold_sec"] | 120;

        p_cfg.ble.enabled        = jm["ble"]["enabled"]        | true;
        p_cfg.ble.rssi_threshold = jm["ble"]["rssi_threshold"] | -70;
        p_cfg.ble.hold_sec       = jm["ble"]["hold_sec"]       | 120;

        p_cfg.ble.device_count = 0;
        if (jm["ble"]["devices"].is<JsonArrayConst>()) {
            JsonArrayConst arr = jm["ble"]["devices"].as<JsonArrayConst>();
            for (JsonObjectConst d : arr) {
                if (p_cfg.ble.device_count >= A10_Const::MAX_BLE_DEVICES) break;
                auto& dev = p_cfg.ble.devices[p_cfg.ble.device_count++];
                strlcpy(dev.mac,   d["mac"]   | "", sizeof(dev.mac));
                strlcpy(dev.alias, d["alias"] | "", sizeof(dev.alias));
                dev.enabled = d["enabled"] | false;
            }
        }
        return true;
    }

    static bool saveMotionConfig(const ST_A10_MotionConfig& p_cfg) {
        JsonDocument v_doc;

        v_doc["motion"]["enabled"]         = p_cfg.enabled;
        v_doc["motion"]["pir"]["enabled"]  = p_cfg.pir.enabled;
        v_doc["motion"]["pir"]["hold_sec"] = p_cfg.pir.hold_sec;

        v_doc["motion"]["ble"]["enabled"]        = p_cfg.ble.enabled;
        v_doc["motion"]["ble"]["rssi_threshold"] = p_cfg.ble.rssi_threshold;
        v_doc["motion"]["ble"]["hold_sec"]       = p_cfg.ble.hold_sec;

        for (uint8_t i = 0; i < p_cfg.ble.device_count; i++) {
            v_doc["motion"]["ble"]["devices"][i]["mac"]     = p_cfg.ble.devices[i].mac;
            v_doc["motion"]["ble"]["devices"][i]["alias"]   = p_cfg.ble.devices[i].alias;
            v_doc["motion"]["ble"]["devices"][i]["enabled"] = p_cfg.ble.devices[i].enabled;
        }
        return ioSaveJson(A10_Const::CFG_MOTION_FILE, A10_Const::CFG_MOTION_FILE_BAK, v_doc);
    }

    // ======================================================
    // CONTROL: load/save (Continuous + Schedules)
    // ======================================================
    static bool loadControlConfig(ST_A10_ControlConfig& p_cfg) {
        JsonDocument v_doc;
        if (!ioLoadJson(A10_Const::CFG_CONTROL_FILE, v_doc)) return false;
        JsonObjectConst jc = v_doc["control"];

        p_cfg.runMode = jc["runMode"] | 0;
        strlcpy(p_cfg.runModeDesc, jc["runModeDesc"] | "0=Continuous Mode, 1=Schedule Mode", sizeof(p_cfg.runModeDesc));

        // Continuous.wind
        JsonObjectConst cw = jc["Continuous"]["wind"];
        p_cfg.Continuous.wind.enabled                    = cw["enabled"]                    | true;
        strlcpy(p_cfg.Continuous.wind.preset,              cw["preset"]                     | "COUNTRY_BREEZE", sizeof(p_cfg.Continuous.wind.preset));
        p_cfg.Continuous.wind.wind_intensity             = cw["wind_intensity"]             | 70.0f;
        p_cfg.Continuous.wind.gust_frequency             = cw["gust_frequency"]             | 45.0f;
        p_cfg.Continuous.wind.wind_variability           = cw["wind_variability"]           | 50.0f;
        p_cfg.Continuous.wind.fan_limit                  = cw["fan_limit"]                  | 90.0f;
        p_cfg.Continuous.wind.min_fan                    = cw["min_fan"]                    | 10.0f;
        p_cfg.Continuous.wind.turbulence_length_scale    = cw["turbulence_length_scale"]    | 40.0f;
        p_cfg.Continuous.wind.turbulence_intensity_sigma = cw["turbulence_intensity_sigma"] | 0.5f;
        p_cfg.Continuous.wind.thermal_bubble_strength    = cw["thermal_bubble_strength"]    | 2.0f;
        p_cfg.Continuous.wind.thermal_bubble_radius      = cw["thermal_bubble_radius"]      | 18.0f;

        // Continuous.motion
        JsonObjectConst cm = jc["Continuous"]["motion"];
        p_cfg.Continuous.motion.pir.enabled        = cm["pir"]["enabled"]        | true;
        p_cfg.Continuous.motion.pir.hold_sec       = cm["pir"]["hold_sec"]       | 120;
        p_cfg.Continuous.motion.ble.enabled        = cm["ble"]["enabled"]        | true;
        p_cfg.Continuous.motion.ble.rssi_threshold = cm["ble"]["rssi_threshold"] | -70;
        p_cfg.Continuous.motion.ble.hold_sec       = cm["ble"]["hold_sec"]       | 120;

        // schedules
        p_cfg.schedule_count = 0;
        if (jc["schedules"].is<JsonArrayConst>()) {
            JsonArrayConst arr = jc["schedules"].as<JsonArrayConst>();
            for (JsonObjectConst js : arr) {
                if (p_cfg.schedule_count >= A10_Const::MAX_SCHEDULES) break;
                auto& sch = p_cfg.schedules[p_cfg.schedule_count++];

                sch.schNo   = js["schNo"] | js["no"] | 0;
                strlcpy(sch.schName, js["schName"] | js["SchName"] | "", sizeof(sch.schName)); // 철자/대소 호환
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
                    JsonArrayConst segs = js["segments"].as<JsonArrayConst>();
                    for (JsonObjectConst jseg : segs) {
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
        return true;
    }

    static bool saveControlConfig(const ST_A10_ControlConfig& p_cfg) {
        JsonDocument v_doc;
        JsonObject j = v_doc["control"];

        j["runMode"]     = p_cfg.runMode;
        j["runModeDesc"] = p_cfg.runModeDesc;

        // Continuous.wind
        j["Continuous"]["wind"]["enabled"]                    = p_cfg.Continuous.wind.enabled;
        j["Continuous"]["wind"]["preset"]                     = p_cfg.Continuous.wind.preset;
        j["Continuous"]["wind"]["wind_intensity"]             = p_cfg.Continuous.wind.wind_intensity;
        j["Continuous"]["wind"]["gust_frequency"]             = p_cfg.Continuous.wind.gust_frequency;
        j["Continuous"]["wind"]["wind_variability"]           = p_cfg.Continuous.wind.wind_variability;
        j["Continuous"]["wind"]["fan_limit"]                  = p_cfg.Continuous.wind.fan_limit;
        j["Continuous"]["wind"]["min_fan"]                    = p_cfg.Continuous.wind.min_fan;
        j["Continuous"]["wind"]["turbulence_length_scale"]    = p_cfg.Continuous.wind.turbulence_length_scale;
        j["Continuous"]["wind"]["turbulence_intensity_sigma"] = p_cfg.Continuous.wind.turbulence_intensity_sigma;
        j["Continuous"]["wind"]["thermal_bubble_strength"]    = p_cfg.Continuous.wind.thermal_bubble_strength;
        j["Continuous"]["wind"]["thermal_bubble_radius"]      = p_cfg.Continuous.wind.thermal_bubble_radius;

        // Continuous.motion
        j["Continuous"]["motion"]["pir"]["enabled"]        = p_cfg.Continuous.motion.pir.enabled;
        j["Continuous"]["motion"]["pir"]["hold_sec"]       = p_cfg.Continuous.motion.pir.hold_sec;
        j["Continuous"]["motion"]["ble"]["enabled"]        = p_cfg.Continuous.motion.ble.enabled;
        j["Continuous"]["motion"]["ble"]["rssi_threshold"] = p_cfg.Continuous.motion.ble.rssi_threshold;
        j["Continuous"]["motion"]["ble"]["hold_sec"]       = p_cfg.Continuous.motion.ble.hold_sec;

        // schedules(인덱스 기반 생성)
        for (uint8_t i = 0; i < p_cfg.schedule_count; i++) {
            const auto& sch = p_cfg.schedules[i];
            j["schedules"][i]["schNo"]    = sch.schNo;
            j["schedules"][i]["schName"]  = sch.schName;
            j["schedules"][i]["enabled"]  = sch.enabled;

            for (uint8_t d = 0; d < 7; d++) {
                j["schedules"][i]["days"][d] = sch.days[d];
            }
            j["schedules"][i]["start_time"] = sch.start_time;
            j["schedules"][i]["end_time"]   = sch.end_time;

            for (uint8_t s = 0; s < sch.seg_count; s++) {
                const auto& seg = sch.segments[s];
                j["schedules"][i]["segments"][s]["segNo"]       = seg.segNo;
                j["schedules"][i]["segments"][s]["on_minutes"]  = seg.on_minutes;
                j["schedules"][i]["segments"][s]["off_minutes"] = seg.off_minutes;
                j["schedules"][i]["segments"][s]["mode"]        = seg.mode;
                j["schedules"][i]["segments"][s]["preset_name"] = seg.preset_name;
                j["schedules"][i]["segments"][s]["preset_adjust"]["intensity"]   = seg.preset_adjust.intensity;
                j["schedules"][i]["segments"][s]["preset_adjust"]["variability"] = seg.preset_adjust.variability;
                j["schedules"][i]["segments"][s]["fixed_speed"] = seg.fixed_speed;
            }

            j["schedules"][i]["motion"]["pir"]["enabled"]        = sch.motion.pir.enabled;
            j["schedules"][i]["motion"]["pir"]["hold_sec"]       = sch.motion.pir.hold_sec;
            j["schedules"][i]["motion"]["ble"]["enabled"]        = sch.motion.ble.enabled;
            j["schedules"][i]["motion"]["ble"]["rssi_threshold"] = sch.motion.ble.rssi_threshold;
            j["schedules"][i]["motion"]["ble"]["hold_sec"]       = sch.motion.ble.hold_sec;
        }

        return ioSaveJson(A10_Const::CFG_CONTROL_FILE, A10_Const::CFG_CONTROL_FILE_BAK, v_doc);
    }

    // ======================================================
    // PATCH: 부분 갱신 (/api/config)
    //  - 입력 JSON에 존재하는 키만 반영
    //  - 변경 섹션만 개별 파일 저장
    //  - p_needWifiReinit: Wi-Fi 재초기화 필요 여부 반환
    // ======================================================
    static bool patchConfigFromJson(ST_A10_ConfigRoot& p_root, const JsonDocument& p_patch, bool& p_needWifiReinit) {
        p_needWifiReinit = false;
        bool v_changed = false;
        JsonObjectConst v_root = p_patch.as<JsonObjectConst>();

        // ---- SYSTEM ----
        if (!v_root["system"].isNull() || !v_root["meta"].isNull() || !v_root["hw"].isNull()
         || !v_root["security"].isNull() || !v_root["time"].isNull()) {

            loadSystemConfig(p_root.system);

            // meta
            if (!v_root["meta"].isNull()) {
                if (!v_root["meta"]["version"].isNull())
                    strlcpy(p_root.system.meta.version, v_root["meta"]["version"], sizeof(p_root.system.meta.version));
                if (!v_root["meta"]["device_name"].isNull())
                    strlcpy(p_root.system.meta.device_name, v_root["meta"]["device_name"], sizeof(p_root.system.meta.device_name));
                if (!v_root["meta"]["last_update"].isNull())
                    strlcpy(p_root.system.meta.last_update, v_root["meta"]["last_update"], sizeof(p_root.system.meta.last_update));
            }

            // system.web/logging
            if (!v_root["system"].isNull()) {
                if (!v_root["system"]["web"].isNull()) {
                    if (!v_root["system"]["web"]["html"].isNull()) strlcpy(p_root.system.system.web.html, v_root["system"]["web"]["html"], sizeof(p_root.system.system.web.html));
                    if (!v_root["system"]["web"]["css"].isNull())  strlcpy(p_root.system.system.web.css,  v_root["system"]["web"]["css"],  sizeof(p_root.system.system.web.css));
                    if (!v_root["system"]["web"]["js"].isNull())   strlcpy(p_root.system.system.web.js,   v_root["system"]["web"]["js"],   sizeof(p_root.system.system.web.js));
                }
                if (!v_root["system"]["logging"].isNull()) {
                    if (!v_root["system"]["logging"]["level"].isNull())
                        strlcpy(p_root.system.system.logging.level, v_root["system"]["logging"]["level"], sizeof(p_root.system.system.logging.level));
                    if (!v_root["system"]["logging"]["max_entries"].isNull())
                        p_root.system.system.logging.max_entries = v_root["system"]["logging"]["max_entries"] | p_root.system.system.logging.max_entries;
                }
            }

            // hw
            if (!v_root["hw"].isNull()) {
                if (!v_root["hw"]["fan_pwm"].isNull()) {
                    if (!v_root["hw"]["fan_pwm"]["pin"].isNull())     p_root.system.hw.fan_pwm.pin     = v_root["hw"]["fan_pwm"]["pin"]     | p_root.system.hw.fan_pwm.pin;
                    if (!v_root["hw"]["fan_pwm"]["channel"].isNull()) p_root.system.hw.fan_pwm.channel = v_root["hw"]["fan_pwm"]["channel"] | p_root.system.hw.fan_pwm.channel;
                    if (!v_root["hw"]["fan_pwm"]["freq"].isNull())    p_root.system.hw.fan_pwm.freq    = v_root["hw"]["fan_pwm"]["freq"]    | p_root.system.hw.fan_pwm.freq;
                    if (!v_root["hw"]["fan_pwm"]["res"].isNull())     p_root.system.hw.fan_pwm.res     = v_root["hw"]["fan_pwm"]["res"]     | p_root.system.hw.fan_pwm.res;
                }
                if (!v_root["hw"]["pir"].isNull()) {
                    if (!v_root["hw"]["pir"]["enabled"].isNull())      p_root.system.hw.pir.enabled      = v_root["hw"]["pir"]["enabled"]      | p_root.system.hw.pir.enabled;
                    if (!v_root["hw"]["pir"]["pin"].isNull())          p_root.system.hw.pir.pin          = v_root["hw"]["pir"]["pin"]          | p_root.system.hw.pir.pin;
                    if (!v_root["hw"]["pir"]["debounce_sec"].isNull()) p_root.system.hw.pir.debounce_sec = v_root["hw"]["pir"]["debounce_sec"] | p_root.system.hw.pir.debounce_sec;
                }
                if (!v_root["hw"]["tempHum"].isNull()) {
                    if (!v_root["hw"]["tempHum"]["enabled"].isNull())      p_root.system.hw.tempHum.enabled      = v_root["hw"]["tempHum"]["enabled"]      | p_root.system.hw.tempHum.enabled;
                    if (!v_root["hw"]["tempHum"]["type"].isNull())         strlcpy(p_root.system.hw.tempHum.type, v_root["hw"]["tempHum"]["type"], sizeof(p_root.system.hw.tempHum.type));
                    if (!v_root["hw"]["tempHum"]["pin"].isNull())          p_root.system.hw.tempHum.pin          = v_root["hw"]["tempHum"]["pin"]          | p_root.system.hw.tempHum.pin;
                    if (!v_root["hw"]["tempHum"]["interval_sec"].isNull()) p_root.system.hw.tempHum.interval_sec = v_root["hw"]["tempHum"]["interval_sec"] | p_root.system.hw.tempHum.interval_sec;
                }
                if (!v_root["hw"]["ble"].isNull()) {
                    if (!v_root["hw"]["ble"]["enabled"].isNull())       p_root.system.hw.ble.enabled       = v_root["hw"]["ble"]["enabled"]       | p_root.system.hw.ble.enabled;
                    if (!v_root["hw"]["ble"]["scan_interval"].isNull()) p_root.system.hw.ble.scan_interval = v_root["hw"]["ble"]["scan_interval"] | p_root.system.hw.ble.scan_interval;
                }
            }

            // security
            if (!v_root["security"].isNull()) {
                if (!v_root["security"]["api_key"].isNull())
                    strlcpy(p_root.system.security.api_key, v_root["security"]["api_key"], sizeof(p_root.system.security.api_key));
            }

            // time
            if (!v_root["time"].isNull()) {
                if (!v_root["time"]["ntp_server"].isNull())
                    strlcpy(p_root.system.time.ntp_server, v_root["time"]["ntp_server"], sizeof(p_root.system.time.ntp_server));
                if (!v_root["time"]["timezone"].isNull())
                    strlcpy(p_root.system.time.timezone,   v_root["time"]["timezone"],   sizeof(p_root.system.time.timezone));
                if (!v_root["time"]["sync_interval_min"].isNull())
                    p_root.system.time.sync_interval_min = v_root["time"]["sync_interval_min"] | p_root.system.time.sync_interval_min;
            }

            saveSystemConfig(p_root.system);
            v_changed = true;
        }

        // ---- WIFI ----
        if (!v_root["wifi"].isNull()) {
            if (!p_root.wifi) p_root.wifi = new ST_A10_WifiConfig();
            loadWifiConfig(*p_root.wifi);

            JsonObjectConst jw = v_root["wifi"];
            if (!jw["wifiMode"].isNull() || !jw["mode"].isNull()) {
                uint8_t v_mode = jw["wifiMode"] | jw["mode"] | static_cast<uint8_t>(p_root.wifi->wifiMode);
                p_root.wifi->wifiMode = static_cast<EN_A10_WIFI_MODE_t>(v_mode);
                p_needWifiReinit = true;
            }
            if (!jw["wifiModeDesc"].isNull()) {
                strlcpy(p_root.wifi->wifiModeDesc, jw["wifiModeDesc"], sizeof(p_root.wifi->wifiModeDesc));
            }
            if (!jw["ap"].isNull()) {
                if (!jw["ap"]["ssid"].isNull())     { strlcpy(p_root.wifi->ap.ssid,     jw["ap"]["ssid"],     sizeof(p_root.wifi->ap.ssid));         p_needWifiReinit = true; }
                if (!jw["ap"]["password"].isNull()) { strlcpy(p_root.wifi->ap.password, jw["ap"]["password"], sizeof(p_root.wifi->ap.password));     p_needWifiReinit = true; }
            }
            if (jw["sta"].is<JsonArrayConst>()) {
                p_root.wifi->sta_count = 0;
                JsonArrayConst arr = jw["sta"].as<JsonArrayConst>();
                for (JsonObjectConst s : arr) {
                    if (p_root.wifi->sta_count >= A10_Const::MAX_STA_NETWORKS) break;
                    strlcpy(p_root.wifi->sta[p_root.wifi->sta_count].ssid, s["ssid"] | "", sizeof(p_root.wifi->sta[0].ssid));
                    strlcpy(p_root.wifi->sta[p_root.wifi->sta_count].pass, s["pass"] | "", sizeof(p_root.wifi->sta[0].pass));
                    p_root.wifi->sta_count++;
                }
                p_needWifiReinit = true;
            }
            saveWifiConfig(*p_root.wifi);
            v_changed = true;
        }

        // ---- MOTION ----
        if (!v_root["motion"].isNull()) {
            if (!p_root.motion) p_root.motion = new ST_A10_MotionConfig();
            loadMotionConfig(*p_root.motion);

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
                    JsonArrayConst arr = jm["ble"]["devices"].as<JsonArrayConst>();
                    for (JsonObjectConst d : arr) {
                        if (p_root.motion->ble.device_count >= A10_Const::MAX_BLE_DEVICES) break;
                        auto& dev = p_root.motion->ble.devices[p_root.motion->ble.device_count++];
                        strlcpy(dev.mac,   d["mac"]   | "", sizeof(dev.mac));
                        strlcpy(dev.alias, d["alias"] | "", sizeof(dev.alias));
                        dev.enabled = d["enabled"] | false;
                    }
                }
            }
            saveMotionConfig(*p_root.motion);
            v_changed = true;
        }

        // ---- CONTROL ----
        if (!v_root["control"].isNull()) {
            if (!p_root.control) p_root.control = new ST_A10_ControlConfig();
            loadControlConfig(*p_root.control);

            JsonObjectConst jc = v_root["control"];
            if (!jc["runMode"].isNull())     p_root.control->runMode = jc["runMode"] | p_root.control->runMode;
            if (!jc["runModeDesc"].isNull()) strlcpy(p_root.control->runModeDesc, jc["runModeDesc"], sizeof(p_root.control->runModeDesc));

            // Continuous.wind
            if (!jc["Continuous"].isNull() && !jc["Continuous"]["wind"].isNull()) {
                JsonObjectConst cw = jc["Continuous"]["wind"];
                if (!cw["enabled"].isNull())                    p_root.control->Continuous.wind.enabled = cw["enabled"] | p_root.control->Continuous.wind.enabled;
                if (!cw["preset"].isNull())                     strlcpy(p_root.control->Continuous.wind.preset, cw["preset"], sizeof(p_root.control->Continuous.wind.preset));
                if (!cw["wind_intensity"].isNull())             p_root.control->Continuous.wind.wind_intensity             = cw["wind_intensity"]             | p_root.control->Continuous.wind.wind_intensity;
                if (!cw["gust_frequency"].isNull())             p_root.control->Continuous.wind.gust_frequency             = cw["gust_frequency"]             | p_root.control->Continuous.wind.gust_frequency;
                if (!cw["wind_variability"].isNull())           p_root.control->Continuous.wind.wind_variability           = cw["wind_variability"]           | p_root.control->Continuous.wind.wind_variability;
                if (!cw["fan_limit"].isNull())                  p_root.control->Continuous.wind.fan_limit                  = cw["fan_limit"]                  | p_root.control->Continuous.wind.fan_limit;
                if (!cw["min_fan"].isNull())                    p_root.control->Continuous.wind.min_fan                    = cw["min_fan"]                    | p_root.control->Continuous.wind.min_fan;
                if (!cw["turbulence_length_scale"].isNull())    p_root.control->Continuous.wind.turbulence_length_scale    = cw["turbulence_length_scale"]    | p_root.control->Continuous.wind.turbulence_length_scale;
                if (!cw["turbulence_intensity_sigma"].isNull()) p_root.control->Continuous.wind.turbulence_intensity_sigma = cw["turbulence_intensity_sigma"] | p_root.control->Continuous.wind.turbulence_intensity_sigma;
                if (!cw["thermal_bubble_strength"].isNull())    p_root.control->Continuous.wind.thermal_bubble_strength    = cw["thermal_bubble_strength"]    | p_root.control->Continuous.wind.thermal_bubble_strength;
                if (!cw["thermal_bubble_radius"].isNull())      p_root.control->Continuous.wind.thermal_bubble_radius      = cw["thermal_bubble_radius"]      | p_root.control->Continuous.wind.thermal_bubble_radius;
            }

            // Continuous.motion
            if (!jc["Continuous"].isNull() && !jc["Continuous"]["motion"].isNull()) {
                JsonObjectConst cm = jc["Continuous"]["motion"];
                if (!cm["pir"]["enabled"].isNull())        p_root.control->Continuous.motion.pir.enabled        = cm["pir"]["enabled"]        | p_root.control->Continuous.motion.pir.enabled;
                if (!cm["pir"]["hold_sec"].isNull())       p_root.control->Continuous.motion.pir.hold_sec       = cm["pir"]["hold_sec"]       | p_root.control->Continuous.motion.pir.hold_sec;
                if (!cm["ble"]["enabled"].isNull())        p_root.control->Continuous.motion.ble.enabled        = cm["ble"]["enabled"]        | p_root.control->Continuous.motion.ble.enabled;
                if (!cm["ble"]["rssi_threshold"].isNull()) p_root.control->Continuous.motion.ble.rssi_threshold = cm["ble"]["rssi_threshold"] | p_root.control->Continuous.motion.ble.rssi_threshold;
                if (!cm["ble"]["hold_sec"].isNull())       p_root.control->Continuous.motion.ble.hold_sec       = cm["ble"]["hold_sec"]       | p_root.control->Continuous.motion.ble.hold_sec;
            }

            // schedules: 전체 교체(입력이 있을 때)
            if (jc["schedules"].is<JsonArrayConst>()) {
                p_root.control->schedule_count = 0;
                JsonArrayConst arr = jc["schedules"].as<JsonArrayConst>();
                for (JsonObjectConst js : arr) {
                    if (p_root.control->schedule_count >= A10_Const::MAX_SCHEDULES) break;
                    auto& sch = p_root.control->schedules[p_root.control->schedule_count++];

                    sch.schNo   = js["schNo"] | js["no"] | 0;
                    strlcpy(sch.schName, js["schName"] | js["SchName"] | "", sizeof(sch.schName));
                    sch.enabled = js["enabled"] | true;

                    memset(sch.days, 1, sizeof(sch.days));
                    if (js["days"].is<JsonArrayConst>()) {
                        JsonArrayConst days = js["days"].as<JsonArrayConst>();
                        for (uint8_t i = 0; i < 7 && i < days.size(); i++) sch.days[i] = days[i] | 1;
                    }

                    strlcpy(sch.start_time, js["start_time"] | "08:00", sizeof(sch.start_time));
                    strlcpy(sch.end_time,   js["end_time"]   | "12:00", sizeof(sch.end_time));

                    sch.seg_count = 0;
                    if (js["segments"].is<JsonArrayConst>()) {
                        JsonArrayConst segs = js["segments"].as<JsonArrayConst>();
                        for (JsonObjectConst jseg : segs) {
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

            saveControlConfig(*p_root.control);
            v_changed = true;
        }

        return v_changed;
    }

    // ======================================================
    // ALL: load/save/reset/restore
    // ======================================================
    static void loadAllConfigs(ST_A10_ConfigRoot& p_root) {
        // system은 상시 상주 기본값 → 파일 로드
        A10_resetToDefault(p_root);
        if (!loadSystemConfig(p_root.system)) {
            A10_resetSystemDefault(p_root.system);
            saveSystemConfig(p_root.system);
        }

        // WiFi
        if (!p_root.wifi) p_root.wifi = new ST_A10_WifiConfig();
        if (!loadWifiConfig(*p_root.wifi)) {
            A10_resetWifiDefault(*p_root.wifi);
            saveWifiConfig(*p_root.wifi);
        }

        // Motion
        if (!p_root.motion) p_root.motion = new ST_A10_MotionConfig();
        if (!loadMotionConfig(*p_root.motion)) {
            A10_resetMotionDefault(*p_root.motion);
            saveMotionConfig(*p_root.motion);
        }

        // Control
        if (!p_root.control) p_root.control = new ST_A10_ControlConfig();
        if (!loadControlConfig(*p_root.control)) {
            A10_resetControlDefault(*p_root.control);
            saveControlConfig(*p_root.control);
        }
    }

    static void saveAllConfigs(const ST_A10_ConfigRoot& p_root) {
        saveSystemConfig(p_root.system);
        if (p_root.wifi)    saveWifiConfig(*p_root.wifi);
        if (p_root.motion)  saveMotionConfig(*p_root.motion);
        if (p_root.control) saveControlConfig(*p_root.control);
    }

    // ======================================================
    // 공장초기화: cfg_default_022.json → 섹션 분리 저장
    // ======================================================
    static void factoryResetFromDefault(ST_A10_ConfigRoot& p_root) {
        CL_D10_Logger::log(EN_L10_LOG_INFO, "[factoryReset] From %s", A10_Const::CFG_DEFAULT_FILE);

        JsonDocument v_doc;
        if (!LittleFS.exists(A10_Const::CFG_DEFAULT_FILE)) {
            CL_D10_Logger::log(EN_L10_LOG_ERROR, "Default file missing: %s", A10_Const::CFG_DEFAULT_FILE);
            A10_resetToDefault(p_root);
            saveAllConfigs(p_root);
            return;
        }
        File v_file = LittleFS.open(A10_Const::CFG_DEFAULT_FILE, "r");
        if (!v_file) {
            CL_D10_Logger::log(EN_L10_LOG_ERROR, "Cannot open default file: %s", A10_Const::CFG_DEFAULT_FILE);
            A10_resetToDefault(p_root);
            saveAllConfigs(p_root);
            return;
        }
        auto v_err = deserializeJson(v_doc, v_file);
        v_file.close();
        if (v_err) {
            CL_D10_Logger::log(EN_L10_LOG_ERROR, "Parse failed in default file: %s", v_err.c_str());
            A10_resetToDefault(p_root);
            saveAllConfigs(p_root);
            return;
        }

        // 1) system 섹션
        {
            JsonDocument js;
            if (!v_doc["meta"].isNull())     js["meta"]     = v_doc["meta"];
            if (!v_doc["system"].isNull())   js["system"]   = v_doc["system"];
            if (!v_doc["hw"].isNull())       js["hw"]       = v_doc["hw"];
            if (!v_doc["security"].isNull()) js["security"] = v_doc["security"];
            if (!v_doc["time"].isNull())     js["time"]     = v_doc["time"];
            ioSaveJson(A10_Const::CFG_SYSTEM_FILE, A10_Const::CFG_SYSTEM_FILE_BAK, js);
        }

        // 2) wifi 섹션
        if (!v_doc["wifi"].isNull()) {
            JsonDocument jw; jw["wifi"] = v_doc["wifi"];
            ioSaveJson(A10_Const::CFG_WIFI_FILE, A10_Const::CFG_WIFI_FILE_BAK, jw);
        } else {
            CL_D10_Logger::log(EN_L10_LOG_WARN, "Default: wifi section missing");
        }

        // 3) motion 섹션
        if (!v_doc["motion"].isNull()) {
            JsonDocument jm; jm["motion"] = v_doc["motion"];
            ioSaveJson(A10_Const::CFG_MOTION_FILE, A10_Const::CFG_MOTION_FILE_BAK, jm);
        } else {
            CL_D10_Logger::log(EN_L10_LOG_WARN, "Default: motion section missing");
        }

        // 4) control 섹션
        if (!v_doc["control"].isNull()) {
            JsonDocument jc; jc["control"] = v_doc["control"];
            ioSaveJson(A10_Const::CFG_CONTROL_FILE, A10_Const::CFG_CONTROL_FILE_BAK, jc);
        } else {
            CL_D10_Logger::log(EN_L10_LOG_WARN, "Default: control section missing");
        }

        // 5) 메모리 구조체 초기화 및 반영
        A10_resetToDefault(p_root);
        loadAllConfigs(p_root);
        CL_D10_Logger::log(EN_L10_LOG_INFO, "Factory reset completed.");
    }

    // ======================================================
    // 백업에서 전체 복구
    // ======================================================
    static void restoreAllFromBackups() {
        ioRestoreBackup(A10_Const::CFG_SYSTEM_FILE_BAK,   A10_Const::CFG_SYSTEM_FILE);
        ioRestoreBackup(A10_Const::CFG_WIFI_FILE_BAK,     A10_Const::CFG_WIFI_FILE);
        ioRestoreBackup(A10_Const::CFG_MOTION_FILE_BAK,   A10_Const::CFG_MOTION_FILE);
        ioRestoreBackup(A10_Const::CFG_CONTROL_FILE_BAK,  A10_Const::CFG_CONTROL_FILE);
    }
};
