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
    // CONTROL, PATCH, LOADALL 등은 다음 단계에 추가
    // ======================================================
};

