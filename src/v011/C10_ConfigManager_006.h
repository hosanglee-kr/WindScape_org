#pragma once
/*
 * C10_ConfigManager_006.h
 * ------------------------------------------------------
 * - 설정(JSON) 로드/저장/백업/복구/리셋 + /api/config 패치 반영
 * - ArduinoJson v7.x 사용 (JsonObjectConst/JsonArrayConst)
 * - 파일: /json/config_012.json
 * - LittleFS 사용
 * ------------------------------------------------------
 */

#include <ArduinoJson.h>
#include <LittleFS.h>
#include "A10_Const_006.h"
#include "D10_Logger_004.h"

class CL_C10_ConfigManager {
public:
	// ======================================================
	// 설정 파일 로드 (없으면 false 반환)
	// ======================================================
	static bool load(ST_A10_WindConfig &cfg) {
		if (!LittleFS.exists(A10_Const::CONFIG_JSON_FILE)) {
			CL_D10_Logger::log(EN_L10_LOG_WARN, "Config not found. Using defaults");
			return false;
		}
		File f = LittleFS.open(A10_Const::CONFIG_JSON_FILE, "r");
		if (!f) {
			CL_D10_Logger::log(EN_L10_LOG_ERROR, "Config open failed");
			return false;
		}
		JsonDocument doc;
		auto err = deserializeJson(doc, f);
		f.close();
		if (err) {
			CL_D10_Logger::log(EN_L10_LOG_ERROR, "Config parse failed: %s", err.c_str());
			return restoreBackup(cfg);
		}
		return parseJson(cfg, doc);
	}

	// ======================================================
	// 설정 저장 (기존 파일 백업 후 새로 저장)
	// ======================================================
	static bool save(const ST_A10_WindConfig &cfg) {
		if (LittleFS.exists(A10_Const::CONFIG_JSON_FILE)) {
			LittleFS.remove(A10_Const::CONFIG_JSON_FILE_BACKUP);
			LittleFS.rename(A10_Const::CONFIG_JSON_FILE, A10_Const::CONFIG_JSON_FILE_BACKUP);
		}

		JsonDocument doc;
		toJson(cfg, doc);

		File f = LittleFS.open(A10_Const::CONFIG_JSON_FILE, "w");
		if (!f) {
			CL_D10_Logger::log(EN_L10_LOG_ERROR, "Config open for write failed");
			return false;
		}

		if (serializeJson(doc, f) == 0) {
			CL_D10_Logger::log(EN_L10_LOG_ERROR, "Config write failed");
			f.close();
			return false;
		}
		f.close();
		CL_D10_Logger::log(EN_L10_LOG_INFO, "Config saved OK");
		return true;
	}

	// ======================================================
	// 없으면 기본 설정 저장 후 true 반환
	// ======================================================
	static bool saveDefaultConfig() {
		initDefaultConfig(g_A10_config);
		return save(g_A10_config);
	}

	// ======================================================
	// 로드 + 없으면 기본 생성
	// ======================================================
	static bool loadOrCreate(ST_A10_WindConfig &cfg) {
		if (!load(cfg)) {
			CL_D10_Logger::log(EN_L10_LOG_WARN, "Creating default config");
			return saveDefaultConfig();
		}
		return true;
	}

	// ======================================================
	// 공장 초기화 (파일 삭제 후 기본값 저장)
	// ======================================================
	static bool reset() {
		LittleFS.remove(A10_Const::CONFIG_JSON_FILE);
		LittleFS.remove(A10_Const::CONFIG_JSON_FILE_BACKUP);
		initDefaultConfig(g_A10_config);
		save(g_A10_config);
		return true;
	}

	// ======================================================
	// 백업 복구
	// ======================================================
	static bool restoreBackup(ST_A10_WindConfig &cfg) {
		if (!LittleFS.exists(A10_Const::CONFIG_JSON_FILE_BACKUP)) return false;
		File f = LittleFS.open(A10_Const::CONFIG_JSON_FILE_BACKUP, "r");
		if (!f) return false;
		JsonDocument doc;
		auto err = deserializeJson(doc, f);
		f.close();
		if (err) {
			CL_D10_Logger::log(EN_L10_LOG_ERROR, "Backup parse failed: %s", err.c_str());
			return false;
		}
		return parseJson(cfg, doc);
	}

	// ======================================================
	// JSON → 구조체 패치 (/api/config POST)
	// ======================================================
	static bool patchFromJson(ST_A10_WindConfig &cfg, const JsonDocument &doc, bool &wifiChanged) {
		wifiChanged = false;
		JsonObjectConst root = doc.as<JsonObjectConst>();

		// security
		if (!root["security"]["api_key"].isNull()) {
			strncpy(cfg.api_key, root["security"]["api_key"], G_A10_API_KEY_MAX_LEN);
			cfg.api_key[G_A10_API_KEY_MAX_LEN] = '\0';
		}

		// hw
		JsonObjectConst hw = root["hw"];
		if (!hw.isNull()) {
			if (!hw["pwm_pin"].isNull()) cfg.fan_pwm_pin = hw["pwm_pin"].as<int>();
			if (!hw["pwm_channel"].isNull()) cfg.pwm_channel = hw["pwm_channel"].as<int>();
			if (!hw["pwm_freq"].isNull()) cfg.pwm_frequency = hw["pwm_freq"].as<int>();
			if (!hw["pwm_res"].isNull()) cfg.pwm_resolution = hw["pwm_res"].as<int>();
		}

		// sim
		JsonObjectConst sim = root["sim"];
		if (!sim.isNull()) {
			if (!sim["preset"].isNull()) {
				const char *name = sim["preset"];
				for (int i = 0; i < EN_A10_PRESET_COUNT; i++)
					if (strcmp(name, g_A10_PRESET_MODE_NAMES_Arr[i]) == 0)
						cfg.preset_mode_index = i;
			}
			if (!sim["intensity"].isNull()) cfg.wind_intensity = sim["intensity"].as<float>();
			if (!sim["gust_freq"].isNull()) cfg.gust_frequency = sim["gust_freq"].as<float>();
			if (!sim["variability"].isNull()) cfg.wind_variability = sim["variability"].as<float>();
			if (!sim["fan_limit"].isNull()) cfg.fan_speed_limit = sim["fan_limit"].as<float>();
			if (!sim["min_fan"].isNull()) cfg.minimum_fan_speed = sim["min_fan"].as<float>();
			if (!sim["turb_len"].isNull()) cfg.turbulence_length_scale = sim["turb_len"].as<float>();
			if (!sim["turb_sig"].isNull()) cfg.turbulence_intensity_sigma = sim["turb_sig"].as<float>();
			if (!sim["therm_str"].isNull()) cfg.thermal_bubble_strength = sim["therm_str"].as<float>();
			if (!sim["therm_rad"].isNull()) cfg.thermal_bubble_radius = sim["therm_rad"].as<float>();
		}

		// timing
		JsonObjectConst t = root["timing"];
		if (!t.isNull()) {
			if (!t["sim_int"].isNull()) cfg.wind_sim_interval_ms = t["sim_int"].as<int>();
			if (!t["gust_int"].isNull()) cfg.gust_check_interval_ms = t["gust_int"].as<int>();
			if (!t["thermal_int"].isNull()) cfg.thermal_check_interval_ms = t["thermal_int"].as<int>();
		}

		// wifi
		JsonObjectConst w = root["wifi"];
		if (!w.isNull()) {
			if (!w["wifi_mode"].isNull()) {
				cfg.wifi_mode = w["wifi_mode"].as<int>();
				wifiChanged = true;
			}
			if (!w["ap_ssid"].isNull()) {
				strlcpy(cfg.ap_ssid, w["ap_ssid"], sizeof(cfg.ap_ssid));
				wifiChanged = true;
			}
			if (!w["ap_password"].isNull()) {
				strlcpy(cfg.ap_password, w["ap_password"], sizeof(cfg.ap_password));
				wifiChanged = true;
			}
			if (w["sta_networks"].is<JsonArrayConst>()) {
				cfg.sta_network_count = 0;
				for (JsonObjectConst net : w["sta_networks"].as<JsonArrayConst>()) {
					if (cfg.sta_network_count >= A10_Const::MAX_STA_NETWORKS) break;
					const char *ssid = net["ssid"] | "";
					const char *pass = net["pass"] | "";
					if (*ssid) {
						strlcpy(cfg.sta_networks[cfg.sta_network_count].ssid, ssid, sizeof(ST_A10_StaCredential::ssid));
						strlcpy(cfg.sta_networks[cfg.sta_network_count].password, pass, sizeof(ST_A10_StaCredential::password));
						cfg.sta_network_count++;
					}
				}
				wifiChanged = true;
			}
		}
		return true;
	}

	// ======================================================
	// 구조체 → JSON 직렬화
	// ======================================================
	static void toJson(const ST_A10_WindConfig &cfg, JsonDocument &doc) {
		JsonObject root = doc.to<JsonObject>();
		toJson(cfg, root);
	}
	static void toJson(const ST_A10_WindConfig &cfg, JsonObject root) {
		root["security"]["api_key_set"] = (cfg.api_key[0] != '\0');
		JsonObject hw = root["hw"].to<JsonObject>();
		hw["pwm_pin"] = cfg.fan_pwm_pin;
		hw["pwm_channel"] = cfg.pwm_channel;
		hw["pwm_freq"] = cfg.pwm_frequency;
		hw["pwm_res"] = cfg.pwm_resolution;

		JsonObject sim = root["sim"].to<JsonObject>();
		sim["intensity"] = cfg.wind_intensity;
		sim["gust_freq"] = cfg.gust_frequency;
		sim["variability"] = cfg.wind_variability;
		sim["fan_limit"] = cfg.fan_speed_limit;
		sim["min_fan"] = cfg.minimum_fan_speed;
		sim["turb_len"] = cfg.turbulence_length_scale;
		sim["turb_sig"] = cfg.turbulence_intensity_sigma;
		sim["therm_str"] = cfg.thermal_bubble_strength;
		sim["therm_rad"] = cfg.thermal_bubble_radius;
		sim["preset"] = g_A10_PRESET_MODE_NAMES_Arr[cfg.preset_mode_index];

		JsonObject timing = root["timing"].to<JsonObject>();
		timing["sim_int"] = cfg.wind_sim_interval_ms;
		timing["gust_int"] = cfg.gust_check_interval_ms;
		timing["thermal_int"] = cfg.thermal_check_interval_ms;

		JsonObject wifi = root["wifi"].to<JsonObject>();
		wifi["wifi_mode"] = cfg.wifi_mode;
		wifi["ap_ssid"] = cfg.ap_ssid;

		JsonArray arr = wifi["sta_networks"].to<JsonArray>();
		for (int i = 0; i < cfg.sta_network_count; ++i) {
			JsonObject net = arr.add<JsonObject>();
			net["ssid"] = cfg.sta_networks[i].ssid;
		}
	}

private:
	// ======================================================
	// 내부: JSON → 구조체 파싱
	// ======================================================
	static bool parseJson(ST_A10_WindConfig &cfg, JsonDocument &doc) {
		JsonObjectConst root = doc.as<JsonObjectConst>();
		strlcpy(cfg.api_key, root["security"]["api_key"] | "", sizeof(cfg.api_key));
		cfg.wifi_mode = root["wifi"]["wifi_mode"] | cfg.wifi_mode;
		strlcpy(cfg.ap_ssid, root["wifi"]["ap_ssid"] | cfg.ap_ssid, sizeof(cfg.ap_ssid));
		strlcpy(cfg.ap_password, root["wifi"]["ap_password"] | cfg.ap_password, sizeof(cfg.ap_password));

		cfg.sta_network_count = 0;
		JsonArrayConst arr = root["wifi"]["sta_networks"].as<JsonArrayConst>();
		for (JsonObjectConst net : arr) {
			if (cfg.sta_network_count >= A10_Const::MAX_STA_NETWORKS) break;
			strlcpy(cfg.sta_networks[cfg.sta_network_count].ssid, net["ssid"] | "", sizeof(ST_A10_StaCredential::ssid));
			strlcpy(cfg.sta_networks[cfg.sta_network_count].password, net["pass"] | "", sizeof(ST_A10_StaCredential::password));
			cfg.sta_network_count++;
		}
		return true;
	}
};
