#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : C10_ConfigManager_015.h
 * 모듈 약어 : C10
 * 모듈명 : Smart Nature Wind Configuration Manager (v015)
 * ------------------------------------------------------
 * 기능 요약:
 *  - Smart Nature Wind 설정 파일 관리 단일 모듈
 *  - windProfile / schedules / userProfiles / system / wifi / motion 관리
 *  - WindProfile 사전(preset/style) 로드 및 해석
 *  - presetCode × styleCode × adjust → ResolvedWind 계산 유틸 제공
 *  - Schedules / UserProfiles JSON ↔ 구조체 매핑
 *  - .bak 백업, 복구 및 Factory Reset 지원
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
 * ------------------------------------------------------
 */

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <string.h>

#include "A10_Const_014.h"
#include "D10_Logger_011.h"

class CL_C10_ConfigManager {
public:
	// ==================================================
	// 공통 JSON IO Helper
	// ==================================================
	static bool loadJson(const char* p_path, JsonDocument& p_doc) {
		if (!LittleFS.exists(p_path)) {
			CL_D10_Logger::log(EN_L10_LOG_WARN, "[C10] JSON not found: %s", p_path);
			return false;
		}
		File v_f = LittleFS.open(p_path, "r");
		if (!v_f) {
			CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] open fail: %s", p_path);
			return false;
		}
		DeserializationError v_err = deserializeJson(p_doc, v_f);
		v_f.close();
		if (v_err) {
			CL_D10_Logger::log(EN_L10_LOG_ERROR,
							   "[C10] parse fail: %s (%s)", p_path, v_err.c_str());
			return false;
		}
		return true;
	}

	static bool saveJson(const char* p_path, const char* p_bak, const JsonDocument& p_doc) {
		if (LittleFS.exists(p_path)) {
			LittleFS.remove(p_bak);
			LittleFS.rename(p_path, p_bak);
		}
		File v_f = LittleFS.open(p_path, "w");
		if (!v_f) {
			CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] save open fail: %s", p_path);
			return false;
		}
		if (serializeJsonPretty(p_doc, v_f) == 0) {
			CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] save write fail: %s", p_path);
			v_f.close();
			return false;
		}
		v_f.close();
		return true;
	}

	static bool restoreBackup(const char* p_bak, const char* p_path) {
		if (!LittleFS.exists(p_bak)) return false;
		LittleFS.remove(p_path);
		return LittleFS.rename(p_bak, p_path);
	}

	// ==================================================
	// Wind Profile Dict
	// 파일: cfg_windProfile_xxx.json
	// 구조: { "windProfile": { "version":.., "presets":[..], "styles":[..] } }
	// ==================================================
	static bool loadWindProfileDict(ST_A10_WindProfileDict_t& p_dict) {
		memset(&p_dict, 0, sizeof(p_dict));

		JsonDocument v_doc;
		if (!loadJson(A10_Const::CFG_WINDPROFILE_FILE, v_doc)) {
			CL_D10_Logger::log(EN_L10_LOG_WARN,
							   "[C10] loadWindProfile: using compiled defaults");
			return false;
		}

		JsonObjectConst v_wp = v_doc["windProfile"];
		if (v_wp.isNull()) {
			CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] windProfile root missing");
			return false;
		}

		// presets
		JsonArrayConst v_presets = v_wp["presets"].as<JsonArrayConst>();
		uint16_t v_pi = 0;
		for (JsonObjectConst v_p : v_presets) {
			if (v_pi >= A10_Const::MAX_WIND_PRESETS) break;
			auto& v_dst = p_dict.presets[v_pi];

			const char* v_name = v_p["name"] | "";
			const char* v_code = v_p["code"] | "";
			strlcpy(v_dst.name, v_name, sizeof(v_dst.name));
			strlcpy(v_dst.code, v_code, sizeof(v_dst.code));

			JsonObjectConst v_b = v_p["base"];
			v_dst.base.wind_intensity             = v_b["wind_intensity"]             | 70.0f;
			v_dst.base.gust_frequency             = v_b["gust_frequency"]             | 45.0f;
			v_dst.base.wind_variability           = v_b["wind_variability"]           | 50.0f;
			v_dst.base.fan_limit                  = v_b["fan_limit"]                  | 95.0f;
			v_dst.base.min_fan                    = v_b["min_fan"]                    | 10.0f;
			v_dst.base.turbulence_length_scale    = v_b["turbulence_length_scale"]    | 40.0f;
			v_dst.base.turbulence_intensity_sigma = v_b["turbulence_intensity_sigma"] | 0.5f;
			v_dst.base.thermal_bubble_strength    = v_b["thermal_bubble_strength"]    | 2.0f;
			v_dst.base.thermal_bubble_radius      = v_b["thermal_bubble_radius"]      | 18.0f;

			v_pi++;
		}
		p_dict.presetCount = v_pi;

		// styles
		JsonArrayConst v_styles = v_wp["styles"].as<JsonArrayConst>();
		uint16_t v_si = 0;
		for (JsonObjectConst v_s : v_styles) {
			if (v_si >= A10_Const::MAX_WIND_STYLES) break;
			auto& v_dst = p_dict.styles[v_si];

			const char* v_name = v_s["name"] | "";
			const char* v_code = v_s["code"] | "";
			strlcpy(v_dst.name, v_name, sizeof(v_dst.name));
			strlcpy(v_dst.code, v_code, sizeof(v_dst.code));

			JsonObjectConst v_f = v_s["factors"];
			v_dst.factors.intensity_factor  = v_f["intensity_factor"]  | 1.0f;
			v_dst.factors.variability_factor= v_f["variability_factor"]| 1.0f;
			v_dst.factors.gust_factor       = v_f["gust_factor"]       | 1.0f;
			v_dst.factors.thermal_factor    = v_f["thermal_factor"]    | 1.0f;

			v_si++;
		}
		p_dict.styleCount = v_si;

		CL_D10_Logger::log(EN_L10_LOG_INFO,
						   "[C10] WindProfile loaded: %u presets, %u styles",
						   (unsigned)p_dict.presetCount,
						   (unsigned)p_dict.styleCount);
		return true;
	}

	static bool saveWindProfileDict(const ST_A10_WindProfileDict_t& p_dict) {
		JsonDocument v_doc;
		JsonObject v_wp = v_doc["windProfile"].to<JsonObject>();

		// presets
		for (uint16_t v_i = 0; v_i < p_dict.presetCount; v_i++) {
			const auto& v_p = p_dict.presets[v_i];
			JsonObject v_jp = v_wp["presets"][v_i].to<JsonObject>();
			v_jp["name"] = v_p.name;
			v_jp["code"] = v_p.code;

			JsonObject v_b = v_jp["base"].to<JsonObject>();
			v_b["wind_intensity"]             = v_p.base.wind_intensity;
			v_b["gust_frequency"]             = v_p.base.gust_frequency;
			v_b["wind_variability"]           = v_p.base.wind_variability;
			v_b["fan_limit"]                  = v_p.base.fan_limit;
			v_b["min_fan"]                    = v_p.base.min_fan;
			v_b["turbulence_length_scale"]    = v_p.base.turbulence_length_scale;
			v_b["turbulence_intensity_sigma"] = v_p.base.turbulence_intensity_sigma;
			v_b["thermal_bubble_strength"]    = v_p.base.thermal_bubble_strength;
			v_b["thermal_bubble_radius"]      = v_p.base.thermal_bubble_radius;
		}

		// styles
		for (uint16_t v_i = 0; v_i < p_dict.styleCount; v_i++) {
			const auto& v_s = p_dict.styles[v_i];
			JsonObject v_js = v_wp["styles"][v_i].to<JsonObject>();
			v_js["name"] = v_s.name;
			v_js["code"] = v_s.code;

			JsonObject v_f = v_js["factors"].to<JsonObject>();
			v_f["intensity_factor"]   = v_s.factors.intensity_factor;
			v_f["variability_factor"] = v_s.factors.variability_factor;
			v_f["gust_factor"]        = v_s.factors.gust_factor;
			v_f["thermal_factor"]     = v_s.factors.thermal_factor;
		}

		return saveJson(A10_Const::CFG_WINDPROFILE_FILE,
						A10_Const::CFG_WINDPROFILE_FILE_BAK,
						v_doc);
	}

	// preset/style index
	static int16_t findPresetIndexByCode(const ST_A10_WindProfileDict_t& p_dict,
										 const char* p_code) {
		if (!p_code || !p_code[0]) return -1;
		for (uint16_t v_i = 0; v_i < p_dict.presetCount; v_i++) {
			if (strcasecmp(p_code, p_dict.presets[v_i].code) == 0) {
				return (int16_t)v_i;
			}
		}
		return -1;
	}

	static int16_t findStyleIndexByCode(const ST_A10_WindProfileDict_t& p_dict,
										const char* p_code) {
		if (!p_code || !p_code[0]) return -1;
		for (uint16_t v_i = 0; v_i < p_dict.styleCount; v_i++) {
			if (strcasecmp(p_code, p_dict.styles[v_i].code) == 0) {
				return (int16_t)v_i;
			}
		}
		return -1;
	}

	// preset × style × adjust → resolvedWind
	static bool resolveWindParams(const ST_A10_WindProfileDict_t& p_dict,
								  const char*					 p_presetCode,
								  const char*					 p_styleCode,
								  const ST_A10_AdjustDelta_t*	 p_adj,
								  ST_A10_ResolvedWind_t&		 p_out) {
		memset(&p_out, 0, sizeof(p_out));

		int16_t v_pi = findPresetIndexByCode(p_dict, p_presetCode);
		if (v_pi < 0) return false;
		const auto& v_p = p_dict.presets[v_pi];

		int16_t v_si = findStyleIndexByCode(p_dict, p_styleCode);
		const ST_A10_StyleFactor_t* v_sf = nullptr;
		if (v_si >= 0) {
			v_sf = &p_dict.styles[v_si].factors;
		}

		// base
		p_out.wind_intensity             = v_p.base.wind_intensity;
		p_out.gust_frequency             = v_p.base.gust_frequency;
		p_out.wind_variability           = v_p.base.wind_variability;
		p_out.fan_limit                  = v_p.base.fan_limit;
		p_out.min_fan                    = v_p.base.min_fan;
		p_out.turbulence_length_scale    = v_p.base.turbulence_length_scale;
		p_out.turbulence_intensity_sigma = v_p.base.turbulence_intensity_sigma;
		p_out.thermal_bubble_strength    = v_p.base.thermal_bubble_strength;
		p_out.thermal_bubble_radius      = v_p.base.thermal_bubble_radius;

		// style factors
		if (v_sf) {
			p_out.wind_intensity   *= v_sf->intensity_factor;
			p_out.wind_variability *= v_sf->variability_factor;
			p_out.gust_frequency   *= v_sf->gust_factor;
			p_out.thermal_bubble_strength *= v_sf->thermal_factor;
		}

		// adjust delta
		if (p_adj) {
			p_out.wind_intensity   += p_adj->wind_intensity;
			p_out.wind_variability += p_adj->wind_variability;
			p_out.gust_frequency   += p_adj->gust_frequency;
			p_out.fan_limit        += p_adj->fan_limit;
			p_out.min_fan          += p_adj->min_fan;
			// 필요 시 다른 항목도 delta 허용 가능 (스펙에 맞게 조정)
		}

		// clamp
		auto clampf = [](float v, float lo, float hi) {
			return (v < lo) ? lo : (v > hi ? hi : v);
		};

		p_out.wind_intensity   = clampf(p_out.wind_intensity,   0.0f, 100.0f);
		p_out.wind_variability = clampf(p_out.wind_variability, 0.0f, 100.0f);
		p_out.gust_frequency   = clampf(p_out.gust_frequency,   0.0f, 100.0f);
		p_out.fan_limit        = clampf(p_out.fan_limit,        0.0f, 100.0f);
		p_out.min_fan          = clampf(p_out.min_fan,          0.0f, 100.0f);

		return true;
	}

	// ==================================================
	// Schedules
	// 파일: cfg_schedules_024.json
	// 스펙에 맞게 full parse
	// ==================================================
	static bool loadSchedules(ST_A10_SchedulesConfig_t& p_cfg) {
		memset(&p_cfg, 0, sizeof(p_cfg));

		JsonDocument v_doc;
		if (!loadJson(A10_Const::CFG_SCHEDULES_FILE, v_doc)) {
			return false;
		}

		JsonArrayConst v_arr = v_doc["schedules"].as<JsonArrayConst>();
		if (v_arr.isNull()) return false;

		uint8_t v_idx = 0;
		for (JsonObjectConst v_js : v_arr) {
			if (v_idx >= A10_Const::MAX_SCHEDULES) break;
			auto& v_s = p_cfg.items[v_idx];

			v_s.schNo   = v_js["schNo"] | 0;
			strlcpy(v_s.name, v_js["name"] | "", sizeof(v_s.name));
			v_s.enabled = v_js["enabled"] | false;

			// period
			JsonObjectConst v_p = v_js["period"];
			if (!v_p.isNull()) {
				v_s.period.enabled = v_p["enabled"] | false;
				JsonArrayConst v_days = v_p["days"].as<JsonArrayConst>();
				for (uint8_t d = 0; d < 7; d++) {
					v_s.period.days[d] =
						(v_days.isNull() || d >= v_days.size())
						? 1
						: (uint8_t)(v_days[d] | 1);
				}
				strlcpy(v_s.period.start_time,
						v_p["start_time"] | "00:00",
						sizeof(v_s.period.start_time));
				strlcpy(v_s.period.end_time,
						v_p["end_time"] | "23:59",
						sizeof(v_s.period.end_time));
			}

			// segments
			v_s.segmentCount = 0;
			JsonArrayConst v_segs = v_js["segments"].as<JsonArrayConst>();
			if (!v_segs.isNull()) {
				for (JsonObjectConst v_jseg : v_segs) {
					if (v_s.segmentCount >= A10_Const::MAX_SEGMENTS) break;
					auto& v_seg = v_s.segments[v_s.segmentCount];

					v_seg.segNo       = v_jseg["segNo"]       | 0;
					v_seg.on_minutes  = v_jseg["on_minutes"]  | 0;
					v_seg.off_minutes = v_jseg["off_minutes"] | 0;

					const char* v_mode = v_jseg["mode"] | "OFF";
					strlcpy(v_seg.mode, v_mode, sizeof(v_seg.mode));

					// PRESET 모드
					if (strcasecmp(v_mode, "PRESET") == 0) {
						strlcpy(v_seg.presetCode,
								v_jseg["presetCode"] | "",
								sizeof(v_seg.presetCode));
						strlcpy(v_seg.styleCode,
								v_jseg["styleCode"] | "",
								sizeof(v_seg.styleCode));

						JsonObjectConst v_adj = v_jseg["adjust"];
						if (!v_adj.isNull()) {
							v_seg.adjust.wind_intensity   = v_adj["wind_intensity"]   | 0.0f;
							v_seg.adjust.wind_variability = v_adj["wind_variability"] | 0.0f;
							v_seg.adjust.gust_frequency   = v_adj["gust_frequency"]   | 0.0f;
							v_seg.adjust.fan_limit        = v_adj["fan_limit"]        | 0.0f;
							v_seg.adjust.min_fan          = v_adj["min_fan"]          | 0.0f;
							v_seg.adjust.valid            = true;
						} else {
							memset(&v_seg.adjust, 0, sizeof(v_seg.adjust));
						}
					}
					// FIXED 모드
					else if (strcasecmp(v_mode, "FIXED") == 0) {
						v_seg.fixed_speed = v_jseg["fixed_speed"] | 0.0f;
						memset(&v_seg.adjust, 0, sizeof(v_seg.adjust));
					}
					// OFF 모드 or 기타
					else {
						memset(&v_seg.adjust, 0, sizeof(v_seg.adjust));
						v_seg.fixed_speed = 0.0f;
					}

					v_s.segmentCount++;
				}
			}

			// autoOff (userProfiles와 동일 구조)
			JsonObjectConst v_ao = v_js["autoOff"];
			if (!v_ao.isNull()) {
				JsonObjectConst v_t = v_ao["timer"];
				if (!v_t.isNull()) {
					v_s.autoOff.timer.enabled =
						v_t["enabled"] | false;
					v_s.autoOff.timer.minutes =
						v_t["minutes"] | 0;
				}
				JsonObjectConst v_ot = v_ao["offTime"];
				if (!v_ot.isNull()) {
					v_s.autoOff.offTime.enabled =
						v_ot["enabled"] | false;
					strlcpy(v_s.autoOff.offTime.time,
							v_ot["time"] | "00:00",
							sizeof(v_s.autoOff.offTime.time));
				}
				JsonObjectConst v_tp = v_ao["offTemp"];
				if (!v_tp.isNull()) {
					v_s.autoOff.offTemp.enabled =
						v_tp["enabled"] | false;
					v_s.autoOff.offTemp.temp =
						v_tp["temp"] | 0.0f;
				}
			}

			// motion
			JsonObjectConst v_m = v_js["motion"];
			if (!v_m.isNull()) {
				JsonObjectConst v_mp = v_m["pir"];
				if (!v_mp.isNull()) {
					v_s.motion.pir.enabled  = v_mp["enabled"]  | false;
					v_s.motion.pir.hold_sec = v_mp["hold_sec"] | 0;
				}
				JsonObjectConst v_mb = v_m["ble"];
				if (!v_mb.isNull()) {
					v_s.motion.ble.enabled        = v_mb["enabled"]        | false;
					v_s.motion.ble.rssi_threshold = v_mb["rssi_threshold"] | -70;
					v_s.motion.ble.hold_sec       = v_mb["hold_sec"]       | 0;
				}
			}

			v_idx++;
		}
		p_cfg.count = v_idx;
		return true;
	}

	static bool saveSchedules(const ST_A10_SchedulesConfig_t& p_cfg) {
		JsonDocument v_doc;
		for (uint8_t v_i = 0; v_i < p_cfg.count; v_i++) {
			const auto& v_s = p_cfg.items[v_i];
			JsonObject v_js = v_doc["schedules"][v_i].to<JsonObject>();

			v_js["schNo"]   = v_s.schNo;
			v_js["name"]    = v_s.name;
			v_js["enabled"] = v_s.enabled;

			JsonObject v_p = v_js["period"].to<JsonObject>();
			v_p["enabled"] = v_s.period.enabled;
			for (uint8_t d = 0; d < 7; d++) {
				v_p["days"][d] = v_s.period.days[d];
			}
			v_p["start_time"] = v_s.period.start_time;
			v_p["end_time"]   = v_s.period.end_time;

			for (uint8_t k = 0; k < v_s.segmentCount; k++) {
				const auto& v_seg = v_s.segments[k];
				JsonObject v_jseg = v_js["segments"][k].to<JsonObject>();
				v_jseg["segNo"]       = v_seg.segNo;
				v_jseg["on_minutes"]  = v_seg.on_minutes;
				v_jseg["off_minutes"] = v_seg.off_minutes;
				v_jseg["mode"]        = v_seg.mode;

				if (strcasecmp(v_seg.mode, "PRESET") == 0) {
					v_jseg["presetCode"] = v_seg.presetCode;
					v_jseg["styleCode"]  = v_seg.styleCode;

					if (v_seg.adjust.valid) {
						JsonObject v_adj = v_jseg["adjust"].to<JsonObject>();
						v_adj["wind_intensity"]   = v_seg.adjust.wind_intensity;
						v_adj["wind_variability"] = v_seg.adjust.wind_variability;
						v_adj["gust_frequency"]   = v_seg.adjust.gust_frequency;
						v_adj["fan_limit"]        = v_seg.adjust.fan_limit;
						v_adj["min_fan"]          = v_seg.adjust.min_fan;
					}
				} else if (strcasecmp(v_seg.mode, "FIXED") == 0) {
					v_jseg["fixed_speed"] = v_seg.fixed_speed;
				}
			}

			JsonObject v_ao = v_js["autoOff"].to<JsonObject>();
			v_ao["timer"]["enabled"] = v_s.autoOff.timer.enabled;
			v_ao["timer"]["minutes"] = v_s.autoOff.timer.minutes;

			v_ao["offTime"]["enabled"] = v_s.autoOff.offTime.enabled;
			v_ao["offTime"]["time"]    = v_s.autoOff.offTime.time;

			v_ao["offTemp"]["enabled"] = v_s.autoOff.offTemp.enabled;
			v_ao["offTemp"]["temp"]    = v_s.autoOff.offTemp.temp;

			JsonObject v_m = v_js["motion"].to<JsonObject>();
			v_m["pir"]["enabled"]  = v_s.motion.pir.enabled;
			v_m["pir"]["hold_sec"] = v_s.motion.pir.hold_sec;
			v_m["ble"]["enabled"]  = v_s.motion.ble.enabled;
			v_m["ble"]["rssi_threshold"] = v_s.motion.ble.rssi_threshold;
			v_m["ble"]["hold_sec"]       = v_s.motion.ble.hold_sec;
		}

		return saveJson(A10_Const::CFG_SCHEDULES_FILE,
						A10_Const::CFG_SCHEDULES_FILE_BAK,
						v_doc);
	}

	// ==================================================
	// User Profiles
	// 파일: cfg_uzOpProfile_025_final.json
	// ==================================================
	static bool loadUserProfiles(ST_A10_UserProfilesConfig_t& p_cfg) {
		memset(&p_cfg, 0, sizeof(p_cfg));

		JsonDocument v_doc;
		if (!loadJson(A10_Const::CFG_USER_PROFILES_FILE, v_doc)) {
			return false;
		}

		JsonObjectConst v_root = v_doc["userProfiles"];
		if (v_root.isNull()) return false;

		JsonArrayConst v_arr = v_root["profiles"].as<JsonArrayConst>();
		if (v_arr.isNull()) return false;

		uint8_t v_idx = 0;
		for (JsonObjectConst v_jp : v_arr) {
			if (v_idx >= A10_Const::MAX_USER_PROFILES) break;
			auto& v_p = p_cfg.items[v_idx];

			v_p.profileNo = v_jp["profileNo"] | 0;
			strlcpy(v_p.name, v_jp["name"] | "", sizeof(v_p.name));
			v_p.enabled        = v_jp["enabled"]        | false;
			v_p.repeatSegments = v_jp["repeatSegments"] | false;

			// segments
			v_p.segmentCount = 0;
			JsonArrayConst v_segs = v_jp["segments"].as<JsonArrayConst>();
			if (!v_segs.isNull()) {
				for (JsonObjectConst v_js : v_segs) {
					if (v_p.segmentCount >= A10_Const::MAX_SEGMENTS) break;
					auto& v_seg = v_p.segments[v_p.segmentCount];

					v_seg.segNo       = v_js["segNo"]       | 0;
					v_seg.on_minutes  = v_js["on_minutes"]  | 0;
					v_seg.off_minutes = v_js["off_minutes"] | 0;
					const char* v_mode = v_js["mode"] | "OFF";
					strlcpy(v_seg.mode, v_mode, sizeof(v_seg.mode));

					if (strcasecmp(v_mode, "PRESET") == 0) {
						strlcpy(v_seg.presetCode,
								v_js["presetCode"] | "",
								sizeof(v_seg.presetCode));
						strlcpy(v_seg.styleCode,
								v_js["styleCode"] | "",
								sizeof(v_seg.styleCode));

						JsonObjectConst v_adj = v_js["adjust"];
						if (!v_adj.isNull()) {
							v_seg.adjust.wind_intensity   = v_adj["wind_intensity"]   | 0.0f;
							v_seg.adjust.wind_variability = v_adj["wind_variability"] | 0.0f;
							v_seg.adjust.gust_frequency   = v_adj["gust_frequency"]   | 0.0f;
							v_seg.adjust.fan_limit        = v_adj["fan_limit"]        | 0.0f;
							v_seg.adjust.min_fan          = v_adj["min_fan"]          | 0.0f;
							v_seg.adjust.valid            = true;
						} else {
							memset(&v_seg.adjust, 0, sizeof(v_seg.adjust));
						}
					} else if (strcasecmp(v_mode, "FIXED") == 0) {
						v_seg.fixed_speed = v_js["fixed_speed"] | 0.0f;
						memset(&v_seg.adjust, 0, sizeof(v_seg.adjust));
					} else {
						memset(&v_seg.adjust, 0, sizeof(v_seg.adjust));
						v_seg.fixed_speed = 0.0f;
					}

					v_p.segmentCount++;
				}
			}

			// autoOff
			JsonObjectConst v_ao = v_jp["autoOff"];
			if (!v_ao.isNull()) {
				JsonObjectConst v_t = v_ao["timer"];
				if (!v_t.isNull()) {
					v_p.autoOff.timer.enabled =
						v_t["enabled"] | false;
					v_p.autoOff.timer.minutes =
						v_t["minutes"] | 0;
				}
				JsonObjectConst v_ot = v_ao["offTime"];
				if (!v_ot.isNull()) {
					v_p.autoOff.offTime.enabled =
						v_ot["enabled"] | false;
					strlcpy(v_p.autoOff.offTime.time,
							v_ot["time"] | "00:00",
							sizeof(v_p.autoOff.offTime.time));
				}
				JsonObjectConst v_tp = v_ao["offTemp"];
				if (!v_tp.isNull()) {
					v_p.autoOff.offTemp.enabled =
						v_tp["enabled"] | false;
					v_p.autoOff.offTemp.temp =
						v_tp["temp"] | 0.0f;
				}
			}

			// motion
			JsonObjectConst v_m = v_jp["motion"];
			if (!v_m.isNull()) {
				JsonObjectConst v_mp = v_m["pir"];
				if (!v_mp.isNull()) {
					v_p.motion.pir.enabled  = v_mp["enabled"]  | false;
					v_p.motion.pir.hold_sec = v_mp["hold_sec"] | 0;
				}
				JsonObjectConst v_mb = v_m["ble"];
				if (!v_mb.isNull()) {
					v_p.motion.ble.enabled        = v_mb["enabled"]        | false;
					v_p.motion.ble.rssi_threshold = v_mb["rssi_threshold"] | -70;
					v_p.motion.ble.hold_sec       = v_mb["hold_sec"]       | 0;
				}
			}

			v_idx++;
		}

		p_cfg.count = v_idx;
		return true;
	}

	static bool saveUserProfiles(const ST_A10_UserProfilesConfig_t& p_cfg) {
		JsonDocument v_doc;
		JsonObject v_root = v_doc["userProfiles"].to<JsonObject>();
		JsonArray v_arr   = v_root["profiles"].to<JsonArray>();

		for (uint8_t v_i = 0; v_i < p_cfg.count; v_i++) {
			const auto& v_p = p_cfg.items[v_i];
			JsonObject v_jp = v_arr.add<JsonObject>();

			v_jp["profileNo"]     = v_p.profileNo;
			v_jp["name"]          = v_p.name;
			v_jp["enabled"]       = v_p.enabled;
			v_jp["repeatSegments"]= v_p.repeatSegments;

			JsonArray v_segs = v_jp["segments"].to<JsonArray>();
			for (uint8_t k = 0; k < v_p.segmentCount; k++) {
				const auto& v_seg = v_p.segments[k];
				JsonObject v_js = v_segs.add<JsonObject>();
				v_js["segNo"]       = v_seg.segNo;
				v_js["on_minutes"]  = v_seg.on_minutes;
				v_js["off_minutes"] = v_seg.off_minutes;
				v_js["mode"]        = v_seg.mode;

				if (strcasecmp(v_seg.mode, "PRESET") == 0) {
					v_js["presetCode"] = v_seg.presetCode;
					v_js["styleCode"]  = v_seg.styleCode;
					if (v_seg.adjust.valid) {
						JsonObject v_adj = v_js["adjust"].to<JsonObject>();
						v_adj["wind_intensity"]   = v_seg.adjust.wind_intensity;
						v_adj["wind_variability"] = v_seg.adjust.wind_variability;
						v_adj["gust_frequency"]   = v_seg.adjust.gust_frequency;
						v_adj["fan_limit"]        = v_seg.adjust.fan_limit;
						v_adj["min_fan"]          = v_seg.adjust.min_fan;
					}
				} else if (strcasecmp(v_seg.mode, "FIXED") == 0) {
					v_js["fixed_speed"] = v_seg.fixed_speed;
				}
			}

			JsonObject v_ao = v_jp["autoOff"].to<JsonObject>();
			v_ao["timer"]["enabled"] = v_p.autoOff.timer.enabled;
			v_ao["timer"]["minutes"] = v_p.autoOff.timer.minutes;

			v_ao["offTime"]["enabled"] = v_p.autoOff.offTime.enabled;
			v_ao["offTime"]["time"]    = v_p.autoOff.offTime.time;

			v_ao["offTemp"]["enabled"] = v_p.autoOff.offTemp.enabled;
			v_ao["offTemp"]["temp"]    = v_p.autoOff.offTemp.temp;

			JsonObject v_m = v_jp["motion"].to<JsonObject>();
			v_m["pir"]["enabled"]  = v_p.motion.pir.enabled;
			v_m["pir"]["hold_sec"] = v_p.motion.pir.hold_sec;
			v_m["ble"]["enabled"]  = v_p.motion.ble.enabled;
			v_m["ble"]["rssi_threshold"] = v_p.motion.ble.rssi_threshold;
			v_m["ble"]["hold_sec"]       = v_p.motion.ble.hold_sec;
		}

		return saveJson(A10_Const::CFG_USER_PROFILES_FILE,
						A10_Const::CFG_USER_PROFILES_FILE_BAK,
						v_doc);
	}

	// ==================================================
	// System / WiFi / Motion (필수 최소구현)
	// ==================================================
	static bool loadSystem(ST_A10_SystemConfig& p_cfg) {
		JsonDocument v_doc;
		if (!loadJson(A10_Const::CFG_SYSTEM_FILE, v_doc)) return false;
		JsonObjectConst j = v_doc.as<JsonObjectConst>();

		memset(&p_cfg, 0, sizeof(p_cfg));
		// 필드명은 기존 스펙 유지 (필요 항목만 사용)
		strlcpy(p_cfg.meta.version,     j["meta"]["version"]     | "", sizeof(p_cfg.meta.version));
		strlcpy(p_cfg.meta.device_name, j["meta"]["device_name"] | "", sizeof(p_cfg.meta.device_name));
		strlcpy(p_cfg.meta.last_update, j["meta"]["last_update"] | "", sizeof(p_cfg.meta.last_update));

		strlcpy(p_cfg.time.ntp_server, j["time"]["ntp_server"] | "pool.ntp.org", sizeof(p_cfg.time.ntp_server));
		strlcpy(p_cfg.time.timezone,   j["time"]["timezone"]   | "Asia/Seoul",  sizeof(p_cfg.time.timezone));
		p_cfg.time.sync_interval_min   = j["time"]["sync_interval_min"] | 60;

		return true;
	}

	static bool saveSystem(const ST_A10_SystemConfig& p_cfg) {
		JsonDocument v_doc;
		JsonObject j = v_doc.to<JsonObject>();

		j["meta"]["version"]     = p_cfg.meta.version;
		j["meta"]["device_name"] = p_cfg.meta.device_name;
		j["meta"]["last_update"] = p_cfg.meta.last_update;

		j["time"]["ntp_server"]        = p_cfg.time.ntp_server;
		j["time"]["timezone"]          = p_cfg.time.timezone;
		j["time"]["sync_interval_min"] = p_cfg.time.sync_interval_min;

		return saveJson(A10_Const::CFG_SYSTEM_FILE,
						A10_Const::CFG_SYSTEM_FILE_BAK,
						v_doc);
	}

	static bool loadWifi(ST_A10_WifiConfig& p_cfg) {
		JsonDocument v_doc;
		if (!loadJson(A10_Const::CFG_WIFI_FILE, v_doc)) return false;
		JsonObjectConst j = v_doc["wifi"];

		memset(&p_cfg, 0, sizeof(p_cfg));
		p_cfg.wifiMode = (EN_A10_WIFI_MODE_t)(j["wifiMode"] | 0);
		strlcpy(p_cfg.wifiModeDesc,
				j["wifiModeDesc"] | "",
				sizeof(p_cfg.wifiModeDesc));

		strlcpy(p_cfg.ap.ssid,     j["ap"]["ssid"]     | "", sizeof(p_cfg.ap.ssid));
		strlcpy(p_cfg.ap.password, j["ap"]["password"] | "", sizeof(p_cfg.ap.password));

		p_cfg.sta_count = 0;
		JsonArrayConst v_sta = j["sta"].as<JsonArrayConst>();
		if (!v_sta.isNull()) {
			for (JsonObjectConst v_s : v_sta) {
				if (p_cfg.sta_count >= A10_Const::MAX_STA_NETWORKS) break;
				strlcpy(p_cfg.sta[p_cfg.sta_count].ssid,
						v_s["ssid"] | "",
						sizeof(p_cfg.sta[0].ssid));
				strlcpy(p_cfg.sta[p_cfg.sta_count].pass,
						v_s["pass"] | "",
						sizeof(p_cfg.sta[0].pass));
				p_cfg.sta_count++;
			}
		}
		return true;
	}

	static bool saveWifi(const ST_A10_WifiConfig& p_cfg) {
		JsonDocument v_doc;
		JsonObject j = v_doc["wifi"].to<JsonObject>();

		j["wifiMode"]    = p_cfg.wifiMode;
		j["wifiModeDesc"]= p_cfg.wifiModeDesc;

		j["ap"]["ssid"]     = p_cfg.ap.ssid;
		j["ap"]["password"] = p_cfg.ap.password;

		for (uint8_t i = 0; i < p_cfg.sta_count; i++) {
			j["sta"][i]["ssid"] = p_cfg.sta[i].ssid;
			j["sta"][i]["pass"] = p_cfg.sta[i].pass;
		}

		return saveJson(A10_Const::CFG_WIFI_FILE,
						A10_Const::CFG_WIFI_FILE_BAK,
						v_doc);
	}

	static bool loadMotion(ST_A10_MotionConfig& p_cfg) {
		JsonDocument v_doc;
		if (!loadJson(A10_Const::CFG_MOTION_FILE, v_doc)) return false;

		JsonObjectConst j = v_doc["motion"];
		memset(&p_cfg, 0, sizeof(p_cfg));

		p_cfg.enabled = j["enabled"] | true;

		p_cfg.pir.enabled  = j["pir"]["enabled"]  | true;
		p_cfg.pir.hold_sec = j["pir"]["hold_sec"] | 120;

		p_cfg.ble.enabled = j["ble"]["enabled"] | true;

		JsonObjectConst r = j["ble"]["rssi"];
		p_cfg.ble.rssi.on             = r["on"]             | -65;
		p_cfg.ble.rssi.off            = r["off"]            | -75;
		p_cfg.ble.rssi.avg_count      = r["avg_count"]      | 8;
		p_cfg.ble.rssi.persist_count  = r["persist_count"]  | 5;
		p_cfg.ble.rssi.exit_delay_sec = r["exit_delay_sec"] | 12;

		p_cfg.ble.trusted_count = 0;
		JsonArrayConst v_td = j["ble"]["trusted_devices"].as<JsonArrayConst>();
		if (!v_td.isNull()) {
			for (JsonObjectConst v_d : v_td) {
				if (p_cfg.ble.trusted_count >= A10_Const::MAX_BLE_DEVICES) break;
				auto& v = p_cfg.ble.trusted_devices[p_cfg.ble.trusted_count];
				strlcpy(v.alias, v_d["alias"] | "", sizeof(v.alias));
				strlcpy(v.name,  v_d["name"]  | "", sizeof(v.name));
				strlcpy(v.mac,   v_d["mac"]   | "", sizeof(v.mac));
				strlcpy(v.manuf_prefix,
						v_d["manuf_prefix"] | "",
						sizeof(v.manuf_prefix));
				v.prefix_len = v_d["prefix_len"] | 0;
				v.enabled    = v_d["enabled"]    | true;
				p_cfg.ble.trusted_count++;
			}
		}
		return true;
	}

	static bool saveMotion(const ST_A10_MotionConfig& p_cfg) {
		JsonDocument v_doc;
		JsonObject j = v_doc["motion"].to<JsonObject>();

		j["enabled"] = p_cfg.enabled;

		j["pir"]["enabled"]  = p_cfg.pir.enabled;
		j["pir"]["hold_sec"] = p_cfg.pir.hold_sec;

		j["ble"]["enabled"]              = p_cfg.ble.enabled;
		j["ble"]["rssi"]["on"]           = p_cfg.ble.rssi.on;
		j["ble"]["rssi"]["off"]          = p_cfg.ble.rssi.off;
		j["ble"]["rssi"]["avg_count"]    = p_cfg.ble.rssi.avg_count;
		j["ble"]["rssi"]["persist_count"]= p_cfg.ble.rssi.persist_count;
		j["ble"]["rssi"]["exit_delay_sec"]=p_cfg.ble.rssi.exit_delay_sec;

		for (uint8_t i = 0; i < p_cfg.ble.trusted_count; i++) {
			const auto& v = p_cfg.ble.trusted_devices[i];
			JsonObject t = j["ble"]["trusted_devices"][i].to<JsonObject>();
			t["alias"]        = v.alias;
			t["name"]         = v.name;
			t["mac"]          = v.mac;
			t["manuf_prefix"] = v.manuf_prefix;
			t["prefix_len"]   = v.prefix_len;
			t["enabled"]      = v.enabled;
		}

		return saveJson(A10_Const::CFG_MOTION_FILE,
						A10_Const::CFG_MOTION_FILE_BAK,
						v_doc);
	}

	// ==================================================
	// All / Factory Reset (JSON 관점)
	// ==================================================
	static bool loadAll(ST_A10_ConfigRoot& p_root,
						ST_A10_WindProfileDict_t& p_wp,
						ST_A10_SchedulesConfig_t& p_sc,
						ST_A10_UserProfilesConfig_t& p_up) {
		bool ok = true;
		ok &= loadSystem(p_root.system);
		ok &= loadWifi(*p_root.wifi);
		ok &= loadMotion(*p_root.motion);
		ok &= loadWindProfileDict(p_wp);
		ok &= loadSchedules(p_sc);
		ok &= loadUserProfiles(p_up);
		return ok;
	}

	static bool saveAll(const ST_A10_ConfigRoot& p_root,
						const ST_A10_WindProfileDict_t& p_wp,
						const ST_A10_SchedulesConfig_t& p_sc,
						const ST_A10_UserProfilesConfig_t& p_up) {
		bool ok = true;
		ok &= saveSystem(p_root.system);
		if (p_root.wifi)   ok &= saveWifi(*p_root.wifi);
		if (p_root.motion) ok &= saveMotion(*p_root.motion);
		ok &= saveWindProfileDict(p_wp);
		ok &= saveSchedules(p_sc);
		ok &= saveUserProfiles(p_up);
		return ok;
	}

	static void factoryReset() {
		// JSON 파일 삭제 (실제 기본값 재생성은 상위 로직에서 처리)
		const char* files[] = {
			A10_Const::CFG_SYSTEM_FILE,
			A10_Const::CFG_SYSTEM_FILE_BAK,
			A10_Const::CFG_WIFI_FILE,
			A10_Const::CFG_WIFI_FILE_BAK,
			A10_Const::CFG_MOTION_FILE,
			A10_Const::CFG_MOTION_FILE_BAK,
			A10_Const::CFG_WINDPROFILE_FILE,
			A10_Const::CFG_WINDPROFILE_FILE_BAK,
			A10_Const::CFG_SCHEDULES_FILE,
			A10_Const::CFG_SCHEDULES_FILE_BAK,
			A10_Const::CFG_USER_PROFILES_FILE,
			A10_Const::CFG_USER_PROFILES_FILE_BAK
		};
		for (size_t i = 0; i < sizeof(files)/sizeof(files[0]); i++) {
			if (LittleFS.exists(files[i])) LittleFS.remove(files[i]);
		}
		CL_D10_Logger::log(EN_L10_LOG_WARN,"[C10] Factory reset: all cfg json removed");
	}
};
