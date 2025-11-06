#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : C10_ConfigManager_014.h
 * 모듈약어 : C10
 * 모듈명 : Smart Nature Wind Configuration Manager (v014)
 * ------------------------------------------------------
 * 기능 요약
 *  - Schedules / UserProfiles / WindProfile JSON 로드
 *  - 프리셋/스타일 사전 로딩 및 조회 (코드 기반)
 *  - preset × style × adjust → 시뮬레이션 파라미터 해석
 *  - .bak 백업/복구, 안전 저장 API (필요 시)
 *  - NVS 연동 (현재 모드/프로파일/세그먼트/프리셋/스타일 상태 저장)
 * ------------------------------------------------------
 * [구현 규칙]
 *  - 항상 소스 시작 주석 부분 체계 유지 및 내용 업데이트
 *  - 소스 시작 주석 부분 구현규칙, 코드네이밍규칙 내용 그대로 유지, 수정금지
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

#include "A10_Const_014.h"
#include "N10_NvsManager_001.h"	 // 별도 제공된 NVS 매니저
// 외부 인스턴스 (다른 소스에서 정의)
extern CL_N10_NvsManager g_N10_nvs;

/* ======================================================
 * 전역 Config Root 정의
 * ====================================================== */
ST_A10_ConfigRoot_t g_A10_config_root;

/* ======================================================
 * 내부 헬퍼
 * ====================================================== */
static bool C10_readJsonFile(const char* path, JsonDocument& doc) {
	if (!LittleFS.exists(path))
		return false;
	File f = LittleFS.open(path, "r");
	if (!f)
		return false;
	auto e = deserializeJson(doc, f);
	f.close();
	return !e;
}

static bool C10_writeJsonFile(const char* path, const JsonDocument& doc, const char* bakPath = nullptr) {
	if (bakPath && LittleFS.exists(path)) {
		LittleFS.remove(bakPath);
		LittleFS.rename(path, bakPath);
	}
	File f = LittleFS.open(path, "w");
	if (!f)
		return false;
	serializeJsonPretty(doc, f);
	f.close();
	return true;
}

/* ======================================================
 * windDict 탐색 구현
 * ====================================================== */
int16_t C10_findPresetIndexByCode(const ST_A10_WindProfileDict_t& dict, const char* code) {
	if (!code)
		return -1;
	for (uint8_t i = 0; i < dict.preset_count; i++) {
		if (strcmp(dict.presets[i].code, code) == 0)
			return (int16_t)i;
	}
	return -1;
}
int16_t C10_findStyleIndexByCode(const ST_A10_WindProfileDict_t& dict, const char* code) {
	if (!code)
		return -1;
	for (uint8_t i = 0; i < dict.style_count; i++) {
		if (strcmp(dict.styles[i].code, code) == 0)
			return (int16_t)i;
	}
	return -1;
}

/* ======================================================
 * preset × style × adjust → ResolvedWind 구현
 * ====================================================== */
bool C10_resolveWindParams(const ST_A10_WindProfileDict_t& dict,
						   const char*					   presetCode,
						   const char*					   styleCode,
						   const ST_A10_AdjustDelta_t*	   adj,
						   ST_A10_ResolvedWind_t&		   outResolved) {
	int16_t pi = C10_findPresetIndexByCode(dict, presetCode);
	if (pi < 0)
		return false;
	int16_t si = C10_findStyleIndexByCode(dict, styleCode);
	if (si < 0)
		return false;

	const auto& base = dict.presets[pi].base;
	const auto& fac	 = dict.styles[si].factors;

	A10_safe_strlcpy(outResolved.presetCode, presetCode, sizeof(outResolved.presetCode));
	A10_safe_strlcpy(outResolved.styleCode, styleCode, sizeof(outResolved.styleCode));

	// 1) base × factor
	outResolved.wind_intensity			= base.wind_intensity * fac.intensity_factor;
	outResolved.wind_variability		= base.wind_variability * fac.variability_factor;
	outResolved.gust_frequency			= base.gust_frequency * fac.gust_factor;
	outResolved.thermal_bubble_strength = base.thermal_bubble_strength * fac.thermal_factor;

	// factor 비적용(절대치 유지) 항목
	outResolved.fan_limit				   = base.fan_limit;
	outResolved.min_fan					   = base.min_fan;
	outResolved.turbulence_length_scale	   = base.turbulence_length_scale;
	outResolved.turbulence_intensity_sigma = base.turbulence_intensity_sigma;
	outResolved.thermal_bubble_radius	   = base.thermal_bubble_radius;

	// 2) + adjust delta (있으면)
	if (adj) {
		outResolved.wind_intensity += adj->wind_intensity;
		outResolved.gust_frequency += adj->gust_frequency;
		outResolved.wind_variability += adj->wind_variability;
		outResolved.fan_limit += adj->fan_limit;
		outResolved.min_fan += adj->min_fan;
		outResolved.turbulence_length_scale += adj->turbulence_length_scale;
		outResolved.turbulence_intensity_sigma += adj->turbulence_intensity_sigma;
	}

	// 3) clamp
	outResolved.wind_intensity	 = A10_clampf(outResolved.wind_intensity, 0.0f, 100.0f);
	outResolved.wind_variability = A10_clampf(outResolved.wind_variability, 0.0f, 100.0f);
	outResolved.gust_frequency	 = A10_clampf(outResolved.gust_frequency, 0.0f, 100.0f);
	outResolved.fan_limit		 = A10_clampf(outResolved.fan_limit, 0.0f, 100.0f);
	outResolved.min_fan			 = A10_clampf(outResolved.min_fan, 0.0f, 100.0f);
	// 나머지는 물리적 범위를 따로 적용 (여기선 보수적으로 유지)
	outResolved.turbulence_intensity_sigma = A10_clampf(outResolved.turbulence_intensity_sigma, 0.0f, 5.0f);
	outResolved.thermal_bubble_strength	   = A10_clampf(outResolved.thermal_bubble_strength, 0.5f, 3.5f);

	return true;
}

/* ======================================================
 * Config Manager
 * ====================================================== */
class CL_C10_ConfigManager {
   public:
	// 로드 일괄
	static bool loadAll() {
		bool ok = true;
		ok &= loadWindProfileDict();
		ok &= loadSchedules();
		ok &= loadUserProfiles();
		return ok;
	}

	// WindProfile 사전 로드
	static bool loadWindProfileDict() {
		JsonDocument d;
		if (!C10_readJsonFile(A10_Const::WIND_PROFILE_FILE, d))
			return false;

		auto root = d["windProfile"];
		if (!root.is<JsonObjectConst>())
			return false;

		// presets
		g_A10_config_root.windDict.preset_count = 0;
		if (root["presets"].is<JsonArrayConst>()) {
			JsonArrayConst arr = root["presets"].as<JsonArrayConst>();
			for (JsonObjectConst jp : arr) {
				if (g_A10_config_root.windDict.preset_count >= 16)
					break;
				auto& dst = g_A10_config_root.windDict.presets[g_A10_config_root.windDict.preset_count++];

				A10_safe_strlcpy(dst.name, jp["name"] | "", sizeof(dst.name));
				A10_safe_strlcpy(dst.code, jp["code"] | "", sizeof(dst.code));

				dst.base.wind_intensity				= jp["base"]["wind_intensity"] | 70.0f;
				dst.base.gust_frequency				= jp["base"]["gust_frequency"] | 50.0f;
				dst.base.wind_variability			= jp["base"]["wind_variability"] | 50.0f;
				dst.base.fan_limit					= jp["base"]["fan_limit"] | 95.0f;
				dst.base.min_fan					= jp["base"]["min_fan"] | 10.0f;
				dst.base.turbulence_length_scale	= jp["base"]["turbulence_length_scale"] | 40.0f;
				dst.base.turbulence_intensity_sigma = jp["base"]["turbulence_intensity_sigma"] | 0.5f;
				dst.base.thermal_bubble_strength	= jp["base"]["thermal_bubble_strength"] | 2.0f;
				dst.base.thermal_bubble_radius		= jp["base"]["thermal_bubble_radius"] | 18.0f;
			}
		}

		// styles
		g_A10_config_root.windDict.style_count = 0;
		if (root["styles"].is<JsonArrayConst>()) {
			JsonArrayConst arr = root["styles"].as<JsonArrayConst>();
			for (JsonObjectConst js : arr) {
				if (g_A10_config_root.windDict.style_count >= 16)
					break;
				auto& dst = g_A10_config_root.windDict.styles[g_A10_config_root.windDict.style_count++];

				A10_safe_strlcpy(dst.name, js["name"] | "", sizeof(dst.name));
				A10_safe_strlcpy(dst.code, js["code"] | "", sizeof(dst.code));

				dst.factors.intensity_factor   = js["factors"]["intensity_factor"] | 1.0f;
				dst.factors.variability_factor = js["factors"]["variability_factor"] | 1.0f;
				dst.factors.gust_factor		   = js["factors"]["gust_factor"] | 1.0f;
				dst.factors.thermal_factor	   = js["factors"]["thermal_factor"] | 1.0f;
			}
		}
		return true;
	}

	// Schedules 로드
	static bool loadSchedules() {
		JsonDocument d;
		if (!C10_readJsonFile(A10_Const::SCHEDULES_FILE, d))
			return false;

		auto arr = d["schedules"];
		if (!arr.is<JsonArrayConst>()) {
			g_A10_config_root.schedules.count = 0;
			return true;
		}

		g_A10_config_root.schedules.count = 0;
		for (JsonObjectConst js : arr.as<JsonArrayConst>()) {
			if (g_A10_config_root.schedules.count >= A10_Const::MAX_SCHEDULES)
				break;
			auto& dst = g_A10_config_root.schedules.items[g_A10_config_root.schedules.count++];

			dst.schNo = js["schNo"] | 0;
			A10_safe_strlcpy(dst.name, js["name"] | "", sizeof(dst.name));
			dst.enabled = js["enabled"] | true;

			// period
			dst.period.enabled = js["period"]["enabled"] | false;
			if (dst.period.enabled) {
				// days
				for (uint8_t i = 0; i < 7; i++) {
					if (js["period"]["days"][i].isNull())
						dst.period.days[i] = 1;
					else
						dst.period.days[i] = (uint8_t)(js["period"]["days"][i] | 1);
				}
				A10_safe_strlcpy(dst.period.start_time, js["period"]["start_time"] | "00:00", sizeof(dst.period.start_time));
				A10_safe_strlcpy(dst.period.end_time, js["period"]["end_time"] | "23:59", sizeof(dst.period.end_time));
			}

			// segments
			dst.seg_count = 0;
			if (js["segments"].is<JsonArrayConst>()) {
				for (JsonObjectConst jseg : js["segments"].as<JsonArrayConst>()) {
					if (dst.seg_count >= A10_Const::MAX_SEGMENTS_PER_SCHEDULE)
						break;
					auto& sg = dst.segments[dst.seg_count++];

					sg.segNo	   = jseg["segNo"] | 0;
					sg.on_minutes  = jseg["on_minutes"] | 0;
					sg.off_minutes = jseg["off_minutes"] | 0;

					const char* mode = jseg["mode"] | "PRESET";
					sg.mode			 = (strcmp(mode, "FIXED") == 0) ? EN_A10_SEG_MODE_FIXED : EN_A10_SEG_MODE_PRESET;

					if (sg.mode == EN_A10_SEG_MODE_PRESET) {
						A10_safe_strlcpy(sg.presetCode, jseg["presetCode"] | "OCEAN", sizeof(sg.presetCode));
						A10_safe_strlcpy(sg.styleCode, jseg["styleCode"] | "BALANCE", sizeof(sg.styleCode));
						sg.adjust.wind_intensity			 = jseg["adjust"]["wind_intensity"] | 0.0f;
						sg.adjust.wind_variability			 = jseg["adjust"]["wind_variability"] | 0.0f;
						sg.adjust.gust_frequency			 = jseg["adjust"]["gust_frequency"] | 0.0f;
						sg.adjust.fan_limit					 = jseg["adjust"]["fan_limit"] | 0.0f;
						sg.adjust.min_fan					 = jseg["adjust"]["min_fan"] | 0.0f;
						sg.adjust.turbulence_length_scale	 = jseg["adjust"]["turbulence_length_scale"] | 0.0f;
						sg.adjust.turbulence_intensity_sigma = jseg["adjust"]["turbulence_intensity_sigma"] | 0.0f;
					} else {
						sg.fixed_speed = jseg["fixed_speed"] | 0.0f;
					}
				}
			}

			// autoOffTimer(통일 구조)
			dst.autoOff.timer.enabled	= js["autoOffTimer"]["timer"]["enabled"] | (bool)(js["autoOffTimer"]["enabled"] | false);
			dst.autoOff.timer.minutes	= js["autoOffTimer"]["timer"]["minutes"] | (uint32_t)(js["autoOffTimer"]["minutes"] | 0);
			dst.autoOff.offTime.enabled = js["autoOffTimer"]["offTime"]["enabled"] | false;
			A10_safe_strlcpy(dst.autoOff.offTime.time, js["autoOffTimer"]["offTime"]["time"] | "", sizeof(dst.autoOff.offTime.time));
			dst.autoOff.offTemp.enabled = js["autoOffTimer"]["offTemp"]["enabled"] | false;
			dst.autoOff.offTemp.temp	= js["autoOffTimer"]["offTemp"]["temp"] | 0.0f;

			// motion
			dst.motion.pir.enabled		  = js["motion"]["pir"]["enabled"] | false;
			dst.motion.pir.hold_sec		  = js["motion"]["pir"]["hold_sec"] | 0;
			dst.motion.ble.enabled		  = js["motion"]["ble"]["enabled"] | false;
			dst.motion.ble.rssi_threshold = js["motion"]["ble"]["rssi_threshold"] | -70;
			dst.motion.ble.hold_sec		  = js["motion"]["ble"]["hold_sec"] | 0;
		}
		return true;
	}

	// UserProfiles 로드
	static bool loadUserProfiles() {
		JsonDocument d;
		if (!C10_readJsonFile(A10_Const::USER_PROFILES_FILE, d))
			return false;

		auto root = d["userProfiles"];
		if (!root.is<JsonObjectConst>()) {
			g_A10_config_root.userProfiles.count = 0;
			return true;
		}

		g_A10_config_root.userProfiles.count = 0;
		if (root["profiles"].is<JsonArrayConst>()) {
			for (JsonObjectConst jp : root["profiles"].as<JsonArrayConst>()) {
				if (g_A10_config_root.userProfiles.count >= A10_Const::MAX_USER_PROFILES)
					break;
				auto& it = g_A10_config_root.userProfiles.items[g_A10_config_root.userProfiles.count++];

				it.profileNo = jp["profileNo"] | 0;
				A10_safe_strlcpy(it.name, jp["name"] | "", sizeof(it.name));
				it.enabled		  = jp["enabled"] | true;
				it.repeatSegments = jp["repeatSegments"] | true;

				// segments
				it.seg_count = 0;
				if (jp["segments"].is<JsonArrayConst>()) {
					for (JsonObjectConst js : jp["segments"].as<JsonArrayConst>()) {
						if (it.seg_count >= A10_Const::MAX_SEGMENTS_PER_PROFILE)
							break;
						auto& sg = it.segments[it.seg_count++];

						sg.segNo	   = js["segNo"] | 0;
						sg.on_minutes  = js["on_minutes"] | 0;
						sg.off_minutes = js["off_minutes"] | 0;

						const char* mode = js["mode"] | "PRESET";
						sg.mode			 = (strcmp(mode, "FIXED") == 0) ? EN_A10_SEG_MODE_FIXED : EN_A10_SEG_MODE_PRESET;

						if (sg.mode == EN_A10_SEG_MODE_PRESET) {
							A10_safe_strlcpy(sg.presetCode, js["presetCode"] | "OCEAN", sizeof(sg.presetCode));
							A10_safe_strlcpy(sg.styleCode, js["styleCode"] | "BALANCE", sizeof(sg.styleCode));
							sg.adjust.wind_intensity			 = js["adjust"]["wind_intensity"] | 0.0f;
							sg.adjust.wind_variability			 = js["adjust"]["wind_variability"] | 0.0f;
							sg.adjust.gust_frequency			 = js["adjust"]["gust_frequency"] | 0.0f;
							sg.adjust.fan_limit					 = js["adjust"]["fan_limit"] | 0.0f;
							sg.adjust.min_fan					 = js["adjust"]["min_fan"] | 0.0f;
							sg.adjust.turbulence_length_scale	 = js["adjust"]["turbulence_length_scale"] | 0.0f;
							sg.adjust.turbulence_intensity_sigma = js["adjust"]["turbulence_intensity_sigma"] | 0.0f;
						} else {
							sg.fixed_speed = js["fixed_speed"] | 0.0f;
						}
					}
				}

				// autoOff
				it.autoOff.timer.enabled   = jp["autoOff"]["timer"]["enabled"] | false;
				it.autoOff.timer.minutes   = jp["autoOff"]["timer"]["minutes"] | 0;
				it.autoOff.offTime.enabled = jp["autoOff"]["offTime"]["enabled"] | false;
				A10_safe_strlcpy(it.autoOff.offTime.time, jp["autoOff"]["offTime"]["time"] | "", sizeof(it.autoOff.offTime.time));
				it.autoOff.offTemp.enabled = jp["autoOff"]["offTemp"]["enabled"] | false;
				it.autoOff.offTemp.temp	   = jp["autoOff"]["offTemp"]["temp"] | 0.0f;

				// motion
				it.motion.pir.enabled		 = jp["motion"]["pir"]["enabled"] | false;
				it.motion.pir.hold_sec		 = jp["motion"]["pir"]["hold_sec"] | 0;
				it.motion.ble.enabled		 = jp["motion"]["ble"]["enabled"] | false;
				it.motion.ble.rssi_threshold = jp["motion"]["ble"]["rssi_threshold"] | -70;
				it.motion.ble.hold_sec		 = jp["motion"]["ble"]["hold_sec"] | 0;
			}
		}
		return true;
	}

	/* -----------------------------------------------
	 * NVS 도움 유틸 (상태 반영)
	 *  - 모드/프로파일/세그먼트/프리셋/스타일 상태만 저장
	 *  - Dirty → 5분 주기 lazy save (N10 내부 tick 필요)
	 * ----------------------------------------------- */
	static void nvsSetRunMode(uint8_t mode) {
		g_N10_nvs.setRunMode(mode);
	}
	static void nvsSetActiveProfile(uint8_t profileNo) {
		g_N10_nvs.setActiveProfile(profileNo);
	}
	static void nvsSetActiveSegment(uint8_t segNo) {
		g_N10_nvs.setActiveSegment(segNo);
	}
	static void nvsSetCodes(const char* presetCode, const char* styleCode) {
		g_N10_nvs.setPresetCode(presetCode ? presetCode : "");
		g_N10_nvs.setStyleCode(styleCode ? styleCode : "");
	}

	/* -----------------------------------------------
	 * 세그먼트 → 해석 파라미터로 변환
	 *  - PRESET일 때만 유효한 파라미터 반환(true)
	 *  - FIXED는 false 리턴(상위에서 고정속도 처리)
	 * ----------------------------------------------- */
	static bool resolveFromScheduleSegment(const ST_A10_ScheduleSegment_t& seg,
										   ST_A10_ResolvedWind_t&		   out) {
		if (seg.mode != EN_A10_SEG_MODE_PRESET)
			return false;
		return A10_resolveWindParams(g_A10_config_root.windDict,
									 seg.presetCode, seg.styleCode,
									 &seg.adjust, out);
	}

	static bool resolveFromUserProfileSegment(const ST_A10_UserProfileSegment_t& seg,
											  ST_A10_ResolvedWind_t&			 out) {
		if (seg.mode != EN_A10_SEG_MODE_PRESET)
			return false;
		return A10_resolveWindParams(g_A10_config_root.windDict,
									 seg.presetCode, seg.styleCode,
									 &seg.adjust, out);
	}

	/* -----------------------------------------------
	 * (선택) JSON 저장기 (툴/웹에서 수정 반영 시)
	 * ----------------------------------------------- */
	static bool saveSchedulesJson(const ST_A10_SchedulesRoot_t& src) {
		JsonDocument d;
		JsonArray	 arr = d["schedules"].to<JsonArray>();
		for (uint8_t i = 0; i < src.count; i++) {
			const auto& it = src.items[i];
			JsonObject	o  = arr.add<JsonObject>();
			o["schNo"]	   = it.schNo;
			o["name"]	   = it.name;
			o["enabled"]   = it.enabled;

			// period
			o["period"]["enabled"] = it.period.enabled;
			if (it.period.enabled) {
				JsonArray days = o["period"]["days"].to<JsonArray>();
				for (uint8_t k = 0; k < 7; k++) days.add(it.period.days[k]);
				o["period"]["start_time"] = it.period.start_time;
				o["period"]["end_time"]	  = it.period.end_time;
			}

			// segments
			JsonArray segs = o["segments"].to<JsonArray>();
			for (uint8_t s = 0; s < it.seg_count; s++) {
				const auto& sg	  = it.segments[s];
				JsonObject	js	  = segs.add<JsonObject>();
				js["segNo"]		  = sg.segNo;
				js["on_minutes"]  = sg.on_minutes;
				js["off_minutes"] = sg.off_minutes;
				js["mode"]		  = (sg.mode == EN_A10_SEG_MODE_FIXED) ? "FIXED" : "PRESET";
				if (sg.mode == EN_A10_SEG_MODE_PRESET) {
					js["presetCode"]						   = sg.presetCode;
					js["styleCode"]							   = sg.styleCode;
					js["adjust"]["wind_intensity"]			   = sg.adjust.wind_intensity;
					js["adjust"]["wind_variability"]		   = sg.adjust.wind_variability;
					js["adjust"]["gust_frequency"]			   = sg.adjust.gust_frequency;
					js["adjust"]["fan_limit"]				   = sg.adjust.fan_limit;
					js["adjust"]["min_fan"]					   = sg.adjust.min_fan;
					js["adjust"]["turbulence_length_scale"]	   = sg.adjust.turbulence_length_scale;
					js["adjust"]["turbulence_intensity_sigma"] = sg.adjust.turbulence_intensity_sigma;
				} else {
					js["fixed_speed"] = sg.fixed_speed;
				}
			}

			// autoOffTimer 통합 구조
			o["autoOffTimer"]["timer"]["enabled"]	= src.items[i].autoOff.timer.enabled;
			o["autoOffTimer"]["timer"]["minutes"]	= src.items[i].autoOff.timer.minutes;
			o["autoOffTimer"]["offTime"]["enabled"] = src.items[i].autoOff.offTime.enabled;
			o["autoOffTimer"]["offTime"]["time"]	= src.items[i].autoOff.offTime.time;
			o["autoOffTimer"]["offTemp"]["enabled"] = src.items[i].autoOff.offTemp.enabled;
			o["autoOffTimer"]["offTemp"]["temp"]	= src.items[i].autoOff.offTemp.temp;

			// motion
			o["motion"]["pir"]["enabled"]		 = it.motion.pir.enabled;
			o["motion"]["pir"]["hold_sec"]		 = it.motion.pir.hold_sec;
			o["motion"]["ble"]["enabled"]		 = it.motion.ble.enabled;
			o["motion"]["ble"]["rssi_threshold"] = it.motion.ble.rssi_threshold;
			o["motion"]["ble"]["hold_sec"]		 = it.motion.ble.hold_sec;
		}
		return C10_writeJsonFile(A10_Const::SCHEDULES_FILE, d, A10_Const::SCHEDULES_FILE_BAK);
	}

	static bool saveUserProfilesJson(const ST_A10_UserProfilesRoot_t& src) {
		JsonDocument d;
		JsonObject	 root = d["userProfiles"].to<JsonObject>();
		JsonArray	 arr  = root["profiles"].to<JsonArray>();

		for (uint8_t i = 0; i < src.count; i++) {
			const auto& it		= src.items[i];
			JsonObject	o		= arr.add<JsonObject>();
			o["profileNo"]		= it.profileNo;
			o["name"]			= it.name;
			o["enabled"]		= it.enabled;
			o["repeatSegments"] = it.repeatSegments;

			JsonArray segs = o["segments"].to<JsonArray>();
			for (uint8_t s = 0; s < it.seg_count; s++) {
				const auto& sg	  = it.segments[s];
				JsonObject	js	  = segs.add<JsonObject>();
				js["segNo"]		  = sg.segNo;
				js["on_minutes"]  = sg.on_minutes;
				js["off_minutes"] = sg.off_minutes;
				js["mode"]		  = (sg.mode == EN_A10_SEG_MODE_FIXED) ? "FIXED" : "PRESET";

				if (sg.mode == EN_A10_SEG_MODE_PRESET) {
					js["presetCode"]						   = sg.presetCode;
					js["styleCode"]							   = sg.styleCode;
					js["adjust"]["wind_intensity"]			   = sg.adjust.wind_intensity;
					js["adjust"]["wind_variability"]		   = sg.adjust.wind_variability;
					js["adjust"]["gust_frequency"]			   = sg.adjust.gust_frequency;
					js["adjust"]["fan_limit"]				   = sg.adjust.fan_limit;
					js["adjust"]["min_fan"]					   = sg.adjust.min_fan;
					js["adjust"]["turbulence_length_scale"]	   = sg.adjust.turbulence_length_scale;
					js["adjust"]["turbulence_intensity_sigma"] = sg.adjust.turbulence_intensity_sigma;
				} else {
					js["fixed_speed"] = sg.fixed_speed;
				}
			}

			// autoOff
			o["autoOff"]["timer"]["enabled"]   = it.autoOff.timer.enabled;
			o["autoOff"]["timer"]["minutes"]   = it.autoOff.timer.minutes;
			o["autoOff"]["offTime"]["enabled"] = it.autoOff.offTime.enabled;
			o["autoOff"]["offTime"]["time"]	   = it.autoOff.offTime.time;
			o["autoOff"]["offTemp"]["enabled"] = it.autoOff.offTemp.enabled;
			o["autoOff"]["offTemp"]["temp"]	   = it.autoOff.offTemp.temp;

			// motion
			o["motion"]["pir"]["enabled"]		 = it.motion.pir.enabled;
			o["motion"]["pir"]["hold_sec"]		 = it.motion.pir.hold_sec;
			o["motion"]["ble"]["enabled"]		 = it.motion.ble.enabled;
			o["motion"]["ble"]["rssi_threshold"] = it.motion.ble.rssi_threshold;
			o["motion"]["ble"]["hold_sec"]		 = it.motion.ble.hold_sec;
		}

		return C10_writeJsonFile(A10_Const::USER_PROFILES_FILE, d, A10_Const::USER_PROFILES_FILE_BAK);
	}
};
