

#pragma once
/*
 * C10_ConfigManager_006.h
 * ------------------------------------------------------
 * WindScape Configuration Manager
 * ------------------------------------------------------
 * 기능 요약:
 *  - JSON 설정 파일 로드/저장/백업/복구/기본생성
 *  - 구조체 ↔ JSON 직렬화
 *  - /api/config 패치 반영
 *  - 공장 초기화 및 Default 생성 지원
 * ------------------------------------------------------
 */

#include <ArduinoJson.h>
#include <LittleFS.h>

#include "A10_Const_006.h"
#include "D10_Logger_004.h"

class CL_C10_ConfigManager {
   public:
    // ======================================================
    // 1️⃣ 설정 파일 로드 (config_012.json)
    // ======================================================
    static bool load(ST_A10_WindConfig &p_config) {
        if (!LittleFS.exists(A10_Const::CONFIG_JSON_FILE)) {
            CL_D10_Logger::log(EN_L10_LOG_WARN, "Config file not found: %s", A10_Const::CONFIG_JSON_FILE);
            return false;
        }

        File v_file = LittleFS.open(A10_Const::CONFIG_JSON_FILE, "r");
        if (!v_file) {
            CL_D10_Logger::log(EN_L10_LOG_ERROR, "Failed to open config file");
            return false;
        }

        JsonDocument v_doc;
        DeserializationError v_err = deserializeJson(v_doc, v_file);

        Serial.println("load config.JSON:");
        serializeJsonPretty(v_doc, Serial);

        v_file.close();

        if (v_err) {
            CL_D10_Logger::log(EN_L10_LOG_ERROR, "Config parse failed: %s", v_err.c_str());
            return restoreBackup(p_config);
        }

        return parseJson(p_config, v_doc);
    }

    // ======================================================
    // 2️⃣ 로드 or 기본 생성
    // ======================================================
    static bool loadOrCreate(ST_A10_WindConfig &p_config) {
        if (load(p_config)) {
            return true;
        }

        initDefaultConfig(p_config);
        
        return save(p_config);
    }

    // ======================================================
    // 3️⃣ 설정 저장 (백업 포함)
    // ======================================================
    static bool save(const ST_A10_WindConfig &p_config) {
        // 기존 파일 백업
        if (LittleFS.exists(A10_Const::CONFIG_JSON_FILE)) {
            LittleFS.remove(A10_Const::CONFIG_JSON_FILE_BACKUP);
            LittleFS.rename(A10_Const::CONFIG_JSON_FILE, A10_Const::CONFIG_JSON_FILE_BACKUP);
        }

        JsonDocument v_doc;
        toJson(p_config, v_doc);

        File v_file = LittleFS.open(A10_Const::CONFIG_JSON_FILE, "w");
        if (!v_file) {
            CL_D10_Logger::log(EN_L10_LOG_ERROR, "Config open for write failed");
            return false;
        }

        if (serializeJson(v_doc, v_file) == 0) {
            CL_D10_Logger::log(EN_L10_LOG_ERROR, "Config write failed");
            v_file.close();
            return false;
        }

        v_file.close();
        CL_D10_Logger::log(EN_L10_LOG_INFO, "Config saved successfully");
        return true;
    }

    // ======================================================
    // 4️⃣ 공장 초기화
    // ======================================================
    static bool reset() {
        LittleFS.remove(A10_Const::CONFIG_JSON_FILE);
        LittleFS.remove(A10_Const::CONFIG_JSON_FILE_BACKUP);
        initDefaultConfig(g_A10_config);
        return save(g_A10_config);
    }

    // ======================================================
    // 5️⃣ 기본 설정 생성 (파일 없을 때만)
    // ======================================================
    static bool saveDefaultConfig() {
        if (LittleFS.exists(A10_Const::CONFIG_JSON_FILE)) {
            CL_D10_Logger::log(EN_L10_LOG_WARN, "Config already exists. Skip default creation.");
            return false;
        }
        initDefaultConfig(g_A10_config);
        return save(g_A10_config);
    }

    // ======================================================
    // 6️⃣ 백업 복구
    // ======================================================
    static bool restoreBackup(ST_A10_WindConfig &p_config) {
        if (!LittleFS.exists(A10_Const::CONFIG_JSON_FILE_BACKUP)) {
            CL_D10_Logger::log(EN_L10_LOG_WARN, "No backup file found");
            return false;
        }

        File v_file = LittleFS.open(A10_Const::CONFIG_JSON_FILE_BACKUP, "r");
        if (!v_file) return false;

        JsonDocument v_doc;
        DeserializationError v_err = deserializeJson(v_doc, v_file);
        v_file.close();

        if (v_err) {
            CL_D10_Logger::log(EN_L10_LOG_ERROR, "Backup parse failed: %s", v_err.c_str());
            return false;
        }

        CL_D10_Logger::log(EN_L10_LOG_INFO, "Config restored from backup");
        return parseJson(p_config, v_doc);
    }

    // ======================================================
    // 7️⃣ JSON → 구조체 파싱
    // ======================================================
    static bool parseJson(ST_A10_WindConfig &p_config, JsonDocument &p_doc) {
        JsonObjectConst v_root = p_doc.as<JsonObjectConst>();

        // [security]
        strlcpy(p_config.api_key, v_root["security"]["api_key"] | "", sizeof(p_config.api_key));

        // [wifi]
        p_config.wifi_mode = v_root["wifi"]["wifi_mode"] | p_config.wifi_mode;
        strlcpy(p_config.ap_ssid, v_root["wifi"]["ap_ssid"] | "", sizeof(p_config.ap_ssid));
        strlcpy(p_config.ap_password, v_root["wifi"]["ap_password"] | "", sizeof(p_config.ap_password));

        p_config.sta_network_count = 0;
        for (JsonObjectConst v_net : v_root["wifi"]["sta_networks"].as<JsonArrayConst>()) {
            if (p_config.sta_network_count >= A10_Const::MAX_STA_NETWORKS) break;
            strlcpy(p_config.sta_networks[p_config.sta_network_count].ssid,
                    v_net["ssid"] | "", sizeof(p_config.sta_networks[0].ssid));
            strlcpy(p_config.sta_networks[p_config.sta_network_count].password,
                    v_net["pass"] | "", sizeof(p_config.sta_networks[0].password));
            p_config.sta_network_count++;
        }

        // [hw]
        p_config.fan_pwm_pin = v_root["hw"]["pwm_pin"] | p_config.fan_pwm_pin;
        p_config.pwm_channel = v_root["hw"]["pwm_channel"] | p_config.pwm_channel;
        p_config.pwm_frequency = v_root["hw"]["pwm_freq"] | p_config.pwm_frequency;
        p_config.pwm_resolution = v_root["hw"]["pwm_res"] | p_config.pwm_resolution;

        // [timing]
        p_config.wind_sim_interval_ms = v_root["timing"]["sim_int"] | p_config.wind_sim_interval_ms;
        p_config.gust_check_interval_ms = v_root["timing"]["gust_int"] | p_config.gust_check_interval_ms;
        p_config.thermal_check_interval_ms = v_root["timing"]["thermal_int"] | p_config.thermal_check_interval_ms;

        // [sim]
        p_config.wind_intensity = v_root["sim"]["intensity"] | p_config.wind_intensity;
        p_config.gust_frequency = v_root["sim"]["gust_freq"] | p_config.gust_frequency;
        p_config.wind_variability = v_root["sim"]["variability"] | p_config.wind_variability;
        p_config.fan_speed_limit = v_root["sim"]["fan_limit"] | p_config.fan_speed_limit;
        p_config.minimum_fan_speed = v_root["sim"]["min_fan"] | p_config.minimum_fan_speed;
        p_config.turbulence_length_scale = v_root["sim"]["turb_len"] | p_config.turbulence_length_scale;
        p_config.turbulence_intensity_sigma = v_root["sim"]["turb_sig"] | p_config.turbulence_intensity_sigma;
        p_config.thermal_bubble_strength = v_root["sim"]["therm_str"] | p_config.thermal_bubble_strength;
        p_config.thermal_bubble_radius = v_root["sim"]["therm_rad"] | p_config.thermal_bubble_radius;

        if (!v_root["sim"]["preset"].isNull()) {
            const char *v_presetName = v_root["sim"]["preset"];
            for (int i = 0; i < EN_A10_PRESET_COUNT; i++) {
                if (strcmp(v_presetName, g_A10_PRESET_MODE_NAMES_Arr[i]) == 0) {
                    p_config.preset_mode_index = i;
                    break;
                }
            }
        }

        return true;
    }

    	// ======================================================
	// 8️⃣ -1 구조체 → JSON 직렬화 (JsonDocument para 버전)
	// ======================================================
	static void toJson(const ST_A10_WindConfig &p_config, JsonDocument &p_doc) {
		JsonObject v_root = p_doc.to<JsonObject>();
		_toJson_Common(p_config, v_root);
	}
	 
	// ======================================================
	// 8️⃣-2 구조체 → JSON 직렬화 오버로드 (JsonObject para 버전)
	// ======================================================
	static void toJson(const ST_A10_WindConfig &p_config, JsonObject p_jsonObj_cfg) {
		_toJson_Common(p_config, p_jsonObj_cfg);
	}

    static void _toJson_Common(const ST_A10_WindConfig &p_config, JsonObject p_jsonObj_cfg) {
		// [security]
		p_jsonObj_cfg["security"]["api_key"] = p_config.api_key;
	 
		// [wifi]
		JsonObject v_wifi = p_jsonObj_cfg["wifi"].to<JsonObject>();
		v_wifi["wifi_mode"] = p_config.wifi_mode;
		v_wifi["ap_ssid"] = p_config.ap_ssid;
		v_wifi["ap_password"] = p_config.ap_password;
	 
		JsonArray v_staArr = v_wifi["sta_networks"].to<JsonArray>();
		for (int i = 0; i < p_config.sta_network_count; i++) {
			JsonObject v_net = v_staArr.add<JsonObject>();
			v_net["ssid"] = p_config.sta_networks[i].ssid;
			v_net["pass"] = p_config.sta_networks[i].password;
		}
	 
		// [hw]
		p_jsonObj_cfg["hw"]["pwm_pin"] = p_config.fan_pwm_pin;
		p_jsonObj_cfg["hw"]["pwm_channel"] = p_config.pwm_channel;
		p_jsonObj_cfg["hw"]["pwm_freq"] = p_config.pwm_frequency;
		p_jsonObj_cfg["hw"]["pwm_res"] = p_config.pwm_resolution;
	 
		// [timing]
		p_jsonObj_cfg["timing"]["sim_int"] = p_config.wind_sim_interval_ms;
		p_jsonObj_cfg["timing"]["gust_int"] = p_config.gust_check_interval_ms;
		p_jsonObj_cfg["timing"]["thermal_int"] = p_config.thermal_check_interval_ms;
	 
		// [sim]
		JsonObject v_sim = p_jsonObj_cfg["sim"].to<JsonObject>();
		v_sim["intensity"] = p_config.wind_intensity;
		v_sim["gust_freq"] = p_config.gust_frequency;
		v_sim["variability"] = p_config.wind_variability;
		v_sim["fan_limit"] = p_config.fan_speed_limit;
		v_sim["min_fan"] = p_config.minimum_fan_speed;
		v_sim["turb_len"] = p_config.turbulence_length_scale;
		v_sim["turb_sig"] = p_config.turbulence_intensity_sigma;
		v_sim["therm_str"] = p_config.thermal_bubble_strength;
		v_sim["therm_rad"] = p_config.thermal_bubble_radius;
		v_sim["preset"] = g_A10_PRESET_MODE_NAMES_Arr[p_config.preset_mode_index];
	}

    /*
    // ======================================================
    // 8️⃣ 구조체 → JSON 직렬화
    // ======================================================
    static void toJson_old(const ST_A10_WindConfig &p_config, JsonDocument &p_doc) {
        JsonObject v_root = p_doc.to<JsonObject>();

        // [security]
        v_root["security"]["api_key"] = p_config.api_key;

        // [wifi]
        JsonObject v_wifi = v_root["wifi"].to<JsonObject>();
        v_wifi["wifi_mode"] = p_config.wifi_mode;
        v_wifi["ap_ssid"] = p_config.ap_ssid;
        v_wifi["ap_password"] = p_config.ap_password;

        JsonArray v_staArr = v_wifi["sta_networks"].to<JsonArray>();
        for (int i = 0; i < p_config.sta_network_count; i++) {
            JsonObject v_net = v_staArr.add<JsonObject>();
            v_net["ssid"] = p_config.sta_networks[i].ssid;
            v_net["pass"] = p_config.sta_networks[i].password;
        }

        // [hw]
        v_root["hw"]["pwm_pin"] = p_config.fan_pwm_pin;
        v_root["hw"]["pwm_channel"] = p_config.pwm_channel;
        v_root["hw"]["pwm_freq"] = p_config.pwm_frequency;
        v_root["hw"]["pwm_res"] = p_config.pwm_resolution;

        // [timing]
        v_root["timing"]["sim_int"] = p_config.wind_sim_interval_ms;
        v_root["timing"]["gust_int"] = p_config.gust_check_interval_ms;
        v_root["timing"]["thermal_int"] = p_config.thermal_check_interval_ms;

        // [sim]
        JsonObject v_sim = v_root["sim"].to<JsonObject>();
        v_sim["intensity"] = p_config.wind_intensity;
        v_sim["gust_freq"] = p_config.gust_frequency;
        v_sim["variability"] = p_config.wind_variability;
        v_sim["fan_limit"] = p_config.fan_speed_limit;
        v_sim["min_fan"] = p_config.minimum_fan_speed;
        v_sim["turb_len"] = p_config.turbulence_length_scale;
        v_sim["turb_sig"] = p_config.turbulence_intensity_sigma;
        v_sim["therm_str"] = p_config.thermal_bubble_strength;
        v_sim["therm_rad"] = p_config.thermal_bubble_radius;
        v_sim["preset"] = g_A10_PRESET_MODE_NAMES_Arr[p_config.preset_mode_index];
    }
    */

    // ======================================================
    // 9️⃣ /api/config PATCH 반영
    // ======================================================
    static bool patchFromJson(ST_A10_WindConfig &p_config, const JsonDocument &p_doc, bool &p_wifiChanged) {
        JsonObjectConst v_root = p_doc.as<JsonObjectConst>();
        p_wifiChanged = false;

        // security
        if (!v_root["security"]["api_key"].isNull()) {
            strlcpy(p_config.api_key, v_root["security"]["api_key"], sizeof(p_config.api_key));
        }

        // hw
        if (!v_root["hw"].isNull()) {
            if (!v_root["hw"]["pwm_pin"].isNull()) p_config.fan_pwm_pin = v_root["hw"]["pwm_pin"].as<int>();
            if (!v_root["hw"]["pwm_channel"].isNull()) p_config.pwm_channel = v_root["hw"]["pwm_channel"].as<int>();
            if (!v_root["hw"]["pwm_freq"].isNull()) p_config.pwm_frequency = v_root["hw"]["pwm_freq"].as<int>();
            if (!v_root["hw"]["pwm_res"].isNull()) p_config.pwm_resolution = v_root["hw"]["pwm_res"].as<int>();
        }

        // sim
        if (!v_root["sim"].isNull()) {
            if (!v_root["sim"]["preset"].isNull()) {
                const char *v_presetName = v_root["sim"]["preset"];
                for (int i = 0; i < EN_A10_PRESET_COUNT; i++) {
                    if (strcmp(v_presetName, g_A10_PRESET_MODE_NAMES_Arr[i]) == 0)
                        p_config.preset_mode_index = i;
                }
            }
            if (!v_root["sim"]["intensity"].isNull()) p_config.wind_intensity = v_root["sim"]["intensity"].as<float>();
            if (!v_root["sim"]["gust_freq"].isNull()) p_config.gust_frequency = v_root["sim"]["gust_freq"].as<float>();
            if (!v_root["sim"]["variability"].isNull()) p_config.wind_variability = v_root["sim"]["variability"].as<float>();
            if (!v_root["sim"]["fan_limit"].isNull()) p_config.fan_speed_limit = v_root["sim"]["fan_limit"].as<float>();
            if (!v_root["sim"]["min_fan"].isNull()) p_config.minimum_fan_speed = v_root["sim"]["min_fan"].as<float>();
            if (!v_root["sim"]["turb_len"].isNull()) p_config.turbulence_length_scale = v_root["sim"]["turb_len"].as<float>();
            if (!v_root["sim"]["turb_sig"].isNull()) p_config.turbulence_intensity_sigma = v_root["sim"]["turb_sig"].as<float>();
            if (!v_root["sim"]["therm_str"].isNull()) p_config.thermal_bubble_strength = v_root["sim"]["therm_str"].as<float>();
            if (!v_root["sim"]["therm_rad"].isNull()) p_config.thermal_bubble_radius = v_root["sim"]["therm_rad"].as<float>();
        }

        // timing
        if (!v_root["timing"].isNull()) {
            if (!v_root["timing"]["sim_int"].isNull()) p_config.wind_sim_interval_ms = v_root["timing"]["sim_int"].as<int>();
            if (!v_root["timing"]["gust_int"].isNull()) p_config.gust_check_interval_ms = v_root["timing"]["gust_int"].as<int>();
            if (!v_root["timing"]["thermal_int"].isNull()) p_config.thermal_check_interval_ms = v_root["timing"]["thermal_int"].as<int>();
        }

        // wifi
        if (!v_root["wifi"].isNull()) {
            if (!v_root["wifi"]["wifi_mode"].isNull()) {
                p_config.wifi_mode = v_root["wifi"]["wifi_mode"].as<int>();
                p_wifiChanged = true;
            }
            if (!v_root["wifi"]["ap_ssid"].isNull()) {
                strlcpy(p_config.ap_ssid, v_root["wifi"]["ap_ssid"], sizeof(p_config.ap_ssid));
                p_wifiChanged = true;
            }
            if (!v_root["wifi"]["ap_password"].isNull()) {
                strlcpy(p_config.ap_password, v_root["wifi"]["ap_password"], sizeof(p_config.ap_password));
                p_wifiChanged = true;
            }

            if (v_root["wifi"]["sta_networks"].is<JsonArrayConst>()) {
                p_config.sta_network_count = 0;
                for (JsonObjectConst v_net : v_root["wifi"]["sta_networks"].as<JsonArrayConst>()) {
                    if (p_config.sta_network_count >= A10_Const::MAX_STA_NETWORKS) break;
                    strlcpy(p_config.sta_networks[p_config.sta_network_count].ssid,
                            v_net["ssid"] | "", sizeof(p_config.sta_networks[0].ssid));
                    strlcpy(p_config.sta_networks[p_config.sta_network_count].password,
                            v_net["pass"] | "", sizeof(p_config.sta_networks[0].password));
                    p_config.sta_network_count++;
                }
                p_wifiChanged = true;
            }
        }

        return true;
    }

    // ======================================================
    // 🔟 기본 설정 초기화 (코드 내 기본값)
    // ======================================================
    static void initDefaultConfig(ST_A10_WindConfig &p_config) {
        memset(&p_config, 0, sizeof(p_config));

        strlcpy(p_config.api_key, "my_secure_api_key_123", sizeof(p_config.api_key));
        p_config.wifi_mode = G_A10_WIFI_MODE_AP;
        strlcpy(p_config.ap_ssid, "SC10_Config_AP", sizeof(p_config.ap_ssid));
        strlcpy(p_config.ap_password, "newpassword", sizeof(p_config.ap_password));

        p_config.fan_pwm_pin = 6;
        p_config.pwm_channel = 0;
        p_config.pwm_frequency = 25000;
        p_config.pwm_resolution = 10;

        p_config.wind_sim_interval_ms = 250;
        p_config.gust_check_interval_ms = 500;
        p_config.thermal_check_interval_ms = 2000;

        p_config.wind_intensity = 75.0f;
        p_config.gust_frequency = 50.0f;
        p_config.wind_variability = 60.0f;
        p_config.fan_speed_limit = 90.0f;
        p_config.minimum_fan_speed = 10.0f;
        p_config.turbulence_length_scale = 45.0f;
        p_config.turbulence_intensity_sigma = 0.45f;
        p_config.thermal_bubble_strength = 2.2f;
        p_config.thermal_bubble_radius = 20.0f;
        p_config.preset_mode_index = EN_A10_PRESET_COUNTRY;
    }
};
