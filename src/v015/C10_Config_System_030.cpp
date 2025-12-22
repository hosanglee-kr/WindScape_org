/*
 * ------------------------------------------------------
 * 소스명 : C10_Config_System_030.cpp
 * 모듈 약어 : C10
 * 모듈명 : Smart Nature Wind Configuration Manager - System/Wifi/Motion
 * ------------------------------------------------------
 * 기능 요약:
 *  - System / Wifi / Motion 설정 Load/Save
 *  - System / Wifi / Motion 설정 JSON Patch 적용
 *  - System / Wifi / Motion 설정 JSON Export
 * ------------------------------------------------------
 * [구현 규칙]
 *  - 항상 소스 시작 주석 체계 유지
 *  - ArduinoJson v7.x.x 사용 (v6 이하 사용 금지)
 *  - JsonDocument 단일 타입만 사용
 *  - createNestedArray/Object/containsKey 사용 금지
 *  - memset + strlcpy 기반 안전 초기화
 *  - 주석/필드명은 JSON 구조와 동일하게 유지
 *  - 모듈별 헤더(h) + 목적물별 cpp 분리 구성 (Core/System/Schedule)
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

#include "C10_Config_030.h"

// =====================================================
// 2-1. 목적물별 Load 구현 (System/Wifi/Motion)
// =====================================================
bool CL_C10_ConfigManager::loadSystemConfig(ST_A20_SystemConfig& p_cfg) {
	JsonDocument v_doc;

	const char*	 v_cfgJsonPath = nullptr;

	// [수정] std::string::empty() -> strlen() 또는 첫 문자 비교로 변경
	if (s_cfgJsonFileMap.system[0] != '\0') {
		v_cfgJsonPath = s_cfgJsonFileMap.system;
	} else {
		CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] loadSystemConfig: s_cfgJsonFileMap.system is empty");
		return false;
	}

	if (!ioLoadJson(v_cfgJsonPath, v_doc)) {  // bak는 자동 .bak 처리
		CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] loadSystemConfig: ioLoadJson failed (%s)", v_cfgJsonPath);
		// A20_resetSystemDefault(p_cfg);
		return false;
	}

	JsonObjectConst j = v_doc.as<JsonObjectConst>();

	strlcpy(p_cfg.meta.version, j["meta"]["version"] | A20_Const::FW_VERSION, sizeof(p_cfg.meta.version));
	strlcpy(p_cfg.meta.device_name, j["meta"]["device_name"] | "SmartNatureWind", sizeof(p_cfg.meta.device_name));
	strlcpy(p_cfg.meta.last_update, j["meta"]["last_update"] | "", sizeof(p_cfg.meta.last_update));

	strlcpy(p_cfg.system.logging.level, j["system"]["logging"]["level"] | "INFO", sizeof(p_cfg.system.logging.level));
	p_cfg.system.logging.max_entries	 = j["system"]["logging"]["max_entries"] | 300;

	p_cfg.hw.fan_pwm.pin				 = j["hw"]["fan_pwm"]["pin"] | 6;
	p_cfg.hw.fan_pwm.channel			 = j["hw"]["fan_pwm"]["channel"] | 0;
	p_cfg.hw.fan_pwm.freq				 = j["hw"]["fan_pwm"]["freq"] | 25000;
	p_cfg.hw.fan_pwm.res				 = j["hw"]["fan_pwm"]["res"] | 10;

	// ----------------------------------------------------------------
	// [추가] hw.fanConfig 로드
	// ----------------------------------------------------------------
	// >> [추가] hw.fanConfig 로드
	p_cfg.hw.fanConfig.startPercentMin	 = j["hw"]["fanConfig"]["startPercentMin"] | 18;
	p_cfg.hw.fanConfig.comfortPercentMin = j["hw"]["fanConfig"]["comfortPercentMin"] | 22;
	p_cfg.hw.fanConfig.comfortPercentMax = j["hw"]["fanConfig"]["comfortPercentMax"] | 65;
	p_cfg.hw.fanConfig.hardPercentMax	 = j["hw"]["fanConfig"]["hardPercentMax"] | 90;

	p_cfg.hw.pir.enabled				 = j["hw"]["pir"]["enabled"] | true;
	p_cfg.hw.pir.pin					 = j["hw"]["pir"]["pin"] | 13;
	p_cfg.hw.pir.debounce_sec			 = j["hw"]["pir"]["debounce_sec"] | 5;

	p_cfg.hw.ble.enabled				 = j["hw"]["ble"]["enabled"] | true;
	p_cfg.hw.ble.scan_interval			 = j["hw"]["ble"]["scan_interval"] | 5;

	strlcpy(p_cfg.security.api_key, j["security"]["api_key"] | "", sizeof(p_cfg.security.api_key));

	strlcpy(p_cfg.time.ntp_server, j["time"]["ntp_server"] | "pool.ntp.org", sizeof(p_cfg.time.ntp_server));
	strlcpy(p_cfg.time.timezone, j["time"]["timezone"] | "Asia/Seoul", sizeof(p_cfg.time.timezone));
	p_cfg.time.sync_interval_min = j["time"]["sync_interval_min"] | 60;

	return true;
}

bool CL_C10_ConfigManager::loadWifiConfig(ST_A20_WifiConfig& p_cfg) {
	JsonDocument d;

	const char*	 v_cfgJsonPath = nullptr;

	if (s_cfgJsonFileMap.wifi[0] != '\0') {
		v_cfgJsonPath = s_cfgJsonFileMap.wifi;
	} else {
		CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] loadWifiConfig: s_cfgJsonFileMap.wifi failed");
		return false;
	}
	if (!ioLoadJson(v_cfgJsonPath, d)) {  // bak는 자동 .bak 처리
		CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] loadWifiConfig: ioLoadJson failed (%s)", v_cfgJsonPath);
		return false;
	}

	JsonObjectConst j = d["wifi"];

	p_cfg.wifiMode	  = (EN_A20_WIFI_MODE_t)(j["wifiMode"] | EN_A20_WIFI_MODE_AP_STA);
	strlcpy(p_cfg.wifiModeDesc, j["wifiModeDesc"] | "0=AP,1=STA,2=AP+STA", sizeof(p_cfg.wifiModeDesc));

	strlcpy(p_cfg.ap.ssid, j["ap"]["ssid"] | "NatureWind", sizeof(p_cfg.ap.ssid));
	strlcpy(p_cfg.ap.password, j["ap"]["password"] | "2540", sizeof(p_cfg.ap.password));

	p_cfg.sta_count = 0;
	if (j["sta"].is<JsonArrayConst>()) {
		JsonArrayConst v_arr = j["sta"].as<JsonArrayConst>();
		for (JsonObjectConst v_js : v_arr) {
			if (p_cfg.sta_count >= A20_Const::MAX_STA_NETWORKS)
				break;
			strlcpy(p_cfg.sta[p_cfg.sta_count].ssid, v_js["ssid"] | "", sizeof(p_cfg.sta[p_cfg.sta_count].ssid));
			strlcpy(p_cfg.sta[p_cfg.sta_count].pass, v_js["pass"] | "", sizeof(p_cfg.sta[p_cfg.sta_count].pass));
			p_cfg.sta_count++;
		}
	}
	return true;
}

bool CL_C10_ConfigManager::loadMotionConfig(ST_A20_MotionConfig& p_cfg) {
	JsonDocument d;

	const char*	 v_cfgJsonPath = nullptr;

	if (s_cfgJsonFileMap.motion[0] != '\0') {
		v_cfgJsonPath = s_cfgJsonFileMap.motion;
	} else {
		CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] loadMotionConfig: s_cfgJsonFileMap.motion failed");
		return false;
	}
	if (!ioLoadJson(v_cfgJsonPath, d)) {  // bak는 자동 .bak 처리
		CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] loadMotionConfig: ioLoadJson failed (%s)", v_cfgJsonPath);
		return false;
	}

	JsonObjectConst j			  = d["motion"];

	p_cfg.enabled				  = j["enabled"] | true;
	p_cfg.pir.enabled			  = j["pir"]["enabled"] | true;
	p_cfg.pir.hold_sec			  = j["pir"]["hold_sec"] | 120;

	p_cfg.ble.enabled			  = j["ble"]["enabled"] | true;
	p_cfg.ble.rssi.on			  = j["ble"]["rssi"]["on"] | -65;
	p_cfg.ble.rssi.off			  = j["ble"]["rssi"]["off"] | -75;
	p_cfg.ble.rssi.avg_count	  = j["ble"]["rssi"]["avg_count"] | 8;
	p_cfg.ble.rssi.persist_count  = j["ble"]["rssi"]["persist_count"] | 5;
	p_cfg.ble.rssi.exit_delay_sec = j["ble"]["rssi"]["exit_delay_sec"] | 12;

	p_cfg.ble.trusted_count		  = 0;
	if (j["ble"]["trusted_devices"].is<JsonArrayConst>()) {
		JsonArrayConst v_arr = j["ble"]["trusted_devices"].as<JsonArrayConst>();
		for (JsonObjectConst v_js : v_arr) {
			if (p_cfg.ble.trusted_count >= A20_Const::MAX_BLE_DEVICES)
				break;

			ST_A20_BLETrustedDevice& v_d = p_cfg.ble.trusted_devices[p_cfg.ble.trusted_count++];

			strlcpy(v_d.alias, v_js["alias"] | "", sizeof(v_d.alias));
			strlcpy(v_d.name, v_js["name"] | "", sizeof(v_d.name));
			strlcpy(v_d.mac, v_js["mac"] | "", sizeof(v_d.mac));
			strlcpy(v_d.manuf_prefix, v_js["manuf_prefix"] | "", sizeof(v_d.manuf_prefix));
			v_d.prefix_len = v_js["prefix_len"] | 0;
			v_d.enabled	   = v_js["enabled"] | true;
		}
	}
	return true;
}

// =====================================================
// 2-2. 목적물별 Save 구현 (System/Wifi/Motion)
// =====================================================
bool CL_C10_ConfigManager::saveSystemConfig(const ST_A20_SystemConfig& p_cfg) {
	JsonDocument v;

	v["meta"]["version"]					  = p_cfg.meta.version;
	v["meta"]["device_name"]				  = p_cfg.meta.device_name;
	v["meta"]["last_update"]				  = p_cfg.meta.last_update;

	v["system"]["logging"]["level"]			  = p_cfg.system.logging.level;
	v["system"]["logging"]["max_entries"]	  = p_cfg.system.logging.max_entries;

	v["hw"]["fan_pwm"]["pin"]				  = p_cfg.hw.fan_pwm.pin;
	v["hw"]["fan_pwm"]["channel"]			  = p_cfg.hw.fan_pwm.channel;
	v["hw"]["fan_pwm"]["freq"]				  = p_cfg.hw.fan_pwm.freq;
	v["hw"]["fan_pwm"]["res"]				  = p_cfg.hw.fan_pwm.res;

	// >> [추가] hw.fanConfig 저장
	v["hw"]["fanConfig"]["startPercentMin"]	  = p_cfg.hw.fanConfig.startPercentMin;
	v["hw"]["fanConfig"]["comfortPercentMin"] = p_cfg.hw.fanConfig.comfortPercentMin;
	v["hw"]["fanConfig"]["comfortPercentMax"] = p_cfg.hw.fanConfig.comfortPercentMax;
	v["hw"]["fanConfig"]["hardPercentMax"]	  = p_cfg.hw.fanConfig.hardPercentMax;

	v["hw"]["pir"]["enabled"]				  = p_cfg.hw.pir.enabled;
	v["hw"]["pir"]["pin"]					  = p_cfg.hw.pir.pin;
	v["hw"]["pir"]["debounce_sec"]			  = p_cfg.hw.pir.debounce_sec;

	v["hw"]["ble"]["enabled"]				  = p_cfg.hw.ble.enabled;
	v["hw"]["ble"]["scan_interval"]			  = p_cfg.hw.ble.scan_interval;

	v["security"]["api_key"]				  = p_cfg.security.api_key;

	v["time"]["ntp_server"]					  = p_cfg.time.ntp_server;
	v["time"]["timezone"]					  = p_cfg.time.timezone;
	v["time"]["sync_interval_min"]			  = p_cfg.time.sync_interval_min;

	//char v_bakPath[A20_Const::LEN_NAME + 5];  // ".bak" 4자 + null 1자 여유
	//snprintf(v_bakPath, sizeof(v_bakPath), "%s.bak", s_cfgJsonFileMap.system);

	return ioSaveJson(s_cfgJsonFileMap.system, v);
}

bool CL_C10_ConfigManager::saveWifiConfig(const ST_A20_WifiConfig& p_cfg) {
	JsonDocument d;

	d["wifi"]["wifiMode"]		= p_cfg.wifiMode;
	d["wifi"]["wifiModeDesc"]	= p_cfg.wifiModeDesc;
	d["wifi"]["ap"]["ssid"]		= p_cfg.ap.ssid;
	d["wifi"]["ap"]["password"] = p_cfg.ap.password;

	for (uint8_t v_i = 0; v_i < p_cfg.sta_count; v_i++) {
		d["wifi"]["sta"][v_i]["ssid"] = p_cfg.sta[v_i].ssid;
		d["wifi"]["sta"][v_i]["pass"] = p_cfg.sta[v_i].pass;
	}

	//char v_bakPath[A20_Const::LEN_NAME + 5];  // ".bak" 4자 + null 1자 여유
	//snprintf(v_bakPath, sizeof(v_bakPath), "%s.bak", s_cfgJsonFileMap.wifi);

	return ioSaveJson(s_cfgJsonFileMap.wifi, d);
}

bool CL_C10_ConfigManager::saveMotionConfig(const ST_A20_MotionConfig& p_cfg) {
	JsonDocument d;

	d["motion"]["enabled"]						 = p_cfg.enabled;
	d["motion"]["pir"]["enabled"]				 = p_cfg.pir.enabled;
	d["motion"]["pir"]["hold_sec"]				 = p_cfg.pir.hold_sec;

	d["motion"]["ble"]["enabled"]				 = p_cfg.ble.enabled;
	d["motion"]["ble"]["rssi"]["on"]			 = p_cfg.ble.rssi.on;
	d["motion"]["ble"]["rssi"]["off"]			 = p_cfg.ble.rssi.off;
	d["motion"]["ble"]["rssi"]["avg_count"]		 = p_cfg.ble.rssi.avg_count;
	d["motion"]["ble"]["rssi"]["persist_count"]	 = p_cfg.ble.rssi.persist_count;
	d["motion"]["ble"]["rssi"]["exit_delay_sec"] = p_cfg.ble.rssi.exit_delay_sec;

	for (uint8_t v_i = 0; v_i < p_cfg.ble.trusted_count; v_i++) {
		const ST_A20_BLETrustedDevice& v_d	= p_cfg.ble.trusted_devices[v_i];
		JsonObject					   v_td = d["motion"]["ble"]["trusted_devices"][v_i];

		v_td["alias"]						= v_d.alias;
		v_td["name"]						= v_d.name;
		v_td["mac"]							= v_d.mac;
		v_td["manuf_prefix"]				= v_d.manuf_prefix;
		v_td["prefix_len"]					= v_d.prefix_len;
		v_td["enabled"]						= v_d.enabled;
	}

	//char v_bakPath[A20_Const::LEN_NAME + 5];  // ".bak" 4자 + null 1자 여유
	//snprintf(v_bakPath, sizeof(v_bakPath), "%s.bak", s_cfgJsonFileMap.motion);

	return ioSaveJson(s_cfgJsonFileMap.motion, d);
}

// =====================================================
// 4. JSON Patch (System/Wifi/Motion)
// =====================================================
bool CL_C10_ConfigManager::patchSystemFromJson(ST_A20_SystemConfig& p_config, const JsonDocument& p_patch) {
	bool v_changed = false;

	C10_MUTEX_ACQUIRE_BOOL();

	JsonObjectConst j_sys	   = p_patch["system"];
	JsonObjectConst j_sec_root = p_patch["security"];
	JsonObjectConst j_hw_root  = p_patch["hw"];	 // H/W root 객체 추가

	if (j_sys.isNull() && j_sec_root.isNull()) {
		C10_MUTEX_RELEASE();
		return false;
	}

	// system.logging
	if (!j_sys.isNull()) {
		JsonObjectConst j_log = j_sys["logging"];
		if (!j_log.isNull()) {
			const char* v_lv = j_log["level"] | "";
			if (j_log["max_entries"].is<uint16_t>()) {
				uint16_t v_max = j_log["max_entries"];
				if (v_max != p_config.system.logging.max_entries) {
					p_config.system.logging.max_entries = v_max;
					v_changed							= true;
				}
			}

			if (strlen(v_lv) > 0 && strcmp(v_lv, p_config.system.logging.level) != 0) {
				strlcpy(p_config.system.logging.level, v_lv, sizeof(p_config.system.logging.level));
				v_changed = true;
			}
		}
	}

	// security.api_key
	if (!j_sec_root.isNull()) {
		const char* v_key = j_sec_root["api_key"] | "";
		if (strlen(v_key) > 0 && strcmp(v_key, p_config.security.api_key) != 0) {
			strlcpy(p_config.security.api_key, v_key, sizeof(p_config.security.api_key));
			v_changed = true;
		}
	}

	// >> [추가] hw.fanConfig 패치
	if (!j_hw_root.isNull()) {
		JsonObjectConst j_fan = j_hw_root["fanConfig"];
		if (!j_fan.isNull()) {
			// startPercentMin
			if (j_fan["startPercentMin"].is<uint8_t>()) {
				uint8_t v_val = j_fan["startPercentMin"];
				if (v_val != p_config.hw.fanConfig.startPercentMin) {
					p_config.hw.fanConfig.startPercentMin = v_val;
					v_changed							  = true;
				}
			}
			// comfortPercentMin
			if (j_fan["comfortPercentMin"].is<uint8_t>()) {
				uint8_t v_val = j_fan["comfortPercentMin"];
				if (v_val != p_config.hw.fanConfig.comfortPercentMin) {
					p_config.hw.fanConfig.comfortPercentMin = v_val;
					v_changed								= true;
				}
			}
			// comfortPercentMax
			if (j_fan["comfortPercentMax"].is<uint8_t>()) {
				uint8_t v_val = j_fan["comfortPercentMax"];
				if (v_val != p_config.hw.fanConfig.comfortPercentMax) {
					p_config.hw.fanConfig.comfortPercentMax = v_val;
					v_changed								= true;
				}
			}
			// hardPercentMax
			if (j_fan["hardPercentMax"].is<uint8_t>()) {
				uint8_t v_val = j_fan["hardPercentMax"];
				if (v_val != p_config.hw.fanConfig.hardPercentMax) {
					p_config.hw.fanConfig.hardPercentMax = v_val;
					v_changed							 = true;
				}
			}
		}
	}

	if (v_changed) {
		_dirty_system = true;
		CL_D10_Logger::log(EN_L10_LOG_INFO, "[C10] System config patched (Memory Only). Dirty=true");
	}

	C10_MUTEX_RELEASE();
	return v_changed;
}

bool CL_C10_ConfigManager::patchWifiFromJson(ST_A20_WifiConfig& p_config, const JsonDocument& p_patch) {
	bool v_changed = false;

	C10_MUTEX_ACQUIRE_BOOL();

	JsonObjectConst j_wifi = p_patch["wifi"];
	if (j_wifi.isNull()) {
		C10_MUTEX_RELEASE();
		return false;
	}

	// wifiMode
	if (j_wifi["wifiMode"].is<uint8_t>()) {
		uint8_t v_mode = j_wifi["wifiMode"];
		if (v_mode != p_config.wifiMode) {
			if (v_mode >= EN_A20_WIFI_MODE_AP && v_mode <= EN_A20_WIFI_MODE_AP_STA) {
				p_config.wifiMode = (EN_A20_WIFI_MODE_t)v_mode;
				v_changed		  = true;
			} else {
				CL_D10_Logger::log(EN_L10_LOG_WARN, "[C10] Invalid wifiMode value: %d", v_mode);
			}
		}
	}

	// ap
	JsonObjectConst j_ap = j_wifi["ap"];
	if (!j_ap.isNull()) {
		const char* v_ssid = j_ap["ssid"] | "";
		if (strlen(v_ssid) > 0 && strcmp(v_ssid, p_config.ap.ssid) != 0) {
			strlcpy(p_config.ap.ssid, v_ssid, sizeof(p_config.ap.ssid));
			v_changed = true;
		}

		const char* v_pwd = j_ap["password"] | "";
		if (strlen(v_pwd) > 0 && strcmp(v_pwd, p_config.ap.password) != 0) {
			strlcpy(p_config.ap.password, v_pwd, sizeof(p_config.ap.password));
			v_changed = true;
		}
	}

	// sta 배열 전체 덮어쓰기(put)
	JsonArrayConst j_sta = j_wifi["sta"].as<JsonArrayConst>();
	if (!j_sta.isNull()) {
		p_config.sta_count = 0;
		for (JsonObjectConst v_js : j_sta) {
			if (p_config.sta_count >= A20_Const::MAX_STA_NETWORKS)
				break;

			ST_A20_STANetwork_t& v_net = p_config.sta[p_config.sta_count];

			strlcpy(v_net.ssid, v_js["ssid"] | "", sizeof(v_net.ssid));
			strlcpy(v_net.pass, v_js["pass"] | "", sizeof(v_net.pass));
			p_config.sta_count++;
		}
		v_changed = true;
		CL_D10_Logger::log(EN_L10_LOG_DEBUG, "[C10] WiFi STA array fully replaced.");
	}

	if (v_changed) {
		_dirty_wifi = true;
		CL_D10_Logger::log(EN_L10_LOG_INFO, "[C10] WiFi config patched (Memory Only). Dirty=true");
	}

	C10_MUTEX_RELEASE();
	return v_changed;
}

bool CL_C10_ConfigManager::patchMotionFromJson(ST_A20_MotionConfig& p_config, const JsonDocument& p_patch) {
	C10_MUTEX_ACQUIRE_BOOL();

	bool			v_changed = false;

	JsonObjectConst j_motion  = p_patch["motion"];
	if (j_motion.isNull()) {
		C10_MUTEX_RELEASE();
		return false;
	}

	// enabled
	if (j_motion["enabled"].is<bool>() && j_motion["enabled"].as<bool>() != p_config.enabled) {
		p_config.enabled = j_motion["enabled"];
		v_changed		 = true;
	}

	// pir
	JsonObjectConst j_pir = j_motion["pir"];
	if (!j_pir.isNull()) {
		if (j_pir["enabled"].is<bool>() && j_pir["enabled"].as<bool>() != p_config.pir.enabled) {
			p_config.pir.enabled = j_pir["enabled"];
			v_changed			 = true;
		}
		if (j_pir["hold_sec"].is<uint16_t>()) {
			if (j_pir["hold_sec"].as<uint16_t>() != p_config.pir.hold_sec) {
				p_config.pir.hold_sec = j_pir["hold_sec"];
				v_changed			  = true;
			}
		}
	}

	// ble / rssi / trusted_devices
	JsonObjectConst j_ble = j_motion["ble"];
	if (!j_ble.isNull()) {
		if (j_ble["enabled"].is<bool>() && j_ble["enabled"].as<bool>() != p_config.ble.enabled) {
			p_config.ble.enabled = j_ble["enabled"];
			v_changed			 = true;
		}

		JsonObjectConst j_rssi = j_ble["rssi"];
		if (!j_rssi.isNull()) {
			if (j_rssi["on"].is<int8_t>() && j_rssi["on"].as<int8_t>() != p_config.ble.rssi.on) {
				p_config.ble.rssi.on = j_rssi["on"];
				v_changed			 = true;
			}
			if (j_rssi["off"].is<int8_t>() && j_rssi["off"].as<int8_t>() != p_config.ble.rssi.off) {
				p_config.ble.rssi.off = j_rssi["off"];
				v_changed			  = true;
			}
			if (j_rssi["avg_count"].is<uint8_t>() && j_rssi["avg_count"].as<uint8_t>() != p_config.ble.rssi.avg_count) {
				p_config.ble.rssi.avg_count = j_rssi["avg_count"];
				v_changed					= true;
			}
			if (j_rssi["persist_count"].is<uint8_t>() && j_rssi["persist_count"].as<uint8_t>() != p_config.ble.rssi.persist_count) {
				p_config.ble.rssi.persist_count = j_rssi["persist_count"];
				v_changed						= true;
			}
			if (j_rssi["exit_delay_sec"].is<uint16_t>() && j_rssi["exit_delay_sec"].as<uint16_t>() != p_config.ble.rssi.exit_delay_sec) {
				p_config.ble.rssi.exit_delay_sec = j_rssi["exit_delay_sec"];
				v_changed						 = true;
			}
		}

		JsonArrayConst j_devices = j_ble["trusted_devices"].as<JsonArrayConst>();
		if (!j_devices.isNull()) {
			p_config.ble.trusted_count = 0;
			for (JsonObjectConst j_dev : j_devices) {
				if (p_config.ble.trusted_count >= A20_Const::MAX_BLE_DEVICES)
					break;

				ST_A20_BLETrustedDevice& v_d = p_config.ble.trusted_devices[p_config.ble.trusted_count];

				strlcpy(v_d.alias, j_dev["alias"] | "", sizeof(v_d.alias));
				strlcpy(v_d.name, j_dev["name"] | "", sizeof(v_d.name));
				strlcpy(v_d.mac, j_dev["mac"] | "", sizeof(v_d.mac));
				strlcpy(v_d.manuf_prefix, j_dev["manuf_prefix"] | "", sizeof(v_d.manuf_prefix));
				v_d.prefix_len = j_dev["prefix_len"] | 0;
				v_d.enabled	   = j_dev["enabled"] | true;

				p_config.ble.trusted_count++;
			}
			v_changed = true;
			CL_D10_Logger::log(EN_L10_LOG_DEBUG, "[C10] Motion Trusted Devices array fully replaced.");
		}
	}

	if (v_changed) {
		_dirty_motion = true;
		CL_D10_Logger::log(EN_L10_LOG_INFO, "[C10] motion config patched (Memory Only). Dirty=true");
	}

	C10_MUTEX_RELEASE();
	return v_changed;
}

// =====================================================
// 3-1. JSON Export (System/Wifi/Motion)
// =====================================================
void CL_C10_ConfigManager::toJson_System(const ST_A20_SystemConfig& p, JsonDocument& d) {
	d["meta"]["version"]					  = p.meta.version;
	d["meta"]["device_name"]				  = p.meta.device_name;
	d["meta"]["last_update"]				  = p.meta.last_update;

	d["system"]["logging"]["level"]			  = p.system.logging.level;
	d["system"]["logging"]["max_entries"]	  = p.system.logging.max_entries;

	d["hw"]["fan_pwm"]["pin"]				  = p.hw.fan_pwm.pin;
	d["hw"]["fan_pwm"]["channel"]			  = p.hw.fan_pwm.channel;
	d["hw"]["fan_pwm"]["freq"]				  = p.hw.fan_pwm.freq;
	d["hw"]["fan_pwm"]["res"]				  = p.hw.fan_pwm.res;

	// >> [추가] hw.fanConfig Export
	d["hw"]["fanConfig"]["startPercentMin"]	  = p.hw.fanConfig.startPercentMin;
	d["hw"]["fanConfig"]["comfortPercentMin"] = p.hw.fanConfig.comfortPercentMin;
	d["hw"]["fanConfig"]["comfortPercentMax"] = p.hw.fanConfig.comfortPercentMax;
	d["hw"]["fanConfig"]["hardPercentMax"]	  = p.hw.fanConfig.hardPercentMax;

	d["hw"]["pir"]["enabled"]				  = p.hw.pir.enabled;
	d["hw"]["pir"]["pin"]					  = p.hw.pir.pin;
	d["hw"]["pir"]["debounce_sec"]			  = p.hw.pir.debounce_sec;

	d["hw"]["ble"]["enabled"]				  = p.hw.ble.enabled;
	d["hw"]["ble"]["scan_interval"]			  = p.hw.ble.scan_interval;

	d["security"]["api_key"]				  = p.security.api_key;

	d["time"]["ntp_server"]					  = p.time.ntp_server;
	d["time"]["timezone"]					  = p.time.timezone;
	d["time"]["sync_interval_min"]			  = p.time.sync_interval_min;
}

void CL_C10_ConfigManager::toJson_Wifi(const ST_A20_WifiConfig& p, JsonDocument& d) {
	d["wifi"]["wifiMode"]		= p.wifiMode;
	d["wifi"]["wifiModeDesc"]	= p.wifiModeDesc;
	d["wifi"]["ap"]["ssid"]		= p.ap.ssid;
	d["wifi"]["ap"]["password"] = p.ap.password;

	for (uint8_t i = 0; i < p.sta_count; i++) {
		d["wifi"]["sta"][i]["ssid"] = p.sta[i].ssid;
		d["wifi"]["sta"][i]["pass"] = p.sta[i].pass;
	}
}

void CL_C10_ConfigManager::toJson_Motion(const ST_A20_MotionConfig& p, JsonDocument& d) {
	d["motion"]["enabled"]						 = p.enabled;
	d["motion"]["pir"]["enabled"]				 = p.pir.enabled;
	d["motion"]["pir"]["hold_sec"]				 = p.pir.hold_sec;

	d["motion"]["ble"]["enabled"]				 = p.ble.enabled;
	d["motion"]["ble"]["rssi"]["on"]			 = p.ble.rssi.on;
	d["motion"]["ble"]["rssi"]["off"]			 = p.ble.rssi.off;
	d["motion"]["ble"]["rssi"]["avg_count"]		 = p.ble.rssi.avg_count;
	d["motion"]["ble"]["rssi"]["persist_count"]	 = p.ble.rssi.persist_count;
	d["motion"]["ble"]["rssi"]["exit_delay_sec"] = p.ble.rssi.exit_delay_sec;

	for (uint8_t i = 0; i < p.ble.trusted_count; i++) {
		const ST_A20_BLETrustedDevice& v_d	= p.ble.trusted_devices[i];
		JsonObject					   v_td = d["motion"]["ble"]["trusted_devices"][i];

		v_td["alias"]						= v_d.alias;
		v_td["name"]						= v_d.name;
		v_td["mac"]							= v_d.mac;
		v_td["manuf_prefix"]				= v_d.manuf_prefix;
		v_td["prefix_len"]					= v_d.prefix_len;
		v_td["enabled"]						= v_d.enabled;
	}
}
