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

	static bool ioSaveJson(const char*		   p_path,
						   const char*		   p_bak,
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
							  JsonDocument&				 d) {
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

	/* =====================================================
	 * Motion
	 * ===================================================== */
	static bool loadMotionConfig(ST_A10_MotionConfig& p) {
		JsonDocument d;
		if (!ioLoadJson(A10_Const::CFG_MOTION_FILE,
						A10_Const::CFG_MOTION_FILE_BAK,
						d)) {
			A10_resetMotionDefault(p);
			return false;
		}
		JsonObjectConst j = d["motion"];

		p.enabled	   = j["enabled"] | true;
		p.pir.enabled  = j["pir"]["enabled"] | true;
		p.pir.hold_sec = j["pir"]["hold_sec"] | 120;

		p.ble.enabled = j["ble"]["enabled"] | true;

		p.ble.rssi.on			  = j["ble"]["rssi"]["on"] | -65;
		p.ble.rssi.off			  = j["ble"]["rssi"]["off"] | -75;
		p.ble.rssi.avg_count	  = j["ble"]["rssi"]["avg_count"] | 8;
		p.ble.rssi.persist_count  = j["ble"]["rssi"]["persist_count"] | 5;
		p.ble.rssi.exit_delay_sec = j["ble"]["rssi"]["exit_delay_sec"] | 12;

		p.ble.trusted_count = 0;
		if (j["ble"]["trusted_devices"].is<JsonArrayConst>()) {
			JsonArrayConst v_arr =
				j["ble"]["trusted_devices"].as<JsonArrayConst>();
			for (JsonObjectConst v_js : v_arr) {
				if (p.ble.trusted_count >= A10_Const::MAX_BLE_DEVICES)
					break;

				ST_A10_BLETrustedDevice& v_d =
					p.ble.trusted_devices[p.ble.trusted_count++];

				strlcpy(v_d.alias,
						v_js["alias"] | "",
						sizeof(v_d.alias));
				strlcpy(v_d.name,
						v_js["name"] | "",
						sizeof(v_d.name));
				strlcpy(v_d.mac,
						v_js["mac"] | "",
						sizeof(v_d.mac));
				strlcpy(v_d.manuf_prefix,
						v_js["manuf_prefix"] | "",
						sizeof(v_d.manuf_prefix));
				v_d.prefix_len = v_js["prefix_len"] | 0;
				v_d.enabled	   = v_js["enabled"] | true;
			}
		}
		return true;
	}

	static bool saveMotionConfig(const ST_A10_MotionConfig& p) {
		JsonDocument d;

		d["motion"]["enabled"]		   = p.enabled;
		d["motion"]["pir"]["enabled"]  = p.pir.enabled;
		d["motion"]["pir"]["hold_sec"] = p.pir.hold_sec;

		d["motion"]["ble"]["enabled"]				 = p.ble.enabled;
		d["motion"]["ble"]["rssi"]["on"]			 = p.ble.rssi.on;
		d["motion"]["ble"]["rssi"]["off"]			 = p.ble.rssi.off;
		d["motion"]["ble"]["rssi"]["avg_count"]		 = p.ble.rssi.avg_count;
		d["motion"]["ble"]["rssi"]["persist_count"]	 = p.ble.rssi.persist_count;
		d["motion"]["ble"]["rssi"]["exit_delay_sec"] = p.ble.rssi.exit_delay_sec;

		for (uint8_t v_i = 0; v_i < p.ble.trusted_count; v_i++) {
			const ST_A10_BLETrustedDevice& v_d =
				p.ble.trusted_devices[v_i];
			JsonObject v_td =
				d["motion"]["ble"]["trusted_devices"][v_i];

			v_td["alias"]		 = v_d.alias;
			v_td["name"]		 = v_d.name;
			v_td["mac"]			 = v_d.mac;
			v_td["manuf_prefix"] = v_d.manuf_prefix;
			v_td["prefix_len"]	 = v_d.prefix_len;
			v_td["enabled"]		 = v_d.enabled;
		}

		return ioSaveJson(A10_Const::CFG_MOTION_FILE,
						  A10_Const::CFG_MOTION_FILE_BAK,
						  d);
	}

	static void toJson_Motion(const ST_A10_MotionConfig& p,
							  JsonObject&				 d) {
		d["motion"]["enabled"]		   = p.enabled;
		d["motion"]["pir"]["enabled"]  = p.pir.enabled;
		d["motion"]["pir"]["hold_sec"] = p.pir.hold_sec;

		d["motion"]["ble"]["enabled"]				 = p.ble.enabled;
		d["motion"]["ble"]["rssi"]["on"]			 = p.ble.rssi.on;
		d["motion"]["ble"]["rssi"]["off"]			 = p.ble.rssi.off;
		d["motion"]["ble"]["rssi"]["avg_count"]		 = p.ble.rssi.avg_count;
		d["motion"]["ble"]["rssi"]["persist_count"]	 = p.ble.rssi.persist_count;
		d["motion"]["ble"]["rssi"]["exit_delay_sec"] = p.ble.rssi.exit_delay_sec;

		for (uint8_t i = 0; i < p.ble.trusted_count; i++) {
			const ST_A10_BLETrustedDevice& v_d =
				p.ble.trusted_devices[i];
			JsonObject v_td =
				d["motion"]["ble"]["trusted_devices"][i];

			v_td["alias"]		 = v_d.alias;
			v_td["name"]		 = v_d.name;
			v_td["mac"]			 = v_d.mac;
			v_td["manuf_prefix"] = v_d.manuf_prefix;
			v_td["prefix_len"]	 = v_d.prefix_len;
			v_td["enabled"]		 = v_d.enabled;
		}
	}

	/* =====================================================
	 * WindProfile Dict (cfg_dft_windProfile_025.json)
	 * ===================================================== */
	static bool loadWindProfileDict(ST_A10_WindProfileDict_t& p_dict) {
		JsonDocument d;
		if (!ioLoadJson(A10_Const::CFG_WIND_PROFILE_FILE,
						A10_Const::CFG_WIND_PROFILE_FILE_BAK,
						d)) {
			A10_resetWindProfileDictDefault(p_dict);
			return false;
		}
		JsonObjectConst j = d["windProfile"];

		p_dict.preset_count = 0;
		if (j["presets"].is<JsonArrayConst>()) {
			JsonArrayConst v_arr = j["presets"].as<JsonArrayConst>();
			for (JsonObjectConst v_js : v_arr) {
				if (p_dict.preset_count >= 16)
					break;

				ST_A10_PresetEntry_t& v_p =
					p_dict.presets[p_dict.preset_count++];

				strlcpy(v_p.name,
						v_js["name"] | "",
						sizeof(v_p.name));
				strlcpy(v_p.code,
						v_js["code"] | "",
						sizeof(v_p.code));

				JsonObjectConst v_b = v_js["base"];
				v_p.base.wind_intensity =
					v_b["wind_intensity"] | 70.0f;
				v_p.base.gust_frequency =
					v_b["gust_frequency"] | 40.0f;
				v_p.base.wind_variability =
					v_b["wind_variability"] | 50.0f;
				v_p.base.fan_limit =
					v_b["fan_limit"] | 95.0f;
				v_p.base.min_fan =
					v_b["min_fan"] | 10.0f;
				v_p.base.turbulence_length_scale =
					v_b["turbulence_length_scale"] | 40.0f;
				v_p.base.turbulence_intensity_sigma =
					v_b["turbulence_intensity_sigma"] | 0.5f;
				v_p.base.thermal_bubble_strength =
					v_b["thermal_bubble_strength"] | 2.0f;
				v_p.base.thermal_bubble_radius =
					v_b["thermal_bubble_radius"] | 18.0f;
			}
		}

		p_dict.style_count = 0;
		if (j["styles"].is<JsonArrayConst>()) {
			JsonArrayConst v_arr = j["styles"].as<JsonArrayConst>();
			for (JsonObjectConst v_js : v_arr) {
				if (p_dict.style_count >= 16)
					break;

				ST_A10_StyleEntry_t& v_s =
					p_dict.styles[p_dict.style_count++];

				strlcpy(v_s.name,
						v_js["name"] | "",
						sizeof(v_s.name));
				strlcpy(v_s.code,
						v_js["code"] | "",
						sizeof(v_s.code));

				JsonObjectConst v_f = v_js["factors"];
				v_s.factors.intensity_factor =
					v_f["intensity_factor"] | 1.0f;
				v_s.factors.variability_factor =
					v_f["variability_factor"] | 1.0f;
				v_s.factors.gust_factor =
					v_f["gust_factor"] | 1.0f;
				v_s.factors.thermal_factor =
					v_f["thermal_factor"] | 1.0f;
			}
		}
		return true;
	}

	/* =====================================================
	 * Schedules (cfg_schedules_025.json)
	 * ===================================================== */
	static bool loadSchedules(ST_A10_SchedulesRoot_t& p_cfg) {
		JsonDocument d;
		if (!ioLoadJson(A10_Const::CFG_SCHEDULES_FILE,
						A10_Const::CFG_SCHEDULES_FILE_BAK,
						d)) {
			A10_resetSchedulesDefault(p_cfg);
			return false;
		}

		JsonArrayConst arr = d["schedules"].as<JsonArrayConst>();
		p_cfg.count		   = 0;

		for (JsonObjectConst js : arr) {
			if (p_cfg.count >= A10_Const::MAX_SCHEDULES)
				break;

			ST_A10_ScheduleItem_t& s =
				p_cfg.items[p_cfg.count++];

			s.schNo = js["schNo"] | 0;
			strlcpy(s.name,
					js["name"] | "",
					sizeof(s.name));
			s.enabled = js["enabled"] | true;

			// period
			s.period.enabled =
				js["period"]["enabled"] | false;
			for (uint8_t v_d = 0; v_d < 7; v_d++) {
				s.period.days[v_d] =
					js["period"]["days"][v_d] | 1;
			}
			strlcpy(s.period.start_time,
					js["period"]["start_time"] | "00:00",
					sizeof(s.period.start_time));
			strlcpy(s.period.end_time,
					js["period"]["end_time"] | "23:59",
					sizeof(s.period.end_time));

			// segments
			s.seg_count = 0;
			if (js["segments"].is<JsonArrayConst>()) {
				JsonArrayConst segArr =
					js["segments"].as<JsonArrayConst>();
				for (JsonObjectConst jseg : segArr) {
					if (s.seg_count >= A10_Const::MAX_SEGMENTS_PER_SCHEDULE)
						break;

					ST_A10_ScheduleSegment_t& sg =
						s.segments[s.seg_count++];

					sg.segNo =
						jseg["segNo"] | 0;
					sg.on_minutes =
						jseg["on_minutes"] | 10;
					sg.off_minutes =
						jseg["off_minutes"] | 0;

					const char* v_mode =
						jseg["mode"] | "PRESET";
					sg.mode = A10_modeFromString(v_mode);

					strlcpy(sg.presetCode,
							jseg["presetCode"] | "",
							sizeof(sg.presetCode));
					strlcpy(sg.styleCode,
							jseg["styleCode"] | "",
							sizeof(sg.styleCode));

					memset(&sg.adjust, 0,
						   sizeof(sg.adjust));
					if (jseg["adjust"].is<JsonObjectConst>()) {
						JsonObjectConst adj =
							jseg["adjust"];
						sg.adjust.wind_intensity =
							adj["wind_intensity"] | 0.0f;
						sg.adjust.wind_variability =
							adj["wind_variability"] | 0.0f;
						sg.adjust.gust_frequency =
							adj["gust_frequency"] | 0.0f;
						sg.adjust.fan_limit =
							adj["fan_limit"] | 0.0f;
						sg.adjust.min_fan =
							adj["min_fan"] | 0.0f;
					}

					sg.fixed_speed =
						jseg["fixed_speed"] | 0.0f;
				}
			}

			// autoOff
			memset(&s.autoOff, 0, sizeof(s.autoOff));
			if (js["autoOff"].is<JsonObjectConst>()) {
				JsonObjectConst ao = js["autoOff"];
				s.autoOff.timer.enabled =
					ao["timer"]["enabled"] | false;
				s.autoOff.timer.minutes =
					ao["timer"]["minutes"] | 0;
				s.autoOff.offTime.enabled =
					ao["offTime"]["enabled"] | false;
				strlcpy(s.autoOff.offTime.time,
						ao["offTime"]["time"] | "",
						sizeof(s.autoOff.offTime.time));
				s.autoOff.offTemp.enabled =
					ao["offTemp"]["enabled"] | false;
				s.autoOff.offTemp.temp =
					ao["offTemp"]["temp"] | 0.0f;
			}

			// motion
			s.motion.pir.enabled =
				js["motion"]["pir"]["enabled"] | false;
			s.motion.pir.hold_sec =
				js["motion"]["pir"]["hold_sec"] | 0;
			s.motion.ble.enabled =
				js["motion"]["ble"]["enabled"] | false;
			s.motion.ble.rssi_threshold =
				js["motion"]["ble"]["rssi_threshold"] | -70;
			s.motion.ble.hold_sec =
				js["motion"]["ble"]["hold_sec"] | 0;
		}
		return true;
	}

	static bool saveSchedules(const ST_A10_SchedulesRoot_t& p_cfg) {
		JsonDocument d;

		for (uint8_t v_i = 0; v_i < p_cfg.count; v_i++) {
			const ST_A10_ScheduleItem_t& s =
				p_cfg.items[v_i];
			JsonObject js =
				d["schedules"][v_i];

			js["schNo"]	  = s.schNo;
			js["name"]	  = s.name;
			js["enabled"] = s.enabled;

			js["period"]["enabled"] =
				s.period.enabled;
			for (uint8_t v_d = 0; v_d < 7; v_d++) {
				js["period"]["days"][v_d] =
					s.period.days[v_d];
			}
			js["period"]["start_time"] =
				s.period.start_time;
			js["period"]["end_time"] =
				s.period.end_time;

			for (uint8_t v_k = 0;
				 v_k < s.seg_count;
				 v_k++) {
				const ST_A10_ScheduleSegment_t& sg =
					s.segments[v_k];
				JsonObject jseg =
					js["segments"][v_k];

				jseg["segNo"]		= sg.segNo;
				jseg["on_minutes"]	= sg.on_minutes;
				jseg["off_minutes"] = sg.off_minutes;
				jseg["mode"] =
					A10_modeToString(sg.mode);
				jseg["presetCode"] = sg.presetCode;
				jseg["styleCode"]  = sg.styleCode;

				JsonObject adj = jseg["adjust"];
				adj["wind_intensity"] =
					sg.adjust.wind_intensity;
				adj["wind_variability"] =
					sg.adjust.wind_variability;
				adj["gust_frequency"] =
					sg.adjust.gust_frequency;
				adj["fan_limit"] =
					sg.adjust.fan_limit;
				adj["min_fan"] =
					sg.adjust.min_fan;

				jseg["fixed_speed"] =
					sg.fixed_speed;
			}

			JsonObject ao = js["autoOff"];
			ao["timer"]["enabled"] =
				s.autoOff.timer.enabled;
			ao["timer"]["minutes"] =
				s.autoOff.timer.minutes;
			ao["offTime"]["enabled"] =
				s.autoOff.offTime.enabled;
			ao["offTime"]["time"] =
				s.autoOff.offTime.time;
			ao["offTemp"]["enabled"] =
				s.autoOff.offTemp.enabled;
			ao["offTemp"]["temp"] =
				s.autoOff.offTemp.temp;

			js["motion"]["pir"]["enabled"] =
				s.motion.pir.enabled;
			js["motion"]["pir"]["hold_sec"] =
				s.motion.pir.hold_sec;
			js["motion"]["ble"]["enabled"] =
				s.motion.ble.enabled;
			js["motion"]["ble"]["rssi_threshold"] =
				s.motion.ble.rssi_threshold;
			js["motion"]["ble"]["hold_sec"] =
				s.motion.ble.hold_sec;
		}

		return ioSaveJson(A10_Const::CFG_SCHEDULES_FILE,
						  A10_Const::CFG_SCHEDULES_FILE_BAK,
						  d);
	}

	static void toJson_Schedules(const ST_A10_SchedulesRoot_t& p,
								 JsonDocument&				   d) {
		d["schedules_count"] = p.count;

		for (uint8_t i = 0; i < p.count; i++) {
			const ST_A10_ScheduleItem_t& s =
				p.items[i];
			JsonObject js =
				d["schedules"][i];

			js["schNo"]	  = s.schNo;
			js["name"]	  = s.name;
			js["enabled"] = s.enabled;

			js["period"]["enabled"] =
				s.period.enabled;
			for (uint8_t d_i = 0; d_i < 7; d_i++) {
				js["period"]["days"][d_i] =
					s.period.days[d_i];
			}
			js["period"]["start_time"] =
				s.period.start_time;
			js["period"]["end_time"] =
				s.period.end_time;

			js["seg_count"] = s.seg_count;
			for (uint8_t k = 0;
				 k < s.seg_count;
				 k++) {
				const ST_A10_ScheduleSegment_t& sg =
					s.segments[k];
				JsonObject jseg =
					js["segments"][k];

				jseg["segNo"]		= sg.segNo;
				jseg["on_minutes"]	= sg.on_minutes;
				jseg["off_minutes"] = sg.off_minutes;
				jseg["mode"] =
					A10_modeToString(sg.mode);
				jseg["presetCode"] = sg.presetCode;
				jseg["styleCode"]  = sg.styleCode;

				JsonObject adj =
					jseg["adjust"];
				adj["wind_intensity"] =
					sg.adjust.wind_intensity;
				adj["wind_variability"] =
					sg.adjust.wind_variability;
				adj["gust_frequency"] =
					sg.adjust.gust_frequency;
				adj["fan_limit"] =
					sg.adjust.fan_limit;
				adj["min_fan"] =
					sg.adjust.min_fan;

				jseg["fixed_speed"] =
					sg.fixed_speed;
			}

			JsonObject ao =
				js["autoOff"];
			ao["timer"]["enabled"] =
				s.autoOff.timer.enabled;
			ao["timer"]["minutes"] =
				s.autoOff.timer.minutes;
			ao["offTime"]["enabled"] =
				s.autoOff.offTime.enabled;
			ao["offTime"]["time"] =
				s.autoOff.offTime.time;
			ao["offTemp"]["enabled"] =
				s.autoOff.offTemp.enabled;
			ao["offTemp"]["temp"] =
				s.autoOff.offTemp.temp;

			js["motion"]["pir"]["enabled"] =
				s.motion.pir.enabled;
			js["motion"]["pir"]["hold_sec"] =
				s.motion.pir.hold_sec;
			js["motion"]["ble"]["enabled"] =
				s.motion.ble.enabled;
			js["motion"]["ble"]["rssi_threshold"] =
				s.motion.ble.rssi_threshold;
			js["motion"]["ble"]["hold_sec"] =
				s.motion.ble.hold_sec;
		}
	}

	/* =====================================================
	 * UserProfiles (cfg_uzOpProfile_025_final.json)
	 * ===================================================== */
	static bool loadUserProfiles(ST_A10_UserProfilesRoot_t& p_cfg) {
		JsonDocument d;
		if (!ioLoadJson(A10_Const::CFG_USER_PROFILES_FILE,
						A10_Const::CFG_USER_PROFILES_FILE_BAK,
						d)) {
			A10_resetUserProfilesDefault(p_cfg);
			return false;
		}

		JsonArrayConst arr =
			d["userProfiles"]["profiles"].as<JsonArrayConst>();
		p_cfg.count = 0;

		for (JsonObjectConst jp : arr) {
			if (p_cfg.count >= A10_Const::MAX_USER_PROFILES)
				break;

			ST_A10_UserProfileItem_t& up =
				p_cfg.items[p_cfg.count++];

			up.profileNo =
				jp["profileNo"] | 0;
			strlcpy(up.name,
					jp["name"] | "",
					sizeof(up.name));
			up.enabled =
				jp["enabled"] | true;
			up.repeatSegments =
				jp["repeatSegments"] | true;

			// segments
			up.seg_count = 0;
			if (jp["segments"].is<JsonArrayConst>()) {
				JsonArrayConst sArr =
					jp["segments"].as<JsonArrayConst>();
				for (JsonObjectConst jseg : sArr) {
					if (up.seg_count >= A10_Const::MAX_SEGMENTS_PER_PROFILE)
						break;

					ST_A10_UserProfileSegment_t& sg =
						up.segments[up.seg_count++];

					sg.segNo =
						jseg["segNo"] | 0;
					sg.on_minutes =
						jseg["on_minutes"] | 10;
					sg.off_minutes =
						jseg["off_minutes"] | 0;

					const char* v_mode =
						jseg["mode"] | "PRESET";
					sg.mode = A10_modeFromString(v_mode);

					strlcpy(sg.presetCode,
							jseg["presetCode"] | "",
							sizeof(sg.presetCode));
					strlcpy(sg.styleCode,
							jseg["styleCode"] | "",
							sizeof(sg.styleCode));

					memset(&sg.adjust, 0,
						   sizeof(sg.adjust));
					if (jseg["adjust"].is<JsonObjectConst>()) {
						JsonObjectConst adj =
							jseg["adjust"];
						sg.adjust.wind_intensity =
							adj["wind_intensity"] | 0.0f;
						sg.adjust.wind_variability =
							adj["wind_variability"] | 0.0f;
						sg.adjust.gust_frequency =
							adj["gust_frequency"] | 0.0f;
						sg.adjust.fan_limit =
							adj["fan_limit"] | 0.0f;
						sg.adjust.min_fan =
							adj["min_fan"] | 0.0f;
					}

					sg.fixed_speed =
						jseg["fixed_speed"] | 0.0f;
				}
			}

			// autoOff
			memset(&up.autoOff, 0, sizeof(up.autoOff));
			if (jp["autoOff"].is<JsonObjectConst>()) {
				JsonObjectConst ao =
					jp["autoOff"];
				up.autoOff.timer.enabled =
					ao["timer"]["enabled"] | false;
				up.autoOff.timer.minutes =
					ao["timer"]["minutes"] | 0;
				up.autoOff.offTime.enabled =
					ao["offTime"]["enabled"] | false;
				strlcpy(up.autoOff.offTime.time,
						ao["offTime"]["time"] | "",
						sizeof(up.autoOff.offTime.time));
				up.autoOff.offTemp.enabled =
					ao["offTemp"]["enabled"] | false;
				up.autoOff.offTemp.temp =
					ao["offTemp"]["temp"] | 0.0f;
			}

			// motion
			up.motion.pir.enabled =
				jp["motion"]["pir"]["enabled"] | false;
			up.motion.pir.hold_sec =
				jp["motion"]["pir"]["hold_sec"] | 0;
			up.motion.ble.enabled =
				jp["motion"]["ble"]["enabled"] | false;
			up.motion.ble.rssi_threshold =
				jp["motion"]["ble"]["rssi_threshold"] | -70;
			up.motion.ble.hold_sec =
				jp["motion"]["ble"]["hold_sec"] | 0;
		}
		return true;
	}

	static bool saveUserProfiles(const ST_A10_UserProfilesRoot_t& p_cfg) {
		JsonDocument d;

		for (uint8_t v_i = 0; v_i < p_cfg.count; v_i++) {
			const ST_A10_UserProfileItem_t& up =
				p_cfg.items[v_i];
			JsonObject jp =
				d["userProfiles"]["profiles"][v_i];

			jp["profileNo"]		 = up.profileNo;
			jp["name"]			 = up.name;
			jp["enabled"]		 = up.enabled;
			jp["repeatSegments"] = up.repeatSegments;

			for (uint8_t v_k = 0;
				 v_k < up.seg_count;
				 v_k++) {
				const ST_A10_UserProfileSegment_t& sg =
					up.segments[v_k];
				JsonObject jseg =
					jp["segments"][v_k];

				jseg["segNo"]		= sg.segNo;
				jseg["on_minutes"]	= sg.on_minutes;
				jseg["off_minutes"] = sg.off_minutes;
				jseg["mode"] =
					A10_modeToString(sg.mode);
				jseg["presetCode"] = sg.presetCode;
				jseg["styleCode"]  = sg.styleCode;

				JsonObject adj =
					jseg["adjust"];
				adj["wind_intensity"] =
					sg.adjust.wind_intensity;
				adj["wind_variability"] =
					sg.adjust.wind_variability;
				adj["gust_frequency"] =
					sg.adjust.gust_frequency;
				adj["fan_limit"] =
					sg.adjust.fan_limit;
				adj["min_fan"] =
					sg.adjust.min_fan;

				jseg["fixed_speed"] =
					sg.fixed_speed;
			}

			JsonObject ao =
				jp["autoOff"];
			ao["timer"]["enabled"] =
				up.autoOff.timer.enabled;
			ao["timer"]["minutes"] =
				up.autoOff.timer.minutes;
			ao["offTime"]["enabled"] =
				up.autoOff.offTime.enabled;
			ao["offTime"]["time"] =
				up.autoOff.offTime.time;
			ao["offTemp"]["enabled"] =
				up.autoOff.offTemp.enabled;
			ao["offTemp"]["temp"] =
				up.autoOff.offTemp.temp;

			jp["motion"]["pir"]["enabled"] =
				up.motion.pir.enabled;
			jp["motion"]["pir"]["hold_sec"] =
				up.motion.pir.hold_sec;
			jp["motion"]["ble"]["enabled"] =
				up.motion.ble.enabled;
			jp["motion"]["ble"]["rssi_threshold"] =
				up.motion.ble.rssi_threshold;
			jp["motion"]["ble"]["hold_sec"] =
				up.motion.ble.hold_sec;
		}

		return ioSaveJson(A10_Const::CFG_USER_PROFILES_FILE,
						  A10_Const::CFG_USER_PROFILES_FILE_BAK,
						  d);
	}

	static void toJson_UserProfiles(const ST_A10_UserProfilesRoot_t& p,
									JsonDocument&					 d) {
		d["userProfiles"]["count"] = p.count;

		for (uint8_t i = 0; i < p.count; i++) {
			const ST_A10_UserProfileItem_t& up =
				p.items[i];
			JsonObject jp =
				d["userProfiles"]["profiles"][i];

			jp["profileNo"]		 = up.profileNo;
			jp["name"]			 = up.name;
			jp["enabled"]		 = up.enabled;
			jp["repeatSegments"] = up.repeatSegments;
			jp["seg_count"]		 = up.seg_count;

			for (uint8_t k = 0;
				 k < up.seg_count;
				 k++) {
				const ST_A10_UserProfileSegment_t& sg =
					up.segments[k];
				JsonObject jseg =
					jp["segments"][k];

				jseg["segNo"]		= sg.segNo;
				jseg["on_minutes"]	= sg.on_minutes;
				jseg["off_minutes"] = sg.off_minutes;
				jseg["mode"] =
					A10_modeToString(sg.mode);
				jseg["presetCode"] = sg.presetCode;
				jseg["styleCode"]  = sg.styleCode;

				JsonObject adj =
					jseg["adjust"];
				adj["wind_intensity"] =
					sg.adjust.wind_intensity;
				adj["wind_variability"] =
					sg.adjust.wind_variability;
				adj["gust_frequency"] =
					sg.adjust.gust_frequency;
				adj["fan_limit"] =
					sg.adjust.fan_limit;
				adj["min_fan"] =
					sg.adjust.min_fan;

				jseg["fixed_speed"] =
					sg.fixed_speed;
			}

			JsonObject ao =
				jp["autoOff"];
			ao["timer"]["enabled"] =
				up.autoOff.timer.enabled;
			ao["timer"]["minutes"] =
				up.autoOff.timer.minutes;
			ao["offTime"]["enabled"] =
				up.autoOff.offTime.enabled;
			ao["offTime"]["time"] =
				up.autoOff.offTime.time;
			ao["offTemp"]["enabled"] =
				up.autoOff.offTemp.enabled;
			ao["offTemp"]["temp"] =
				up.autoOff.offTemp.temp;

			jp["motion"]["pir"]["enabled"] =
				up.motion.pir.enabled;
			jp["motion"]["pir"]["hold_sec"] =
				up.motion.pir.hold_sec;
			jp["motion"]["ble"]["enabled"] =
				up.motion.ble.enabled;
			jp["motion"]["ble"]["rssi_threshold"] =
				up.motion.ble.rssi_threshold;
			jp["motion"]["ble"]["hold_sec"] =
				up.motion.ble.hold_sec;
		}
	}

	/* =====================================================
	 * Lazy-Load 전체 로드 / 해제 / 저장 / Export
	 * ===================================================== */
	static bool loadLazySection(const char*			 p_section,
								ST_A10_ConfigRoot_t& p_root) {
		if (strcmp(p_section, "wifi") == 0) {
			if (!p_root.wifi)
				p_root.wifi = new ST_A10_WifiConfig();
			return loadWifiConfig(*p_root.wifi);
		}
		if (strcmp(p_section, "motion") == 0) {
			if (!p_root.motion)
				p_root.motion = new ST_A10_MotionConfig();
			return loadMotionConfig(*p_root.motion);
		}
		if (strcmp(p_section, "schedules") == 0) {
			if (!p_root.schedules)
				p_root.schedules = new ST_A10_SchedulesRoot_t();
			return loadSchedules(*p_root.schedules);
		}
		if (strcmp(p_section, "userProfiles") == 0) {
			if (!p_root.userProfiles)
				p_root.userProfiles = new ST_A10_UserProfilesRoot_t();
			return loadUserProfiles(*p_root.userProfiles);
		}
		return false;
	}

	static bool loadAll(ST_A10_ConfigRoot_t& p_root) {
		bool v_ok = true;

		// 필수 섹션 객체 확보
		if (!p_root.system)
			p_root.system = new ST_A10_SystemConfig();
		if (!p_root.windDict)
			p_root.windDict = new ST_A10_WindProfileDict_t();
		if (!p_root.schedules)
			p_root.schedules = new ST_A10_SchedulesRoot_t();
		if (!p_root.userProfiles)
			p_root.userProfiles = new ST_A10_UserProfilesRoot_t();
		if (!p_root.wifi)
			p_root.wifi = new ST_A10_WifiConfig();
		if (!p_root.motion)
			p_root.motion = new ST_A10_MotionConfig();

		// 기본값
		A10_resetSystemDefault(*p_root.system);
		A10_resetWindProfileDictDefault(*p_root.windDict);
		A10_resetSchedulesDefault(*p_root.schedules);
		A10_resetUserProfilesDefault(*p_root.userProfiles);
		A10_resetWifiDefault(*p_root.wifi);
		A10_resetMotionDefault(*p_root.motion);

		// 실제 로드
		if (!loadSystemConfig(*p_root.system))
			v_ok = false;
		if (!loadWindProfileDict(*p_root.windDict))
			v_ok = false;
		if (!loadWifiConfig(*p_root.wifi))
			v_ok = false;
		if (!loadMotionConfig(*p_root.motion))
			v_ok = false;
		if (!loadSchedules(*p_root.schedules))
			v_ok = false;
		if (!loadUserProfiles(*p_root.userProfiles))
			v_ok = false;

		CL_D10_Logger::log(
			EN_L10_LOG_INFO,
			"[C10] Config loaded (all sections, result=%d)",
			v_ok);
		return v_ok;
	}

	static void freeLazySection(const char*			 p_section,
								ST_A10_ConfigRoot_t& p_root) {
		if (strcmp(p_section, "wifi") == 0 && p_root.wifi) {
			delete p_root.wifi;
			p_root.wifi = nullptr;
			return;
		}
		if (strcmp(p_section, "motion") == 0 && p_root.motion) {
			delete p_root.motion;
			p_root.motion = nullptr;
			return;
		}
		if (strcmp(p_section, "schedules") == 0 && p_root.schedules) {
			delete p_root.schedules;
			p_root.schedules = nullptr;
			return;
		}
		if (strcmp(p_section, "userProfiles") == 0 && p_root.userProfiles) {
			delete p_root.userProfiles;
			p_root.userProfiles = nullptr;
			return;
		}
	}

	static void freeAll(ST_A10_ConfigRoot_t& p_root) {
		if (p_root.system) {
			delete p_root.system;
			p_root.system = nullptr;
		}
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

		CL_D10_Logger::log(EN_L10_LOG_INFO,
						   "[C10] All config objects freed");
	}

	static void saveAll(const ST_A10_ConfigRoot_t& p_root) {
		if (p_root.system)
			saveSystemConfig(*p_root.system);
		if (p_root.wifi)
			saveWifiConfig(*p_root.wifi);
		if (p_root.motion)
			saveMotionConfig(*p_root.motion);
		if (p_root.schedules)
			saveSchedules(*p_root.schedules);
		if (p_root.userProfiles)
			saveUserProfiles(*p_root.userProfiles);
		// windDict는 일반적으로 펌웨어 내장 (필요 시 별도 save 함수 추가)
	}

	/* =====================================================
	 * All Config → JSON Export
	 * ===================================================== */
	static void toJson_All(
		const ST_A10_ConfigRoot_t& p,
		JsonDocument&			   d,
		bool					   includeSystem	   = true,
		bool					   includeWifi		   = true,
		bool					   includeMotion	   = true,
		bool					   includeSchedules	   = true,
		bool					   includeUserProfiles = true) {
		if (includeSystem && p.system)
			toJson_System(*p.system, d);
		if (includeWifi && p.wifi)
			toJson_Wifi(*p.wifi, d);
		if (includeMotion && p.motion){
			JsonObject v_motion = d["motion"].to<JsonObject>();
            toJson_Motion(*p.motion, v_motion);
			//toJson_Motion(*p.motion, d["motion"].to<JsonObject>());
		}
		if (includeSchedules && p.schedules)
			toJson_Schedules(*p.schedules, d);
		if (includeUserProfiles && p.userProfiles)
			toJson_UserProfiles(*p.userProfiles, d);

		CL_D10_Logger::log(
			EN_L10_LOG_DEBUG,
			"[C10] Config export → JSON (sys=%d wifi=%d motion=%d sch=%d up=%d)",
			includeSystem, includeWifi,
			includeMotion, includeSchedules,
			includeUserProfiles);
	}

	/* =====================================================
	 * PATCH 기반 JSON 부분 업데이트 (예시)
	 * ===================================================== */
	static bool patchConfigFromJson(const char*			p_section,
									const JsonDocument& p_patch) {
		if (strcmp(p_section, "system") == 0) {
			ST_A10_SystemConfig v_cfg;
			if (!loadSystemConfig(v_cfg))
				return false;

			JsonObjectConst j_sys = p_patch["system"];
			if (!j_sys.isNull()) {
				JsonObjectConst j_log = j_sys["logging"];
				if (!j_log.isNull()) {
					const char* v_lv =
						j_log["level"] | v_cfg.system.logging.level;
					strlcpy(v_cfg.system.logging.level,
							v_lv,
							sizeof(v_cfg.system.logging.level));
				}
				JsonObjectConst j_sec = j_sys["security"];
				if (!j_sec.isNull()) {
					const char* v_key =
						j_sec["api_key"] | v_cfg.security.api_key;
					strlcpy(v_cfg.security.api_key,
							v_key,
							sizeof(v_cfg.security.api_key));
				}
			}
			return saveSystemConfig(v_cfg);
		}

		if (strcmp(p_section, "wifi") == 0) {
			ST_A10_WifiConfig v_cfg;
			if (!loadWifiConfig(v_cfg))
				return false;

			JsonObjectConst j_wifi = p_patch["wifi"];
			if (!j_wifi.isNull()) {
				JsonObjectConst j_ap = j_wifi["ap"];
				if (!j_ap.isNull()) {
					const char* v_ssid =
						j_ap["ssid"] | v_cfg.ap.ssid;
					const char* v_pwd =
						j_ap["password"] | v_cfg.ap.password;
					strlcpy(v_cfg.ap.ssid,
							v_ssid,
							sizeof(v_cfg.ap.ssid));
					strlcpy(v_cfg.ap.password,
							v_pwd,
							sizeof(v_cfg.ap.password));
				}
			}
			return saveWifiConfig(v_cfg);
		}

		// motion / schedules / userProfiles 도 동일 패턴으로 확장 가능
		return false;
	}

	/* =====================================================
	 * Factory Reset (기본값 파일 기반 복구)
	 *  - CFG_DEFAULT_FILE 관련 상수는 A10_Const 내 정의 가정
	 * ===================================================== */
	static bool factoryResetFromDefault() {
#ifdef CFG_DEFAULT_FILE_EXISTS
		JsonDocument v_def;
		if (!ioLoadJson(A10_Const::CFG_DEFAULT_FILE,
						A10_Const::CFG_DEFAULT_FILE_BAK,
						v_def)) {
			CL_D10_Logger::log(
				EN_L10_LOG_ERROR,
				"[C10] Default file missing: %s",
				A10_Const::CFG_DEFAULT_FILE);
			return false;
		}

		if (v_def["system"].is<JsonObjectConst>()) {
			JsonDocument v_sys;
			v_sys["system"] = v_def["system"];
			ioSaveJson(A10_Const::CFG_SYSTEM_FILE,
					   A10_Const::CFG_SYSTEM_FILE_BAK,
					   v_sys);
		}
		if (v_def["wifi"].is<JsonObjectConst>()) {
			JsonDocument v_wifi;
			v_wifi["wifi"] = v_def["wifi"];
			ioSaveJson(A10_Const::CFG_WIFI_FILE,
					   A10_Const::CFG_WIFI_FILE_BAK,
					   v_wifi);
		}
		if (v_def["motion"].is<JsonObjectConst>()) {
			JsonDocument v_motion;
			v_motion["motion"] = v_def["motion"];
			ioSaveJson(A10_Const::CFG_MOTION_FILE,
					   A10_Const::CFG_MOTION_FILE_BAK,
					   v_motion);
		}
		if (v_def["schedules"].is<JsonArrayConst>()) {
			JsonDocument v_sch;
			v_sch["schedules"] = v_def["schedules"];
			ioSaveJson(A10_Const::CFG_SCHEDULES_FILE,
					   A10_Const::CFG_SCHEDULES_FILE_BAK,
					   v_sch);
		}
		if (v_def["userProfiles"].is<JsonObjectConst>()) {
			JsonDocument v_up;
			v_up["userProfiles"] = v_def["userProfiles"];
			ioSaveJson(A10_Const::CFG_USER_PROFILES_FILE,
					   A10_Const::CFG_USER_PROFILES_FILE_BAK,
					   v_up);
		}

		CL_D10_Logger::log(
			EN_L10_LOG_INFO,
			"[C10] Factory reset completed from default");
		return true;
#else
		// 기본값 파일 미사용 시 false
		return false;
#endif
	}
};	// class CL_C10_ConfigManager

// 전역 Config Root (포인터 보관용)
inline ST_A10_ConfigRoot_t g_A10_config_root;
