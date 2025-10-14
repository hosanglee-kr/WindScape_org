
#pragma once
/*
 * C10_ConfigManager_004.h
 * - 설정(JSON) 로드/저장/백업/복구/리셋 + /api/config 패치 반영
 * - ArduinoJson v7.x 사용: 읽기(JsonObjectConst/JsonArrayConst), 쓰기(JsonObject/JsonArray)
 * - 모든 주석 한글, 네이밍 규칙 유지(SC10_, g_SC10_, p_, v_)
 */

#include <ArduinoJson.h>
#include <LittleFS.h>

#include "A10_Const_004.h"
#include "D10_Logger_004.h"

class C10_ConfigManager {
   public:
	// -----------------------------------------------------------------------------
	// 설정 로드: /json/config_003.json → 파싱 실패 시 백업(/json/config_003.json.bak) 복구 시도
	// -----------------------------------------------------------------------------
	static bool load(WindConfig &p_cfg) {
		// if (!LittleFS.begin(true)) {
		// 	SC10_Logger::log(SC10_LOG_ERROR, "LittleFS mount failed");
		// 	return false;
		// }

		if (!LittleFS.exists(A10_Const::CONFIG_FILE)) {
			SC10_Logger::log(SC10_LOG_WARN, "Config not found. Using defaults");
			return false;  // 기본값으로 진행
		}

		File v_f = LittleFS.open(A10_Const::CONFIG_FILE, "r");
		if (!v_f) {
			SC10_Logger::log(SC10_LOG_ERROR, "Config open failed");
			return false;
		}

		JsonDocument		 v_doc;
		DeserializationError v_err = deserializeJson(v_doc, v_f);
		v_f.close();

		if (v_err) {
			SC10_Logger::log(SC10_LOG_ERROR, "Config parse failed: %s", v_err.c_str());
			// 백업 복구 시도
			return restoreBackup(p_cfg);
		}

		return parseJson(p_cfg, v_doc);
	}

	// -----------------------------------------------------------------------------
	// 설정 저장: 기존 파일을 .bak 으로 백업 후 새 파일 기록
	// -----------------------------------------------------------------------------
	static bool save(WindConfig &p_cfg) {
		// 기존 파일을 백업으로 이동
		if (LittleFS.exists(A10_Const::CONFIG_FILE)) {
			LittleFS.remove(A10_Const::BACKUP_FILE);
			LittleFS.rename(A10_Const::CONFIG_FILE, SC10_Const::BACKUP_FILE);
		}

		JsonDocument v_doc;
		// 문서 루트에 객체 생성 후 필드 채우기
		JsonObject v_root = v_doc.to<JsonObject>();
		toJson(p_cfg, v_root);

		File v_f = LittleFS.open(A10_Const::CONFIG_FILE, "w");
		if (!v_f) {
			SC10_Logger::log(SC10_LOG_ERROR, "Config open for write failed");
			return false;
		}

		if (serializeJson(v_doc, v_f) == 0) {
			SC10_Logger::log(SC10_LOG_ERROR, "Config write failed");
			v_f.close();
			return false;
		}
		v_f.close();
		SC10_Logger::log(SC10_LOG_INFO, "Config saved");
		return true;
	}

	// -----------------------------------------------------------------------------
	// 공장 초기화: 설정/백업 파일 삭제
	// -----------------------------------------------------------------------------
	static bool reset() {
		bool a = LittleFS.remove(A10_Const::CONFIG_FILE);
		bool b = LittleFS.remove(A10_Const::BACKUP_FILE);
		(void)a;
		(void)b;

		g_SC10_config.api_key[0] = '\0';

		return true;
	}

	// -----------------------------------------------------------------------------
	// 백업 복구: .bak → 파싱해서 구조체에 반영
	// -----------------------------------------------------------------------------
	static bool restoreBackup(WindConfig &p_cfg) {
		if (!LittleFS.exists(A10_Const::BACKUP_FILE)) {
			SC10_Logger::log(SC10_LOG_WARN, "No backup to restore");
			return false;
		}

		File v_b = LittleFS.open(A10_Const::BACKUP_FILE, "r");
		if (!v_b) {
			SC10_Logger::log(SC10_LOG_ERROR, "Backup open failed");
			return false;
		}

		JsonDocument		 v_doc;
		DeserializationError v_err = deserializeJson(v_doc, v_b);
		v_b.close();

		if (v_err) {
			SC10_Logger::log(SC10_LOG_ERROR, "Backup parse failed: %s", v_err.c_str());
			return false;
		}

		SC10_Logger::log(SC10_LOG_WARN, "Restored from backup");
		return parseJson(p_cfg, v_doc);
	}

	// -----------------------------------------------------------------------------
	// /api/config 바디(JSON)를 WindConfig에 패치. Wi-Fi 항목 변경 여부를 p_wifiChanged로 반환.
	//  - 유효 필드만 조건 적용(존재하지 않는 필드는 건드리지 않음)
	// -----------------------------------------------------------------------------

	// C10_ConfigManager::patchFromJson()
	static bool patchFromJson(WindConfig &p_cfg, const JsonDocument &p_doc, bool &p_wifiChanged) {
		p_wifiChanged		 = false;
		JsonObjectConst root = p_doc.as<JsonObjectConst>();

		// --- security ---
		if (!root["security"]["api_key"].isNull()) {
			const char *k = root["security"]["api_key"] | "";
			strncpy(p_cfg.api_key, k, SC10_API_KEY_MAX_LEN);
			p_cfg.api_key[SC10_API_KEY_MAX_LEN] = '\0';
			SC10_Logger::log(SC10_LOG_INFO, "API Key updated via patch.");
		}
		// hw
		JsonObjectConst h = root["hw"];
		if (!h.isNull()) {
			if (!h["pwm_pin"].isNull())		p_cfg.fan_pwm_pin 		= h["pwm_pin"].as<int>();
			if (!h["pwm_freq"].isNull())	p_cfg.pwm_frequency 	= h["pwm_freq"].as<int>();
			if (!h["pwm_res"].isNull())		p_cfg.pwm_resolution 	= h["pwm_res"].as<int>();
		}

		// --- sim ---
		JsonObjectConst s = root["sim"];
		if (!s.isNull()) {
			if (!s["preset"].isNull()) {
				const char *name = s["preset"];
				for (int i = 0; i < SC10_PRESET_COUNT; i++) {
					if (strcmp(name, G_SC10_PRESET_MODE_NAMES[i]) == 0) {
						p_cfg.preset_mode_index = i;
						break;
					}
				}
			}
			if (!s["intensity"].isNull())	p_cfg.wind_intensity 				= s["intensity"].as<float>();
			if (!s["gust_freq"].isNull())	p_cfg.gust_frequency 				= s["gust_freq"].as<float>();
			if (!s["variability"].isNull()) p_cfg.wind_variability 				= s["variability"].as<float>();
			if (!s["fan_limit"].isNull())  	p_cfg.fan_speed_limit 				= s["fan_limit"].as<float>();
			if (!s["min_fan"].isNull())		p_cfg.minimum_fan_speed 			= s["min_fan"].as<float>();
			if (!s["turb_len"].isNull())	p_cfg.turbulence_length_scale 		= s["turb_len"].as<float>();
			if (!s["turb_sig"].isNull())	p_cfg.turbulence_intensity_sigma 	= s["turb_sig"].as<float>();
			if (!s["therm_str"].isNull())	p_cfg.thermal_bubble_strength 		= s["therm_str"].as<float>();
			if (!s["therm_rad"].isNull())	p_cfg.thermal_bubble_radius 		= s["therm_rad"].as<float>();
		}

		// --- timing ---
		JsonObjectConst t = root["timing"];
		if (!t.isNull()) {
			if (!t["sim_int"].isNull())		p_cfg.wind_sim_interval_ms 			= t["sim_int"].as<int>();
			if (!t["gust_int"].isNull())	p_cfg.gust_check_interval_ms 		= t["gust_int"].as<int>();
			if (!t["thermal_int"].isNull())	p_cfg.thermal_check_interval_ms 	= t["thermal_int"].as<int>();
		}

		// --- wifi ---
		JsonObjectConst w = root["wifi"];
		if (!w.isNull()) {
			if (!w["wifi_mode"].isNull()) {
				p_cfg.wifi_mode = w["wifi_mode"].as<int>();
				p_wifiChanged	= true;
			}
			if (!w["ap_ssid"].isNull()) {
				strlcpy(p_cfg.ap_ssid, w["ap_ssid"], sizeof(p_cfg.ap_ssid));
				p_wifiChanged = true;
			}
			if (!w["ap_password"].isNull()) {
				strlcpy(p_cfg.ap_password, w["ap_password"], sizeof(p_cfg.ap_password));
				p_wifiChanged = true;
			}

			if (w["sta_networks"].is<JsonArrayConst>()) {
				p_cfg.sta_network_count = 0;
				for (JsonObjectConst net : w["sta_networks"].as<JsonArrayConst>()) {
					if (p_cfg.sta_network_count >= SC10_Const::MAX_STA_NETWORKS)
						break;
					const char *ssid = net["ssid"] | "";
					const char *pass = net["pass"] | "";
					if (*ssid) {
						strlcpy(p_cfg.sta_networks[p_cfg.sta_network_count].ssid, ssid, sizeof(SC10_StaCredential::ssid));
						strlcpy(p_cfg.sta_networks[p_cfg.sta_network_count].password, pass, sizeof(SC10_StaCredential::password));
						p_cfg.sta_network_count++;
					}
				}
				p_wifiChanged = true;
			}
		}
		return true;
	}



	// -----------------------------------------------------------------------------
	// 직렬화(쓰기) 오버로드 1: JsonObject에 직접 채우기 (서브트리 기록용)
	// -----------------------------------------------------------------------------
	static void toJson(const WindConfig &p_config, JsonObject p_root) {
		// --- hw (신규) ---
		p_root["hw"]["pwm_pin"]	 	= p_config.fan_pwm_pin;
		p_root["hw"]["pwm_freq"] 	= p_config.pwm_frequency;
		p_root["hw"]["pwm_res"]	 	= p_config.pwm_resolution;

		// --- sim ---
		p_root["sim"]["intensity"]	 = p_config.wind_intensity;
		p_root["sim"]["gust_freq"]	 = p_config.gust_frequency;
		p_root["sim"]["variability"] = p_config.wind_variability;
		p_root["sim"]["fan_limit"]	 = p_config.fan_speed_limit;
		p_root["sim"]["min_fan"]	 = p_config.minimum_fan_speed;
		p_root["sim"]["turb_len"]	 = p_config.turbulence_length_scale;
		p_root["sim"]["turb_sig"]	 = p_config.turbulence_intensity_sigma;
		p_root["sim"]["therm_str"]	 = p_config.thermal_bubble_strength;
		p_root["sim"]["therm_rad"]	 = p_config.thermal_bubble_radius;
		p_root["sim"]["preset"]		 = G_SC10_PRESET_MODE_NAMES[p_config.preset_mode_index];

		// --- timing ---
		p_root["timing"]["sim_int"]		= p_config.wind_sim_interval_ms;
		p_root["timing"]["gust_int"]	= p_config.gust_check_interval_ms;
		p_root["timing"]["thermal_int"] = p_config.thermal_check_interval_ms;

		JsonObject sec	   				= p_root["security"].to<JsonObject>();
		sec["api_key_set"] 				= (p_config.api_key[0] != '\0');	// true/false만 노출
														// 키 원문은 내보내지 않음

		// --- wifi ---
		JsonObject v_w	 = p_root["wifi"].to<JsonObject>();
		v_w["wifi_mode"] = p_config.wifi_mode;
		v_w["ap_ssid"]	 = p_config.ap_ssid;
		// v_w["ap_password"] = p_config.ap_password; // 상태 응답에서는 비노출 권장

		JsonArray v_arr = v_w["sta_networks"].to<JsonArray>();
		for (int v_i = 0; v_i < p_config.sta_network_count; ++v_i) {
			JsonObject v_net = v_arr.add<JsonObject>();
			v_net["ssid"]	 = p_config.sta_networks[v_i].ssid;
			// v_net["pass"] = p_config.sta_networks[v_i].password; // 상태 응답에서는 비노출 권장
		}
	}

	// -----------------------------------------------------------------------------
	// 직렬화(쓰기) 오버로드 2: JsonDocument 루트에 생성 후 채우기 (전체 파일 기록용)
	// -----------------------------------------------------------------------------
	static void toJson(const WindConfig &p_config, JsonDocument &p_doc) {
		JsonObject v_root = p_doc.to<JsonObject>();
		toJson(p_config, v_root);
	}

   private:
	// -----------------------------------------------------------------------------
	// 내부: JSON → 구조체 파싱 (읽기 전용 뷰 사용: JsonObjectConst/JsonArrayConst)
	// -----------------------------------------------------------------------------
	static bool parseJson(WindConfig &p_config, JsonDocument &p_doc) {
		JsonObjectConst v_root = p_doc.as<JsonObjectConst>();  // v7: const 뷰로 읽기

		if (!v_root["security"]["api_key"].isNull()) {
			strlcpy(p_config.api_key, v_root["security"]["api_key"] | "", sizeof(p_config.api_key));
		}

		// --- wifi ---
		p_config.wifi_mode 	= v_root["wifi"]["wifi_mode"] | p_config.wifi_mode;

		strlcpy(p_config.ap_ssid,		v_root["wifi"]["ap_ssid"] | p_config.ap_ssid,			sizeof(p_config.ap_ssid));
		strlcpy(p_config.ap_password,	v_root["wifi"]["ap_password"] | p_config.ap_password,	sizeof(p_config.ap_password));

		p_config.sta_network_count = 0;
		JsonArrayConst v_arr  = v_root["wifi"]["sta_networks"].as<JsonArrayConst>();
		for (JsonObjectConst v_net : v_arr) {
			if (p_config.sta_network_count >= A10_Const::MAX_STA_NETWORKS)
				break;
			const char *v_ssid = v_net["ssid"] | "";
			const char *v_pass = v_net["pass"] | "";
			strlcpy(p_config.sta_networks[p_config.sta_network_count].ssid, 	v_ssid, sizeof(SC10_StaCredential::ssid));
			strlcpy(p_config.sta_networks[p_config.sta_network_count].password, v_pass, sizeof(SC10_StaCredential::password));
			p_config.sta_network_count++;
		}

		// --- hw  ---
		p_config.fan_pwm_pin	   	= v_root["hw"]["pwm_pin"] | p_config.fan_pwm_pin;
		p_config.pwm_frequency  	= v_root["hw"]["pwm_freq"] | p_config.pwm_frequency;
		p_config.pwm_resolution 	= v_root["hw"]["pwm_res"] | p_config.pwm_resolution;

		// --- (선택) 시뮬/타이밍 값도 JSON에 있으면 적용 ---
		// 존재하지 않으면 기존값 유지
		if (!v_root["sim"]["intensity"].isNull())		p_config.wind_intensity 			= v_root["sim"]["intensity"].as<float>();
		if (!v_root["sim"]["gust_freq"].isNull())		p_config.gust_frequency 			= v_root["sim"]["gust_freq"].as<float>();
		if (!v_root["sim"]["variability"].isNull())		p_config.wind_variability 			= v_root["sim"]["variability"].as<float>();
		if (!v_root["sim"]["fan_limit"].isNull())		p_config.fan_speed_limit 			= v_root["sim"]["fan_limit"].as<float>();
		if (!v_root["sim"]["min_fan"].isNull())			p_config.minimum_fan_speed 			= v_root["sim"]["min_fan"].as<float>();
		if (!v_root["sim"]["turb_len"].isNull())		p_config.turbulence_length_scale 	= v_root["sim"]["turb_len"].as<float>();
		if (!v_root["sim"]["turb_sig"].isNull())		p_config.turbulence_intensity_sigma = v_root["sim"]["turb_sig"].as<float>();
		if (!v_root["sim"]["therm_str"].isNull())		p_config.thermal_bubble_strength 	= v_root["sim"]["therm_str"].as<float>();
		if (!v_root["sim"]["therm_rad"].isNull())		p_config.thermal_bubble_radius 		= v_root["sim"]["therm_rad"].as<float>();

		if (!v_root["timing"]["sim_int"].isNull())		p_config.wind_sim_interval_ms 		= v_root["timing"]["sim_int"].as<int>();
		if (!v_root["timing"]["gust_int"].isNull())		p_config.gust_check_interval_ms 	= v_root["timing"]["gust_int"].as<int>();
		if (!v_root["timing"]["thermal_int"].isNull())	p_config.thermal_check_interval_ms 	= v_root["timing"]["thermal_int"].as<int>();

		// --- 프리셋 (문자열 이름일 수도 있으니 방어적 처리) ---
		if (!v_root["sim"]["preset"].isNull()) {
			const char *v_preset = v_root["sim"]["preset"];
			for (int v_i = 0; v_i < SC10_PRESET_COUNT; ++v_i) {
				if (strcmp(v_preset, G_SC10_PRESET_MODE_NAMES[v_i]) == 0) {
					p_config.preset_mode_index = v_i;
					break;
				}
			}
		}

		return true;
	}
};
