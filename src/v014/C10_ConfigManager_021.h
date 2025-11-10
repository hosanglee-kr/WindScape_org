#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : C10_ConfigManager_021.h
 * 모듈 약어 : C10
 * 모듈명 : Smart Nature Wind Configuration Manager
 * ------------------------------------------------------
 * 기능 요약:
 *  - 시스템 전체 설정 파일 관리
 *    - System / WiFi / Motion
 *    - WindProfile (Preset + Style 사전)
 *    - Schedules
 *    - UserProfiles
 *  - JSON 파일 <-> 구조체 매핑 (ArduinoJson v7 전용)
 *  - Lazy-Load 및 안전 기본값 초기화
 *  - 공통 Wind 해석 유틸 제공
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

#include "A10_Const_014.h"
#include "D10_Logger_014.h"

class CL_C10_ConfigManager {
public:
	// =====================================================
	// 공용: JSON IO Helper
	// =====================================================
	static bool C10_ioLoadJson(const char* p_path, JsonDocument& p_doc) {
		if (!LittleFS.exists(p_path)) {
			CL_D10_Logger::log(EN_L10_LOG_WARN, "[C10] Config not found: %s", p_path);
			return false;
		}
		File v_f = LittleFS.open(p_path, "r");
		if (!v_f) {
			CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] Open failed: %s", p_path);
			return false;
		}
		auto v_e = deserializeJson(p_doc, v_f);
		v_f.close();
		if (v_e) {
			CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] Parse error(%s): %s",
							   p_path, v_e.c_str());
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
		if (!v_f) {
			CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] Save open failed: %s", p_path);
			return false;
		}
		if (serializeJsonPretty(p_doc, v_f) == 0) {
			CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] Save write failed: %s", p_path);
			v_f.close();
			return false;
		}
		v_f.close();
		return true;
	}

	// =====================================================
	// System / WiFi / Motion (기존 스펙 유지)
	// =====================================================
	static bool C10_loadSystemConfig(ST_A10_SystemConfig& p_cfg) {
		JsonDocument v_doc;
		if (!C10_ioLoadJson(A10_Const::CFG_SYSTEM_FILE, v_doc)) {
			A10_resetSystemDefault(p_cfg);
			return false;
		}
		JsonObjectConst j = v_doc.as<JsonObjectConst>();

		strlcpy(p_cfg.meta.version,     j["meta"]["version"]     | A10_Const::FW_VERSION, sizeof(p_cfg.meta.version));
		strlcpy(p_cfg.meta.device_name, j["meta"]["device_name"] | "WindScape",           sizeof(p_cfg.meta.device_name));
		strlcpy(p_cfg.meta.last_update, j["meta"]["last_update"] | "",                    sizeof(p_cfg.meta.last_update));

		strlcpy(p_cfg.system.web.html, j["system"]["web"]["html"] | "/html/main.html", sizeof(p_cfg.system.web.html));
		strlcpy(p_cfg.system.web.css,  j["system"]["web"]["css"]  | "/html/main.css",  sizeof(p_cfg.system.web.css));
		strlcpy(p_cfg.system.web.js,   j["system"]["web"]["js"]   | "/html/main.js",   sizeof(p_cfg.system.web.js));

		strlcpy(p_cfg.system.logging.level, j["system"]["logging"]["level"] | "INFO", sizeof(p_cfg.system.logging.level));
		p_cfg.system.logging.max_entries = j["system"]["logging"]["max_entries"] | 300;

		p_cfg.hw.fan_pwm.pin     = j["hw"]["fan_pwm"]["pin"]     | 6;
		p_cfg.hw.fan_pwm.channel = j["hw"]["fan_pwm"]["channel"] | 0;
		p_cfg.hw.fan_pwm.freq    = j["hw"]["fan_pwm"]["freq"]    | 25000;
		p_cfg.hw.fan_pwm.res     = j["hw"]["fan_pwm"]["res"]     | 10;

		p_cfg.hw.pir.enabled      = j["hw"]["pir"]["enabled"]      | true;
		p_cfg.hw.pir.pin          = j["hw"]["pir"]["pin"]          | 13;
		p_cfg.hw.pir.debounce_sec = j["hw"]["pir"]["debounce_sec"] | 5;

		p_cfg.hw.ble.enabled       = j["hw"]["ble"]["enabled"]       | true;
		p_cfg.hw.ble.scan_interval = j["hw"]["ble"]["scan_interval"] | 5;

		strlcpy(p_cfg.security.api_key, j["security"]["api_key"] | "", sizeof(p_cfg.security.api_key));

		strlcpy(p_cfg.time.ntp_server, j["time"]["ntp_server"] | "pool.ntp.org", sizeof(p_cfg.time.ntp_server));
		strlcpy(p_cfg.time.timezone,   j["time"]["timezone"]   | "Asia/Seoul",   sizeof(p_cfg.time.timezone));
		p_cfg.time.sync_interval_min   = j["time"]["sync_interval_min"] | 60;

		return true;
	}

	static bool C10_saveSystemConfig(const ST_A10_SystemConfig& p_cfg) {
		JsonDocument v;
		v["meta"]["version"]     = p_cfg.meta.version;
		v["meta"]["device_name"] = p_cfg.meta.device_name;
		v["meta"]["last_update"] = p_cfg.meta.last_update;

		v["system"]["web"]["html"] = p_cfg.system.web.html;
		v["system"]["web"]["css"]  = p_cfg.system.web.css;
		v["system"]["web"]["js"]   = p_cfg.system.web.js;

		v["system"]["logging"]["level"]       = p_cfg.system.logging.level;
		v["system"]["logging"]["max_entries"] = p_cfg.system.logging.max_entries;

		v["hw"]["fan_pwm"]["pin"]     = p_cfg.hw.fan_pwm.pin;
		v["hw"]["fan_pwm"]["channel"] = p_cfg.hw.fan_pwm.channel;
		v["hw"]["fan_pwm"]["freq"]    = p_cfg.hw.fan_pwm.freq;
		v["hw"]["fan_pwm"]["res"]     = p_cfg.hw.fan_pwm.res;

		v["hw"]["pir"]["enabled"]      = p_cfg.hw.pir.enabled;
		v["hw"]["pir"]["pin"]          = p_cfg.hw.pir.pin;
		v["hw"]["pir"]["debounce_sec"] = p_cfg.hw.pir.debounce_sec;

		v["hw"]["ble"]["enabled"]       = p_cfg.hw.ble.enabled;
		v["hw"]["ble"]["scan_interval"] = p_cfg.hw.ble.scan_interval;

		v["security"]["api_key"] = p_cfg.security.api_key;

		v["time"]["ntp_server"]        = p_cfg.time.ntp_server;
		v["time"]["timezone"]          = p_cfg.time.timezone;
		v["time"]["sync_interval_min"] = p_cfg.time.sync_interval_min;

		return C10_ioSaveJson(A10_Const::CFG_SYSTEM_FILE, A10_Const::CFG_SYSTEM_FILE_BAK, v);
	}

static void C10_toJson_System(const ST_A10_SystemConfig& p, JsonDocument& d) {
    d["meta"]["version"]     = p.meta.version;
    d["meta"]["device_name"] = p.meta.device_name;
    d["meta"]["last_update"] = p.meta.last_update;

    d["system"]["web"]["html"] = p.system.web.html;
    d["system"]["web"]["css"]  = p.system.web.css;
    d["system"]["web"]["js"]   = p.system.web.js;

    d["system"]["logging"]["level"]       = p.system.logging.level;
    d["system"]["logging"]["max_entries"] = p.system.logging.max_entries;

    d["hw"]["fan_pwm"]["pin"]     = p.hw.fan_pwm.pin;
    d["hw"]["fan_pwm"]["channel"] = p.hw.fan_pwm.channel;
    d["hw"]["fan_pwm"]["freq"]    = p.hw.fan_pwm.freq;
    d["hw"]["fan_pwm"]["res"]     = p.hw.fan_pwm.res;

    d["hw"]["pir"]["enabled"]      = p.hw.pir.enabled;
    d["hw"]["pir"]["pin"]          = p.hw.pir.pin;
    d["hw"]["pir"]["debounce_sec"] = p.hw.pir.debounce_sec;

    d["hw"]["ble"]["enabled"]       = p.hw.ble.enabled;
    d["hw"]["ble"]["scan_interval"] = p.hw.ble.scan_interval;

    d["security"]["api_key"] = p.security.api_key;

    d["time"]["ntp_server"]        = p.time.ntp_server;
    d["time"]["timezone"]          = p.time.timezone;
    d["time"]["sync_interval_min"] = p.time.sync_interval_min;
}


	static bool C10_loadWifiConfig(ST_A10_WifiConfig& p) {
		JsonDocument d;
		if (!C10_ioLoadJson(A10_Const::CFG_WIFI_FILE, d)) {
			A10_resetWifiDefault(p);
			return false;
		}
		JsonObjectConst j = d["wifi"];
		p.wifiMode = (EN_A10_WIFI_MODE_t)(j["wifiMode"] | EN_A10_WIFI_MODE_AP_STA);
		strlcpy(p.wifiModeDesc, j["wifiModeDesc"] | "0=AP,1=STA,2=AP+STA", sizeof(p.wifiModeDesc));

		strlcpy(p.ap.ssid,     j["ap"]["ssid"]     | "NatureWind", sizeof(p.ap.ssid));
		strlcpy(p.ap.password, j["ap"]["password"] | "2540",       sizeof(p.ap.password));

		p.sta_count = 0;
		if (j["sta"].is<JsonArrayConst>()) {
			JsonArrayConst v_arr = j["sta"].as<JsonArrayConst>();
			for (JsonObjectConst v_js : v_arr) {
				if (p.sta_count >= A10_Const::MAX_STA_NETWORKS) break;
				strlcpy(p.sta[p.sta_count].ssid, v_js["ssid"] | "", sizeof(p.sta[p.sta_count].ssid));
				strlcpy(p.sta[p.sta_count].pass, v_js["pass"] | "", sizeof(p.sta[p.sta_count].pass));
				p.sta_count++;
			}
		}
		return true;
	}

	static bool C10_saveWifiConfig(const ST_A10_WifiConfig& p) {
		JsonDocument d;
		d["wifi"]["wifiMode"]     = p.wifiMode;
		d["wifi"]["wifiModeDesc"] = p.wifiModeDesc;
		d["wifi"]["ap"]["ssid"]   = p.ap.ssid;
		d["wifi"]["ap"]["password"] = p.ap.password;

		for (uint8_t v_i=0; v_i<p.sta_count; v_i++) {
			d["wifi"]["sta"][v_i]["ssid"] = p.sta[v_i].ssid;
			d["wifi"]["sta"][v_i]["pass"] = p.sta[v_i].pass;
		}
		return C10_ioSaveJson(A10_Const::CFG_WIFI_FILE, A10_Const::CFG_WIFI_FILE_BAK, d);
	}

static void C10_toJson_Wifi(const ST_A10_WifiConfig& p, JsonDocument& d) {
    d["wifi"]["wifiMode"]     = p.wifiMode;
    d["wifi"]["wifiModeDesc"] = p.wifiModeDesc;
    d["wifi"]["ap"]["ssid"]   = p.ap.ssid;
    d["wifi"]["ap"]["password"] = p.ap.password;

    for (uint8_t i=0; i<p.sta_count; i++) {
        d["wifi"]["sta"][i]["ssid"] = p.sta[i].ssid;
        d["wifi"]["sta"][i]["pass"] = p.sta[i].pass;
    }
}



	static bool C10_loadMotionConfig(ST_A10_MotionConfig& p) {
		JsonDocument d;
		if (!C10_ioLoadJson(A10_Const::CFG_MOTION_FILE, d)) {
			A10_resetMotionDefault(p);
			return false;
		}
		JsonObjectConst j = d["motion"];

		p.enabled       = j["enabled"] | true;
		p.pir.enabled   = j["pir"]["enabled"]  | true;
		p.pir.hold_sec  = j["pir"]["hold_sec"] | 120;
		p.ble.enabled   = j["ble"]["enabled"]  | true;

		p.ble.rssi.on            = j["ble"]["rssi"]["on"]            | -65;
		p.ble.rssi.off           = j["ble"]["rssi"]["off"]           | -75;
		p.ble.rssi.avg_count     = j["ble"]["rssi"]["avg_count"]     | 8;
		p.ble.rssi.persist_count = j["ble"]["rssi"]["persist_count"] | 5;
		p.ble.rssi.exit_delay_sec= j["ble"]["rssi"]["exit_delay_sec"]| 12;

		p.ble.trusted_count = 0;
		if (j["ble"]["trusted_devices"].is<JsonArrayConst>()) {
			JsonArrayConst v_arr = j["ble"]["trusted_devices"].as<JsonArrayConst>();
			for (JsonObjectConst v_js : v_arr) {
				if (p.ble.trusted_count >= A10_Const::MAX_BLE_DEVICES) break;
				ST_A10_BLETrustedDevice& v_d = p.ble.trusted_devices[p.ble.trusted_count++];
				strlcpy(v_d.alias, v_js["alias"] | "", sizeof(v_d.alias));
				strlcpy(v_d.name,  v_js["name"]  | "", sizeof(v_d.name));
				strlcpy(v_d.mac,   v_js["mac"]   | "", sizeof(v_d.mac));
				strlcpy(v_d.manuf_prefix, v_js["manuf_prefix"] | "", sizeof(v_d.manuf_prefix));
				v_d.prefix_len = v_js["prefix_len"] | 0;
				v_d.enabled    = v_js["enabled"]    | true;
			}
		}
		return true;
	}

	static bool C10_saveMotionConfig(const ST_A10_MotionConfig& p) {
		JsonDocument d;
		d["motion"]["enabled"]          = p.enabled;
		d["motion"]["pir"]["enabled"]   = p.pir.enabled;
		d["motion"]["pir"]["hold_sec"]  = p.pir.hold_sec;
		d["motion"]["ble"]["enabled"]   = p.ble.enabled;
		d["motion"]["ble"]["rssi"]["on"]= p.ble.rssi.on;
		d["motion"]["ble"]["rssi"]["off"]=p.ble.rssi.off;
		d["motion"]["ble"]["rssi"]["avg_count"]     = p.ble.rssi.avg_count;
		d["motion"]["ble"]["rssi"]["persist_count"] = p.ble.rssi.persist_count;
		d["motion"]["ble"]["rssi"]["exit_delay_sec"]= p.ble.rssi.exit_delay_sec;

		for (uint8_t v_i=0; v_i<p.ble.trusted_count; v_i++) {
			const ST_A10_BLETrustedDevice& v_d = p.ble.trusted_devices[v_i];
			JsonObject v_td = d["motion"]["ble"]["trusted_devices"][v_i];
			v_td["alias"]        = v_d.alias;
			v_td["name"]         = v_d.name;
			v_td["mac"]          = v_d.mac;
			v_td["manuf_prefix"] = v_d.manuf_prefix;
			v_td["prefix_len"]   = v_d.prefix_len;
			v_td["enabled"]      = v_d.enabled;
		}
		return C10_ioSaveJson(A10_Const::CFG_MOTION_FILE, A10_Const::CFG_MOTION_FILE_BAK, d);
	}


static void C10_toJson_Motion(const ST_A10_MotionConfig& p, JsonDocument& d) {
    d["motion"]["enabled"] = p.enabled;
    d["motion"]["pir"]["enabled"]  = p.pir.enabled;
    d["motion"]["pir"]["hold_sec"] = p.pir.hold_sec;
    d["motion"]["ble"]["enabled"]  = p.ble.enabled;

    d["motion"]["ble"]["rssi"]["on"]             = p.ble.rssi.on;
    d["motion"]["ble"]["rssi"]["off"]            = p.ble.rssi.off;
    d["motion"]["ble"]["rssi"]["avg_count"]      = p.ble.rssi.avg_count;
    d["motion"]["ble"]["rssi"]["persist_count"]  = p.ble.rssi.persist_count;
    d["motion"]["ble"]["rssi"]["exit_delay_sec"] = p.ble.rssi.exit_delay_sec;

    for (uint8_t i=0; i<p.ble.trusted_count; i++) {
        const ST_A10_BLETrustedDevice& v_d = p.ble.trusted_devices[i];
        JsonObject v_td = d["motion"]["ble"]["trusted_devices"][i];
        v_td["alias"] = v_d.alias;
        v_td["name"]  = v_d.name;
        v_td["mac"]   = v_d.mac;
        v_td["manuf_prefix"] = v_d.manuf_prefix;
        v_td["prefix_len"]   = v_d.prefix_len;
        v_td["enabled"]      = v_d.enabled;
    }
}


	// =====================================================
	// WindProfile Dict (cfg_default_windProfile_023.json)
	// =====================================================
	static bool C10_loadWindProfileDict(ST_A10_WindProfileDict_t& p_dict) {
		JsonDocument d;
		if (!C10_ioLoadJson(A10_Const::WIND_PROFILE_FILE, d)) {
			A10_resetWindProfileDictDefault(p_dict);
			return false;
		}
		JsonObjectConst j = d["windProfile"];

		p_dict.version = j["version"] | 23;

		// presets
		p_dict.presetCount = 0;
		if (j["presets"].is<JsonArrayConst>()) {
			JsonArrayConst v_arr = j["presets"].as<JsonArrayConst>();
			for (JsonObjectConst v_js : v_arr) {
				if (p_dict.presetCount >= A10_Const::MAX_WIND_PRESETS) break;
				ST_A10_WindPresetDef_t& v_p = p_dict.presets[p_dict.presetCount++];

				strlcpy(v_p.name, v_js["name"] | "", sizeof(v_p.name));
				strlcpy(v_p.code, v_js["code"] | "", sizeof(v_p.code));

				JsonObjectConst v_b = v_js["base"];
				v_p.base.wind_intensity             = v_b["wind_intensity"]             | 70.0f;
				v_p.base.gust_frequency             = v_b["gust_frequency"]             | 40.0f;
				v_p.base.wind_variability           = v_b["wind_variability"]           | 50.0f;
				v_p.base.fan_limit                  = v_b["fan_limit"]                  | 95.0f;
				v_p.base.min_fan                    = v_b["min_fan"]                    | 10.0f;
				v_p.base.turbulence_length_scale    = v_b["turbulence_length_scale"]    | 40.0f;
				v_p.base.turbulence_intensity_sigma = v_b["turbulence_intensity_sigma"] | 0.5f;
				v_p.base.thermal_bubble_strength    = v_b["thermal_bubble_strength"]    | 2.0f;
				v_p.base.thermal_bubble_radius      = v_b["thermal_bubble_radius"]      | 18.0f;
			}
		}

		// styles
		p_dict.styleCount = 0;
		if (j["styles"].is<JsonArrayConst>()) {
			JsonArrayConst v_arr = j["styles"].as<JsonArrayConst>();
			for (JsonObjectConst v_js : v_arr) {
				if (p_dict.styleCount >= A10_Const::MAX_WIND_STYLES) break;
				ST_A10_WindStyleDef_t& v_s = p_dict.styles[p_dict.styleCount++];

				strlcpy(v_s.name, v_js["name"] | "", sizeof(v_s.name));
				strlcpy(v_s.code, v_js["code"] | "", sizeof(v_s.code));

				JsonObjectConst v_f = v_js["factors"];
				v_s.factor.intensity_factor = v_f["intensity_factor"] | 1.0f;
				v_s.factor.variability_factor = v_f["variability_factor"] | 1.0f;
				v_s.factor.gust_factor        = v_f["gust_factor"]        | 1.0f;
				v_s.factor.thermal_factor     = v_f["thermal_factor"]     | 1.0f;
			}
		}
		return true;
	}

	// =====================================================
	// Schedules (cfg_schedules_024.json)
	// =====================================================
	static bool C10_loadSchedules(ST_A10_ScheduleConfig& p_cfg) {
		JsonDocument d;
		if (!C10_ioLoadJson(A10_Const::CFG_SCHEDULES_FILE, d)) {
			A10_resetSchedulesDefault(p_cfg);
			return false;
		}
		JsonArrayConst arr = d["schedules"].as<JsonArrayConst>();
		p_cfg.count = 0;
		for (JsonObjectConst js : arr) {
			if (p_cfg.count >= A10_Const::MAX_SCHEDULES) break;
			ST_A10_ScheduleItem_t& s = p_cfg.items[p_cfg.count++];

			s.schNo   = js["schNo"] | 0;
			strlcpy(s.name, js["name"] | "", sizeof(s.name));
			s.enabled = js["enabled"] | true;

			// period
			s.period.enabled = js["period"]["enabled"] | false;
			for (uint8_t v_d=0; v_d<7; v_d++) {
				s.period.days[v_d] = js["period"]["days"][v_d] | 1;
			}
			strlcpy(s.period.start_time, js["period"]["start_time"] | "00:00", sizeof(s.period.start_time));
			strlcpy(s.period.end_time,   js["period"]["end_time"]   | "23:59", sizeof(s.period.end_time));

			// segments
			s.segCount = 0;
			if (js["segments"].is<JsonArrayConst>()) {
				JsonArrayConst segArr = js["segments"].as<JsonArrayConst>();
				for (JsonObjectConst jseg : segArr) {
					if (s.segCount >= A10_Const::MAX_SEGMENTS_PER_SCHEDULE) break;
					ST_A10_OpSegment_t& sg = s.segments[s.segCount++];

					sg.segNo       = jseg["segNo"]       | 0;
					sg.on_minutes  = jseg["on_minutes"]  | 10;
					sg.off_minutes = jseg["off_minutes"] | 0;

					strlcpy(sg.mode, jseg["mode"] | "PRESET", sizeof(sg.mode));
					strlcpy(sg.presetCode, jseg["presetCode"] | "", sizeof(sg.presetCode));
					strlcpy(sg.styleCode,  jseg["styleCode"]  | "", sizeof(sg.styleCode));

					memset(&sg.adjust, 0, sizeof(sg.adjust));
					if (jseg["adjust"].is<JsonObjectConst>()) {
						JsonObjectConst adj = jseg["adjust"];
						sg.adjust.wind_intensity   = adj["wind_intensity"]   | 0.0f;
						sg.adjust.wind_variability = adj["wind_variability"] | 0.0f;
						sg.adjust.gust_frequency   = adj["gust_frequency"]   | 0.0f;
						sg.adjust.fan_limit        = adj["fan_limit"]        | 0.0f;
						sg.adjust.min_fan          = adj["min_fan"]          | 0.0f;
					}
					sg.fixed_speed = jseg["fixed_speed"] | 0.0f;
				}
			}

			// autoOff (userProfiles와 동일 구조)
			memset(&s.autoOff, 0, sizeof(s.autoOff));
			if (js["autoOff"].is<JsonObjectConst>()) {
				JsonObjectConst ao = js["autoOff"];
				s.autoOff.timer.enabled  = ao["timer"]["enabled"]  | false;
				s.autoOff.timer.minutes  = ao["timer"]["minutes"]  | 0;
				s.autoOff.offTime.enabled= ao["offTime"]["enabled"]| false;
				strlcpy(s.autoOff.offTime.time, ao["offTime"]["time"] | "", sizeof(s.autoOff.offTime.time));
				s.autoOff.offTemp.enabled= ao["offTemp"]["enabled"] | false;
				s.autoOff.offTemp.temp   = ao["offTemp"]["temp"]    | 0.0f;
			}

			// motion
			s.motion.pir.enabled        = js["motion"]["pir"]["enabled"]        | false;
			s.motion.pir.hold_sec       = js["motion"]["pir"]["hold_sec"]       | 0;
			s.motion.ble.enabled        = js["motion"]["ble"]["enabled"]        | false;
			s.motion.ble.rssi_threshold = js["motion"]["ble"]["rssi_threshold"] | -70;
			s.motion.ble.hold_sec       = js["motion"]["ble"]["hold_sec"]       | 0;
		}
		return true;
	}

	static bool C10_saveSchedules(const ST_A10_ScheduleConfig& p_cfg) {
		JsonDocument d;
		for (uint8_t v_i=0; v_i<p_cfg.count; v_i++) {
			const ST_A10_ScheduleItem_t& s = p_cfg.items[v_i];
			JsonObject js = d["schedules"][v_i];

			js["schNo"]   = s.schNo;
			js["name"]    = s.name;
			js["enabled"] = s.enabled;

			js["period"]["enabled"] = s.period.enabled;
			for (uint8_t v_d=0; v_d<7; v_d++)
				js["period"]["days"][v_d] = s.period.days[v_d];
			js["period"]["start_time"] = s.period.start_time;
			js["period"]["end_time"]   = s.period.end_time;

			for (uint8_t v_k=0; v_k<s.segCount; v_k++) {
				const ST_A10_OpSegment_t& sg = s.segments[v_k];
				JsonObject jseg = js["segments"][v_k];
				jseg["segNo"]       = sg.segNo;
				jseg["on_minutes"]  = sg.on_minutes;
				jseg["off_minutes"] = sg.off_minutes;
				jseg["mode"]        = sg.mode;
				jseg["presetCode"]  = sg.presetCode;
				jseg["styleCode"]   = sg.styleCode;

				if (sg.mode[0] == 'P') {
					JsonObject adj = jseg["adjust"];
					adj["wind_intensity"]   = sg.adjust.wind_intensity;
					adj["wind_variability"] = sg.adjust.wind_variability;
					adj["gust_frequency"]   = sg.adjust.gust_frequency;
					adj["fan_limit"]        = sg.adjust.fan_limit;
					adj["min_fan"]          = sg.adjust.min_fan;
				}
				jseg["fixed_speed"] = sg.fixed_speed;
			}

			JsonObject ao = js["autoOff"];
			ao["timer"]["enabled"] = s.autoOff.timer.enabled;
			ao["timer"]["minutes"] = s.autoOff.timer.minutes;
			ao["offTime"]["enabled"] = s.autoOff.offTime.enabled;
			ao["offTime"]["time"]    = s.autoOff.offTime.time;
			ao["offTemp"]["enabled"] = s.autoOff.offTemp.enabled;
			ao["offTemp"]["temp"]    = s.autoOff.offTemp.temp;

			js["motion"]["pir"]["enabled"]        = s.motion.pir.enabled;
			js["motion"]["pir"]["hold_sec"]       = s.motion.pir.hold_sec;
			js["motion"]["ble"]["enabled"]        = s.motion.ble.enabled;
			js["motion"]["ble"]["rssi_threshold"] = s.motion.ble.rssi_threshold;
			js["motion"]["ble"]["hold_sec"]       = s.motion.ble.hold_sec;
		}
		return C10_ioSaveJson(A10_Const::CFG_SCHEDULES_FILE, A10_Const::CFG_SCHEDULES_FILE_BAK, d);
	}

static void C10_toJson_Schedules(const ST_A10_ScheduleConfig& p, JsonDocument& d) {
    d["schedules_count"] = p.count;

    for (uint8_t i = 0; i < p.count; i++) {
        const ST_A10_ScheduleItem_t& s = p.items[i];
        JsonObject js = d["schedules"][i];

        js["schNo"]   = s.schNo;
        js["name"]    = s.name;
        js["enabled"] = s.enabled;

        // period
        js["period"]["enabled"] = s.period.enabled;
        for (uint8_t d_i = 0; d_i < 7; d_i++) {
            js["period"]["days"][d_i] = s.period.days[d_i];
        }
        js["period"]["start_time"] = s.period.start_time;
        js["period"]["end_time"]   = s.period.end_time;

        // segments
        js["segCount"] = s.segCount;
        for (uint8_t k = 0; k < s.segCount; k++) {
            const ST_A10_OpSegment_t& sg = s.segments[k];
            JsonObject jseg = js["segments"][k];

            jseg["segNo"]       = sg.segNo;
            jseg["on_minutes"]  = sg.on_minutes;
            jseg["off_minutes"] = sg.off_minutes;
            jseg["mode"]        = sg.mode;
            jseg["presetCode"]  = sg.presetCode;
            jseg["styleCode"]   = sg.styleCode;

            // adjust
            JsonObject adj = jseg["adjust"];
            adj["wind_intensity"]   = sg.adjust.wind_intensity;
            adj["wind_variability"] = sg.adjust.wind_variability;
            adj["gust_frequency"]   = sg.adjust.gust_frequency;
            adj["fan_limit"]        = sg.adjust.fan_limit;
            adj["min_fan"]          = sg.adjust.min_fan;

            jseg["fixed_speed"] = sg.fixed_speed;
        }

        // autoOff
        JsonObject ao = js["autoOff"];
        ao["timer"]["enabled"] = s.autoOff.timer.enabled;
        ao["timer"]["minutes"] = s.autoOff.timer.minutes;
        ao["offTime"]["enabled"] = s.autoOff.offTime.enabled;
        ao["offTime"]["time"]    = s.autoOff.offTime.time;
        ao["offTemp"]["enabled"] = s.autoOff.offTemp.enabled;
        ao["offTemp"]["temp"]    = s.autoOff.offTemp.temp;

        // motion
        js["motion"]["pir"]["enabled"]        = s.motion.pir.enabled;
        js["motion"]["pir"]["hold_sec"]       = s.motion.pir.hold_sec;
        js["motion"]["ble"]["enabled"]        = s.motion.ble.enabled;
        js["motion"]["ble"]["rssi_threshold"] = s.motion.ble.rssi_threshold;
        js["motion"]["ble"]["hold_sec"]       = s.motion.ble.hold_sec;
    }
}


	// =====================================================
	// UserProfiles (cfg_uzOpProfile_025_final.json)
	// =====================================================
	static bool C10_loadUserProfiles(ST_A10_UserProfileConfig_t& p_cfg) {
		JsonDocument d;
		if (!C10_ioLoadJson(A10_Const::CFG_USER_PROFILES_FILE, d)) {
			A10_resetUserProfilesDefault(p_cfg);
			return false;
		}
		JsonArrayConst arr = d["userProfiles"]["profiles"].as<JsonArrayConst>();
		p_cfg.count = 0;
		for (JsonObjectConst jp : arr) {
			if (p_cfg.count >= A10_Const::MAX_USER_PROFILES) break;
			ST_A10_UserProfile_t& up = p_cfg.items[p_cfg.count++];

			up.profileNo = jp["profileNo"] | 0;
			strlcpy(up.name, jp["name"] | "", sizeof(up.name));
			up.enabled        = jp["enabled"]        | true;
			up.repeatSegments = jp["repeatSegments"] | true;

			// segments
			up.segCount = 0;
			if (jp["segments"].is<JsonArrayConst>()) {
				JsonArrayConst sArr = jp["segments"].as<JsonArrayConst>();
				for (JsonObjectConst jseg : sArr) {
					if (up.segCount >= A10_Const::MAX_SEGMENTS_PER_PROFILE) break;
					ST_A10_OpSegment_t& sg = up.segments[up.segCount++];

					sg.segNo       = jseg["segNo"]       | 0;
					sg.on_minutes  = jseg["on_minutes"]  | 10;
					sg.off_minutes = jseg["off_minutes"] | 0;
					strlcpy(sg.mode, jseg["mode"] | "PRESET", sizeof(sg.mode));
					strlcpy(sg.presetCode, jseg["presetCode"] | "", sizeof(sg.presetCode));
					strlcpy(sg.styleCode,  jseg["styleCode"]  | "", sizeof(sg.styleCode));

					memset(&sg.adjust, 0, sizeof(sg.adjust));
					if (jseg["adjust"].is<JsonObjectConst>()) {
						JsonObjectConst adj = jseg["adjust"];
						sg.adjust.wind_intensity   = adj["wind_intensity"]   | 0.0f;
						sg.adjust.wind_variability = adj["wind_variability"] | 0.0f;
						sg.adjust.gust_frequency   = adj["gust_frequency"]   | 0.0f;
						sg.adjust.fan_limit        = adj["fan_limit"]        | 0.0f;
						sg.adjust.min_fan          = adj["min_fan"]          | 0.0f;
					}
					sg.fixed_speed = jseg["fixed_speed"] | 0.0f;
				}
			}

			// autoOff
			memset(&up.autoOff, 0, sizeof(up.autoOff));
			if (jp["autoOff"].is<JsonObjectConst>()) {
				JsonObjectConst ao = jp["autoOff"];
				up.autoOff.timer.enabled  = ao["timer"]["enabled"]  | false;
				up.autoOff.timer.minutes  = ao["timer"]["minutes"]  | 0;
				up.autoOff.offTime.enabled= ao["offTime"]["enabled"]| false;
				strlcpy(up.autoOff.offTime.time, ao["offTime"]["time"] | "", sizeof(up.autoOff.offTime.time));
				up.autoOff.offTemp.enabled= ao["offTemp"]["enabled"] | false;
				up.autoOff.offTemp.temp   = ao["offTemp"]["temp"]    | 0.0f;
			}

			// motion
			up.motion.pir.enabled        = jp["motion"]["pir"]["enabled"]        | false;
			up.motion.pir.hold_sec       = jp["motion"]["pir"]["hold_sec"]       | 0;
			up.motion.ble.enabled        = jp["motion"]["ble"]["enabled"]        | false;
			up.motion.ble.rssi_threshold = jp["motion"]["ble"]["rssi_threshold"] | -70;
			up.motion.ble.hold_sec       = jp["motion"]["ble"]["hold_sec"]       | 0;
		}
		return true;
	}

	static bool C10_saveUserProfiles(const ST_A10_UserProfileConfig_t& p_cfg) {
		JsonDocument d;
		for (uint8_t v_i=0; v_i<p_cfg.count; v_i++) {
			const ST_A10_UserProfile_t& up = p_cfg.items[v_i];
			JsonObject jp = d["userProfiles"]["profiles"][v_i];

			jp["profileNo"]      = up.profileNo;
			jp["name"]           = up.name;
			jp["enabled"]        = up.enabled;
			jp["repeatSegments"] = up.repeatSegments;

			for (uint8_t v_k=0; v_k<up.segCount; v_k++) {
				const ST_A10_OpSegment_t& sg = up.segments[v_k];
				JsonObject jseg = jp["segments"][v_k];
				jseg["segNo"]       = sg.segNo;
				jseg["on_minutes"]  = sg.on_minutes;
				jseg["off_minutes"] = sg.off_minutes;
				jseg["mode"]        = sg.mode;
				jseg["presetCode"]  = sg.presetCode;
				jseg["styleCode"]   = sg.styleCode;

				if (sg.mode[0] == 'P') {
					JsonObject adj = jseg["adjust"];
					adj["wind_intensity"]   = sg.adjust.wind_intensity;
					adj["wind_variability"] = sg.adjust.wind_variability;
					adj["gust_frequency"]   = sg.adjust.gust_frequency;
					adj["fan_limit"]        = sg.adjust.fan_limit;
					adj["min_fan"]          = sg.adjust.min_fan;
				}
				jseg["fixed_speed"] = sg.fixed_speed;
			}

			JsonObject ao = jp["autoOff"];
			ao["timer"]["enabled"] = up.autoOff.timer.enabled;
			ao["timer"]["minutes"] = up.autoOff.timer.minutes;
			ao["offTime"]["enabled"] = up.autoOff.offTime.enabled;
			ao["offTime"]["time"]    = up.autoOff.offTime.time;
			ao["offTemp"]["enabled"] = up.autoOff.offTemp.enabled;
			ao["offTemp"]["temp"]    = up.autoOff.offTemp.temp;

			jp["motion"]["pir"]["enabled"]        = up.motion.pir.enabled;
			jp["motion"]["pir"]["hold_sec"]       = up.motion.pir.hold_sec;
			jp["motion"]["ble"]["enabled"]        = up.motion.ble.enabled;
			jp["motion"]["ble"]["rssi_threshold"] = up.motion.ble.rssi_threshold;
			jp["motion"]["ble"]["hold_sec"]       = up.motion.ble.hold_sec;
		}
		return C10_ioSaveJson(A10_Const::CFG_USER_PROFILES_FILE, A10_Const::CFG_USER_PROFILES_FILE_BAK, d);
	}

static void C10_toJson_UserProfiles(const ST_A10_UserProfileConfig_t& p, JsonDocument& d) {
    d["userProfiles"]["count"] = p.count;

    for (uint8_t i = 0; i < p.count; i++) {
        const ST_A10_UserProfile_t& up = p.items[i];
        JsonObject jp = d["userProfiles"]["profiles"][i];

        jp["profileNo"]      = up.profileNo;
        jp["name"]           = up.name;
        jp["enabled"]        = up.enabled;
        jp["repeatSegments"] = up.repeatSegments;
        jp["segCount"]       = up.segCount;

        // segments
        for (uint8_t k = 0; k < up.segCount; k++) {
            const ST_A10_OpSegment_t& sg = up.segments[k];
            JsonObject jseg = jp["segments"][k];

            jseg["segNo"]       = sg.segNo;
            jseg["on_minutes"]  = sg.on_minutes;
            jseg["off_minutes"] = sg.off_minutes;
            jseg["mode"]        = sg.mode;
            jseg["presetCode"]  = sg.presetCode;
            jseg["styleCode"]   = sg.styleCode;

            // adjust
            JsonObject adj = jseg["adjust"];
            adj["wind_intensity"]   = sg.adjust.wind_intensity;
            adj["wind_variability"] = sg.adjust.wind_variability;
            adj["gust_frequency"]   = sg.adjust.gust_frequency;
            adj["fan_limit"]        = sg.adjust.fan_limit;
            adj["min_fan"]          = sg.adjust.min_fan;

            jseg["fixed_speed"] = sg.fixed_speed;
        }

        // autoOff
        JsonObject ao = jp["autoOff"];
        ao["timer"]["enabled"]  = up.autoOff.timer.enabled;
        ao["timer"]["minutes"]  = up.autoOff.timer.minutes;
        ao["offTime"]["enabled"]= up.autoOff.offTime.enabled;
        ao["offTime"]["time"]   = up.autoOff.offTime.time;
        ao["offTemp"]["enabled"]= up.autoOff.offTemp.enabled;
        ao["offTemp"]["temp"]   = up.autoOff.offTemp.temp;

        // motion
        jp["motion"]["pir"]["enabled"]        = up.motion.pir.enabled;
        jp["motion"]["pir"]["hold_sec"]       = up.motion.pir.hold_sec;
        jp["motion"]["ble"]["enabled"]        = up.motion.ble.enabled;
        jp["motion"]["ble"]["rssi_threshold"] = up.motion.ble.rssi_threshold;
        jp["motion"]["ble"]["hold_sec"]       = up.motion.ble.hold_sec;
    }
}

	// =====================================================
	// Wind 해석 유틸 (CT10, S10에서 사용)
	// =====================================================
	static int16_t C10_findPresetIndexByCode(const ST_A10_WindProfileDict_t& p_dict,
											 const char* p_code) {
		if (!p_code || !p_code[0]) return -1;
		for (uint8_t v_i=0; v_i<p_dict.presetCount; v_i++) {
			if (strcasecmp(p_dict.presets[v_i].code, p_code) == 0) return (int16_t)v_i;
		}
		return -1;
	}

	static int16_t C10_findStyleIndexByCode(const ST_A10_WindProfileDict_t& p_dict,
											const char* p_code) {
		if (!p_code || !p_code[0]) return -1;
		for (uint8_t v_i=0; v_i<p_dict.styleCount; v_i++) {
			if (strcasecmp(p_dict.styles[v_i].code, p_code) == 0) return (int16_t)v_i;
		}
		return -1;
	}

	static bool C10_resolveWindParams(
    const ST_A10_WindProfileDict_t& p_dict,
    const char* p_presetCode,
    const char* p_styleCode,
    const ST_A10_AdjustDelta_t* p_adj,
    ST_A10_ResolvedWind_t& p_out
) {
    int16_t v_pi = C10_findPresetIndexByCode(p_dict, p_presetCode);
    if (v_pi < 0) return false;

    const ST_A10_WindPresetDef_t& v_p = p_dict.presets[v_pi];
    float v_int  = v_p.base.wind_intensity;
    float v_var  = v_p.base.wind_variability;
    float v_gust = v_p.base.gust_frequency;
    float v_fl   = v_p.base.fan_limit;
    float v_min  = v_p.base.min_fan;
    float v_tL   = v_p.base.turbulence_length_scale;
    float v_tS   = v_p.base.turbulence_intensity_sigma;
    float v_thB  = v_p.base.thermal_bubble_strength;
    float v_thR  = v_p.base.thermal_bubble_radius;

    if (p_styleCode && p_styleCode[0]) {
        int16_t v_si = C10_findStyleIndexByCode(p_dict, p_styleCode);
        if (v_si >= 0) {
            const ST_A10_WindStyleDef_t& v_s = p_dict.styles[v_si];
            v_int  *= v_s.factor.intensity_factor;
            v_var  *= v_s.factor.variability_factor;
            v_gust *= v_s.factor.gust_factor;
            v_thB  *= v_s.factor.thermal_factor;  // Thermal strength scaling
        }
    }

    if (p_adj) {
        v_int  += p_adj->wind_intensity;
        v_var  += p_adj->wind_variability;
        v_gust += p_adj->gust_frequency;
        v_fl   += p_adj->fan_limit;
        v_min  += p_adj->min_fan;
    }

    p_out.wind_intensity             = constrain(v_int,  0.0f, 100.0f);
    p_out.wind_variability           = constrain(v_var,  0.0f, 100.0f);
    p_out.gust_frequency             = constrain(v_gust, 0.0f, 100.0f);
    p_out.fan_limit                  = constrain(v_fl,   0.0f, 100.0f);
    p_out.min_fan                    = constrain(v_min,  0.0f, 100.0f);
    p_out.turbulence_length_scale    = max(1.0f, v_tL);
    p_out.turbulence_intensity_sigma = max(0.0f, v_tS);
    p_out.thermal_bubble_strength    = max(0.1f, v_thB);
    p_out.thermal_bubble_radius      = max(1.0f, v_thR);
    return true;
}

	// =====================================================
	// All-in-One 초기 로드
	// =====================================================
	// ---------------------------------------------------
// ✅ 변경됨 : bool 반환, 전체 로드 결과 전달
// ---------------------------------------------------
static bool C10_loadAll(ST_A10_ConfigRoot& p_root) {
    bool v_ok = true;
    A10_resetToDefault(p_root);

    if (!C10_loadSystemConfig(p_root.system)) {
        CL_D10_Logger::log(EN_L10_LOG_WARN, "[C10] System load failed");
        v_ok = false;
    }

    p_root.wifi = new ST_A10_WifiConfig();
    if (!C10_loadWifiConfig(*p_root.wifi)) {
        A10_resetWifiDefault(*p_root.wifi);
        C10_saveWifiConfig(*p_root.wifi);
        v_ok = false;
    }

    p_root.motion = new ST_A10_MotionConfig();
    if (!C10_loadMotionConfig(*p_root.motion)) {
        A10_resetMotionDefault(*p_root.motion);
        C10_saveMotionConfig(*p_root.motion);
        v_ok = false;
    }

    p_root.windDict = new ST_A10_WindProfileDict_t();
    if (!C10_loadWindProfileDict(*p_root.windDict)) {
        A10_resetWindProfileDictDefault(*p_root.windDict);
        v_ok = false;
    }

    p_root.schedules = new ST_A10_ScheduleConfig();
    if (!C10_loadSchedules(*p_root.schedules)) {
        A10_resetSchedulesDefault(*p_root.schedules);
        C10_saveSchedules(*p_root.schedules);
        v_ok = false;
    }

    p_root.userProfiles = new ST_A10_UserProfileConfig_t();
    if (!C10_loadUserProfiles(*p_root.userProfiles)) {
        A10_resetUserProfilesDefault(*p_root.userProfiles);
        C10_saveUserProfiles(*p_root.userProfiles);
        v_ok = false;
    }

    CL_D10_Logger::log(EN_L10_LOG_INFO, "[C10] All configs loaded (result=%d)", v_ok);
    return v_ok;
}

// =====================================================
// 메모리 해제 (동적 섹션 정리)
// =====================================================
static void C10_freeAll(ST_A10_ConfigRoot& p_root) {
    if (p_root.wifi) {
        delete p_root.wifi;
        p_root.wifi = nullptr;
    }
    if (p_root.motion) {
        delete p_root.motion;
        p_root.motion = nullptr;
    }
    if (p_root.windDict) {
        delete p_root.windDict;
        p_root.windDict = nullptr;
    }
    if (p_root.schedules) {
        delete p_root.schedules;
        p_root.schedules = nullptr;
    }
    if (p_root.userProfiles) {
        delete p_root.userProfiles;
        p_root.userProfiles = nullptr;
    }

    CL_D10_Logger::log(EN_L10_LOG_INFO, "[C10] All config objects freed");
}


	static void C10_saveAll(const ST_A10_ConfigRoot& p_root) {
		C10_saveSystemConfig(p_root.system);
		if (p_root.wifi)         C10_saveWifiConfig(*p_root.wifi);
		if (p_root.motion)       C10_saveMotionConfig(*p_root.motion);
		if (p_root.schedules)    C10_saveSchedules(*p_root.schedules);
		if (p_root.userProfiles) C10_saveUserProfiles(*p_root.userProfiles);
		// windProfile는 보통 고정/펌웨어 내장이라 save 생략 또는 필요 시 추가
	}

// =====================================================
// All Config → JSON Export (선택적 섹션 포함)
// -----------------------------------------------------
//  - 모든 섹션 기본 출력
//  - 특정 섹션만 내보내려면 include* 인자 조합
// =====================================================
static void C10_toJson_All(
    const ST_A10_ConfigRoot& p,
    JsonDocument& d,
    bool includeSystem       = true,
    bool includeWifi         = true,
    bool includeMotion       = true,
    bool includeSchedules    = true,
    bool includeUserProfiles = true
) {
    if (includeSystem)       C10_toJson_System(p.system, d);
    if (includeWifi && p.wifi)
        C10_toJson_Wifi(*p.wifi, d);
    if (includeMotion && p.motion)
        C10_toJson_Motion(*p.motion, d);
    if (includeSchedules && p.schedules)
        C10_toJson_Schedules(*p.schedules, d);
    if (includeUserProfiles && p.userProfiles)
        C10_toJson_UserProfiles(*p.userProfiles, d);

    CL_D10_Logger::log(
        EN_L10_LOG_DEBUG,
        "[C10] Config export → JSON (sys=%d wifi=%d motion=%d sch=%d up=%d)",
        includeSystem, includeWifi, includeMotion, includeSchedules, includeUserProfiles
    );
}


};

inline ST_A10_ConfigRoot g_A10_config_root;
