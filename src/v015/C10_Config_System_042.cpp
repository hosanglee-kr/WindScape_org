/*
 * ------------------------------------------------------
 * 소스명 : C10_Config_System_042.cpp
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

#include "C10_Config_041.h"

#include <cstring>

// =====================================================
// 내부 Helper: key 호환 (camelCase 우선, snake_case fallback)
// =====================================================
static const char* C10_getStr2(JsonObjectConst p_obj, const char* p_k1, const char* p_k2, const char* p_def) {
	if (p_obj.isNull()) return p_def;

	if (p_obj[p_k1].is<const char*>()) {
		const char* v = p_obj[p_k1].as<const char*>();
		return v ? v : p_def;
	}
	if (p_obj[p_k2].is<const char*>()) {
		const char* v = p_obj[p_k2].as<const char*>();
		return v ? v : p_def;
	}
	return p_def;
}

template <typename T>
static T C10_getNum2(JsonObjectConst p_obj, const char* p_k1, const char* p_k2, T p_def) {
	if (p_obj.isNull()) return p_def;
	if (p_obj[p_k1].is<T>()) return p_obj[p_k1].as<T>();
	if (p_obj[p_k2].is<T>()) return p_obj[p_k2].as<T>();
	return p_def;
}

static bool C10_getBool2(JsonObjectConst p_obj, const char* p_k1, const char* p_k2, bool p_def) {
	if (p_obj.isNull()) return p_def;
	if (p_obj[p_k1].is<bool>()) return p_obj[p_k1].as<bool>();
	if (p_obj[p_k2].is<bool>()) return p_obj[p_k2].as<bool>();
	return p_def;
}

// =====================================================
// 2-1. 목적물별 Load 구현 (System/Wifi/Motion)
// =====================================================
bool CL_C10_ConfigManager::loadSystemConfig(ST_A20_SystemConfig_t& p_cfg) {
	JsonDocument v_doc;

	const char* v_cfgJsonPath = nullptr;
	if (s_cfgJsonFileMap.system[0] != '\0') {
		v_cfgJsonPath = s_cfgJsonFileMap.system;
	} else {
		CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] loadSystemConfig: s_cfgJsonFileMap.system is empty");
		return false;
	}

	if (!ioLoadJson(v_cfgJsonPath, v_doc)) {
		CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] loadSystemConfig: ioLoadJson failed (%s)", v_cfgJsonPath);
		return false;
	}

	JsonObjectConst j_root = v_doc.as<JsonObjectConst>();

	JsonObjectConst j_meta = j_root["meta"].as<JsonObjectConst>();
	JsonObjectConst j_sys  = j_root["system"].as<JsonObjectConst>();

	// hw는 루트가 기본, 추후 system.hw 로 내려가도 대응
	JsonObjectConst j_hw = j_root["hw"].as<JsonObjectConst>();
	if (j_hw.isNull() && !j_sys.isNull()) {
		j_hw = j_sys["hw"].as<JsonObjectConst>();
	}

	if (j_sys.isNull() || j_hw.isNull()) {
		CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] loadSystemConfig: missing 'system' or 'hw'");
		return false;
	}

	// meta (없어도 기본값)
	if (j_meta.isNull()) {
		CL_D10_Logger::log(EN_L10_LOG_WARN, "[C10] loadSystemConfig: missing 'meta' (defaults used)");
	}
	strlcpy(p_cfg.meta.version,
	        C10_getStr2(j_meta, "version", "version", A20_Const::FW_VERSION),
	        sizeof(p_cfg.meta.version));
	strlcpy(p_cfg.meta.deviceName,
	        C10_getStr2(j_meta, "deviceName", "deviceName", "SmartNatureWind"),
	        sizeof(p_cfg.meta.deviceName));
	strlcpy(p_cfg.meta.lastUpdate,
	        C10_getStr2(j_meta, "lastUpdate", "lastUpdate", ""),
	        sizeof(p_cfg.meta.lastUpdate));

	// system.logging
	JsonObjectConst j_log = j_sys["logging"].as<JsonObjectConst>();
	if (j_log.isNull()) {
		CL_D10_Logger::log(EN_L10_LOG_WARN, "[C10] loadSystemConfig: missing system.logging (defaults used)");
	}
	strlcpy(p_cfg.system.logging.level,
	        C10_getStr2(j_log, "level", "level", "INFO"),
	        sizeof(p_cfg.system.logging.level));
	p_cfg.system.logging.maxEntries = C10_getNum2<uint16_t>(j_log, "maxEntries", "maxEntries", 300);

	// hw.fanPwm (기존 fanPwm 호환)
	JsonObjectConst j_pwm = j_hw["fanPwm"].as<JsonObjectConst>();
	if (j_pwm.isNull()) j_pwm = j_hw["fanPwm"].as<JsonObjectConst>();
	if (j_pwm.isNull()) {
		CL_D10_Logger::log(EN_L10_LOG_WARN, "[C10] loadSystemConfig: missing hw.fanPwm (defaults used)");
	}
	p_cfg.hw.fanPwm.pin     = C10_getNum2<uint8_t>(j_pwm, "pin", "pin", 6);
	p_cfg.hw.fanPwm.channel = C10_getNum2<uint8_t>(j_pwm, "channel", "channel", 0);
	p_cfg.hw.fanPwm.freq    = C10_getNum2<uint32_t>(j_pwm, "freq", "freq", 25000);
	p_cfg.hw.fanPwm.res     = C10_getNum2<uint8_t>(j_pwm, "res", "res", 10);

	// hw.fanConfig (이미 camelCase)
	JsonObjectConst j_fcfg = j_hw["fanConfig"].as<JsonObjectConst>();
	if (j_fcfg.isNull()) {
		CL_D10_Logger::log(EN_L10_LOG_WARN, "[C10] loadSystemConfig: missing hw.fanConfig (defaults used)");
	}
	p_cfg.hw.fanConfig.startPercentMin   = j_fcfg["startPercentMin"] | 18;
	p_cfg.hw.fanConfig.comfortPercentMin = j_fcfg["comfortPercentMin"] | 22;
	p_cfg.hw.fanConfig.comfortPercentMax = j_fcfg["comfortPercentMax"] | 65;
	p_cfg.hw.fanConfig.hardPercentMax    = j_fcfg["hardPercentMax"] | 90;

	// hw.pir (debounceSec camelCase + debounceSec 호환)
	JsonObjectConst j_pir = j_hw["pir"].as<JsonObjectConst>();
	if (j_pir.isNull()) {
		CL_D10_Logger::log(EN_L10_LOG_WARN, "[C10] loadSystemConfig: missing hw.pir (defaults used)");
	}
	p_cfg.hw.pir.enabled      = C10_getBool2(j_pir, "enabled", "enabled", true);
	p_cfg.hw.pir.pin          = C10_getNum2<uint8_t>(j_pir, "pin", "pin", 13);
	p_cfg.hw.pir.debounceSec = C10_getNum2<uint16_t>(j_pir, "debounceSec", "debounceSec", 5);
	p_cfg.hw.pir.holdSec      = C10_getNum2<uint16_t>(j_pir, "holdSec", "holdSec", 120);

	// hw.tempHum (intervalSec camelCase + intervalSec 호환)
	JsonObjectConst j_th = j_hw["tempHum"].as<JsonObjectConst>();
	if (j_th.isNull()) {
		CL_D10_Logger::log(EN_L10_LOG_WARN, "[C10] loadSystemConfig: missing hw.tempHum (defaults used)");
	}
	p_cfg.hw.tempHum.enabled      = C10_getBool2(j_th, "enabled", "enabled", true);
	strlcpy(p_cfg.hw.tempHum.type,
	        C10_getStr2(j_th, "type", "type", "DHT22"),
	        sizeof(p_cfg.hw.tempHum.type));
	p_cfg.hw.tempHum.pin          = C10_getNum2<uint8_t>(j_th, "pin", "pin", 23);
	p_cfg.hw.tempHum.intervalSec = C10_getNum2<uint16_t>(j_th, "intervalSec", "intervalSec", 30);

	// hw.ble (scanInterval camelCase + scanInterval 호환)
	JsonObjectConst j_ble = j_hw["ble"].as<JsonObjectConst>();
	if (j_ble.isNull()) {
		CL_D10_Logger::log(EN_L10_LOG_WARN, "[C10] loadSystemConfig: missing hw.ble (defaults used)");
	}
	p_cfg.hw.ble.enabled       = C10_getBool2(j_ble, "enabled", "enabled", true);
	p_cfg.hw.ble.scanInterval = C10_getNum2<uint16_t>(j_ble, "scanInterval", "scanInterval", 5);

	// security (apiKey camelCase + apiKey 호환)
	JsonObjectConst j_sec = j_root["security"].as<JsonObjectConst>();
	if (j_sec.isNull()) {
		CL_D10_Logger::log(EN_L10_LOG_WARN, "[C10] loadSystemConfig: missing security (defaults used)");
	}
	strlcpy(p_cfg.security.apiKey,
	        C10_getStr2(j_sec, "apiKey", "apiKey", ""),
	        sizeof(p_cfg.security.apiKey));

	// time (ntpServer/syncIntervalMin camelCase + ntpServer/syncIntervalMin 호환)
	JsonObjectConst j_time = j_root["time"].as<JsonObjectConst>();
	if (j_time.isNull()) {
		CL_D10_Logger::log(EN_L10_LOG_WARN, "[C10] loadSystemConfig: missing time (defaults used)");
	}
	strlcpy(p_cfg.time.ntpServer,
	        C10_getStr2(j_time, "ntpServer", "ntpServer", "pool.ntp.org"),
	        sizeof(p_cfg.time.ntpServer));
	strlcpy(p_cfg.time.timezone,
	        C10_getStr2(j_time, "timezone", "timezone", "Asia/Seoul"),
	        sizeof(p_cfg.time.timezone));
	p_cfg.time.syncIntervalMin = C10_getNum2<uint16_t>(j_time, "syncIntervalMin", "syncIntervalMin", 60);

	return true;
}

bool CL_C10_ConfigManager::loadWifiConfig(ST_A20_WifiConfig_t& p_cfg) {
	JsonDocument d;

	const char* v_cfgJsonPath = nullptr;
	if (s_cfgJsonFileMap.wifi[0] != '\0') {
		v_cfgJsonPath = s_cfgJsonFileMap.wifi;
	} else {
		CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] loadWifiConfig: s_cfgJsonFileMap.wifi is empty");
		return false;
	}

	if (!ioLoadJson(v_cfgJsonPath, d)) {
		CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] loadWifiConfig: ioLoadJson failed (%s)", v_cfgJsonPath);
		return false;
	}

	JsonObjectConst j = d["wifi"].as<JsonObjectConst>();
	if (j.isNull()) {
		CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] loadWifiConfig: missing 'wifi' object");
		return false;
	}

	p_cfg.wifiMode = (EN_A20_WIFI_MODE_t)(j["wifiMode"] | EN_A20_WIFI_MODE_AP_STA);
	strlcpy(p_cfg.wifiModeDesc, j["wifiModeDesc"] | "0=AP,1=STA,2=AP+STA", sizeof(p_cfg.wifiModeDesc));

	// ap: JSON은 pass 로 통일하되, pass 호환
	strlcpy(p_cfg.ap.ssid, j["ap"]["ssid"] | "NatureWind", sizeof(p_cfg.ap.ssid));

	const char* v_apPass = C10_getStr2(j["ap"].as<JsonObjectConst>(), "pass", "pass", "2540");
	strlcpy(p_cfg.ap.pass, v_apPass, sizeof(p_cfg.ap.pass));

	// sta: pass 로 통일 (pass는 호환)
	p_cfg.staCount = 0;
	if (j["sta"].is<JsonArrayConst>()) {
		JsonArrayConst v_arr = j["sta"].as<JsonArrayConst>();
		for (JsonObjectConst v_js : v_arr) {
			if (p_cfg.staCount >= A20_Const::MAX_STA_NETWORKS) break;

			strlcpy(p_cfg.sta[p_cfg.staCount].ssid, v_js["ssid"] | "", sizeof(p_cfg.sta[p_cfg.staCount].ssid));

			const char* v_pass = C10_getStr2(v_js, "pass", "pass", "");
			strlcpy(p_cfg.sta[p_cfg.staCount].pass, v_pass, sizeof(p_cfg.sta[p_cfg.staCount].pass));

			p_cfg.staCount++;
		}
	}

	return true;
}

bool CL_C10_ConfigManager::loadMotionConfig(ST_A20_MotionConfig_t& p_cfg) {
	JsonDocument d;

	const char* v_cfgJsonPath = nullptr;
	if (s_cfgJsonFileMap.motion[0] != '\0') {
		v_cfgJsonPath = s_cfgJsonFileMap.motion;
	} else {
		CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] loadMotionConfig: s_cfgJsonFileMap.motion is empty");
		return false;
	}

	if (!ioLoadJson(v_cfgJsonPath, d)) {
		CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] loadMotionConfig: ioLoadJson failed (%s)", v_cfgJsonPath);
		return false;
	}

	JsonObjectConst j = d["motion"].as<JsonObjectConst>();
	if (j.isNull()) {
		CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] loadMotionConfig: missing 'motion' object");
		return false;
	}

	// p_cfg.enabled     = j["enabled"] | true;
	p_cfg.pir.enabled = j["pir"]["enabled"] | true;
	p_cfg.pir.holdSec = j["pir"]["holdSec"] | 120;

	p_cfg.ble.enabled = j["ble"]["enabled"] | true;

	// rssi: avgCount/persistCount/exitDelaySec camelCase + snake_case 호환
	JsonObjectConst r = j["ble"]["rssi"].as<JsonObjectConst>();
	p_cfg.ble.rssi.on             = C10_getNum2<int8_t>(r, "on", "on", -65);
	p_cfg.ble.rssi.off            = C10_getNum2<int8_t>(r, "off", "off", -75);
	p_cfg.ble.rssi.avgCount      = C10_getNum2<uint8_t>(r, "avgCount", "avgCount", 8);
	p_cfg.ble.rssi.persistCount  = C10_getNum2<uint8_t>(r, "persistCount", "persistCount", 5);
	p_cfg.ble.rssi.exitDelaySec = C10_getNum2<uint16_t>(r, "exitDelaySec", "exitDelaySec", 12);

	// trustedDevices camelCase + trustedDevices 호환
	p_cfg.ble.trustedCount = 0;

	JsonArrayConst v_arr = j["ble"]["trustedDevices"].as<JsonArrayConst>();
	if (v_arr.isNull()) v_arr = j["ble"]["trustedDevices"].as<JsonArrayConst>();

	if (!v_arr.isNull()) {
		for (JsonObjectConst v_js : v_arr) {
			if (p_cfg.ble.trustedCount >= A20_Const::MAX_BLE_DEVICES) break;

			ST_A20_BLETrustedDevice_t& v_d = p_cfg.ble.trustedDevices[p_cfg.ble.trustedCount++];

			strlcpy(v_d.alias, v_js["alias"] | "", sizeof(v_d.alias));
			strlcpy(v_d.name, v_js["name"] | "", sizeof(v_d.name));
			strlcpy(v_d.mac, v_js["mac"] | "", sizeof(v_d.mac));

			const char* v_mp = C10_getStr2(v_js, "manufPrefix", "manufPrefix", "");
			strlcpy(v_d.manufPrefix, v_mp, sizeof(v_d.manufPrefix));

			v_d.prefixLen = C10_getNum2<uint8_t>(v_js, "prefixLen", "prefixLen", 0);
			v_d.enabled    = C10_getBool2(v_js, "enabled", "enabled", true);
		}
	}

	return true;
}

// =====================================================
// 2-2. 목적물별 Save 구현 (System/Wifi/Motion) - camelCase 저장
// =====================================================
bool CL_C10_ConfigManager::saveSystemConfig(const ST_A20_SystemConfig_t& p_cfg) {
	JsonDocument v;

	v["meta"]["version"]    = p_cfg.meta.version;
	v["meta"]["deviceName"] = p_cfg.meta.deviceName;
	v["meta"]["lastUpdate"] = p_cfg.meta.lastUpdate;

	v["system"]["logging"]["level"]      = p_cfg.system.logging.level;
	v["system"]["logging"]["maxEntries"] = p_cfg.system.logging.maxEntries;

	// hw.fanPwm (camelCase)
	v["hw"]["fanPwm"]["pin"]     = p_cfg.hw.fanPwm.pin;
	v["hw"]["fanPwm"]["channel"] = p_cfg.hw.fanPwm.channel;
	v["hw"]["fanPwm"]["freq"]    = p_cfg.hw.fanPwm.freq;
	v["hw"]["fanPwm"]["res"]     = p_cfg.hw.fanPwm.res;

	v["hw"]["fanConfig"]["startPercentMin"]   = p_cfg.hw.fanConfig.startPercentMin;
	v["hw"]["fanConfig"]["comfortPercentMin"] = p_cfg.hw.fanConfig.comfortPercentMin;
	v["hw"]["fanConfig"]["comfortPercentMax"] = p_cfg.hw.fanConfig.comfortPercentMax;
	v["hw"]["fanConfig"]["hardPercentMax"]    = p_cfg.hw.fanConfig.hardPercentMax;

	v["hw"]["pir"]["enabled"]     = p_cfg.hw.pir.enabled;
	v["hw"]["pir"]["pin"]         = p_cfg.hw.pir.pin;
	v["hw"]["pir"]["debounceSec"] = p_cfg.hw.pir.debounceSec;
	v["hw"]["pir"]["holdSec"]     = p_cfg.hw.pir.holdSec;

	v["hw"]["tempHum"]["enabled"]     = p_cfg.hw.tempHum.enabled;
	v["hw"]["tempHum"]["type"]        = p_cfg.hw.tempHum.type;
	v["hw"]["tempHum"]["pin"]         = p_cfg.hw.tempHum.pin;
	v["hw"]["tempHum"]["intervalSec"] = p_cfg.hw.tempHum.intervalSec;

	v["hw"]["ble"]["enabled"]      = p_cfg.hw.ble.enabled;
	v["hw"]["ble"]["scanInterval"] = p_cfg.hw.ble.scanInterval;

	v["security"]["apiKey"] = p_cfg.security.apiKey;

	v["time"]["ntpServer"]       = p_cfg.time.ntpServer;
	v["time"]["timezone"]        = p_cfg.time.timezone;
	v["time"]["syncIntervalMin"] = p_cfg.time.syncIntervalMin;

	return ioSaveJson(s_cfgJsonFileMap.system, v);
}

bool CL_C10_ConfigManager::saveWifiConfig(const ST_A20_WifiConfig_t& p_cfg) {
	JsonDocument d;

	d["wifi"]["wifiMode"]     = p_cfg.wifiMode;
	d["wifi"]["wifiModeDesc"] = p_cfg.wifiModeDesc;
	d["wifi"]["ap"]["ssid"]   = p_cfg.ap.ssid;

	// JSON 키 pass 로 통일 (pass는 호환으로만 유지)
	d["wifi"]["ap"]["pass"] = p_cfg.ap.pass;

	JsonArray v_staArr = d["wifi"]["sta"].to<JsonArray>();
	for (uint8_t v_i = 0; v_i < p_cfg.staCount; v_i++) {
		JsonObject v_net = v_staArr.add<JsonObject>();
		v_net["ssid"] = p_cfg.sta[v_i].ssid;
		v_net["pass"] = p_cfg.sta[v_i].pass;
	}

	return ioSaveJson(s_cfgJsonFileMap.wifi, d);
}

bool CL_C10_ConfigManager::saveMotionConfig(const ST_A20_MotionConfig_t& p_cfg) {
	JsonDocument d;

	// d["motion"]["enabled"]        = p_cfg.enabled;
	d["motion"]["pir"]["enabled"] = p_cfg.pir.enabled;
	d["motion"]["pir"]["holdSec"] = p_cfg.pir.holdSec;

	d["motion"]["ble"]["enabled"]      = p_cfg.ble.enabled;
	d["motion"]["ble"]["rssi"]["on"]   = p_cfg.ble.rssi.on;
	d["motion"]["ble"]["rssi"]["off"]  = p_cfg.ble.rssi.off;

	// camelCase
	d["motion"]["ble"]["rssi"]["avgCount"]     = p_cfg.ble.rssi.avgCount;
	d["motion"]["ble"]["rssi"]["persistCount"] = p_cfg.ble.rssi.persistCount;
	d["motion"]["ble"]["rssi"]["exitDelaySec"] = p_cfg.ble.rssi.exitDelaySec;

	JsonArray v_tdArr = d["motion"]["ble"]["trustedDevices"].to<JsonArray>();
	for (uint8_t v_i = 0; v_i < p_cfg.ble.trustedCount; v_i++) {
		const ST_A20_BLETrustedDevice_t& v_d = p_cfg.ble.trustedDevices[v_i];
		JsonObject v_td = v_tdArr.add<JsonObject>();

		v_td["alias"]       = v_d.alias;
		v_td["name"]        = v_d.name;
		v_td["mac"]         = v_d.mac;
		v_td["manufPrefix"] = v_d.manufPrefix;
		v_td["prefixLen"]   = v_d.prefixLen;
		v_td["enabled"]     = v_d.enabled;
	}

	return ioSaveJson(s_cfgJsonFileMap.motion, d);
}

// =====================================================
// 4. JSON Patch (System/Wifi/Motion) - camelCase 기준, snake_case 호환
// =====================================================
bool CL_C10_ConfigManager::patchSystemFromJson(ST_A20_SystemConfig_t& p_config, const JsonDocument& p_patch) {
	bool v_changed = false;

	C10_MUTEX_ACQUIRE_BOOL();

	JsonObjectConst j_meta = p_patch["meta"].as<JsonObjectConst>();
	JsonObjectConst j_sys  = p_patch["system"].as<JsonObjectConst>();
	JsonObjectConst j_sec  = p_patch["security"].as<JsonObjectConst>();
	JsonObjectConst j_hw   = p_patch["hw"].as<JsonObjectConst>();
	JsonObjectConst j_time = p_patch["time"].as<JsonObjectConst>();

	if (j_meta.isNull() && j_sys.isNull() && j_sec.isNull() && j_hw.isNull() && j_time.isNull()) {
		C10_MUTEX_RELEASE();
		return false;
	}

	// meta
	if (!j_meta.isNull()) {
		const char* v_dn = C10_getStr2(j_meta, "deviceName", "deviceName", "");
		if (strlen(v_dn) > 0 && strcmp(v_dn, p_config.meta.deviceName) != 0) {
			strlcpy(p_config.meta.deviceName, v_dn, sizeof(p_config.meta.deviceName));
			v_changed = true;
		}

		const char* v_lu = C10_getStr2(j_meta, "lastUpdate", "lastUpdate", "");
		if (strlen(v_lu) > 0 && strcmp(v_lu, p_config.meta.lastUpdate) != 0) {
			strlcpy(p_config.meta.lastUpdate, v_lu, sizeof(p_config.meta.lastUpdate));
			v_changed = true;
		}
	}

	// system.logging
	if (!j_sys.isNull()) {
		JsonObjectConst j_log = j_sys["logging"].as<JsonObjectConst>();
		if (!j_log.isNull()) {
			const char* v_lv = C10_getStr2(j_log, "level", "level", "");
			if (strlen(v_lv) > 0 && strcmp(v_lv, p_config.system.logging.level) != 0) {
				strlcpy(p_config.system.logging.level, v_lv, sizeof(p_config.system.logging.level));
				v_changed = true;
			}

			uint16_t v_max = C10_getNum2<uint16_t>(j_log, "maxEntries", "maxEntries", p_config.system.logging.maxEntries);
			if (v_max != p_config.system.logging.maxEntries) {
				p_config.system.logging.maxEntries = v_max;
				v_changed = true;
			}
		}
	}

	// security.apiKey
	if (!j_sec.isNull()) {
		const char* v_key = C10_getStr2(j_sec, "apiKey", "apiKey", "");
		if (strlen(v_key) > 0 && strcmp(v_key, p_config.security.apiKey) != 0) {
			strlcpy(p_config.security.apiKey, v_key, sizeof(p_config.security.apiKey));
			v_changed = true;
		}
	}

	// hw
	if (!j_hw.isNull()) {
		// fanConfig
		JsonObjectConst j_fan = j_hw["fanConfig"].as<JsonObjectConst>();
		if (!j_fan.isNull()) {
			if (j_fan["startPercentMin"].is<uint8_t>()) {
				uint8_t v_val = j_fan["startPercentMin"].as<uint8_t>();
				if (v_val != p_config.hw.fanConfig.startPercentMin) { p_config.hw.fanConfig.startPercentMin = v_val; v_changed = true; }
			}
			if (j_fan["comfortPercentMin"].is<uint8_t>()) {
				uint8_t v_val = j_fan["comfortPercentMin"].as<uint8_t>();
				if (v_val != p_config.hw.fanConfig.comfortPercentMin) { p_config.hw.fanConfig.comfortPercentMin = v_val; v_changed = true; }
			}
			if (j_fan["comfortPercentMax"].is<uint8_t>()) {
				uint8_t v_val = j_fan["comfortPercentMax"].as<uint8_t>();
				if (v_val != p_config.hw.fanConfig.comfortPercentMax) { p_config.hw.fanConfig.comfortPercentMax = v_val; v_changed = true; }
			}
			if (j_fan["hardPercentMax"].is<uint8_t>()) {
				uint8_t v_val = j_fan["hardPercentMax"].as<uint8_t>();
				if (v_val != p_config.hw.fanConfig.hardPercentMax) { p_config.hw.fanConfig.hardPercentMax = v_val; v_changed = true; }
			}
		}

		// pir
		JsonObjectConst j_pir = j_hw["pir"].as<JsonObjectConst>();
		if (!j_pir.isNull()) {
			if (j_pir["enabled"].is<bool>()) {
				bool v_en = j_pir["enabled"].as<bool>();
				if (v_en != p_config.hw.pir.enabled) { p_config.hw.pir.enabled = v_en; v_changed = true; }
			}
			if (j_pir["pin"].is<uint8_t>()) {
				uint8_t v_pin = j_pir["pin"].as<uint8_t>();
				if (v_pin != p_config.hw.pir.pin) { p_config.hw.pir.pin = v_pin; v_changed = true; }
			}
			uint16_t v_db = C10_getNum2<uint16_t>(j_pir, "debounceSec", "debounceSec", p_config.hw.pir.debounceSec);
			if (v_db != p_config.hw.pir.debounceSec) { p_config.hw.pir.debounceSec = v_db; v_changed = true; }

			if (j_pir["holdSec"].is<uint16_t>()) {
				uint16_t v_hold = j_pir["holdSec"].as<uint16_t>();
				if (v_hold != p_config.hw.pir.holdSec) { p_config.hw.pir.holdSec = v_hold; v_changed = true; }
			}
		}

		// tempHum
		JsonObjectConst j_th = j_hw["tempHum"].as<JsonObjectConst>();
		if (!j_th.isNull()) {
			if (j_th["enabled"].is<bool>()) {
				bool v_en = j_th["enabled"].as<bool>();
				if (v_en != p_config.hw.tempHum.enabled) { p_config.hw.tempHum.enabled = v_en; v_changed = true; }
			}
			const char* v_type = j_th["type"] | "";
			if (strlen(v_type) > 0 && strcmp(v_type, p_config.hw.tempHum.type) != 0) {
				strlcpy(p_config.hw.tempHum.type, v_type, sizeof(p_config.hw.tempHum.type));
				v_changed = true;
			}
			if (j_th["pin"].is<uint8_t>()) {
				uint8_t v_pin = j_th["pin"].as<uint8_t>();
				if (v_pin != p_config.hw.tempHum.pin) { p_config.hw.tempHum.pin = v_pin; v_changed = true; }
			}
			uint16_t v_itv = C10_getNum2<uint16_t>(j_th, "intervalSec", "intervalSec", p_config.hw.tempHum.intervalSec);
			if (v_itv != p_config.hw.tempHum.intervalSec) { p_config.hw.tempHum.intervalSec = v_itv; v_changed = true; }
		}

		// fanPwm (fanPwm 호환)
		JsonObjectConst j_pwm = j_hw["fanPwm"].as<JsonObjectConst>();
		if (j_pwm.isNull()) j_pwm = j_hw["fanPwm"].as<JsonObjectConst>();
		if (!j_pwm.isNull()) {
			if (j_pwm["pin"].is<uint8_t>()) {
				uint8_t v_pin = j_pwm["pin"].as<uint8_t>();
				if (v_pin != p_config.hw.fanPwm.pin) { p_config.hw.fanPwm.pin = v_pin; v_changed = true; }
			}
			if (j_pwm["channel"].is<uint8_t>()) {
				uint8_t v_ch = j_pwm["channel"].as<uint8_t>();
				if (v_ch != p_config.hw.fanPwm.channel) { p_config.hw.fanPwm.channel = v_ch; v_changed = true; }
			}
			if (j_pwm["freq"].is<uint32_t>()) {
				uint32_t v_fr = j_pwm["freq"].as<uint32_t>();
				if (v_fr != p_config.hw.fanPwm.freq) { p_config.hw.fanPwm.freq = v_fr; v_changed = true; }
			}
			if (j_pwm["res"].is<uint8_t>()) {
				uint8_t v_res = j_pwm["res"].as<uint8_t>();
				if (v_res != p_config.hw.fanPwm.res) { p_config.hw.fanPwm.res = v_res; v_changed = true; }
			}
		}

		// ble (scanInterval 호환)
		JsonObjectConst j_ble = j_hw["ble"].as<JsonObjectConst>();
		if (!j_ble.isNull()) {
			if (j_ble["enabled"].is<bool>()) {
				bool v_en = j_ble["enabled"].as<bool>();
				if (v_en != p_config.hw.ble.enabled) { p_config.hw.ble.enabled = v_en; v_changed = true; }
			}
			uint16_t v_si = C10_getNum2<uint16_t>(j_ble, "scanInterval", "scanInterval", p_config.hw.ble.scanInterval);
			if (v_si != p_config.hw.ble.scanInterval) { p_config.hw.ble.scanInterval = v_si; v_changed = true; }
		}
	}

	// time
	if (!j_time.isNull()) {
		const char* v_ntp = C10_getStr2(j_time, "ntpServer", "ntpServer", "");
		if (strlen(v_ntp) > 0 && strcmp(v_ntp, p_config.time.ntpServer) != 0) {
			strlcpy(p_config.time.ntpServer, v_ntp, sizeof(p_config.time.ntpServer));
			v_changed = true;
		}

		const char* v_tz = C10_getStr2(j_time, "timezone", "timezone", "");
		if (strlen(v_tz) > 0 && strcmp(v_tz, p_config.time.timezone) != 0) {
			strlcpy(p_config.time.timezone, v_tz, sizeof(p_config.time.timezone));
			v_changed = true;
		}

		uint16_t v_si = C10_getNum2<uint16_t>(j_time, "syncIntervalMin", "syncIntervalMin", p_config.time.syncIntervalMin);
		if (v_si != p_config.time.syncIntervalMin) {
			p_config.time.syncIntervalMin = v_si;
			v_changed = true;
		}
	}

	if (v_changed) {
		_dirty_system = true;
		CL_D10_Logger::log(EN_L10_LOG_INFO, "[C10] System config patched (Memory Only, camelCase). Dirty=true");
	}

	C10_MUTEX_RELEASE();
	return v_changed;
}

bool CL_C10_ConfigManager::patchWifiFromJson(ST_A20_WifiConfig_t& p_config, const JsonDocument& p_patch) {
	bool v_changed = false;

	C10_MUTEX_ACQUIRE_BOOL();

	JsonObjectConst j_wifi = p_patch["wifi"].as<JsonObjectConst>();
	if (j_wifi.isNull()) {
		C10_MUTEX_RELEASE();
		return false;
	}

	if (j_wifi["wifiMode"].is<uint8_t>()) {
		uint8_t v_mode = j_wifi["wifiMode"].as<uint8_t>();
		if (v_mode != p_config.wifiMode) {
			if (v_mode >= EN_A20_WIFI_MODE_AP && v_mode <= EN_A20_WIFI_MODE_AP_STA) {
				p_config.wifiMode = (EN_A20_WIFI_MODE_t)v_mode;
				v_changed = true;
			} else {
				CL_D10_Logger::log(EN_L10_LOG_WARN, "[C10] Invalid wifiMode value: %d", v_mode);
			}
		}
	}

	JsonObjectConst j_ap = j_wifi["ap"].as<JsonObjectConst>();
	if (!j_ap.isNull()) {
		const char* v_ssid = j_ap["ssid"] | "";
		if (strlen(v_ssid) > 0 && strcmp(v_ssid, p_config.ap.ssid) != 0) {
			strlcpy(p_config.ap.ssid, v_ssid, sizeof(p_config.ap.ssid));
			v_changed = true;
		}

		// pass 우선, pass 호환
		const char* v_pwd = C10_getStr2(j_ap, "pass", "pass", "");
		if (strlen(v_pwd) > 0 && strcmp(v_pwd, p_config.ap.pass) != 0) {
			strlcpy(p_config.ap.pass, v_pwd, sizeof(p_config.ap.pass));
			v_changed = true;
		}
	}

	JsonArrayConst j_sta = j_wifi["sta"].as<JsonArrayConst>();
	if (!j_sta.isNull()) {
		p_config.staCount = 0;
		for (JsonObjectConst v_js : j_sta) {
			if (p_config.staCount >= A20_Const::MAX_STA_NETWORKS) break;

			ST_A20_STANetwork_t& v_net = p_config.sta[p_config.staCount];

			strlcpy(v_net.ssid, v_js["ssid"] | "", sizeof(v_net.ssid));

			const char* v_pass = C10_getStr2(v_js, "pass", "pass", "");
			strlcpy(v_net.pass, v_pass, sizeof(v_net.pass));

			p_config.staCount++;
		}
		v_changed = true;
		CL_D10_Logger::log(EN_L10_LOG_DEBUG, "[C10] WiFi STA array fully replaced.");
	}

	if (v_changed) {
		_dirty_wifi = true;
		CL_D10_Logger::log(EN_L10_LOG_INFO, "[C10] WiFi config patched (Memory Only, camelCase). Dirty=true");
	}

	C10_MUTEX_RELEASE();
	return v_changed;
}

bool CL_C10_ConfigManager::patchMotionFromJson(ST_A20_MotionConfig_t& p_config, const JsonDocument& p_patch) {
	C10_MUTEX_ACQUIRE_BOOL();

	bool v_changed = false;

	JsonObjectConst j_motion = p_patch["motion"].as<JsonObjectConst>();
	if (j_motion.isNull()) {
		C10_MUTEX_RELEASE();
		return false;
	}

	// if (j_motion["enabled"].is<bool>() && j_motion["enabled"].as<bool>() != p_config.enabled) {
	// 	p_config.enabled = j_motion["enabled"].as<bool>();
	// 	v_changed = true;
	// }

	JsonObjectConst j_pir = j_motion["pir"].as<JsonObjectConst>();
	if (!j_pir.isNull()) {
		if (j_pir["enabled"].is<bool>() && j_pir["enabled"].as<bool>() != p_config.pir.enabled) {
			p_config.pir.enabled = j_pir["enabled"].as<bool>();
			v_changed = true;
		}
		if (j_pir["holdSec"].is<uint16_t>() && j_pir["holdSec"].as<uint16_t>() != p_config.pir.holdSec) {
			p_config.pir.holdSec = j_pir["holdSec"].as<uint16_t>();
			v_changed = true;
		}
	}

	JsonObjectConst j_ble = j_motion["ble"].as<JsonObjectConst>();
	if (!j_ble.isNull()) {
		if (j_ble["enabled"].is<bool>() && j_ble["enabled"].as<bool>() != p_config.ble.enabled) {
			p_config.ble.enabled = j_ble["enabled"].as<bool>();
			v_changed = true;
		}

		JsonObjectConst j_rssi = j_ble["rssi"].as<JsonObjectConst>();
		if (!j_rssi.isNull()) {
			if (j_rssi["on"].is<int8_t>() && j_rssi["on"].as<int8_t>() != p_config.ble.rssi.on) {
				p_config.ble.rssi.on = j_rssi["on"].as<int8_t>();
				v_changed = true;
			}
			if (j_rssi["off"].is<int8_t>() && j_rssi["off"].as<int8_t>() != p_config.ble.rssi.off) {
				p_config.ble.rssi.off = j_rssi["off"].as<int8_t>();
				v_changed = true;
			}

			uint8_t v_avg = C10_getNum2<uint8_t>(j_rssi, "avgCount", "avgCount", p_config.ble.rssi.avgCount);
			if (v_avg != p_config.ble.rssi.avgCount) { p_config.ble.rssi.avgCount = v_avg; v_changed = true; }

			uint8_t v_pst = C10_getNum2<uint8_t>(j_rssi, "persistCount", "persistCount", p_config.ble.rssi.persistCount);
			if (v_pst != p_config.ble.rssi.persistCount) { p_config.ble.rssi.persistCount = v_pst; v_changed = true; }

			uint16_t v_exit = C10_getNum2<uint16_t>(j_rssi, "exitDelaySec", "exitDelaySec", p_config.ble.rssi.exitDelaySec);
			if (v_exit != p_config.ble.rssi.exitDelaySec) { p_config.ble.rssi.exitDelaySec = v_exit; v_changed = true; }
		}

		JsonArrayConst j_devices = j_ble["trustedDevices"].as<JsonArrayConst>();
		if (j_devices.isNull()) j_devices = j_ble["trustedDevices"].as<JsonArrayConst>();

		if (!j_devices.isNull()) {
			p_config.ble.trustedCount = 0;
			for (JsonObjectConst j_dev : j_devices) {
				if (p_config.ble.trustedCount >= A20_Const::MAX_BLE_DEVICES) break;

				ST_A20_BLETrustedDevice_t& v_d = p_config.ble.trustedDevices[p_config.ble.trustedCount];

				strlcpy(v_d.alias, j_dev["alias"] | "", sizeof(v_d.alias));
				strlcpy(v_d.name, j_dev["name"] | "", sizeof(v_d.name));
				strlcpy(v_d.mac, j_dev["mac"] | "", sizeof(v_d.mac));

				const char* v_mp = C10_getStr2(j_dev, "manufPrefix", "manufPrefix", "");
				strlcpy(v_d.manufPrefix, v_mp, sizeof(v_d.manufPrefix));

				v_d.prefixLen = C10_getNum2<uint8_t>(j_dev, "prefixLen", "prefixLen", 0);
				v_d.enabled    = C10_getBool2(j_dev, "enabled", "enabled", true);

				p_config.ble.trustedCount++;
			}
			v_changed = true;
			CL_D10_Logger::log(EN_L10_LOG_DEBUG, "[C10] Motion Trusted Devices array fully replaced.");
		}
	}

	if (v_changed) {
		_dirty_motion = true;
		CL_D10_Logger::log(EN_L10_LOG_INFO, "[C10] Motion config patched (Memory Only, camelCase). Dirty=true");
	}

	C10_MUTEX_RELEASE();
	return v_changed;
}

// =====================================================
// 3-1. JSON Export (System/Wifi/Motion) - camelCase Export
// =====================================================
void CL_C10_ConfigManager::toJson_System(const ST_A20_SystemConfig_t& p, JsonDocument& d) {
	d["meta"]["version"]    = p.meta.version;
	d["meta"]["deviceName"] = p.meta.deviceName;
	d["meta"]["lastUpdate"] = p.meta.lastUpdate;

	d["system"]["logging"]["level"]      = p.system.logging.level;
	d["system"]["logging"]["maxEntries"] = p.system.logging.maxEntries;

	d["hw"]["fanPwm"]["pin"]     = p.hw.fanPwm.pin;
	d["hw"]["fanPwm"]["channel"] = p.hw.fanPwm.channel;
	d["hw"]["fanPwm"]["freq"]    = p.hw.fanPwm.freq;
	d["hw"]["fanPwm"]["res"]     = p.hw.fanPwm.res;

	d["hw"]["fanConfig"]["startPercentMin"]   = p.hw.fanConfig.startPercentMin;
	d["hw"]["fanConfig"]["comfortPercentMin"] = p.hw.fanConfig.comfortPercentMin;
	d["hw"]["fanConfig"]["comfortPercentMax"] = p.hw.fanConfig.comfortPercentMax;
	d["hw"]["fanConfig"]["hardPercentMax"]    = p.hw.fanConfig.hardPercentMax;

	d["hw"]["pir"]["enabled"]     = p.hw.pir.enabled;
	d["hw"]["pir"]["pin"]         = p.hw.pir.pin;
	d["hw"]["pir"]["debounceSec"] = p.hw.pir.debounceSec;
	d["hw"]["pir"]["holdSec"]     = p.hw.pir.holdSec;

	d["hw"]["tempHum"]["enabled"]     = p.hw.tempHum.enabled;
	d["hw"]["tempHum"]["type"]        = p.hw.tempHum.type;
	d["hw"]["tempHum"]["pin"]         = p.hw.tempHum.pin;
	d["hw"]["tempHum"]["intervalSec"] = p.hw.tempHum.intervalSec;

	d["hw"]["ble"]["enabled"]      = p.hw.ble.enabled;
	d["hw"]["ble"]["scanInterval"] = p.hw.ble.scanInterval;

	d["security"]["apiKey"] = p.security.apiKey;

	d["time"]["ntpServer"]       = p.time.ntpServer;
	d["time"]["timezone"]        = p.time.timezone;
	d["time"]["syncIntervalMin"] = p.time.syncIntervalMin;
}

void CL_C10_ConfigManager::toJson_Wifi(const ST_A20_WifiConfig_t& p, JsonDocument& d) {
	d["wifi"]["wifiMode"]     = p.wifiMode;
	d["wifi"]["wifiModeDesc"] = p.wifiModeDesc;
	d["wifi"]["ap"]["ssid"]   = p.ap.ssid;
	d["wifi"]["ap"]["pass"]   = p.ap.pass;

	JsonArray v_staArr = d["wifi"]["sta"].to<JsonArray>();
	for (uint8_t i = 0; i < p.staCount; i++) {
		JsonObject v_net = v_staArr.add<JsonObject>();
		v_net["ssid"] = p.sta[i].ssid;
		v_net["pass"] = p.sta[i].pass;
	}
}

void CL_C10_ConfigManager::toJson_Motion(const ST_A20_MotionConfig_t& p, JsonDocument& d) {
	// d["motion"]["enabled"]        = p.enabled;
	d["motion"]["pir"]["enabled"] = p.pir.enabled;
	d["motion"]["pir"]["holdSec"] = p.pir.holdSec;

	d["motion"]["ble"]["enabled"]      = p.ble.enabled;
	d["motion"]["ble"]["rssi"]["on"]   = p.ble.rssi.on;
	d["motion"]["ble"]["rssi"]["off"]  = p.ble.rssi.off;

	d["motion"]["ble"]["rssi"]["avgCount"]     = p.ble.rssi.avgCount;
	d["motion"]["ble"]["rssi"]["persistCount"] = p.ble.rssi.persistCount;
	d["motion"]["ble"]["rssi"]["exitDelaySec"] = p.ble.rssi.exitDelaySec;

	JsonArray v_tdArr = d["motion"]["ble"]["trustedDevices"].to<JsonArray>();
	for (uint8_t i = 0; i < p.ble.trustedCount; i++) {
		const ST_A20_BLETrustedDevice_t& v_d = p.ble.trustedDevices[i];
		JsonObject v_td = v_tdArr.add<JsonObject>();

		v_td["alias"]       = v_d.alias;
		v_td["name"]        = v_d.name;
		v_td["mac"]         = v_d.mac;
		v_td["manufPrefix"] = v_d.manufPrefix;
		v_td["prefixLen"]   = v_d.prefixLen;
		v_td["enabled"]     = v_d.enabled;
	}
}
