#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : C10_ConfigManager_015.h
 * 모듈약어 : C10
 * 모듈명 : Smart Nature Wind Configuration Manager (v015)
 * ------------------------------------------------------
 * 기능 요약:
 *  - 모든 JSON 설정 파일 관리 (System, WiFi, Motion, WindProfile, Schedule, UserProfile)
 *  - 구조체 ↔ JSON 직렬화/역직렬화
 *  - Wind Preset × Style × Adjust → ResolvedWind 계산
 *  - FactoryReset / Reload 지원
 *  - Lazy-Load 하이브리드 구성 (필요 시 동적 로드)
 * ------------------------------------------------------
 * [구현 규칙]
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
#include <FS.h>
#include <LittleFS.h>

#include "A10_Const_014.h"
#include "D10_Logger_011.h"

class CL_C10_ConfigManager {
public:
	// ==========================================================
	// 초기화 및 로드
	// ==========================================================
	static bool begin() {
		if (!LittleFS.begin()) {
			CL_D10_Logger::log(EN_L10_LOG_ERR, "[C10] LittleFS mount failed");
			return false;
		}
		return true;
	}

	static bool factoryReset() {
		LittleFS.remove("/json/cfg_system_025.json");
		LittleFS.remove("/json/cfg_wifi_025.json");
		LittleFS.remove("/json/cfg_motion_025.json");
		LittleFS.remove("/json/cfg_schedules_024.json");
		LittleFS.remove("/json/cfg_uzOpProfile_025_final.json");
		LittleFS.remove("/json/cfg_windProfile_024.json");
		CL_D10_Logger::log(EN_L10_LOG_WARN, "[C10] Factory reset complete");
		return true;
	}

	// ==========================================================
	// JSON File Load / Save
	// ==========================================================
	static bool loadJsonFile(const char* p_path, JsonDocument& p_doc) {
		if (!p_path) return false;
		File v_f = LittleFS.open(p_path, "r");
		if (!v_f) return false;
		DeserializationError v_err = deserializeJson(p_doc, v_f);
		v_f.close();
		return !v_err;
	}

	static bool saveJsonFile(const char* p_path, const JsonDocument& p_doc) {
		if (!p_path) return false;
		File v_f = LittleFS.open(p_path, "w");
		if (!v_f) return false;
		serializeJsonPretty(p_doc, v_f);
		v_f.close();
		return true;
	}

	// ==========================================================
	// WindProfile Dictionary 관리
	// ==========================================================
	static bool loadWindProfileDict(ST_A10_WindProfileDict_t& p_dict) {
		JsonDocument v;
		if (!loadJsonFile(A10_Const::CFG_WINDPROFILE_FILE, v)) return false;

		memset(&p_dict, 0, sizeof(p_dict));
		p_dict.version = v["windProfile"]["version"] | "0";
		JsonArrayConst v_presets = v["windProfile"]["presets"].as<JsonArrayConst>();
		JsonArrayConst v_styles  = v["windProfile"]["styles"].as<JsonArrayConst>();

		p_dict.presetCount = v_presets.size();
		p_dict.styleCount  = v_styles.size();

		uint8_t i = 0;
		for (JsonObjectConst j : v_presets) {
			strlcpy(p_dict.presets[i].name, j["name"] | "", sizeof(p_dict.presets[i].name));
			strlcpy(p_dict.presets[i].code, j["code"] | "", sizeof(p_dict.presets[i].code));
			JsonObjectConst b = j["base"];
			p_dict.presets[i].base.wind_intensity			= b["wind_intensity"] | 0.0f;
			p_dict.presets[i].base.gust_frequency			= b["gust_frequency"] | 0.0f;
			p_dict.presets[i].base.wind_variability			= b["wind_variability"] | 0.0f;
			p_dict.presets[i].base.fan_limit				= b["fan_limit"] | 100.0f;
			p_dict.presets[i].base.min_fan					= b["min_fan"] | 0.0f;
			p_dict.presets[i].base.turbulence_length_scale	= b["turbulence_length_scale"] | 0.0f;
			p_dict.presets[i].base.turbulence_intensity_sigma= b["turbulence_intensity_sigma"] | 0.0f;
			p_dict.presets[i].base.thermal_bubble_strength	= b["thermal_bubble_strength"] | 0.0f;
			p_dict.presets[i].base.thermal_bubble_radius	= b["thermal_bubble_radius"] | 0.0f;
			if (++i >= G_A10_MAX_PRESETS) break;
		}

		i = 0;
		for (JsonObjectConst s : v_styles) {
			strlcpy(p_dict.styles[i].name, s["name"] | "", sizeof(p_dict.styles[i].name));
			strlcpy(p_dict.styles[i].code, s["code"] | "", sizeof(p_dict.styles[i].code));
			JsonObjectConst f = s["factors"];
			p_dict.styles[i].factors.intensity_factor	= f["intensity_factor"] | 1.0f;
			p_dict.styles[i].factors.variability_factor	= f["variability_factor"] | 1.0f;
			p_dict.styles[i].factors.gust_factor		= f["gust_factor"] | 1.0f;
			p_dict.styles[i].factors.thermal_factor		= f["thermal_factor"] | 1.0f;
			if (++i >= G_A10_MAX_STYLES) break;
		}
		return true;
	}

	// ==========================================================
	// Wind Parameter Resolver
	// preset × style × adjust → resolvedWind
	// ==========================================================
	static bool resolveWindParams(
		const ST_A10_WindProfileDict_t& p_dict,
		const char*					   p_presetCode,
		const char*					   p_styleCode,
		const ST_A10_AdjustDelta_t&	   p_adj,
		ST_A10_ResolvedWind_t&		   p_out
	) {
		if (!p_presetCode || !p_styleCode) return false;
		memset(&p_out, 0, sizeof(p_out));

		int v_preset = findPresetIndexByCode(p_dict, p_presetCode);
		int v_style  = findStyleIndexByCode(p_dict, p_styleCode);
		if (v_preset < 0 || v_style < 0) return false;

		const auto& P = p_dict.presets[v_preset].base;
		const auto& F = p_dict.styles[v_style].factors;

		p_out.wind_intensity			= (P.wind_intensity + p_adj.wind_intensity_delta) * F.intensity_factor;
		p_out.wind_variability			= (P.wind_variability + p_adj.wind_variability_delta) * F.variability_factor;
		p_out.gust_frequency			= (P.gust_frequency + p_adj.gust_frequency_delta) * F.gust_factor;
		p_out.fan_limit					= P.fan_limit;
		p_out.min_fan					= P.min_fan;
		p_out.turbulence_length_scale	= P.turbulence_length_scale;
		p_out.turbulence_intensity_sigma= P.turbulence_intensity_sigma;
		p_out.thermal_bubble_strength	= P.thermal_bubble_strength * F.thermal_factor;
		p_out.thermal_bubble_radius		= P.thermal_bubble_radius;
		return true;
	}

	static int16_t findPresetIndexByCode(const ST_A10_WindProfileDict_t& p_dict, const char* p_code) {
		for (int i = 0; i < p_dict.presetCount; ++i) {
			if (strcasecmp(p_dict.presets[i].code, p_code) == 0) return i;
		}
		return -1;
	}
	static int16_t findStyleIndexByCode(const ST_A10_WindProfileDict_t& p_dict, const char* p_code) {
		for (int i = 0; i < p_dict.styleCount; ++i) {
			if (strcasecmp(p_dict.styles[i].code, p_code) == 0) return i;
		}
		return -1;
	}

	// ==========================================================
	// Schedule JSON 관리 (load/save)
	// ==========================================================
	static bool C10_loadSchedulesJson(JsonDocument& p_doc) {
		return loadJsonFile(A10_Const::CFG_SCHEDULES_FILE, p_doc);
	}
	static bool C10_saveSchedulesJson(const JsonDocument& p_doc) {
		return saveJsonFile(A10_Const::CFG_SCHEDULES_FILE, p_doc);
	}

	// ==========================================================
	// User Profiles JSON 관리
	// ==========================================================
	static bool C10_loadUserProfilesJson(JsonDocument& p_doc) {
		return loadJsonFile(A10_Const::CFG_USERPROFILES_FILE, p_doc);
	}
	static bool C10_saveUserProfilesJson(const JsonDocument& p_doc) {
		return saveJsonFile(A10_Const::CFG_USERPROFILES_FILE, p_doc);
	}

	// ==========================================================
	// Reload (CT10에서 호출)
	// ==========================================================
	static bool reloadSchedulesFromFile(ST_A10_SchedulesConfig_t& outCfg) {
		JsonDocument v;
		if (!C10_loadSchedulesJson(v)) return false;
		memset(&outCfg, 0, sizeof(outCfg));
		JsonArrayConst arr = v["schedules"].as<JsonArrayConst>();
		uint8_t i = 0;
		for (JsonObjectConst j : arr) {
			strlcpy(outCfg.sch[i].name, j["name"] | "", sizeof(outCfg.sch[i].name));
			outCfg.sch[i].enabled = j["enabled"] | true;
			if (++i >= G_A10_MAX_SCHEDULES) break;
		}
		outCfg.count = i;
		return true;
	}

	static bool reloadUserProfilesFromFile(ST_A10_UserProfilesConfig_t& outCfg) {
		JsonDocument v;
		if (!C10_loadUserProfilesJson(v)) return false;
		memset(&outCfg, 0, sizeof(outCfg));
		JsonArrayConst arr = v["userProfiles"]["profiles"].as<JsonArrayConst>();
		uint8_t i = 0;
		for (JsonObjectConst j : arr) {
			strlcpy(outCfg.profile[i].name, j["name"] | "", sizeof(outCfg.profile[i].name));
			outCfg.profile[i].enabled = j["enabled"] | true;
			if (++i >= G_A10_MAX_USERPROFILES) break;
		}
		outCfg.count = i;
		return true;
	}
};
