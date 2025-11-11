#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : C10_ConfigManager_023.h
 * 모듈 약어 : C10
 * 모듈명 : Smart Nature Wind Configuration Manager
 * ------------------------------------------------------
 * 기능 요약:
 *  - Smart Nature Wind 전체 설정(JSON 기반) 관리 매니저
 *  - 설정 파일 단위 분리 관리 (system / wifi / motion / schedules / userProfiles / windProfile)
 *  - 구조체 ↔ JSON 직렬화 및 역직렬화 (ArduinoJson v7 전용)
 *  - 파일 백업(.bak) / 복구 / 공장초기화(factoryResetFromDefault) 지원
 *  - PATCH 기반 부분 업데이트(patchConfigFromJson) 지원
 *  - Lazy-Load 하이브리드 구성 (필요 섹션만 동적 로드)
 *  - Wi-Fi 등 재초기화 판단 로직 확장 가능
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
 *   - 전역 상수,매크로      : G_모듈약어_ 접두사
 *   - 전역 변수             : g_모듈약어_ 접두사
 *   - 전역 함수             : 모듈약어_ 접두사
 *   - type                  : T_모듈약어_ 접두사
 *   - typedef               : _t  접미사
 *   - enum 상수             : EN_모듈약어_ 접두사
 *   - 구조체                : ST_모듈약어_ 접두사
 *   - 클래스명              : CL_모듈약어_ 접두사
 *   - 클래스 private 멤버   : _ 접두사
 *   - 클래스 멤버(함수/변수) : 모듈약어 접두사 미사용
 *   - 클래스 정적 멤버      : s_ 접두사
 *   - 함수 로컬 변수        : v_ 접두사
 *   - 함수 인자             : p_ 접두사
 * ------------------------------------------------------
 */

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <string.h>

#include "A10_Const_015.h"
#include "D10_Logger_016.h"

class CL_C10_ConfigManager {
   public:
	/* =====================================================
	 * 공용: JSON IO Helper
	 * ===================================================== */
	static bool ioLoadJson(const char* p_path, const char* p_bak, JsonDocument& p_doc) {
		if (!LittleFS.exists(p_path)) {
			if (p_bak && LittleFS.exists(p_bak)) {
				LittleFS.rename(p_bak, p_path);
				CL_D10_Logger::log(EN_L10_LOG_WARN,
								   "[C10] Restored from backup: %s -> %s",
								   p_bak, p_path);
			} else {
				CL_D10_Logger::log(EN_L10_LOG_ERROR,
								   "[C10] Missing config & no backup: %s",
								   p_path);
				return false;
			}
		}

		File v_f = LittleFS.open(p_path, "r");
		if (!v_f) {
			CL_D10_Logger::log(EN_L10_LOG_ERROR,
							   "[C10] Open failed: %s", p_path);
			return false;
		}

		auto v_e = deserializeJson(p_doc, v_f);
		v_f.close();
		if (v_e) {
			CL_D10_Logger::log(EN_L10_LOG_ERROR,
							   "[C10] Parse error(%s): %s",
							   p_path, v_e.c_str());
			return false;
		}
		return true;
	}

	static bool ioSaveJson(const char* p_path,
						   const char* p_bak,
						   const JsonDocument& p_doc) {
		if (LittleFS.exists(p_path)) {
			if (p_bak && LittleFS.exists(p_bak)) {
				LittleFS.remove(p_bak);
			}
			if (p_bak) {
				LittleFS.rename(p_path, p_bak);
			}
		}

		File v_f = LittleFS.open(p_path, "w");
		if (!v_f) {
			CL_D10_Logger::log(EN_L10_LOG_ERROR,
							   "[C10] Save open failed: %s", p_path);
			return false;
		}
		if (serializeJsonPretty(p_doc, v_f) == 0) {
			CL_D10_Logger::log(EN_L10_LOG_ERROR,
							   "[C10] Save write failed: %s", p_path);
			v_f.close();
			return false;
		}
		v_f.close();
		return true;
	}

	/* =====================================================
	 * System
	 * ===================================================== */
	static bool loadSystemConfig(ST_A10_SystemConfig& p_cfg) {
		JsonDocument v_doc;
		if (!ioLoadJson(A10_Const::CFG_SYSTEM_FILE,
						A10_Const::CFG_SYSTEM_FILE_BAK,
						v_doc)) {
			A10_resetSystemDefault(p_cfg);
			return false;
		}
		JsonObjectConst j = v_doc.as<JsonObjectConst>();

		strlcpy(p_cfg.meta.version,
				j["meta"]["version"] | A10_Const::FW_VERSION,
				sizeof(p_cfg.meta.version));
		strlcpy(p_cfg.meta.device_name,
				j["meta"]["device_name"] | "SmartNatureWind",
				sizeof(p_cfg.meta.device_name));
		strlcpy(p_cfg.meta.last_update,
				j["meta"]["last_update"] | "",
				sizeof(p_cfg.meta.last_update));

		strlcpy(p_cfg.system.web.html,
				j["system"]["web"]["html"] | "/html/main.html",
				sizeof(p_cfg.system.web.html));
		strlcpy(p_cfg.system.web.css,
				j["system"]["web"]["css"] | "/html/main.css",
				sizeof(p_cfg.system.web.css));
		strlcpy(p_cfg.system.web.js,
				j["system"]["web"]["js"] | "/html/main.js",
				sizeof(p_cfg.system.web.js));

		strlcpy(p_cfg.system.logging.level,
				j["system"]["logging"]["level"] | "INFO",
				sizeof(p_cfg.system.logging.level));
		p_cfg.system.logging.max_entries =
			j["system"]["logging"]["max_entries"] | 300;

		p_cfg.hw.fan_pwm.pin =
			j["hw"]["fan_pwm"]["pin"] | 6;
		p_cfg.hw.fan_pwm.channel =
			j["hw"]["fan_pwm"]["channel"] | 0;
		p_cfg.hw.fan_pwm.freq =
			j["hw"]["fan_pwm"]["freq"] | 25000;
		p_cfg.hw.fan_pwm.res =
			j["hw"]["fan_pwm"]["res"] | 10;

		p_cfg.hw.pir.enabled =
			j["hw"]["pir"]["enabled"] | true;
		p_cfg.hw.pir.pin =
			j["hw"]["pir"]["pin"] | 13;
		p_cfg.hw.pir.debounce_sec =
			j["hw"]["pir"]["debounce_sec"] | 5;

		p_cfg.hw.ble.enabled =
			j["hw"]["ble"]["enabled"] | true;
		p_cfg.hw.ble.scan_interval =
			j["hw"]["ble"]["scan_interval"] | 5;

		strlcpy(p_cfg.security.api_key,
				j["security"]["api_key"] | "",
				sizeof(p_cfg.security.api_key));

		strlcpy(p_cfg.time.ntp_server,
				j["time"]["ntp_server"] | "pool.ntp.org",
				sizeof(p_cfg.time.ntp_server));
		strlcpy(p_cfg.time.timezone,
				j["time"]["timezone"] | "Asia/Seoul",
				sizeof(p_cfg.time.timezone));
		p_cfg.time.sync_interval_min =
			j["time"]["sync_interval_min"] | 60;

		return true;
	}

	static bool saveSystemConfig(const ST_A10_SystemConfig& p_cfg) {
		JsonDocument v;

		v["meta"]["version"]	 = p_cfg.meta.version;
		v["meta"]["device_name"] = p_cfg.meta.device_name;
		v["meta"]["last_update"] = p_cfg.meta.last_update;

		v["system"]["web"]["html"] = p_cfg.system.web.html;
		v["system"]["web"]["css"]  = p_cfg.system.web.css;
		v["system"]["web"]["js"]   = p_cfg.system.web.js;

		v["system"]["logging"]["level"]		  = p_cfg.system.logging.level;
		v["system"]["logging"]["max_entries"] = p_cfg.system.logging.max_entries;

		v["hw"]["fan_pwm"]["pin"]	  = p_cfg.hw.fan_pwm.pin;
		v["hw"]["fan_pwm"]["channel"] = p_cfg.hw.fan_pwm.channel;
		v["hw"]["fan_pwm"]["freq"]	  = p_cfg.hw.fan_pwm.freq;
		v["hw"]["fan_pwm"]["res"]	  = p_cfg.hw.fan_pwm.res;

		v["hw"]["pir"]["enabled"]	   = p_cfg.hw.pir.enabled;
		v["hw"]["pir"]["pin"]		   = p_cfg.hw.pir.pin;
		v["hw"]["pir"]["debounce_sec"] = p_cfg.hw.pir.debounce_sec;

		v["hw"]["ble"]["enabled"]		= p_cfg.hw.ble.enabled;
		v["hw"]["ble"]["scan_interval"] = p_cfg.hw.ble.scan_interval;

		v["security"]["api_key"] = p_cfg.security.api_key;

		v["time"]["ntp_server"]		   = p_cfg.time.ntp_server;
		v["time"]["timezone"]		   = p_cfg.time.timezone;
		v["time"]["sync_interval_min"] = p_cfg.time.sync_interval_min;

		return ioSaveJson(A10_Const::CFG_SYSTEM_FILE,
						  A10_Const::CFG_SYSTEM_FILE_BAK,
						  v);
	}

	static void toJson_System(const ST_A10_SystemConfig& p,
							  JsonDocument&				d) {
		d["meta"]["version"]	 = p.meta.version;
		d["meta"]["device_name"] = p.meta.device_name;
		d["meta"]["last_update"] = p.meta.last_update;

		d["system"]["web"]["html"] = p.system.web.html;
		d["system"]["web"]["css"]  = p.system.web.css;
		d["system"]["web"]["js"]   = p.system.web.js;

		d["system"]["logging"]["level"]		  = p.system.logging.level;
		d["system"]["logging"]["max_entries"] = p.system.logging.max_entries;

		d["hw"]["fan_pwm"]["pin"]	  = p.hw.fan_pwm.pin;
		d["hw"]["fan_pwm"]["channel"] = p.hw.fan_pwm.channel;
		d["hw"]["fan_pwm"]["freq"]	  = p.hw.fan_pwm.freq;
		d["hw"]["fan_pwm"]["res"]	  = p.hw.fan_pwm.res;

		d["hw"]["pir"]["enabled"]	   = p.hw.pir.enabled;
		d["hw"]["pir"]["pin"]		   = p.hw.pir.pin;
		d["hw"]["pir"]["debounce_sec"] = p.hw.pir.debounce_sec;

		d["hw"]["ble"]["enabled"]		= p.hw.ble.enabled;
		d["hw"]["ble"]["scan_interval"] = p.hw.ble.scan_interval;

		d["security"]["api_key"] = p.security.api_key;

		d["time"]["ntp_server"]		   = p.time.ntp_server;
		d["time"]["timezone"]		   = p.time.timezone;
		d["time"]["sync_interval_min"] = p.time.sync_interval_min;
	}

	/* =====================================================
	 * WiFi
	 * ===================================================== */
	static bool loadWifiConfig(ST_A10_WifiConfig& p) {
		JsonDocument d;
		if (!ioLoadJson(A10_Const::CFG_WIFI_FILE,
						A10_Const::CFG_WIFI_FILE_BAK,
						d)) {
			A10_resetWifiDefault(p);
			return false;
		}
		JsonObjectConst j = d["wifi"];

		p.wifiMode =
			(EN_A10_WIFI_MODE_t)(j["wifiMode"] | EN_A10_WIFI_MODE_AP_STA);
		strlcpy(p.wifiModeDesc,
				j["wifiModeDesc"] | "0=AP,1=STA,2=AP+STA",
				sizeof(p.wifiModeDesc));

		strlcpy(p.ap.ssid,
				j["ap"]["ssid"] | "NatureWind",
				sizeof(p.ap.ssid));
		strlcpy(p.ap.password,
				j["ap"]["password"] | "2540",
				sizeof(p.ap.password));

		p.sta_count = 0;
		if (j["sta"].is<JsonArrayConst>()) {
			JsonArrayConst v_arr = j["sta"].as<JsonArrayConst>();
			for (JsonObjectConst v_js : v_arr) {
				if (p.sta_count >= A10_Const::MAX_STA_NETWORKS)
					break;
				strlcpy(p.sta[p.sta_count].ssid,
						v_js["ssid"] | "",
						sizeof(p.sta[p.sta_count].ssid));
				strlcpy(p.sta[p.sta_count].pass,
						v_js["pass"] | "",
						sizeof(p.sta[p.sta_count].pass));
				p.sta_count++;
			}
		}
		return true;
	}

	static bool saveWifiConfig(const ST_A10_WifiConfig& p) {
		JsonDocument d;

		d["wifi"]["wifiMode"]		= p.wifiMode;
		d["wifi"]["wifiModeDesc"]	= p.wifiModeDesc;
		d["wifi"]["ap"]["ssid"]		= p.ap.ssid;
		d["wifi"]["ap"]["password"] = p.ap.password;

		for (uint8_t v_i = 0; v_i < p.sta_count; v_i++) {
			d["wifi"]["sta"][v_i]["ssid"] = p.sta[v_i].ssid;
			d["wifi"]["sta"][v_i]["pass"] = p.sta[v_i].pass;
		}

		return ioSaveJson(A10_Const::CFG_WIFI_FILE,
						  A10_Const::CFG_WIFI_FILE_BAK,
						  d);
	}

	static void toJson_Wifi(const ST_A10_WifiConfig& p,
							JsonDocument&			 d) {
		d["wifi"]["wifiMode"]		= p.wifiMode;
		d["wifi"]["wifiModeDesc"]	= p.wifiModeDesc;
		d["wifi"]["ap"]["ssid"]		= p.ap.ssid;
		d["wifi"]["ap"]["password"] = p.ap.password;

		for (uint8_t i = 0; i < p.sta_count; i++) {
			d["wifi"]["sta"][i]["ssid"] = p.sta[i].ssid;
			d["wifi"]["sta"][i]["pass"] = p.sta[i].pass;
		}
	}
