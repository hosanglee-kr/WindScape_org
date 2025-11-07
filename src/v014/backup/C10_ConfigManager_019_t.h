#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : C10_ConfigManager_019.h
 * 모듈 약어 : C10
 * 모듈명 : Smart Nature Wind Configuration Manager (v019)
 * ------------------------------------------------------
 * 기능 요약:
 *  - 모든 Config(System/WiFi/Motion/WindProfile/Schedule/UserProfile) 통합 관리
 *  - JSON 기반 구조체 ↔ 파일 입출력 (ArduinoJson v7.x.x)
 *  - WindPreset / Style Dict 기반 바람 파라미터 해석 유틸
 *  - WebAPI / CT10 / N10 연동용 직렬화 지원 (toJson)
 *  - Lazy Load / Safe Backup / Factory Reset / Patch Update 지원
 * ------------------------------------------------------
 * 구현 규칙:
 *  - JSON 처리: ArduinoJson v7 사용, createNestedArray/Object/containsKey 금지
 *  - 문자열 처리: memset + strlcpy
 *  - 전역/클래스/멤버 네이밍 규칙 준수
 *  - 모든 멤버 함수 static, 단일 헤더(h) 구조
 * ------------------------------------------------------
 * 네이밍 규칙:
 *  - 전역상수/매크로 : G_모듈약어_
 *  - 전역변수 : g_모듈약어_
 *  - 클래스명 : CL_모듈약어_
 *  - 클래스 멤버 : 접두사 없음
 *  - 인자 : p_, 로컬 : v_
 *  - 타입 : ST_/EN_/T_ 접두사
 * ------------------------------------------------------
 */

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "A10_Const_014.h"
#include "D10_Logger_011.h"

class CL_C10_ConfigManager {
public:
    // ---------------------------------------------------
    // ⚙️ 공통 I/O 헬퍼
    // ---------------------------------------------------
    static bool C10_ioLoadJson(const char* p_path, JsonDocument& p_doc) {
        if (!LittleFS.exists(p_path)) {
            CL_D10_Logger::log(EN_L10_LOG_WARN, "[C10] File not found: %s", p_path);
            return false;
        }
        File v_f = LittleFS.open(p_path, "r");
        if (!v_f) return false;
        auto v_e = deserializeJson(p_doc, v_f);
        v_f.close();
        if (v_e) {
            CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] JSON parse error: %s", v_e.c_str());
            return false;
        }
        return true;
    }

    static bool C10_ioSaveJson(const char* p_path, const char* p_bak, const JsonDocument& p_doc) {
        if (LittleFS.exists(p_path)) {
            if (LittleFS.exists(p_bak)) LittleFS.remove(p_bak);
            LittleFS.rename(p_path, p_bak);
        }
        File v_f = LittleFS.open(p_path, "w");
        if (!v_f) return false;
        bool v_ok = serializeJsonPretty(p_doc, v_f) > 0;
        v_f.close();
        return v_ok;
    }

    // ---------------------------------------------------
    // 🧭 System Config
    // ---------------------------------------------------
    static bool C10_loadSystemConfig(ST_A10_SystemConfig& p_cfg) {
        JsonDocument v;
        if (!C10_ioLoadJson(A10_Const::CFG_SYSTEM_FILE, v)) {
            A10_resetSystemDefault(p_cfg);
            return false;
        }
        JsonObjectConst j = v["system"];
        strlcpy(p_cfg.meta.version, v["meta"]["version"] | A10_Const::FW_VERSION, sizeof(p_cfg.meta.version));
        strlcpy(p_cfg.system.web.html, j["web"]["html"] | "/html/main.html", sizeof(p_cfg.system.web.html));
        p_cfg.hw.fan_pwm.pin = j["hw"]["fan_pwm"]["pin"] | 6;
        return true;
    }

    static bool C10_saveSystemConfig(const ST_A10_SystemConfig& p_cfg) {
        JsonDocument d;
        d["meta"]["version"] = p_cfg.meta.version;
        d["system"]["web"]["html"] = p_cfg.system.web.html;
        d["hw"]["fan_pwm"]["pin"] = p_cfg.hw.fan_pwm.pin;
        return C10_ioSaveJson(A10_Const::CFG_SYSTEM_FILE, A10_Const::CFG_SYSTEM_FILE_BAK, d);
    }

    static void C10_toJson_System(const ST_A10_SystemConfig& p, JsonDocument& d) {
        d["meta"]["version"] = p.meta.version;
        d["system"]["web"]["html"] = p.system.web.html;
        d["hw"]["fan_pwm"]["pin"] = p.hw.fan_pwm.pin;
    }

    // ---------------------------------------------------
    // 🌐 WiFi Config
    // ---------------------------------------------------
    static bool C10_loadWifiConfig(ST_A10_WifiConfig& p) {
        JsonDocument v;
        if (!C10_ioLoadJson(A10_Const::CFG_WIFI_FILE, v)) {
            A10_resetWifiDefault(p);
            return false;
        }
        JsonObjectConst j = v["wifi"];
        p.wifiMode = (EN_A10_WIFI_MODE_t)(j["wifiMode"] | EN_A10_WIFI_MODE_AP_STA);
        strlcpy(p.ap.ssid, j["ap"]["ssid"] | "NatureWind", sizeof(p.ap.ssid));
        strlcpy(p.ap.pass, j["ap"]["pass"] | "12345678", sizeof(p.ap.pass));
        return true;
    }

    static bool C10_saveWifiConfig(const ST_A10_WifiConfig& p) {
        JsonDocument d;
        d["wifi"]["wifiMode"] = p.wifiMode;
        d["wifi"]["ap"]["ssid"] = p.ap.ssid;
        d["wifi"]["ap"]["pass"] = p.ap.pass;
        return C10_ioSaveJson(A10_Const::CFG_WIFI_FILE, A10_Const::CFG_WIFI_FILE_BAK, d);
    }

    static void C10_toJson_Wifi(const ST_A10_WifiConfig& p, JsonDocument& d) {
        d["wifi"]["wifiMode"] = p.wifiMode;
        d["wifi"]["ap"]["ssid"] = p.ap.ssid;
        d["wifi"]["ap"]["pass"] = p.ap.pass;
    }

    // ---------------------------------------------------
    // 🌀 Motion Config
    // ---------------------------------------------------
    static bool C10_loadMotionConfig(ST_A10_MotionConfig& p) {
        JsonDocument d;
        if (!C10_ioLoadJson(A10_Const::CFG_MOTION_FILE, d)) {
            A10_resetMotionDefault(p);
            return false;
        }
        JsonObjectConst j = d["motion"];
        p.enabled = j["enabled"] | true;
        p.pir.enabled = j["pir"]["enabled"] | true;
        p.ble.enabled = j["ble"]["enabled"] | true;
        p.pir.hold_sec = j["pir"]["hold_sec"] | 120;
        p.ble.rssi_threshold = j["ble"]["rssi_threshold"] | -70;
        p.ble.hold_sec = j["ble"]["hold_sec"] | 120;
        return true;
    }

    static void C10_toJson_Motion(const ST_A10_MotionConfig& p, JsonDocument& d) {
        d["motion"]["enabled"] = p.enabled;
        d["motion"]["pir"]["enabled"] = p.pir.enabled;
        d["motion"]["pir"]["hold_sec"] = p.pir.hold_sec;
        d["motion"]["ble"]["enabled"] = p.ble.enabled;
        d["motion"]["ble"]["rssi_threshold"] = p.ble.rssi_threshold;
        d["motion"]["ble"]["hold_sec"] = p.ble.hold_sec;
    }

    // ---------------------------------------------------
    // 🌬 WindProfileDict + 해석 유틸
    // ---------------------------------------------------
    static bool C10_resolveWindParams(
        const ST_A10_WindProfileDict_t& p_dict,
        const char* p_presetCode,
        const char* p_styleCode,
        const ST_A10_AdjustDelta_t* p_adj,
        ST_A10_ResolvedWind_t& p_out
    ) {
        int16_t v_pi = -1;
        for (uint8_t v=0; v<p_dict.presetCount; v++) {
            if (!strcasecmp(p_dict.presets[v].code, p_presetCode)) { v_pi=v; break; }
        }
        if (v_pi<0) return false;

        int16_t v_si = -1;
        for (uint8_t s=0; s<p_dict.styleCount; s++) {
            if (!strcasecmp(p_dict.styles[s].code, p_styleCode)) { v_si=s; break; }
        }
        if (v_si<0) v_si = 0;

        const ST_A10_WindPresetDef_t& v_p = p_dict.presets[v_pi];
        const ST_A10_WindStyleDef_t& v_s = p_dict.styles[v_si];

        p_out.wind_intensity = constrain(
            v_p.base.wind_intensity * v_s.factors.intensity_factor + (p_adj ? p_adj->wind_intensity : 0),
            0, 100
        );
        p_out.gust_frequency = v_p.base.gust_frequency * v_s.factors.gust_factor;
        p_out.wind_variability = v_p.base.wind_variability * v_s.factors.variability_factor;
        p_out.fan_limit = v_p.base.fan_limit;
        p_out.min_fan = v_p.base.min_fan;
        p_out.thermal_bubble_strength = v_p.base.thermal_bubble_strength * v_s.factors.thermal_factor;
        p_out.thermal_bubble_radius = v_p.base.thermal_bubble_radius;
        return true;
    }

    // ---------------------------------------------------
    // 🕒 Schedules / UserProfiles 요약 직렬화
    // ---------------------------------------------------
    static void C10_toJson_Schedules(const ST_A10_ScheduleConfig& p, JsonDocument& d) {
        for (uint8_t i=0;i<p.count;i++) {
            d["schedules"][i]["name"] = p.items[i].name;
            d["schedules"][i]["enabled"] = p.items[i].enabled;
            d["schedules"][i]["period"]["enabled"] = p.items[i].period.enabled;
        }
    }

    static void C10_toJson_UserProfiles(const ST_A10_UserProfileConfig_t& p, JsonDocument& d) {
        for (uint8_t i=0;i<p.count;i++) {
            d["userProfiles"]["profiles"][i]["name"] = p.items[i].name;
            d["userProfiles"]["profiles"][i]["enabled"] = p.items[i].enabled;
        }
    }

    // ---------------------------------------------------
    // 🧩 전체 Config 통합 로드 / 저장 / 직렬화
    // ---------------------------------------------------
    static bool C10_loadAll(ST_A10_ConfigRoot& p_root) {
        bool v_ok = true;
        if (!C10_loadSystemConfig(p_root.system)) v_ok=false;
        if (!C10_loadWifiConfig(*p_root.wifi)) v_ok=false;
        if (!C10_loadMotionConfig(*p_root.motion)) v_ok=false;
        return v_ok;
    }

    static void C10_toJson_All(const ST_A10_ConfigRoot& p, JsonDocument& d) {
        C10_toJson_System(p.system, d);
        if (p.wifi) C10_toJson_Wifi(*p.wifi, d);
        if (p.motion) C10_toJson_Motion(*p.motion, d);
        if (p.schedules) C10_toJson_Schedules(*p.schedules, d);
        if (p.userProfiles) C10_toJson_UserProfiles(*p.userProfiles, d);
    }
};

// 전역 인스턴스
inline ST_A10_ConfigRoot g_A10_config_root;
