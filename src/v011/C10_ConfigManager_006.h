#pragma once
/*
 * C10_ConfigManager_006.h
 * - WindScape 설정(JSON) 로드/저장/백업/복구/리셋/기본 생성
 * - ArduinoJson v7.x 기반
 * - A10_Const_006.h 구조체와 100% 호환
 */

#include <ArduinoJson.h>
#include <LittleFS.h>

#include "A10_Const_006.h"
#include "D10_Logger_004.h"

class CL_C10_ConfigManager {
public:
	// -----------------------------------------------------------------------------
	// 설정 로드: 없으면 기본 생성 후 다시 로드
	// -----------------------------------------------------------------------------
	static bool loadOrCreate(ST_A10_WindConfig &p_cfg) {
		if (!LittleFS.exists(A10_Const::CONFIG_JSON_FILE)) {
			CL_D10_Logger::log(EN_L10_LOG_WARN, "Config file not found. Creating default...");
			saveDefaultConfig();   // 기본 설정 생성
		}
		return load(p_cfg);
	}

	// -----------------------------------------------------------------------------
	// 설정 로드
	// -----------------------------------------------------------------------------
	static bool load(ST_A10_WindConfig &p_cfg) {
		if (!LittleFS.exists(A10_Const::CONFIG_JSON_FILE)) {
			CL_D10_Logger::log(EN_L10_LOG_WARN, "Config not found. Using defaults");
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
			return restoreBackup(p_cfg);
		}
		return parseJson(p_cfg, v_doc);
	}

	// -----------------------------------------------------------------------------
	// 설정 저장
	// -----------------------------------------------------------------------------
	static bool save(ST_A10_WindConfig &p_cfg) {
		if (LittleFS.exists(A10_Const::CONFIG_JSON_FILE)) {
			LittleFS.remove(A10_Const::CONFIG_JSON_FILE_BACKUP);
			LittleFS.rename(A10_Const::CONFIG_JSON_FILE, A10_Const::CONFIG_JSON_FILE_BACKUP);
		}

		JsonDocument v_doc;
		JsonObject v_root = v_doc.to<JsonObject>();
		toJson(p_cfg, v_root);

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
		CL_D10_Logger::log(EN_L10_LOG_INFO, "Config saved");
		return true;
	}

	// -----------------------------------------------------------------------------
	// 공장 초기화
	// -----------------------------------------------------------------------------
	static bool reset() {
		LittleFS.remove(A10_Const::CONFIG_JSON_FILE);
		LittleFS.remove(A10_Const::CONFIG_JSON_FILE_BACKUP);

		initDefaultConfig(g_A10_config);
		save(g_A10_config);
		CL_D10_Logger::log(EN_L10_LOG_INFO, "Config reset to defaults");
		return true;
	}

	// -----------------------------------------------------------------------------
	// 기본 설정 파일 생성 (처음 부팅 시 자동 호출)
	// -----------------------------------------------------------------------------
	static bool saveDefaultConfig() {
		initDefaultConfig(g_A10_config);
		LittleFS.mkdir("/json");	// 폴더 없을 때 대비
		return save(g_A10_config);
	}

	// -----------------------------------------------------------------------------
	// 백업 복구
	// -----------------------------------------------------------------------------
	static bool restoreBackup(ST_A10_WindConfig &p_cfg) {
		if (!LittleFS.exists(A10_Const::CONFIG_JSON_FILE_BACKUP)) {
			CL_D10_Logger::log(EN_L10_LOG_WARN, "No backup to restore");
			return false;
		}

		File v_b = LittleFS.open(A10_Const::CONFIG_JSON_FILE_BACKUP, "r");
		if (!v_b) {
			CL_D10_Logger::log(EN_L10_LOG_ERROR, "Backup open failed");
			return false;
		}

		JsonDocument v_doc;
		DeserializationError v_err = deserializeJson(v_doc, v_b);
		v_b.close();

		if (v_err) {
			CL_D10_Logger::log(EN_L10_LOG_ERROR, "Backup parse failed: %s", v_err.c_str());
			return false;
		}

		CL_D10_Logger::log(EN_L10_LOG_WARN, "Restored from backup");
		return parseJson(p_cfg, v_doc);
	}

	// -----------------------------------------------------------------------------
	// JSON → 구조체 변환
	// -----------------------------------------------------------------------------
	static bool parseJson(ST_A10_WindConfig &cfg, JsonDocument &doc) {
		JsonObjectConst root = doc.as<JsonObjectConst>();

		strlcpy(cfg.api_key, root["security"]["api_key"] | "", sizeof(cfg.api_key));

		cfg.fan_pwm_pin    = root["hw"]["pwm_pin"]     | cfg.fan_pwm_pin;
		cfg.pwm_channel    = root["hw"]["pwm_channel"] | cfg.pwm_channel;
		cfg.pwm_frequency  = root["hw"]["pwm_freq"]    | cfg.pwm_frequency;
		cfg.pwm_resolution = root["hw"]["pwm_res"]     | cfg.pwm_resolution;

		cfg.wind_sim_interval_ms    = root["timing"]["sim_int"]     | cfg.wind_sim_interval_ms;
		cfg.gust_check_interval_ms  = root["timing"]["gust_int"]    | cfg.gust_check_interval_ms;
		cfg.thermal_check_interval_ms = root["timing"]["thermal_int"] | cfg.thermal_check_interval_ms;

		if (!root["sim"].isNull()) {
			cfg.wind_intensity          = root["sim"]["intensity"].as<float>();
			cfg.gust_frequency          = root["sim"]["gust_freq"].as<float>();
			cfg.wind_variability        = root["sim"]["variability"].as<float>();
			cfg.fan_speed_limit         = root["sim"]["fan_limit"].as<float>();
			cfg.minimum_fan_speed       = root["sim"]["min_fan"].as<float>();
			cfg.turbulence_length_scale = root["sim"]["turb_len"].as<float>();
			cfg.turbulence_intensity_sigma = root["sim"]["turb_sig"].as<float>();
			cfg.thermal_bubble_strength = root["sim"]["therm_str"].as<float>();
			cfg.thermal_bubble_radius   = root["sim"]["therm_rad"].as<float>();

			const char *preset = root["sim"]["preset"] | "Off";
			for (int i = 0; i < EN_A10_PRESET_COUNT; ++i) {
				if (strstr(preset, g_A10_PRESET_MODE_NAMES_Arr[i])) {
					cfg.preset_mode_index = i;
					break;
				}
			}
		}

		cfg.wifi_mode = root["wifi"]["wifi_mode"] | cfg.wifi_mode;
		strlcpy(cfg.ap_ssid, root["wifi"]["ap_ssid"] | "", sizeof(cfg.ap_ssid));
		strlcpy(cfg.ap_password, root["wifi"]["ap_password"] | "", sizeof(cfg.ap_password));

		cfg.sta_network_count = 0;
		if (root["wifi"]["sta_networks"].is<JsonArrayConst>()) {
			for (JsonObjectConst net : root["wifi"]["sta_networks"].as<JsonArrayConst>()) {
				if (cfg.sta_network_count >= A10_Const::MAX_STA_NETWORKS)
					break;
				strlcpy(cfg.sta_networks[cfg.sta_network_count].ssid, net["ssid"] | "", sizeof(ST_A10_StaCredential::ssid));
				strlcpy(cfg.sta_networks[cfg.sta_network_count].password, net["password"] | "", sizeof(ST_A10_StaCredential::password));
				cfg.sta_network_count++;
			}
		}
		return true;
	}

	// -----------------------------------------------------------------------------
	// 구조체 → JSON 변환
	// -----------------------------------------------------------------------------
	static void toJson(const ST_A10_WindConfig &cfg, JsonObject root) {
		root["security"]["api_key_set"] = (cfg.api_key[0] != '\0');

		root["hw"]["pwm_pin"]     = cfg.fan_pwm_pin;
		root["hw"]["pwm_channel"] = cfg.pwm_channel;
		root["hw"]["pwm_freq"]    = cfg.pwm_frequency;
		root["hw"]["pwm_res"]     = cfg.pwm_resolution;

		root["timing"]["sim_int"]     = cfg.wind_sim_interval_ms;
		root["timing"]["gust_int"]    = cfg.gust_check_interval_ms;
		root["timing"]["thermal_int"] = cfg.thermal_check_interval_ms;

		JsonObject sim = root["sim"].to<JsonObject>();
		sim["intensity"]   = cfg.wind_intensity;
		sim["gust_freq"]   = cfg.gust_frequency;
		sim["variability"] = cfg.wind_variability;
		sim["fan_limit"]   = cfg.fan_speed_limit;
		sim["min_fan"]     = cfg.minimum_fan_speed;
		sim["turb_len"]    = cfg.turbulence_length_scale;
		sim["turb_sig"]    = cfg.turbulence_intensity_sigma;
		sim["therm_str"]   = cfg.thermal_bubble_strength;
		sim["therm_rad"]   = cfg.thermal_bubble_radius;
		sim["preset"]      = g_A10_PRESET_MODE_NAMES_Arr[cfg.preset_mode_index];

		JsonObject w = root["wifi"].to<JsonObject>();
		w["wifi_mode"] = cfg.wifi_mode;
		w["ap_ssid"]   = cfg.ap_ssid;
		w["ap_password"] = cfg.ap_password;

		JsonArray arr = w["sta_networks"].to<JsonArray>();
		for (int i = 0; i < cfg.sta_network_count; ++i) {
			JsonObject net = arr.add<JsonObject>();
			net["ssid"]     = cfg.sta_networks[i].ssid;
			net["password"] = cfg.sta_networks[i].password;
		}
	}
};
