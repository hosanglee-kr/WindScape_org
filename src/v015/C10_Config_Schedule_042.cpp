/*
 * ------------------------------------------------------
 * 소스명 : C10_Config_Schedule_042.cpp
 * 모듈 약어 : C10
 * 모듈명 : Smart Nature Wind Configuration Manager - Schedule/UserProfiles/WindProfile
 * ------------------------------------------------------
 * 기능 요약:
 *  - Schedules / UserProfiles / WindProfileDict Load/Save
 *  - 위 섹션들의 JSON Export / Patch
 *  - Schedule / UserProfiles / WindProfile CRUD
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

// =====================================================
// 내부 Helper: Schedule / UserProfile / WindPreset 디코더
//  - JSON 키는 camelCase 기준
// =====================================================
static void C10_fromJson_ScheduleItem(const JsonObjectConst& p_js, ST_A20_ScheduleItem_t& p_s) {
	p_s.schId = p_js["schId"] | 0;
	p_s.schNo = p_js["schNo"] | 0;
	strlcpy(p_s.name, p_js["name"] | "", sizeof(p_s.name));
	p_s.enabled = p_js["enabled"] | true;

	p_s.repeatSegments = p_js["repeatSegments"] | true;
	p_s.repeatCount    = p_js["repeatCount"] | 0;

	// period
	for (uint8_t v_d = 0; v_d < 7; v_d++) {
		p_s.period.days[v_d] = p_js["period"]["days"][v_d] | 1;
	}
	strlcpy(p_s.period.startTime, p_js["period"]["startTime"] | "00:00", sizeof(p_s.period.startTime));
	strlcpy(p_s.period.endTime, p_js["period"]["endTime"] | "23:59", sizeof(p_s.period.endTime));

	// segments
	p_s.segCount = 0;
	if (p_js["segments"].is<JsonArrayConst>()) {
		JsonArrayConst v_segArr = p_js["segments"].as<JsonArrayConst>();
		for (JsonObjectConst jseg : v_segArr) {
			if (p_s.segCount >= A20_Const::MAX_SEGMENTS_PER_SCHEDULE) break;

			ST_A20_ScheduleSegment_t& sg = p_s.segments[p_s.segCount++];

			sg.segId      = jseg["segId"] | 0;
			sg.segNo      = jseg["segNo"] | 0;
			sg.onMinutes  = jseg["onMinutes"] | 10;
			sg.offMinutes = jseg["offMinutes"] | 0;

			const char* v_mode = jseg["mode"] | "PRESET";
			sg.mode            = A20_modeFromString(v_mode);

			strlcpy(sg.presetCode, jseg["presetCode"] | "", sizeof(sg.presetCode));
			strlcpy(sg.styleCode, jseg["styleCode"] | "", sizeof(sg.styleCode));

			memset(&sg.adjust, 0, sizeof(sg.adjust));
			if (jseg["adjust"].is<JsonObjectConst>()) {
				JsonObjectConst adj               = jseg["adjust"].as<JsonObjectConst>();
				sg.adjust.windIntensity           = adj["windIntensity"] | 0.0f;
				sg.adjust.windVariability         = adj["windVariability"] | 0.0f;
				sg.adjust.gustFrequency           = adj["gustFrequency"] | 0.0f;
				sg.adjust.fanLimit                = adj["fanLimit"] | 0.0f;
				sg.adjust.minFan                  = adj["minFan"] | 0.0f;
				sg.adjust.turbulenceLengthScale   = adj["turbulenceLengthScale"] | 0.0f;    // camelCase
				sg.adjust.turbulenceIntensitySigma = adj["turbulenceIntensitySigma"] | 0.0f; // camelCase
			}

			sg.fixedSpeed = jseg["fixedSpeed"] | 0.0f; // camelCase
		}
	}

	// autoOff
	memset(&p_s.autoOff, 0, sizeof(p_s.autoOff));
	if (p_js["autoOff"].is<JsonObjectConst>()) {
		JsonObjectConst ao          = p_js["autoOff"].as<JsonObjectConst>();
		p_s.autoOff.timer.enabled   = ao["timer"]["enabled"] | false;
		p_s.autoOff.timer.minutes   = ao["timer"]["minutes"] | 0;
		p_s.autoOff.offTime.enabled = ao["offTime"]["enabled"] | false;
		strlcpy(p_s.autoOff.offTime.time, ao["offTime"]["time"] | "", sizeof(p_s.autoOff.offTime.time));
		p_s.autoOff.offTemp.enabled = ao["offTemp"]["enabled"] | false;
		p_s.autoOff.offTemp.temp    = ao["offTemp"]["temp"] | 0.0f;
	}

	// motion
	p_s.motion.pir.enabled         = p_js["motion"]["pir"]["enabled"] | false;
	p_s.motion.pir.holdSec         = p_js["motion"]["pir"]["holdSec"] | 0;
	p_s.motion.ble.enabled         = p_js["motion"]["ble"]["enabled"] | false;
	p_s.motion.ble.rssiThreshold   = p_js["motion"]["ble"]["rssiThreshold"] | -70; // camelCase
	p_s.motion.ble.holdSec         = p_js["motion"]["ble"]["holdSec"] | 0;
}

static void C10_fromJson_UserProfile(const JsonObjectConst& p_jp, ST_A20_UserProfileItem_t& p_up) {
	p_up.profileId = p_jp["profileId"] | 0;
	p_up.profileNo = p_jp["profileNo"] | 0;
	strlcpy(p_up.name, p_jp["name"] | "", sizeof(p_up.name));
	p_up.enabled = p_jp["enabled"] | true;

	p_up.repeatSegments = p_jp["repeatSegments"] | true;
	p_up.repeatCount    = p_jp["repeatCount"] | 0;

	// segments
	p_up.segCount = 0;
	if (p_jp["segments"].is<JsonArrayConst>()) {
		JsonArrayConst v_sArr = p_jp["segments"].as<JsonArrayConst>();
		for (JsonObjectConst jseg : v_sArr) {
			if (p_up.segCount >= A20_Const::MAX_SEGMENTS_PER_PROFILE) break;

			ST_A20_UserProfileSegment_t& sg = p_up.segments[p_up.segCount++];

			sg.segId      = jseg["segId"] | 0;
			sg.segNo      = jseg["segNo"] | 0;
			sg.onMinutes  = jseg["onMinutes"] | 10;
			sg.offMinutes = jseg["offMinutes"] | 0;

			const char* v_mode = jseg["mode"] | "PRESET";
			sg.mode            = A20_modeFromString(v_mode);

			strlcpy(sg.presetCode, jseg["presetCode"] | "", sizeof(sg.presetCode));
			strlcpy(sg.styleCode, jseg["styleCode"] | "", sizeof(sg.styleCode));

			memset(&sg.adjust, 0, sizeof(sg.adjust));
			if (jseg["adjust"].is<JsonObjectConst>()) {
				JsonObjectConst adj               = jseg["adjust"].as<JsonObjectConst>();
				sg.adjust.windIntensity           = adj["windIntensity"] | 0.0f;
				sg.adjust.windVariability         = adj["windVariability"] | 0.0f;
				sg.adjust.gustFrequency           = adj["gustFrequency"] | 0.0f;
				sg.adjust.fanLimit                = adj["fanLimit"] | 0.0f;
				sg.adjust.minFan                  = adj["minFan"] | 0.0f;
				sg.adjust.turbulenceLengthScale   = adj["turbulenceLengthScale"] | 0.0f;    // camelCase
				sg.adjust.turbulenceIntensitySigma = adj["turbulenceIntensitySigma"] | 0.0f; // camelCase
			}

			sg.fixedSpeed = jseg["fixedSpeed"] | 0.0f; // camelCase
		}
	}

	// autoOff
	memset(&p_up.autoOff, 0, sizeof(p_up.autoOff));
	if (p_jp["autoOff"].is<JsonObjectConst>()) {
		JsonObjectConst ao           = p_jp["autoOff"].as<JsonObjectConst>();
		p_up.autoOff.timer.enabled   = ao["timer"]["enabled"] | false;
		p_up.autoOff.timer.minutes   = ao["timer"]["minutes"] | 0;
		p_up.autoOff.offTime.enabled = ao["offTime"]["enabled"] | false;
		strlcpy(p_up.autoOff.offTime.time, ao["offTime"]["time"] | "", sizeof(p_up.autoOff.offTime.time));
		p_up.autoOff.offTemp.enabled = ao["offTemp"]["enabled"] | false;
		p_up.autoOff.offTemp.temp    = ao["offTemp"]["temp"] | 0.0f;
	}

	// motion
	p_up.motion.pir.enabled         = p_jp["motion"]["pir"]["enabled"] | false;
	p_up.motion.pir.holdSec         = p_jp["motion"]["pir"]["holdSec"] | 0;
	p_up.motion.ble.enabled         = p_jp["motion"]["ble"]["enabled"] | false;
	p_up.motion.ble.rssiThreshold   = p_jp["motion"]["ble"]["rssiThreshold"] | -70; // camelCase
	p_up.motion.ble.holdSec         = p_jp["motion"]["ble"]["holdSec"] | 0;
}

static void C10_fromJson_WindPreset(const JsonObjectConst& p_js, ST_A20_PresetEntry_t& p_p) {
	strlcpy(p_p.name, p_js["name"] | "", sizeof(p_p.name));
	strlcpy(p_p.code, p_js["code"] | "", sizeof(p_p.code));

	JsonObjectConst v_b = p_js["base"].as<JsonObjectConst>();

	p_p.base.windIntensity            = v_b["windIntensity"] | 70.0f;
	p_p.base.gustFrequency            = v_b["gustFrequency"] | 40.0f;
	p_p.base.windVariability          = v_b["windVariability"] | 50.0f;
	p_p.base.fanLimit                 = v_b["fanLimit"] | 95.0f;
	p_p.base.minFan                   = v_b["minFan"] | 10.0f;
	p_p.base.turbulenceLengthScale    = v_b["turbulenceLengthScale"] | 40.0f;      // camelCase
	p_p.base.turbulenceIntensitySigma = v_b["turbulenceIntensitySigma"] | 0.5f;    // camelCase
	p_p.base.thermalBubbleStrength    = v_b["thermalBubbleStrength"] | 2.0f;       // camelCase
	p_p.base.thermalBubbleRadius      = v_b["thermalBubbleRadius"] | 18.0f;        // camelCase
}

// =====================================================
// 2-1. 목적물별 Load 구현 (Schedules/UserProfiles/WindProfileDict)
// =====================================================
bool CL_C10_ConfigManager::loadSchedules(ST_A20_SchedulesRoot_t& p_cfg) {
	JsonDocument d;

	const char* v_cfgJsonPath = nullptr;
	if (s_cfgJsonFileMap.schedules[0] != '\0') {
		v_cfgJsonPath = s_cfgJsonFileMap.schedules;
	} else {
		CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] loadSchedules: s_cfgJsonFileMap.schedules empty");
		return false;
	}

	if (!ioLoadJson(v_cfgJsonPath, d)) {
		CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] loadSchedules: ioLoadJson failed (%s)", v_cfgJsonPath);
		return false;
	}

	JsonArrayConst arr = d["schedules"].as<JsonArrayConst>();
	p_cfg.count        = 0;

	for (JsonObjectConst js : arr) {
		if (p_cfg.count >= A20_Const::MAX_SCHEDULES) break;

		ST_A20_ScheduleItem_t& s = p_cfg.items[p_cfg.count++];
		C10_fromJson_ScheduleItem(js, s);
	}

	return true;
}

bool CL_C10_ConfigManager::loadUserProfiles(ST_A20_UserProfilesRoot_t& p_cfg) {
	JsonDocument d;

	const char* v_cfgJsonPath = nullptr;
	if (s_cfgJsonFileMap.userProfiles[0] != '\0') {
		v_cfgJsonPath = s_cfgJsonFileMap.userProfiles;
	} else {
		CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] loadUserProfiles: s_cfgJsonFileMap.userProfiles empty");
		return false;
	}

	if (!ioLoadJson(v_cfgJsonPath, d)) {
		CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] loadUserProfiles: ioLoadJson failed (%s)", v_cfgJsonPath);
		return false;
	}

	JsonArrayConst arr = d["userProfiles"]["profiles"].as<JsonArrayConst>();
	p_cfg.count        = 0;

	for (JsonObjectConst jp : arr) {
		if (p_cfg.count >= A20_Const::MAX_USER_PROFILES) break;

		ST_A20_UserProfileItem_t& up = p_cfg.items[p_cfg.count++];
		C10_fromJson_UserProfile(jp, up);
	}

	return true;
}

bool CL_C10_ConfigManager::loadWindProfileDict(ST_A20_WindProfileDict_t& p_dict) {
	JsonDocument d;

	const char* v_cfgJsonPath = nullptr;
	if (s_cfgJsonFileMap.windDict[0] != '\0') {
		v_cfgJsonPath = s_cfgJsonFileMap.windDict;
	} else {
		CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] loadWindProfileDict: s_cfgJsonFileMap.windDict empty");
		return false;
	}

	if (!ioLoadJson(v_cfgJsonPath, d)) {
		CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] loadWindProfileDict: ioLoadJson failed (%s)", v_cfgJsonPath);
		return false;
	}

	JsonObjectConst j = d["windProfile"].as<JsonObjectConst>();

	p_dict.presetCount = 0;
	if (j["presets"].is<JsonArrayConst>()) {
		JsonArrayConst v_arr = j["presets"].as<JsonArrayConst>();
		for (JsonObjectConst v_js : v_arr) {
			if (p_dict.presetCount >= 16) break;

			ST_A20_PresetEntry_t& v_p = p_dict.presets[p_dict.presetCount++];
			C10_fromJson_WindPreset(v_js, v_p);
		}
	}

	p_dict.styleCount = 0;
	if (j["styles"].is<JsonArrayConst>()) {
		JsonArrayConst v_arr = j["styles"].as<JsonArrayConst>();
		for (JsonObjectConst v_js : v_arr) {
			if (p_dict.styleCount >= 16) break;

			ST_A20_StyleEntry_t& v_s = p_dict.styles[p_dict.styleCount++];

			strlcpy(v_s.name, v_js["name"] | "", sizeof(v_s.name));
			strlcpy(v_s.code, v_js["code"] | "", sizeof(v_s.code));

			JsonObjectConst v_f            = v_js["factors"].as<JsonObjectConst>();
			v_s.factors.intensityFactor   = v_f["intensityFactor"] | 1.0f;   // camelCase
			v_s.factors.variabilityFactor = v_f["variabilityFactor"] | 1.0f; // camelCase
			v_s.factors.gustFactor        = v_f["gustFactor"] | 1.0f;        // camelCase
			v_s.factors.thermalFactor     = v_f["thermalFactor"] | 1.0f;     // camelCase
		}
	}

	return true;
}

// =====================================================
// 2-2. 목적물별 Save 구현 (Schedules/UserProfiles/WindProfileDict)
// =====================================================
bool CL_C10_ConfigManager::saveSchedules(const ST_A20_SchedulesRoot_t& p_cfg) {
	JsonDocument d;

	for (uint8_t v_i = 0; v_i < p_cfg.count; v_i++) {
		const ST_A20_ScheduleItem_t& s  = p_cfg.items[v_i];
		JsonObject                   js = d["schedules"][v_i];

		js["schId"]          = s.schId;
		js["schNo"]          = s.schNo;
		js["name"]           = s.name;
		js["enabled"]        = s.enabled;
		js["repeatSegments"] = s.repeatSegments;
		js["repeatCount"]    = s.repeatCount;

		for (uint8_t v_d = 0; v_d < 7; v_d++) {
			js["period"]["days"][v_d] = s.period.days[v_d];
		}
		js["period"]["startTime"] = s.period.startTime;
		js["period"]["endTime"]   = s.period.endTime;

		for (uint8_t v_k = 0; v_k < s.segCount; v_k++) {
			const ST_A20_ScheduleSegment_t& sg   = s.segments[v_k];
			JsonObject                      jseg = js["segments"][v_k];

			jseg["segId"]      = sg.segId;
			jseg["segNo"]      = sg.segNo;
			jseg["onMinutes"]  = sg.onMinutes;
			jseg["offMinutes"] = sg.offMinutes;

			jseg["mode"]       = A20_modeToString(sg.mode);
			jseg["presetCode"] = sg.presetCode;
			jseg["styleCode"]  = sg.styleCode;

			JsonObject adj                 = jseg["adjust"];
			adj["windIntensity"]           = sg.adjust.windIntensity;
			adj["windVariability"]         = sg.adjust.windVariability;
			adj["gustFrequency"]           = sg.adjust.gustFrequency;
			adj["fanLimit"]                = sg.adjust.fanLimit;
			adj["minFan"]                  = sg.adjust.minFan;
			adj["turbulenceLengthScale"]   = sg.adjust.turbulenceLengthScale;    // camelCase
			adj["turbulenceIntensitySigma"] = sg.adjust.turbulenceIntensitySigma; // camelCase

			jseg["fixedSpeed"] = sg.fixedSpeed; // camelCase
		}

		JsonObject ao              = js["autoOff"];
		ao["timer"]["enabled"]     = s.autoOff.timer.enabled;
		ao["timer"]["minutes"]     = s.autoOff.timer.minutes;
		ao["offTime"]["enabled"]   = s.autoOff.offTime.enabled;
		ao["offTime"]["time"]      = s.autoOff.offTime.time;
		ao["offTemp"]["enabled"]   = s.autoOff.offTemp.enabled;
		ao["offTemp"]["temp"]      = s.autoOff.offTemp.temp;

		js["motion"]["pir"]["enabled"]      = s.motion.pir.enabled;
		js["motion"]["pir"]["holdSec"]      = s.motion.pir.holdSec;
		js["motion"]["ble"]["enabled"]      = s.motion.ble.enabled;
		js["motion"]["ble"]["rssiThreshold"] = s.motion.ble.rssiThreshold; // camelCase
		js["motion"]["ble"]["holdSec"]      = s.motion.ble.holdSec;
	}

	return ioSaveJson(s_cfgJsonFileMap.schedules, d);
}

bool CL_C10_ConfigManager::saveUserProfiles(const ST_A20_UserProfilesRoot_t& p_cfg) {
	JsonDocument d;

	for (uint8_t v_i = 0; v_i < p_cfg.count; v_i++) {
		const ST_A20_UserProfileItem_t& up = p_cfg.items[v_i];
		JsonObject                      jp = d["userProfiles"]["profiles"][v_i];

		jp["profileId"]      = up.profileId;
		jp["profileNo"]      = up.profileNo;
		jp["name"]           = up.name;
		jp["enabled"]        = up.enabled;
		jp["repeatSegments"] = up.repeatSegments;
		jp["repeatCount"]    = up.repeatCount;

		for (uint8_t v_k = 0; v_k < up.segCount; v_k++) {
			const ST_A20_UserProfileSegment_t& sg   = up.segments[v_k];
			JsonObject                         jseg = jp["segments"][v_k];

			jseg["segId"]      = sg.segId;
			jseg["segNo"]      = sg.segNo;
			jseg["onMinutes"]  = sg.onMinutes;
			jseg["offMinutes"] = sg.offMinutes;

			jseg["mode"]       = A20_modeToString(sg.mode);
			jseg["presetCode"] = sg.presetCode;
			jseg["styleCode"]  = sg.styleCode;

			JsonObject adj                 = jseg["adjust"];
			adj["windIntensity"]           = sg.adjust.windIntensity;
			adj["windVariability"]         = sg.adjust.windVariability;
			adj["gustFrequency"]           = sg.adjust.gustFrequency;
			adj["fanLimit"]                = sg.adjust.fanLimit;
			adj["minFan"]                  = sg.adjust.minFan;
			adj["turbulenceLengthScale"]   = sg.adjust.turbulenceLengthScale;    // camelCase
			adj["turbulenceIntensitySigma"] = sg.adjust.turbulenceIntensitySigma; // camelCase

			jseg["fixedSpeed"] = sg.fixedSpeed; // camelCase
		}

		JsonObject ao              = jp["autoOff"];
		ao["timer"]["enabled"]     = up.autoOff.timer.enabled;
		ao["timer"]["minutes"]     = up.autoOff.timer.minutes;
		ao["offTime"]["enabled"]   = up.autoOff.offTime.enabled;
		ao["offTime"]["time"]      = up.autoOff.offTime.time;
		ao["offTemp"]["enabled"]   = up.autoOff.offTemp.enabled;
		ao["offTemp"]["temp"]      = up.autoOff.offTemp.temp;

		jp["motion"]["pir"]["enabled"]      = up.motion.pir.enabled;
		jp["motion"]["pir"]["holdSec"]      = up.motion.pir.holdSec;
		jp["motion"]["ble"]["enabled"]      = up.motion.ble.enabled;
		jp["motion"]["ble"]["rssiThreshold"] = up.motion.ble.rssiThreshold; // camelCase
		jp["motion"]["ble"]["holdSec"]      = up.motion.ble.holdSec;
	}

	return ioSaveJson(s_cfgJsonFileMap.userProfiles, d);
}

bool CL_C10_ConfigManager::saveWindProfileDict(const ST_A20_WindProfileDict_t& p_cfg) {
	JsonDocument d;

	// presets
	for (uint8_t v_i = 0; v_i < p_cfg.presetCount; v_i++) {
		const ST_A20_PresetEntry_t& v_p  = p_cfg.presets[v_i];
		JsonObject                  v_js = d["windProfile"]["presets"][v_i];

		v_js["name"] = v_p.name;
		v_js["code"] = v_p.code;

		JsonObject v_b                 = v_js["base"];
		v_b["windIntensity"]           = v_p.base.windIntensity;
		v_b["gustFrequency"]           = v_p.base.gustFrequency;
		v_b["windVariability"]         = v_p.base.windVariability;
		v_b["fanLimit"]                = v_p.base.fanLimit;
		v_b["minFan"]                  = v_p.base.minFan;
		v_b["turbulenceLengthScale"]   = v_p.base.turbulenceLengthScale;    // camelCase
		v_b["turbulenceIntensitySigma"] = v_p.base.turbulenceIntensitySigma; // camelCase
		v_b["thermalBubbleStrength"]   = v_p.base.thermalBubbleStrength;    // camelCase
		v_b["thermalBubbleRadius"]     = v_p.base.thermalBubbleRadius;      // camelCase
	}

	// styles
	for (uint8_t v_i = 0; v_i < p_cfg.styleCount; v_i++) {
		const ST_A20_StyleEntry_t& v_s  = p_cfg.styles[v_i];
		JsonObject                 v_js = d["windProfile"]["styles"][v_i];

		v_js["name"] = v_s.name;
		v_js["code"] = v_s.code;

		JsonObject v_f              = v_js["factors"];
		v_f["intensityFactor"]      = v_s.factors.intensityFactor;   // camelCase
		v_f["variabilityFactor"]    = v_s.factors.variabilityFactor; // camelCase
		v_f["gustFactor"]           = v_s.factors.gustFactor;        // camelCase
		v_f["thermalFactor"]        = v_s.factors.thermalFactor;     // camelCase
	}

	return ioSaveJson(s_cfgJsonFileMap.windDict, d);
}

// =====================================================
// 3. JSON Export (Schedules/UserProfiles/WindProfileDict)
// =====================================================
void CL_C10_ConfigManager::toJson_Schedules(const ST_A20_SchedulesRoot_t& p_cfg, JsonDocument& d) {
	for (uint8_t v_i = 0; v_i < p_cfg.count; v_i++) {
		const ST_A20_ScheduleItem_t& s  = p_cfg.items[v_i];
		JsonObject                   js = d["schedules"][v_i];

		js["schId"]          = s.schId;
		js["schNo"]          = s.schNo;
		js["name"]           = s.name;
		js["enabled"]        = s.enabled;
		js["repeatSegments"] = s.repeatSegments;
		js["repeatCount"]    = s.repeatCount;

		for (uint8_t v_d = 0; v_d < 7; v_d++) {
			js["period"]["days"][v_d] = s.period.days[v_d];
		}
		js["period"]["startTime"] = s.period.startTime;
		js["period"]["endTime"]   = s.period.endTime;

		for (uint8_t v_k = 0; v_k < s.segCount; v_k++) {
			const ST_A20_ScheduleSegment_t& sg   = s.segments[v_k];
			JsonObject                      jseg = js["segments"][v_k];

			jseg["segId"]      = sg.segId;
			jseg["segNo"]      = sg.segNo;
			jseg["onMinutes"]  = sg.onMinutes;
			jseg["offMinutes"] = sg.offMinutes;
			jseg["mode"]       = A20_modeToString(sg.mode);
			jseg["presetCode"] = sg.presetCode;
			jseg["styleCode"]  = sg.styleCode;

			JsonObject adj                 = jseg["adjust"];
			adj["windIntensity"]           = sg.adjust.windIntensity;
			adj["windVariability"]         = sg.adjust.windVariability;
			adj["gustFrequency"]           = sg.adjust.gustFrequency;
			adj["fanLimit"]                = sg.adjust.fanLimit;
			adj["minFan"]                  = sg.adjust.minFan;
			adj["turbulenceLengthScale"]   = sg.adjust.turbulenceLengthScale;    // camelCase
			adj["turbulenceIntensitySigma"] = sg.adjust.turbulenceIntensitySigma; // camelCase

			jseg["fixedSpeed"] = sg.fixedSpeed; // camelCase
		}

		JsonObject ao              = js["autoOff"];
		ao["timer"]["enabled"]     = s.autoOff.timer.enabled;
		ao["timer"]["minutes"]     = s.autoOff.timer.minutes;
		ao["offTime"]["enabled"]   = s.autoOff.offTime.enabled;
		ao["offTime"]["time"]      = s.autoOff.offTime.time;
		ao["offTemp"]["enabled"]   = s.autoOff.offTemp.enabled;
		ao["offTemp"]["temp"]      = s.autoOff.offTemp.temp;

		js["motion"]["pir"]["enabled"]      = s.motion.pir.enabled;
		js["motion"]["pir"]["holdSec"]      = s.motion.pir.holdSec;
		js["motion"]["ble"]["enabled"]      = s.motion.ble.enabled;
		js["motion"]["ble"]["rssiThreshold"] = s.motion.ble.rssiThreshold; // camelCase
		js["motion"]["ble"]["holdSec"]      = s.motion.ble.holdSec;
	}
}

void CL_C10_ConfigManager::toJson_UserProfiles(const ST_A20_UserProfilesRoot_t& p_cfg, JsonDocument& d) {
	for (uint8_t v_i = 0; v_i < p_cfg.count; v_i++) {
		const ST_A20_UserProfileItem_t& up = p_cfg.items[v_i];
		JsonObject                      jp = d["userProfiles"]["profiles"][v_i];

		jp["profileId"]      = up.profileId;
		jp["profileNo"]      = up.profileNo;
		jp["name"]           = up.name;
		jp["enabled"]        = up.enabled;
		jp["repeatSegments"] = up.repeatSegments;
		jp["repeatCount"]    = up.repeatCount;

		for (uint8_t v_k = 0; v_k < up.segCount; v_k++) {
			const ST_A20_UserProfileSegment_t& sg   = up.segments[v_k];
			JsonObject                         jseg = jp["segments"][v_k];

			jseg["segId"]      = sg.segId;
			jseg["segNo"]      = sg.segNo;
			jseg["onMinutes"]  = sg.onMinutes;
			jseg["offMinutes"] = sg.offMinutes;
			jseg["mode"]       = A20_modeToString(sg.mode);
			jseg["presetCode"] = sg.presetCode;
			jseg["styleCode"]  = sg.styleCode;

			JsonObject adj                 = jseg["adjust"];
			adj["windIntensity"]           = sg.adjust.windIntensity;
			adj["windVariability"]         = sg.adjust.windVariability;
			adj["gustFrequency"]           = sg.adjust.gustFrequency;
			adj["fanLimit"]                = sg.adjust.fanLimit;
			adj["minFan"]                  = sg.adjust.minFan;
			adj["turbulenceLengthScale"]   = sg.adjust.turbulenceLengthScale;    // camelCase
			adj["turbulenceIntensitySigma"] = sg.adjust.turbulenceIntensitySigma; // camelCase

			jseg["fixedSpeed"] = sg.fixedSpeed; // camelCase
		}

		JsonObject ao              = jp["autoOff"];
		ao["timer"]["enabled"]     = up.autoOff.timer.enabled;
		ao["timer"]["minutes"]     = up.autoOff.timer.minutes;
		ao["offTime"]["enabled"]   = up.autoOff.offTime.enabled;
		ao["offTime"]["time"]      = up.autoOff.offTime.time;
		ao["offTemp"]["enabled"]   = up.autoOff.offTemp.enabled;
		ao["offTemp"]["temp"]      = up.autoOff.offTemp.temp;

		jp["motion"]["pir"]["enabled"]      = up.motion.pir.enabled;
		jp["motion"]["pir"]["holdSec"]      = up.motion.pir.holdSec;
		jp["motion"]["ble"]["enabled"]      = up.motion.ble.enabled;
		jp["motion"]["ble"]["rssiThreshold"] = up.motion.ble.rssiThreshold; // camelCase
		jp["motion"]["ble"]["holdSec"]      = up.motion.ble.holdSec;
	}
}

void CL_C10_ConfigManager::toJson_WindProfileDict(const ST_A20_WindProfileDict_t& p_cfg, JsonDocument& d) {
	for (uint8_t v_i = 0; v_i < p_cfg.presetCount; v_i++) {
		const ST_A20_PresetEntry_t& v_p  = p_cfg.presets[v_i];
		JsonObject                  v_js = d["windProfile"]["presets"][v_i];

		v_js["name"] = v_p.name;
		v_js["code"] = v_p.code;

		JsonObject v_b                 = v_js["base"];
		v_b["windIntensity"]           = v_p.base.windIntensity;
		v_b["gustFrequency"]           = v_p.base.gustFrequency;
		v_b["windVariability"]         = v_p.base.windVariability;
		v_b["fanLimit"]                = v_p.base.fanLimit;
		v_b["minFan"]                  = v_p.base.minFan;
		v_b["turbulenceLengthScale"]   = v_p.base.turbulenceLengthScale;    // camelCase
		v_b["turbulenceIntensitySigma"] = v_p.base.turbulenceIntensitySigma; // camelCase
		v_b["thermalBubbleStrength"]   = v_p.base.thermalBubbleStrength;    // camelCase
		v_b["thermalBubbleRadius"]     = v_p.base.thermalBubbleRadius;      // camelCase
	}

	for (uint8_t v_i = 0; v_i < p_cfg.styleCount; v_i++) {
		const ST_A20_StyleEntry_t& v_s  = p_cfg.styles[v_i];
		JsonObject                 v_js = d["windProfile"]["styles"][v_i];

		v_js["name"] = v_s.name;
		v_js["code"] = v_s.code;

		JsonObject v_f           = v_js["factors"];
		v_f["intensityFactor"]   = v_s.factors.intensityFactor;   // camelCase
		v_f["variabilityFactor"] = v_s.factors.variabilityFactor; // camelCase
		v_f["gustFactor"]        = v_s.factors.gustFactor;        // camelCase
		v_f["thermalFactor"]     = v_s.factors.thermalFactor;     // camelCase
	}
}

// =====================================================
// 4. JSON Patch (Schedules/UserProfiles/WindProfileDict)
// =====================================================
bool CL_C10_ConfigManager::patchSchedulesFromJson(ST_A20_SchedulesRoot_t& p_cfg, const JsonDocument& p_patch) {
	C10_MUTEX_ACQUIRE_BOOL();

	JsonArrayConst arr = p_patch["schedules"].as<JsonArrayConst>();
	if (arr.isNull()) {
		C10_MUTEX_RELEASE();
		return false;
	}

	p_cfg.count = 0;
	for (JsonObjectConst js : arr) {
		if (p_cfg.count >= A20_Const::MAX_SCHEDULES) break;

		ST_A20_ScheduleItem_t& s = p_cfg.items[p_cfg.count++];
		C10_fromJson_ScheduleItem(js, s);
	}

	_dirty_schedules = true;
	CL_D10_Logger::log(EN_L10_LOG_INFO, "[C10] Schedules patched (PUT). Dirty=true");

	C10_MUTEX_RELEASE();
	return true;
}

bool CL_C10_ConfigManager::patchUserProfilesFromJson(ST_A20_UserProfilesRoot_t& p_cfg, const JsonDocument& p_patch) {
	C10_MUTEX_ACQUIRE_BOOL();

	JsonArrayConst arr = p_patch["userProfiles"]["profiles"].as<JsonArrayConst>();
	if (arr.isNull()) {
		C10_MUTEX_RELEASE();
		return false;
	}

	p_cfg.count = 0;
	for (JsonObjectConst jp : arr) {
		if (p_cfg.count >= A20_Const::MAX_USER_PROFILES) break;

		ST_A20_UserProfileItem_t& up = p_cfg.items[p_cfg.count++];
		C10_fromJson_UserProfile(jp, up);
	}

	_dirty_userProfiles = true;
	CL_D10_Logger::log(EN_L10_LOG_INFO, "[C10] UserProfiles patched (PUT). Dirty=true");

	C10_MUTEX_RELEASE();
	return true;
}

bool CL_C10_ConfigManager::patchWindProfileDictFromJson(ST_A20_WindProfileDict_t& p_cfg, const JsonDocument& p_patch) {
	C10_MUTEX_ACQUIRE_BOOL();

	JsonObjectConst j = p_patch["windProfile"].as<JsonObjectConst>();
	if (j.isNull()) {
		C10_MUTEX_RELEASE();
		return false;
	}

	p_cfg.presetCount = 0;
	if (j["presets"].is<JsonArrayConst>()) {
		JsonArrayConst v_arr = j["presets"].as<JsonArrayConst>();
		for (JsonObjectConst v_js : v_arr) {
			if (p_cfg.presetCount >= 16) break;

			ST_A20_PresetEntry_t& v_p = p_cfg.presets[p_cfg.presetCount++];
			C10_fromJson_WindPreset(v_js, v_p);
		}
	}

	p_cfg.styleCount = 0;
	if (j["styles"].is<JsonArrayConst>()) {
		JsonArrayConst v_arr = j["styles"].as<JsonArrayConst>();
		for (JsonObjectConst v_js : v_arr) {
			if (p_cfg.styleCount >= 16) break;

			ST_A20_StyleEntry_t& v_s = p_cfg.styles[p_cfg.styleCount++];

			strlcpy(v_s.name, v_js["name"] | "", sizeof(v_s.name));
			strlcpy(v_s.code, v_js["code"] | "", sizeof(v_s.code));

			JsonObjectConst v_f            = v_js["factors"].as<JsonObjectConst>();
			v_s.factors.intensityFactor   = v_f["intensityFactor"] | 1.0f;   // camelCase
			v_s.factors.variabilityFactor = v_f["variabilityFactor"] | 1.0f; // camelCase
			v_s.factors.gustFactor        = v_f["gustFactor"] | 1.0f;        // camelCase
			v_s.factors.thermalFactor     = v_f["thermalFactor"] | 1.0f;     // camelCase
		}
	}

	_dirty_windProfile = true;
	CL_D10_Logger::log(EN_L10_LOG_INFO, "[C10] WindProfileDict patched (PUT). Dirty=true");

	C10_MUTEX_RELEASE();
	return true;
}

// =====================================================
// 5. CRUD - Schedules / UserProfiles / WindProfile
//  (g_A20_config_root를 대상으로 동작)
// =====================================================

// ---------- Schedule CRUD ----------
int CL_C10_ConfigManager::addScheduleFromJson(const JsonDocument& p_doc) {
	C10_MUTEX_ACQUIRE_BOOL();

	if (!g_A20_config_root.schedules) {
		g_A20_config_root.schedules = new ST_A20_SchedulesRoot_t();
		A20_resetSchedulesDefault(*g_A20_config_root.schedules);
	}

	ST_A20_SchedulesRoot_t& v_root = *g_A20_config_root.schedules;

	if (v_root.count >= A20_Const::MAX_SCHEDULES) {
		C10_MUTEX_RELEASE();
		return -1;
	}

	JsonObjectConst js =
	    p_doc["schedule"].is<JsonObjectConst>() ? p_doc["schedule"].as<JsonObjectConst>() : p_doc.as<JsonObjectConst>();

	ST_A20_ScheduleItem_t& s = v_root.items[v_root.count];
	C10_fromJson_ScheduleItem(js, s);

	int v_index = v_root.count;
	v_root.count++;

	_dirty_schedules = true;
	CL_D10_Logger::log(EN_L10_LOG_INFO, "[C10] Schedule added (index=%d)", v_index);

	C10_MUTEX_RELEASE();
	return v_index;
}

bool CL_C10_ConfigManager::updateScheduleFromJson(uint16_t p_id, const JsonDocument& p_patch) {
	C10_MUTEX_ACQUIRE_BOOL();

	if (!g_A20_config_root.schedules) {
		C10_MUTEX_RELEASE();
		return false;
	}

	ST_A20_SchedulesRoot_t& v_root = *g_A20_config_root.schedules;

	int v_idx = -1;
	for (uint8_t v_i = 0; v_i < v_root.count; v_i++) {
		if (v_root.items[v_i].schId == p_id) {
			v_idx = (int)v_i;
			break;
		}
	}
	if (v_idx < 0) {
		C10_MUTEX_RELEASE();
		return false;
	}

	JsonObjectConst js =
	    p_patch["schedule"].is<JsonObjectConst>() ? p_patch["schedule"].as<JsonObjectConst>() : p_patch.as<JsonObjectConst>();

	C10_fromJson_ScheduleItem(js, v_root.items[(uint8_t)v_idx]);

	_dirty_schedules = true;
	CL_D10_Logger::log(EN_L10_LOG_INFO, "[C10] Schedule updated (id=%d, index=%d)", p_id, v_idx);

	C10_MUTEX_RELEASE();
	return true;
}

bool CL_C10_ConfigManager::deleteSchedule(uint16_t p_id) {
	C10_MUTEX_ACQUIRE_BOOL();

	if (!g_A20_config_root.schedules) {
		C10_MUTEX_RELEASE();
		return false;
	}

	ST_A20_SchedulesRoot_t& v_root = *g_A20_config_root.schedules;

	int v_idx = -1;
	for (uint8_t v_i = 0; v_i < v_root.count; v_i++) {
		if (v_root.items[v_i].schId == p_id) {
			v_idx = (int)v_i;
			break;
		}
	}
	if (v_idx < 0) {
		C10_MUTEX_RELEASE();
		return false;
	}

	for (uint8_t v_i = (uint8_t)v_idx + 1; v_i < v_root.count; v_i++) {
		v_root.items[v_i - 1] = v_root.items[v_i];
	}
	if (v_root.count > 0) v_root.count--;

	_dirty_schedules = true;
	CL_D10_Logger::log(EN_L10_LOG_INFO, "[C10] Schedule deleted (id=%d, index=%d)", p_id, v_idx);

	C10_MUTEX_RELEASE();
	return true;
}

// ---------- UserProfiles CRUD ----------
int CL_C10_ConfigManager::addUserProfilesFromJson(const JsonDocument& p_doc) {
	C10_MUTEX_ACQUIRE_BOOL();

	if (!g_A20_config_root.userProfiles) {
		g_A20_config_root.userProfiles = new ST_A20_UserProfilesRoot_t();
		A20_resetUserProfilesDefault(*g_A20_config_root.userProfiles);
	}

	ST_A20_UserProfilesRoot_t& v_root = *g_A20_config_root.userProfiles;

	if (v_root.count >= A20_Const::MAX_USER_PROFILES) {
		C10_MUTEX_RELEASE();
		return -1;
	}

	JsonObjectConst jp =
	    p_doc["profile"].is<JsonObjectConst>() ? p_doc["profile"].as<JsonObjectConst>() : p_doc.as<JsonObjectConst>();

	ST_A20_UserProfileItem_t& up = v_root.items[v_root.count];
	C10_fromJson_UserProfile(jp, up);

	int v_index = v_root.count;
	v_root.count++;

	_dirty_userProfiles = true;
	CL_D10_Logger::log(EN_L10_LOG_INFO, "[C10] UserProfile added (index=%d)", v_index);

	C10_MUTEX_RELEASE();
	return v_index;
}

bool CL_C10_ConfigManager::updateUserProfilesFromJson(uint16_t p_id, const JsonDocument& p_patch) {
	C10_MUTEX_ACQUIRE_BOOL();

	if (!g_A20_config_root.userProfiles) {
		C10_MUTEX_RELEASE();
		return false;
	}

	ST_A20_UserProfilesRoot_t& v_root = *g_A20_config_root.userProfiles;

	int v_idx = -1;
	for (uint8_t v_i = 0; v_i < v_root.count; v_i++) {
		if (v_root.items[v_i].profileId == p_id) {
			v_idx = (int)v_i;
			break;
		}
	}
	if (v_idx < 0) {
		C10_MUTEX_RELEASE();
		return false;
	}

	JsonObjectConst jp =
	    p_patch["profile"].is<JsonObjectConst>() ? p_patch["profile"].as<JsonObjectConst>() : p_patch.as<JsonObjectConst>();

	C10_fromJson_UserProfile(jp, v_root.items[(uint8_t)v_idx]);

	_dirty_userProfiles = true;
	CL_D10_Logger::log(EN_L10_LOG_INFO, "[C10] UserProfile updated (id=%d, index=%d)", p_id, v_idx);

	C10_MUTEX_RELEASE();
	return true;
}

bool CL_C10_ConfigManager::deleteUserProfiles(uint16_t p_id) {
	C10_MUTEX_ACQUIRE_BOOL();

	if (!g_A20_config_root.userProfiles) {
		C10_MUTEX_RELEASE();
		return false;
	}

	ST_A20_UserProfilesRoot_t& v_root = *g_A20_config_root.userProfiles;

	int v_idx = -1;
	for (uint8_t v_i = 0; v_i < v_root.count; v_i++) {
		if (v_root.items[v_i].profileId == p_id) {
			v_idx = (int)v_i;
			break;
		}
	}
	if (v_idx < 0) {
		C10_MUTEX_RELEASE();
		return false;
	}

	for (uint8_t v_i = (uint8_t)v_idx + 1; v_i < v_root.count; v_i++) {
		v_root.items[v_i - 1] = v_root.items[v_i];
	}
	if (v_root.count > 0) v_root.count--;

	_dirty_userProfiles = true;
	CL_D10_Logger::log(EN_L10_LOG_INFO, "[C10] UserProfile deleted (id=%d, index=%d)", p_id, v_idx);

	C10_MUTEX_RELEASE();
	return true;
}

// ---------- WindProfile CRUD (Preset 중심) ----------
int CL_C10_ConfigManager::addWindProfileFromJson(const JsonDocument& p_doc) {
	C10_MUTEX_ACQUIRE_BOOL();

	if (!g_A20_config_root.windDict) {
		g_A20_config_root.windDict = new ST_A20_WindProfileDict_t();
		A20_resetWindProfileDictDefault(*g_A20_config_root.windDict);
	}

	ST_A20_WindProfileDict_t& v_root = *g_A20_config_root.windDict;

	if (v_root.presetCount >= 16) {
		C10_MUTEX_RELEASE();
		return -1;
	}

	JsonObjectConst v_js =
	    p_doc["preset"].is<JsonObjectConst>() ? p_doc["preset"].as<JsonObjectConst>() : p_doc.as<JsonObjectConst>();

	ST_A20_PresetEntry_t& v_p = v_root.presets[v_root.presetCount];
	C10_fromJson_WindPreset(v_js, v_p);

	int v_index = v_root.presetCount;
	v_root.presetCount++;

	_dirty_windProfile = true;
	CL_D10_Logger::log(EN_L10_LOG_INFO, "[C10] WindPreset added (index=%d)", v_index);

	C10_MUTEX_RELEASE();
	return v_index;
}

bool CL_C10_ConfigManager::updateWindProfileFromJson(uint16_t p_id, const JsonDocument& p_patch) {
	C10_MUTEX_ACQUIRE_BOOL();

	if (!g_A20_config_root.windDict) {
		C10_MUTEX_RELEASE();
		return false;
	}

	ST_A20_WindProfileDict_t& v_root = *g_A20_config_root.windDict;

	if (p_id >= v_root.presetCount) {
		C10_MUTEX_RELEASE();
		return false;
	}

	JsonObjectConst v_js =
	    p_patch["preset"].is<JsonObjectConst>() ? p_patch["preset"].as<JsonObjectConst>() : p_patch.as<JsonObjectConst>();

	C10_fromJson_WindPreset(v_js, v_root.presets[p_id]);

	_dirty_windProfile = true;
	CL_D10_Logger::log(EN_L10_LOG_INFO, "[C10] WindPreset updated (index=%d)", p_id);

	C10_MUTEX_RELEASE();
	return true;
}

bool CL_C10_ConfigManager::deleteWindProfile(uint16_t p_id) {
	C10_MUTEX_ACQUIRE_BOOL();

	if (!g_A20_config_root.windDict) {
		C10_MUTEX_RELEASE();
		return false;
	}

	ST_A20_WindProfileDict_t& v_root = *g_A20_config_root.windDict;

	if (p_id >= v_root.presetCount) {
		C10_MUTEX_RELEASE();
		return false;
	}

	for (uint8_t v_i = p_id + 1; v_i < v_root.presetCount; v_i++) {
		v_root.presets[v_i - 1] = v_root.presets[v_i];
	}
	if (v_root.presetCount > 0) v_root.presetCount--;

	_dirty_windProfile = true;
	CL_D10_Logger::log(EN_L10_LOG_INFO, "[C10] WindPreset deleted (index=%d)", p_id);

	C10_MUTEX_RELEASE();
	return true;
}
