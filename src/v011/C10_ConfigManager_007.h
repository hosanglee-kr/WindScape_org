#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : C10_ConfigManager_007.h
 * 모듈명 : WindScape Configuration Manager
 * ------------------------------------------------------
 * 기능 요약:
 *  - JSON 설정 로드/저장/백업/복구/기본생성(loadOrCreate)
 *  - 구조체 ↔ JSON 직렬화(toJson/parseJson)
 *  - /api/config 패치(부분 갱신) 반영(patchFromJson)
 *  - Wi-Fi 모드(enum)·WEB 파일 3종 반영
 * ------------------------------------------------------
 * - 코드 네이밍 규칙:
 *    - 모듈약어 : C10
 *    - 전역 상수/매크로: G_모듈약어_ 접두사
 *    - 전역 변수: g_모듈약어_ 접두사
 *    - 로컬 변수 : v_ 접두사
 *    - 함수 인자 : p_ 접두사
 *    - type은 T_모듈약어_ 접두사
 *    - enum 상수 : EN_모듈약어_ 접두사
 *    - 구조체 : ST_모듈약어_ 접두사
 *    - 클래스 : CL_모듈약어_ 접두사
 *    - 클래스 private 멤버: _ 접두사, 정적 멤버: s_
 *    - 전역함수 : 모듈약어_ 접두사
 */

#include <ArduinoJson.h>
#include <LittleFS.h>

#include "A10_Const_007.h"
#include "D10_Logger_004.h"

class CL_C10_ConfigManager {
public:
    // 로드 (없으면 false)
    static bool load(ST_A10_WindConfig &p_config) {
        if (!LittleFS.exists(A10_Const::CONFIG_JSON_FILE)) {
            CL_D10_Logger::log(EN_L10_LOG_WARN, "Config not found: %s", A10_Const::CONFIG_JSON_FILE);
            return false;
        }
        File v_file = LittleFS.open(A10_Const::CONFIG_JSON_FILE, "r");
        if (!v_file) {
            CL_D10_Logger::log(EN_L10_LOG_ERROR, "Config open failed");
            return false;
        }
        JsonDocument v_doc;
        DeserializationError v_err = deserializeJson(v_doc, v_file);
        v_file.close();
        if (v_err) {
            CL_D10_Logger::log(EN_L10_LOG_ERROR, "Config parse failed: %s", v_err.c_str());
            return restoreBackup(p_config);
        }
        return parseJson(p_config, v_doc);
    }

    // 로드 or 기본 생성
    static bool loadOrCreate(ST_A10_WindConfig &p_config) {
        if (load(p_config)) return true;
        initDefaultConfig(p_config);
        return save(p_config);
    }

    // 저장 (백업 포함)
    static bool save(const ST_A10_WindConfig &p_config) {
        if (LittleFS.exists(A10_Const::CONFIG_JSON_FILE)) {
            LittleFS.remove(A10_Const::CONFIG_JSON_FILE_BACKUP);
            LittleFS.rename(A10_Const::CONFIG_JSON_FILE, A10_Const::CONFIG_JSON_FILE_BACKUP);
        }
        JsonDocument v_doc;
        toJson(p_config, v_doc);
        File v_file = LittleFS.open(A10_Const::CONFIG_JSON_FILE, "w");
        if (!v_file) {
            CL_D10_Logger::log(EN_L10_LOG_ERROR, "Write open failed");
            return false;
        }
        if (serializeJson(v_doc, v_file) == 0) {
            CL_D10_Logger::log(EN_L10_LOG_ERROR, "Write failed");
            v_file.close();
            return false;
        }
        v_file.close();
        CL_D10_Logger::log(EN_L10_LOG_INFO, "Config saved");
        return true;
    }

    // 공장 초기화
    static bool reset() {
        LittleFS.remove(A10_Const::CONFIG_JSON_FILE);
        LittleFS.remove(A10_Const::CONFIG_JSON_FILE_BACKUP);
        initDefaultConfig(g_A10_config);
        return save(g_A10_config);
    }

    // 기본 생성 (없을 때만)
    static bool saveDefaultConfig() {
        if (LittleFS.exists(A10_Const::CONFIG_JSON_FILE)) return false;
        initDefaultConfig(g_A10_config);
        return save(g_A10_config);
    }

    // 백업 복구
    static bool restoreBackup(ST_A10_WindConfig &p_config) {
        if (!LittleFS.exists(A10_Const::CONFIG_JSON_FILE_BACKUP)) return false;
        File v_file = LittleFS.open(A10_Const::CONFIG_JSON_FILE_BACKUP, "r");
        if (!v_file) return false;
        JsonDocument v_doc;
        DeserializationError v_err = deserializeJson(v_doc, v_file);
        v_file.close();
        if (v_err) {
            CL_D10_Logger::log(EN_L10_LOG_ERROR, "Backup parse failed");
            return false;
        }
        return parseJson(p_config, v_doc);
    }

    // JSON -> 구조체
    static bool parseJson(ST_A10_WindConfig &p_cfg, JsonDocument &p_doc) {
        JsonObjectConst v_root = p_doc.as<JsonObjectConst>();

        // security
        strlcpy(p_cfg.api_key, v_root["security"]["api_key"] | "", sizeof(p_cfg.api_key));

        // web
        #ifdef G_A10_DYNIM_WEB_STATIC_FILE_USE
            _copyWebFile(p_cfg.web.html_file, v_root["web"]["html_file"]);
            _copyWebFile(p_cfg.web.js_file,   v_root["web"]["js_file"]);
            _copyWebFile(p_cfg.web.css_file,  v_root["web"]["css_file"]);
        #endif

        // wifi
        p_cfg.wifi_mode = v_root["wifi"]["wifi_mode"] | p_cfg.wifi_mode;

        strlcpy(p_cfg.ap_ssid,
                v_root["wifi"]["ap_network"]["ap_ssid"] | "",
                sizeof(p_cfg.ap_ssid));
        strlcpy(p_cfg.ap_password,
                v_root["wifi"]["ap_network"]["ap_password"] | "",
                sizeof(p_cfg.ap_password));

        p_cfg.sta_network_count = 0;
        JsonArrayConst v_sta = v_root["wifi"]["sta_networks"].as<JsonArrayConst>();
        for (JsonObjectConst v_net : v_sta) {
            if (p_cfg.sta_network_count >= A10_Const::MAX_STA_NETWORKS) break;
            strlcpy(p_cfg.sta_networks[p_cfg.sta_network_count].ssid,
                    v_net["ssid"] | "", sizeof(p_cfg.sta_networks[0].ssid));
            strlcpy(p_cfg.sta_networks[p_cfg.sta_network_count].password,
                    v_net["pass"] | "", sizeof(p_cfg.sta_networks[0].password));
            p_cfg.sta_network_count++;
        }

        // hw
        p_cfg.fan_pwm_pin   = v_root["hw"]["pwm_pin"]   | p_cfg.fan_pwm_pin;
        p_cfg.pwm_channel   = v_root["hw"]["pwm_channel"] | p_cfg.pwm_channel;
        p_cfg.pwm_frequency = v_root["hw"]["pwm_freq"]  | p_cfg.pwm_frequency;
        p_cfg.pwm_resolution= v_root["hw"]["pwm_res"]   | p_cfg.pwm_resolution;

        // timing
        p_cfg.wind_sim_interval_ms   = v_root["timing"]["sim_int"]    | p_cfg.wind_sim_interval_ms;
        p_cfg.gust_check_interval_ms = v_root["timing"]["gust_int"]   | p_cfg.gust_check_interval_ms;
        p_cfg.thermal_check_interval_ms = v_root["timing"]["thermal_int"] | p_cfg.thermal_check_interval_ms;

        // sim (단위 일관성: 퍼센트 기반 값도 float 0.0~? 로 그대로 저장)
        p_cfg.wind_intensity             = v_root["sim"]["intensity"] | p_cfg.wind_intensity;
        p_cfg.gust_frequency             = v_root["sim"]["gust_freq"] | p_cfg.gust_frequency;
        p_cfg.wind_variability           = v_root["sim"]["variability"] | p_cfg.wind_variability;
        p_cfg.fan_speed_limit            = v_root["sim"]["fan_limit"] | p_cfg.fan_speed_limit;
        p_cfg.minimum_fan_speed          = v_root["sim"]["min_fan"]   | p_cfg.minimum_fan_speed;
        p_cfg.turbulence_length_scale    = v_root["sim"]["turb_len"]  | p_cfg.turbulence_length_scale;
        p_cfg.turbulence_intensity_sigma = v_root["sim"]["turb_sig"]  | p_cfg.turbulence_intensity_sigma;
        p_cfg.thermal_bubble_strength    = v_root["sim"]["therm_str"] | p_cfg.thermal_bubble_strength;
        p_cfg.thermal_bubble_radius      = v_root["sim"]["therm_rad"] | p_cfg.thermal_bubble_radius;

        if (!v_root["sim"]["preset"].isNull()) {
            const char* v_p = v_root["sim"]["preset"];
            for (int v_i=0; v_i<EN_A10_PRESET_COUNT; ++v_i) {
                if (strcmp(v_p, g_A10_PRESET_MODE_NAMES_Arr[v_i])==0) {
                    p_cfg.preset_mode_index = v_i;
                    break;
                }
            }
        }
        return true;
    }

    // 구조체 -> JSON
    static void toJson(const ST_A10_WindConfig &p_cfg, JsonDocument &p_doc) {
        JsonObject v_root = p_doc.to<JsonObject>();
        _toJson_Common(p_cfg, v_root);
    }
    static void toJson(const ST_A10_WindConfig &p_cfg, JsonObject p_obj) {
        _toJson_Common(p_cfg, p_obj);
    }

    // /api/config PATCH
    static bool patchFromJson(ST_A10_WindConfig &p_cfg, const JsonDocument &p_doc, bool &p_wifiChanged) {
        JsonObjectConst v_root = p_doc.as<JsonObjectConst>();
        p_wifiChanged = false;

        // security
        if (!v_root["security"]["api_key"].isNull()) {
            strlcpy(p_cfg.api_key, v_root["security"]["api_key"], sizeof(p_cfg.api_key));
        }

        // web
        #ifdef G_A10_DYNIM_WEB_STATIC_FILE_USE
            if (!v_root["web"].isNull()) {
                _maybeCopyWebFile(p_cfg.web.html_file, v_root["web"]["html_file"]);
                _maybeCopyWebFile(p_cfg.web.js_file,   v_root["web"]["js_file"]);
                _maybeCopyWebFile(p_cfg.web.css_file,  v_root["web"]["css_file"]);
            }
        #endif

        // hw
        if (!v_root["hw"].isNull()) {
            if (!v_root["hw"]["pwm_pin"].isNull())     p_cfg.fan_pwm_pin    = v_root["hw"]["pwm_pin"].as<int>();
            if (!v_root["hw"]["pwm_channel"].isNull()) p_cfg.pwm_channel    = v_root["hw"]["pwm_channel"].as<int>();
            if (!v_root["hw"]["pwm_freq"].isNull())    p_cfg.pwm_frequency  = v_root["hw"]["pwm_freq"].as<int>();
            if (!v_root["hw"]["pwm_res"].isNull())     p_cfg.pwm_resolution = v_root["hw"]["pwm_res"].as<int>();
        }

        // sim
        if (!v_root["sim"].isNull()) {
            if (!v_root["sim"]["preset"].isNull()) {
                const char* v_p = v_root["sim"]["preset"];
                for (int v_i=0; v_i<EN_A10_PRESET_COUNT; ++v_i) {
                    if (strcmp(v_p, g_A10_PRESET_MODE_NAMES_Arr[v_i])==0) p_cfg.preset_mode_index = v_i;
                }
            }
            if (!v_root["sim"]["intensity"].isNull()) p_cfg.wind_intensity = v_root["sim"]["intensity"].as<float>();
            if (!v_root["sim"]["gust_freq"].isNull()) p_cfg.gust_frequency = v_root["sim"]["gust_freq"].as<float>();
            if (!v_root["sim"]["variability"].isNull()) p_cfg.wind_variability = v_root["sim"]["variability"].as<float>();
            if (!v_root["sim"]["fan_limit"].isNull()) p_cfg.fan_speed_limit = v_root["sim"]["fan_limit"].as<float>();
            if (!v_root["sim"]["min_fan"].isNull())   p_cfg.minimum_fan_speed = v_root["sim"]["min_fan"].as<float>();
            if (!v_root["sim"]["turb_len"].isNull())  p_cfg.turbulence_length_scale = v_root["sim"]["turb_len"].as<float>();
            if (!v_root["sim"]["turb_sig"].isNull())  p_cfg.turbulence_intensity_sigma = v_root["sim"]["turb_sig"].as<float>();
            if (!v_root["sim"]["therm_str"].isNull()) p_cfg.thermal_bubble_strength = v_root["sim"]["therm_str"].as<float>();
            if (!v_root["sim"]["therm_rad"].isNull()) p_cfg.thermal_bubble_radius = v_root["sim"]["therm_rad"].as<float>();
        }

        // timing
        if (!v_root["timing"].isNull()) {
            if (!v_root["timing"]["sim_int"].isNull())     p_cfg.wind_sim_interval_ms = v_root["timing"]["sim_int"].as<int>();
            if (!v_root["timing"]["gust_int"].isNull())    p_cfg.gust_check_interval_ms = v_root["timing"]["gust_int"].as<int>();
            if (!v_root["timing"]["thermal_int"].isNull()) p_cfg.thermal_check_interval_ms = v_root["timing"]["thermal_int"].as<int>();
        }

        // wifi
        if (!v_root["wifi"].isNull()) {
            if (!v_root["wifi"]["wifi_mode"].isNull()) {
                p_cfg.wifi_mode = v_root["wifi"]["wifi_mode"].as<int>();
                p_wifiChanged = true;
            }
            if (!v_root["wifi"]["ap_network"].isNull()) {
                if (!v_root["wifi"]["ap_network"]["ap_ssid"].isNull()) {
                    strlcpy(p_cfg.ap_ssid, v_root["wifi"]["ap_network"]["ap_ssid"], sizeof(p_cfg.ap_ssid));
                    p_wifiChanged = true;
                }
                if (!v_root["wifi"]["ap_network"]["ap_password"].isNull()) {
                    strlcpy(p_cfg.ap_password, v_root["wifi"]["ap_network"]["ap_password"], sizeof(p_cfg.ap_password));
                    p_wifiChanged = true;
                }
            }
            if (v_root["wifi"]["sta_networks"].is<JsonArrayConst>()) {
                p_cfg.sta_network_count = 0;
                for (JsonObjectConst v_net : v_root["wifi"]["sta_networks"].as<JsonArrayConst>()) {
                    if (p_cfg.sta_network_count >= A10_Const::MAX_STA_NETWORKS) break;
                    strlcpy(p_cfg.sta_networks[p_cfg.sta_network_count].ssid,
                            v_net["ssid"] | "", sizeof(p_cfg.sta_networks[0].ssid));
                    strlcpy(p_cfg.sta_networks[p_cfg.sta_network_count].password,
                            v_net["pass"] | "", sizeof(p_cfg.sta_networks[0].password));
                    p_cfg.sta_network_count++;
                }
                p_wifiChanged = true;
            }
        }
        return true;
    }

    // 기본값
    static void initDefaultConfig(ST_A10_WindConfig &p_cfg) {
        memset(&p_cfg, 0, sizeof(p_cfg));

        // security
        p_cfg.api_key[0] = '\0';

        // web (fallback 기본 경로)
        #ifdef G_A10_DYNIM_WEB_STATIC_FILE_USE
            strlcpy(p_cfg.web.html_file.file, A10_Const::DEF_HTML_FILE, sizeof(p_cfg.web.html_file.file));
            strlcpy(p_cfg.web.html_file.uri , A10_Const::DEF_HTML_URI , sizeof(p_cfg.web.html_file.uri ));
            strlcpy(p_cfg.web.html_file.mime, A10_Const::DEF_HTML_MIME, sizeof(p_cfg.web.html_file.mime));

            strlcpy(p_cfg.web.js_file.file, A10_Const::DEF_JS_FILE, sizeof(p_cfg.web.js_file.file));
            strlcpy(p_cfg.web.js_file.uri , A10_Const::DEF_JS_URI , sizeof(p_cfg.web.js_file.uri ));
            strlcpy(p_cfg.web.js_file.mime, A10_Const::DEF_JS_MIME, sizeof(p_cfg.web.js_file.mime));

            strlcpy(p_cfg.web.css_file.file, A10_Const::DEF_CSS_FILE, sizeof(p_cfg.web.css_file.file));
            strlcpy(p_cfg.web.css_file.uri , A10_Const::DEF_CSS_URI , sizeof(p_cfg.web.css_file.uri ));
            strlcpy(p_cfg.web.css_file.mime, A10_Const::DEF_CSS_MIME, sizeof(p_cfg.web.css_file.mime));
        #endif

        // wifi
        p_cfg.wifi_mode = EN_A10_WIFI_MODE_AP_STA;
        strlcpy(p_cfg.ap_ssid, "NatureWind", sizeof(p_cfg.ap_ssid));
        strlcpy(p_cfg.ap_password, "2540", sizeof(p_cfg.ap_password));
        p_cfg.sta_network_count = 0;

        // hw
        p_cfg.fan_pwm_pin   = 6;
        p_cfg.pwm_channel   = 0;
        p_cfg.pwm_frequency = 25000;
        p_cfg.pwm_resolution= 10;

        // timing
        p_cfg.wind_sim_interval_ms   = 250;
        p_cfg.gust_check_interval_ms = 500;
        p_cfg.thermal_check_interval_ms = 2000;

        // sim (백분율/스칼라 그대로 저장)
        p_cfg.wind_intensity             = 75.0f;
        p_cfg.gust_frequency             = 50.0f;
        p_cfg.wind_variability           = 60.0f;
        p_cfg.fan_speed_limit            = 90.0f;
        p_cfg.minimum_fan_speed          = 10.0f;
        p_cfg.turbulence_length_scale    = 45.0f;
        p_cfg.turbulence_intensity_sigma = 0.45f;
        p_cfg.thermal_bubble_strength    = 2.2f;
        p_cfg.thermal_bubble_radius      = 20.0f;
        p_cfg.preset_mode_index          = EN_A10_PRESET_COUNTRY;
    }

private:
 #ifdef G_A10_DYNIM_WEB_STATIC_FILE_USE
    static void _copyWebFile(ST_A10_WebFile &p_dst, JsonObjectConst p_src) {
        if (p_src.isNull()) return;
        strlcpy(p_dst.file, p_src["file"] | "", sizeof(p_dst.file));
        strlcpy(p_dst.uri , p_src["uri"]  | "", sizeof(p_dst.uri));
        strlcpy(p_dst.mime, p_src["mime"] | "", sizeof(p_dst.mime));
    }
    static void _maybeCopyWebFile(ST_A10_WebFile &p_dst, JsonObjectConst p_src) {
        if (p_src.isNull()) return;
        if (!p_src["file"].isNull()) strlcpy(p_dst.file, p_src["file"], sizeof(p_dst.file));
        if (!p_src["uri"].isNull())  strlcpy(p_dst.uri , p_src["uri"] , sizeof(p_dst.uri));
        if (!p_src["mime"].isNull()) strlcpy(p_dst.mime, p_src["mime"], sizeof(p_dst.mime));
    }
 #endif
    static void _toJson_Common(const ST_A10_WindConfig &p_cfg, JsonObject p_root) {
        // security
        p_root["security"]["api_key"] = p_cfg.api_key;

        // web
        #ifdef G_A10_DYNIM_WEB_STATIC_FILE_USE
            p_root["web"]["html_file"]["file"] = p_cfg.web.html_file.file;
            p_root["web"]["html_file"]["uri"]  = p_cfg.web.html_file.uri;
            p_root["web"]["html_file"]["mime"] = p_cfg.web.html_file.mime;

            p_root["web"]["js_file"]["file"] = p_cfg.web.js_file.file;
            p_root["web"]["js_file"]["uri"]  = p_cfg.web.js_file.uri;
            p_root["web"]["js_file"]["mime"] = p_cfg.web.js_file.mime;

            p_root["web"]["css_file"]["file"] = p_cfg.web.css_file.file;
            p_root["web"]["css_file"]["uri"]  = p_cfg.web.css_file.uri;
            p_root["web"]["css_file"]["mime"] = p_cfg.web.css_file.mime;
        #endif

        // wifi
        p_root["wifi"]["wifi_mode"] = p_cfg.wifi_mode;
        p_root["wifi"]["ap_network"]["ap_ssid"]     = p_cfg.ap_ssid;
        p_root["wifi"]["ap_network"]["ap_password"] = p_cfg.ap_password;

        JsonArray v_sta = p_root["wifi"]["sta_networks"].to<JsonArray>();
        for (int v_i=0; v_i<p_cfg.sta_network_count; ++v_i) {
            JsonObject v_net = v_sta.add<JsonObject>();
            v_net["ssid"] = p_cfg.sta_networks[v_i].ssid;
            v_net["pass"] = p_cfg.sta_networks[v_i].password;
        }

        // hw
        p_root["hw"]["pwm_pin"]     = p_cfg.fan_pwm_pin;
        p_root["hw"]["pwm_channel"] = p_cfg.pwm_channel;
        p_root["hw"]["pwm_freq"]    = p_cfg.pwm_frequency;
        p_root["hw"]["pwm_res"]     = p_cfg.pwm_resolution;

        // timing
        p_root["timing"]["sim_int"]     = p_cfg.wind_sim_interval_ms;
        p_root["timing"]["gust_int"]    = p_cfg.gust_check_interval_ms;
        p_root["timing"]["thermal_int"] = p_cfg.thermal_check_interval_ms;

        // sim
        JsonObject v_sim = p_root["sim"].to<JsonObject>();
        v_sim["intensity"] = p_cfg.wind_intensity;
        v_sim["gust_freq"] = p_cfg.gust_frequency;
        v_sim["variability"] = p_cfg.wind_variability;
        v_sim["fan_limit"] = p_cfg.fan_speed_limit;
        v_sim["min_fan"]   = p_cfg.minimum_fan_speed;
        v_sim["turb_len"]  = p_cfg.turbulence_length_scale;
        v_sim["turb_sig"]  = p_cfg.turbulence_intensity_sigma;
        v_sim["therm_str"] = p_cfg.thermal_bubble_strength;
        v_sim["therm_rad"] = p_cfg.thermal_bubble_radius;
        v_sim["preset"]    = g_A10_PRESET_MODE_NAMES_Arr[p_cfg.preset_mode_index];
    }
};
