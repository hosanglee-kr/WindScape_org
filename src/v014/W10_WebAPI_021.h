#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : W10_WebAPI_021.h
 * 모듈 약어 : W10
 * 모듈명 : Smart Nature Wind Web API (v021)
 * ------------------------------------------------------
 * 기능 요약:
 *  - Web UI / REST API 엔드포인트 집약 모듈
 *  - System / WiFi / Motion / WindProfile / Schedules / UserProfiles 조회 및 일부 관리
 *  - CT10(ControlManager), C10(ConfigManager), S10(Simulation), N10(NVS), Logger 연동
 *  - Preset × Style × Adjust 기반 바람 프로파일 Override 제어
 *  - UserProfile 선택 / AutoOff 포함 운전제어 상태 조회
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
#include <WiFi.h>
#include <WiFiMulti.h>
#include <ESPAsyncWebServer.h>

#include "A10_Const_014.h"
#include "C10_ConfigManager_020.h"
#include "CT10_ControlManager_020.h"
#include "S10_Simulation_017.h"
#include "M10_MotionLogic_014.h"
#include "P10_PWM_ctrl_014.h"
#include "N10_NvsManager_016.h"
#include "D10_Logger_014.h"

class CL_W10_WebAPI {
public:
	// --------------------------------------------------
	// 초기화: WebServer + ControlManager 연결
	// --------------------------------------------------
	static void begin(AsyncWebServer& p_server, CL_CT10_ControlManager& p_control) {
		s_server  = &p_server;
		s_control = &p_control;

		routeVersion();
		routeState();
		routeSystem();
		routeWifi();
		routeMotion();
		routeWindProfile();
		routeSchedules();
		routeUserProfiles();
		routeControl();
		routeSimulation();
		routeLogs();
		routeReload();

		CL_D10_Logger::log(EN_L10_LOG_INFO, "[W10] WebAPI initialized");
	}

private:
	static AsyncWebServer*        s_server;
	static CL_CT10_ControlManager* s_control;

	// --------------------------------------------------
	// 공통 유틸
	// --------------------------------------------------
	static bool checkApiKey(AsyncWebServerRequest* p_request) {
		// system.security.api_key 가 비어있으면 검사 생략
		const char* v_key = g_A10_config_root.system.security.api_key;
		if (!v_key || v_key[0] == '\0') return true;

		if (!p_request->hasHeader("X-API-Key")) return false;
		String v_val = p_request->getHeader("X-API-Key")->value();
		return (v_val == v_key);
	}

	static void sendJson(AsyncWebServerRequest* p_request, JsonDocument& p_doc, int p_code = 200) {
		String v_out;
		serializeJson(p_doc, v_out);
		p_request->send(p_code, "application/json", v_out);
	}

	static bool parseJsonBody(AsyncWebServerRequest* p_request,
							  uint8_t* p_data, size_t p_len,
							  JsonDocument& p_doc) {
		auto v_err = deserializeJson(p_doc, (const char*)p_data, p_len);
		if (v_err) {
			CL_D10_Logger::log(EN_L10_LOG_WARN,
							   "[W10] JSON parse error: %s",
							   v_err.c_str());
			return false;
		}
		return true;
	}

	// --------------------------------------------------
	// 1. /api/version
	// --------------------------------------------------
	static void routeVersion() {
		s_server->on("/api/version", HTTP_GET,
			[](AsyncWebServerRequest* p_request) {
				if (!checkApiKey(p_request)) {
					p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
					return;
				}
				JsonDocument v_doc;
				v_doc["module"]   = "SmartNatureWind";
				v_doc["api"]      = "W10_WebAPI_v021";
				v_doc["fw"]       = A10_Const::FW_VERSION;
				v_doc["config"]   = "C10_ConfigManager_020";
				v_doc["control"]  = "CT10_ControlManager_020";
				v_doc["sim"]      = "S10_Simulation_017";
				v_doc["nvs"]      = "N10_NvsManager_016";
				v_doc["logger"]   = "D10_Logger_014";
				sendJson(p_request, v_doc);
			}
		);
	}

	// --------------------------------------------------
	// 2. /api/state
	//    - CT10 전체 상태 + sim 상태 + motion 일부 노출
	// --------------------------------------------------
	static void routeState() {
		s_server->on("/api/state", HTTP_GET,
			[](AsyncWebServerRequest* p_request) {
				if (!checkApiKey(p_request)) {
					p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
					return;
				}
				if (!s_control) {
					p_request->send(500, "application/json", "{\"error\":\"control not ready\"}");
					return;
				}
				JsonDocument v_doc;
				s_control->toJson(v_doc); // control + sim + autoOff + override 포함

				// Motion 상태 직렬화 추가
            	if (s_motion) {
		            s_motion->toJson(doc);
	            }
				
				sendJson(p_request, v_doc);
			}
		);
	}

	// --------------------------------------------------
	// 3. /api/system
	//    - System 설정 조회 (read-only)
	// --------------------------------------------------
	static void routeSystem() {
		s_server->on("/api/system", HTTP_GET,
			[](AsyncWebServerRequest* p_request) {
				if (!checkApiKey(p_request)) {
					p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
					return;
				}
				JsonDocument v_doc;
				CL_C10_ConfigManager::C10_toJson_System(g_A10_config_root.system, v_doc);
				sendJson(p_request, v_doc);
			}
		);
	}

	// --------------------------------------------------
	// 4. /api/wifi
	//    - WiFi 설정 조회 (read-only)
	// --------------------------------------------------
	static void routeWifi() {
		s_server->on("/api/wifi", HTTP_GET,
			[](AsyncWebServerRequest* p_request) {
				if (!checkApiKey(p_request)) {
					p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
					return;
				}
				JsonDocument v_doc;
				if (g_A10_config_root.wifi) {
					CL_C10_ConfigManager::C10_toJson_Wifi(*g_A10_config_root.wifi, v_doc);
				}
				sendJson(p_request, v_doc);
			}
		);
	}

	// --------------------------------------------------
	// 5. /api/motion
	//    - Motion 설정 조회 (read-only)
	// --------------------------------------------------
	static void routeMotion() {
		s_server->on("/api/motion", HTTP_GET,
			[](AsyncWebServerRequest* p_request) {
				if (!checkApiKey(p_request)) {
					p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
					return;
				}
				JsonDocument v_doc;
				if (g_A10_config_root.motion) {
					CL_C10_ConfigManager::C10_toJson_Motion(*g_A10_config_root.motion, v_doc);
				}
				sendJson(p_request, v_doc);
			}
		);
	}

	// --------------------------------------------------
	// 6. /api/windProfile
	//    - WindProfile dict 조회 (preset/style 정의)
	// --------------------------------------------------
	static void routeWindProfile() {
		s_server->on("/api/windProfile", HTTP_GET,
			[](AsyncWebServerRequest* p_request) {
				if (!checkApiKey(p_request)) {
					p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
					return;
				}
				JsonDocument v_doc;
				ST_A10_WindProfileDict_t v_dict;
				memset(&v_dict, 0, sizeof(v_dict));

				if (!CL_C10_ConfigManager::C10_loadWindProfileDict(v_dict)) {
					p_request->send(500, "application/json", "{\"error\":\"load failed\"}");
					return;
				}

				v_doc["windProfile"]["version"] = v_dict.version;

				for (uint8_t v_i = 0; v_i < v_dict.presetCount; v_i++) {
					const ST_A10_WindPresetDef_t& v_p = v_dict.presets[v_i];
					JsonObject v_jp = v_doc["windProfile"]["presets"][v_i];
					v_jp["name"] = v_p.name;
					v_jp["code"] = v_p.code;
					v_jp["base"]["wind_intensity"]             = v_p.base.wind_intensity;
					v_jp["base"]["gust_frequency"]             = v_p.base.gust_frequency;
					v_jp["base"]["wind_variability"]           = v_p.base.wind_variability;
					v_jp["base"]["fan_limit"]                  = v_p.base.fan_limit;
					v_jp["base"]["min_fan"]                    = v_p.base.min_fan;
					v_jp["base"]["turbulence_length_scale"]    = v_p.base.turbulence_length_scale;
					v_jp["base"]["turbulence_intensity_sigma"] = v_p.base.turbulence_intensity_sigma;
					v_jp["base"]["thermal_bubble_strength"]    = v_p.base.thermal_bubble_strength;
					v_jp["base"]["thermal_bubble_radius"]      = v_p.base.thermal_bubble_radius;
				}

				for (uint8_t v_i = 0; v_i < v_dict.styleCount; v_i++) {
					const ST_A10_WindStyleDef_t& v_s = v_dict.styles[v_i];
					JsonObject v_js = v_doc["windProfile"]["styles"][v_i];
					v_js["name"] = v_s.name;
					v_js["code"] = v_s.code;
					v_js["factors"]["intensity_factor"]   = v_s.factor.intensity_factor;
					v_js["factors"]["variability_factor"] = v_s.factor.variability_factor;
					v_js["factors"]["gust_factor"]        = v_s.factor.gust_factor;
					v_js["factors"]["thermal_factor"]     = v_s.factor.thermal_factor;
				}

				sendJson(p_request, v_doc);
			}
		);
	}

	// --------------------------------------------------
	// 7. /api/schedules
	//    - GET: 조회 (cfg_schedules_xxx.json 기반)
	//    - POST: 전체 교체 저장
	// --------------------------------------------------
	static void routeSchedules() {
		// GET
		s_server->on("/api/schedules", HTTP_GET,
			[](AsyncWebServerRequest* p_request) {
				if (!checkApiKey(p_request)) {
					p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
					return;
				}

				JsonDocument v_doc;
				ST_A10_ScheduleConfig v_cfg;
				memset(&v_cfg, 0, sizeof(v_cfg));

				CL_C10_ConfigManager::C10_loadSchedules(v_cfg);
				CL_C10_ConfigManager::C10_toJson_Schedules(v_cfg, v_doc);
				sendJson(p_request, v_doc);
			}
		);

		// POST: 전체 교체 저장
		s_server->on("/api/schedules", HTTP_POST,
			[](AsyncWebServerRequest* p_request) {},
			nullptr,
			[](AsyncWebServerRequest* p_request,
			   uint8_t* p_data, size_t p_len,
			   size_t p_index, size_t p_total) {
				if (!checkApiKey(p_request)) {
					p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
					return;
				}
				if (p_index + p_len != p_total) return;

				JsonDocument v_doc;
				if (!parseJsonBody(p_request, p_data, p_len, v_doc)) {
					p_request->send(400, "application/json", "{\"error\":\"json parse\"}");
					return;
				}

				if (!v_doc["schedules"].is<JsonArray>()) {
					p_request->send(400, "application/json", "{\"error\":\"invalid format\"}");
					return;
				}

				ST_A10_ScheduleConfig v_cfg;
				memset(&v_cfg, 0, sizeof(v_cfg));

				JsonArray v_arr = v_doc["schedules"].as<JsonArray>();
				for (JsonObject v_js : v_arr) {
					if (v_cfg.count >= A10_Const::MAX_SCHEDULES) break;
					ST_A10_ScheduleItem_t& v_s = v_cfg.items[v_cfg.count++];

					v_s.schNo   = v_js["schNo"]   | 0;
					strlcpy(v_s.name, v_js["name"] | "", sizeof(v_s.name));
					v_s.enabled = v_js["enabled"] | true;

					v_s.period.enabled = v_js["period"]["enabled"] | false;
					for (uint8_t v_d = 0; v_d < 7; v_d++) {
						v_s.period.days[v_d] = v_js["period"]["days"][v_d] | 0;
					}
					strlcpy(v_s.period.start_time,
							v_js["period"]["start_time"] | "00:00",
							sizeof(v_s.period.start_time));
					strlcpy(v_s.period.end_time,
							v_js["period"]["end_time"] | "23:59",
							sizeof(v_s.period.end_time));

					// segments
					v_s.segCount = 0;
					if (v_js["segments"].is<JsonArray>()) {
						JsonArray v_sArr = v_js["segments"].as<JsonArray>();
						for (JsonObject v_jseg : v_sArr) {
							if (v_s.segCount >= A10_Const::MAX_SEGMENTS_PER_SCHEDULE) break;
							ST_A10_OpSegment_t& v_seg = v_s.segments[v_s.segCount++];

							v_seg.segNo       = v_jseg["segNo"]       | 0;
							v_seg.on_minutes  = v_jseg["on_minutes"]  | 0;
							v_seg.off_minutes = v_jseg["off_minutes"] | 0;
							strlcpy(v_seg.mode, v_jseg["mode"] | "PRESET", sizeof(v_seg.mode));
							strlcpy(v_seg.presetCode, v_jseg["presetCode"] | "", sizeof(v_seg.presetCode));
							strlcpy(v_seg.styleCode,  v_jseg["styleCode"]  | "", sizeof(v_seg.styleCode));

							memset(&v_seg.adjust, 0, sizeof(v_seg.adjust));
							if (v_jseg["adjust"].is<JsonObject>()) {
								JsonObject v_adj = v_jseg["adjust"];
								v_seg.adjust.wind_intensity   = v_adj["wind_intensity"]   | 0.0f;
								v_seg.adjust.wind_variability = v_adj["wind_variability"] | 0.0f;
								v_seg.adjust.gust_frequency   = v_adj["gust_frequency"]   | 0.0f;
								v_seg.adjust.fan_limit        = v_adj["fan_limit"]        | 0.0f;
								v_seg.adjust.min_fan          = v_adj["min_fan"]          | 0.0f;
							}
							v_seg.fixed_speed = v_jseg["fixed_speed"] | 0.0f;
						}
					}

					// autoOff
					memset(&v_s.autoOff, 0, sizeof(v_s.autoOff));
					if (v_js["autoOff"].is<JsonObject>()) {
						JsonObject v_ao = v_js["autoOff"];
						v_s.autoOff.timer.enabled   = v_ao["timer"]["enabled"]   | false;
						v_s.autoOff.timer.minutes   = v_ao["timer"]["minutes"]   | 0;
						v_s.autoOff.offTime.enabled = v_ao["offTime"]["enabled"] | false;
						strlcpy(v_s.autoOff.offTime.time,
								v_ao["offTime"]["time"] | "",
								sizeof(v_s.autoOff.offTime.time));
						v_s.autoOff.offTemp.enabled = v_ao["offTemp"]["enabled"] | false;
						v_s.autoOff.offTemp.temp    = v_ao["offTemp"]["temp"]    | 0.0f;
					}

					// motion
					v_s.motion.pir.enabled        = v_js["motion"]["pir"]["enabled"]        | false;
					v_s.motion.pir.hold_sec       = v_js["motion"]["pir"]["hold_sec"]       | 0;
					v_s.motion.ble.enabled        = v_js["motion"]["ble"]["enabled"]        | false;
					v_s.motion.ble.rssi_threshold = v_js["motion"]["ble"]["rssi_threshold"] | -70;
					v_s.motion.ble.hold_sec       = v_js["motion"]["ble"]["hold_sec"]       | 0;
				}

				if (!CL_C10_ConfigManager::C10_saveSchedules(v_cfg)) {
					p_request->send(500, "application/json", "{\"error\":\"save failed\"}");
					return;
				}
				CL_N10_NvsManager::N10_markDirty("schedules", true);
				p_request->send(200, "application/json", "{\"result\":\"ok\"}");
			}
		);
	}

	// --------------------------------------------------
	// 8. /api/userProfiles
	//    - GET: 조회
	//    - POST: 전체 교체 저장
	// --------------------------------------------------
	static void routeUserProfiles() {
		// GET
		s_server->on("/api/userProfiles", HTTP_GET,
			[](AsyncWebServerRequest* p_request) {
				if (!checkApiKey(p_request)) {
					p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
					return;
				}
				JsonDocument v_doc;
				ST_A10_UserProfileConfig_t v_cfg;
				memset(&v_cfg, 0, sizeof(v_cfg));

				CL_C10_ConfigManager::C10_loadUserProfiles(v_cfg);
				CL_C10_ConfigManager::C10_toJson_UserProfiles(v_cfg, v_doc);
				sendJson(p_request, v_doc);
			}
		);

		// POST
		s_server->on("/api/userProfiles", HTTP_POST,
			[](AsyncWebServerRequest* p_request) {},
			nullptr,
			[](AsyncWebServerRequest* p_request,
			   uint8_t* p_data, size_t p_len,
			   size_t p_index, size_t p_total) {
				if (!checkApiKey(p_request)) {
					p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
					return;
				}
				if (p_index + p_len != p_total) return;

				JsonDocument v_doc;
				if (!parseJsonBody(p_request, p_data, p_len, v_doc)) {
					p_request->send(400, "application/json", "{\"error\":\"json parse\"}");
					return;
				}

				if (!v_doc["userProfiles"]["profiles"].is<JsonArray>()) {
					p_request->send(400, "application/json", "{\"error\":\"invalid format\"}");
					return;
				}

				ST_A10_UserProfileConfig_t v_cfg;
				memset(&v_cfg, 0, sizeof(v_cfg));

				JsonArray v_arr = v_doc["userProfiles"]["profiles"].as<JsonArray>();
				for (JsonObject v_jp : v_arr) {
					if (v_cfg.count >= A10_Const::MAX_USER_PROFILES) break;
					ST_A10_UserProfile_t& v_up = v_cfg.items[v_cfg.count++];

					v_up.profileNo = v_jp["profileNo"] | 0;
					strlcpy(v_up.name, v_jp["name"] | "", sizeof(v_up.name));
					v_up.enabled        = v_jp["enabled"]        | true;
					v_up.repeatSegments = v_jp["repeatSegments"] | true;

					// segments
					v_up.segCount = 0;
					if (v_jp["segments"].is<JsonArray>()) {
						JsonArray v_sArr = v_jp["segments"].as<JsonArray>();
						for (JsonObject v_jseg : v_sArr) {
							if (v_up.segCount >= A10_Const::MAX_SEGMENTS_PER_PROFILE) break;
							ST_A10_OpSegment_t& v_seg = v_up.segments[v_up.segCount++];

							v_seg.segNo       = v_jseg["segNo"]       | 0;
							v_seg.on_minutes  = v_jseg["on_minutes"]  | 0;
							v_seg.off_minutes = v_jseg["off_minutes"] | 0;
							strlcpy(v_seg.mode, v_jseg["mode"] | "PRESET", sizeof(v_seg.mode));
							strlcpy(v_seg.presetCode, v_jseg["presetCode"] | "", sizeof(v_seg.presetCode));
							strlcpy(v_seg.styleCode,  v_jseg["styleCode"]  | "", sizeof(v_seg.styleCode));

							memset(&v_seg.adjust, 0, sizeof(v_seg.adjust));
							if (v_jseg["adjust"].is<JsonObject>()) {
								JsonObject v_adj = v_jseg["adjust"];
								v_seg.adjust.wind_intensity   = v_adj["wind_intensity"]   | 0.0f;
								v_seg.adjust.wind_variability = v_adj["wind_variability"] | 0.0f;
								v_seg.adjust.gust_frequency   = v_adj["gust_frequency"]   | 0.0f;
								v_seg.adjust.fan_limit        = v_adj["fan_limit"]        | 0.0f;
								v_seg.adjust.min_fan          = v_adj["min_fan"]          | 0.0f;
							}
							v_seg.fixed_speed = v_jseg["fixed_speed"] | 0.0f;
						}
					}

					// autoOff
					memset(&v_up.autoOff, 0, sizeof(v_up.autoOff));
					if (v_jp["autoOff"].is<JsonObject>()) {
						JsonObject v_ao = v_jp["autoOff"];
						v_up.autoOff.timer.enabled   = v_ao["timer"]["enabled"]   | false;
						v_up.autoOff.timer.minutes   = v_ao["timer"]["minutes"]   | 0;
						v_up.autoOff.offTime.enabled = v_ao["offTime"]["enabled"] | false;
						strlcpy(v_up.autoOff.offTime.time,
								v_ao["offTime"]["time"] | "",
								sizeof(v_up.autoOff.offTime.time));
						v_up.autoOff.offTemp.enabled = v_ao["offTemp"]["enabled"] | false;
						v_up.autoOff.offTemp.temp    = v_ao["offTemp"]["temp"]    | 0.0f;
					}

					// motion
					v_up.motion.pir.enabled        = v_jp["motion"]["pir"]["enabled"]        | false;
					v_up.motion.pir.hold_sec       = v_jp["motion"]["pir"]["hold_sec"]       | 0;
					v_up.motion.ble.enabled        = v_jp["motion"]["ble"]["enabled"]        | false;
					v_up.motion.ble.rssi_threshold = v_jp["motion"]["ble"]["rssi_threshold"] | -70;
					v_up.motion.ble.hold_sec       = v_jp["motion"]["ble"]["hold_sec"]       | 0;
				}

				if (!CL_C10_ConfigManager::C10_saveUserProfiles(v_cfg)) {
					p_request->send(500, "application/json", "{\"error\":\"save failed\"}");
					return;
				}
				CL_N10_NvsManager::N10_markDirty("userProfiles", true);
				p_request->send(200, "application/json", "{\"result\":\"ok\"}");
			}
		);
	}

	// --------------------------------------------------
	// 9. /api/control
	//    - Profile 선택 / 중지
	//    - Override (fixed / preset) 설정 & 해제
	// --------------------------------------------------
	static void routeControl() {
		// UserProfile 선택
		s_server->on("/api/control/profile/select", HTTP_POST,
			[](AsyncWebServerRequest* p_request) {
				if (!checkApiKey(p_request)) {
					p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
					return;
				}
				if (!s_control) {
					p_request->send(500, "application/json", "{\"error\":\"control not ready\"}");
					return;
				}
				if (!p_request->hasParam("id", true)) {
					p_request->send(400, "application/json", "{\"error\":\"missing id\"}");
					return;
				}
				int v_id = p_request->getParam("id", true)->value().toInt();
				if (!s_control->startUserProfileByNo((uint8_t)v_id)) {
					p_request->send(400, "application/json", "{\"error\":\"invalid profile\"}");
					return;
				}
				p_request->send(200, "application/json", "{\"result\":\"ok\"}");
			}
		);

		// UserProfile 정지
		s_server->on("/api/control/profile/stop", HTTP_POST,
			[](AsyncWebServerRequest* p_request) {
				if (!checkApiKey(p_request)) {
					p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
					return;
				}
				if (!s_control) {
					p_request->send(500, "application/json", "{\"error\":\"control not ready\"}");
					return;
				}
				s_control->stopUserProfile();
				p_request->send(200, "application/json", "{\"result\":\"ok\"}");
			}
		);

		// Override: fixed
		s_server->on("/api/control/override/fixed", HTTP_POST,
			[](AsyncWebServerRequest* p_request) {
				if (!checkApiKey(p_request)) {
					p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
					return;
				}
				if (!s_control) {
					p_request->send(500, "application/json", "{\"error\":\"control not ready\"}");
					return;
				}
				if (!p_request->hasParam("percent", true) ||
					!p_request->hasParam("seconds", true)) {
					p_request->send(400, "application/json", "{\"error\":\"missing param\"}");
					return;
				}
				float v_pct = p_request->getParam("percent", true)->value().toFloat();
				uint32_t v_sec = (uint32_t)p_request->getParam("seconds", true)->value().toInt();
				s_control->startOverrideFixed(v_pct, v_sec);
				p_request->send(200, "application/json", "{\"result\":\"ok\"}");
			}
		);

		// Override: preset+style+adjust (JSON Body)
		s_server->on("/api/control/override/preset", HTTP_POST,
			[](AsyncWebServerRequest* p_request) {},
			nullptr,
			[](AsyncWebServerRequest* p_request,
			   uint8_t* p_data, size_t p_len,
			   size_t p_index, size_t p_total) {
				if (!checkApiKey(p_request)) {
					p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
					return;
				}
				if (!s_control) {
					p_request->send(500, "application/json", "{\"error\":\"control not ready\"}");
					return;
				}
				if (p_index + p_len != p_total) return;

				JsonDocument v_doc;
				if (!parseJsonBody(p_request, p_data, p_len, v_doc)) {
					p_request->send(400, "application/json", "{\"error\":\"json parse\"}");
					return;
				}

				const char* v_preset = v_doc["presetCode"] | "";
				const char* v_style  = v_doc["styleCode"]  | "BALANCE";
				uint32_t v_sec       = v_doc["durationSec"] | 0;

				ST_A10_AdjustDelta_t v_adj;
				memset(&v_adj, 0, sizeof(v_adj));
				if (v_doc["adjust"].is<JsonObject>()) {
					JsonObject v_aj = v_doc["adjust"];
					v_adj.wind_intensity   = v_aj["wind_intensity"]   | 0.0f;
					v_adj.wind_variability = v_aj["wind_variability"] | 0.0f;
					v_adj.gust_frequency   = v_aj["gust_frequency"]   | 0.0f;
					v_adj.fan_limit        = v_aj["fan_limit"]        | 0.0f;
					v_adj.min_fan          = v_aj["min_fan"]          | 0.0f;
				}

				s_control->startOverridePreset(v_preset, v_style, &v_adj, v_sec);
				p_request->send(200, "application/json", "{\"result\":\"ok\"}");
			}
		);

		// Override 해제
		s_server->on("/api/control/override/clear", HTTP_POST,
			[](AsyncWebServerRequest* p_request) {
				if (!checkApiKey(p_request)) {
					p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
					return;
				}
				if (!s_control) {
					p_request->send(500, "application/json", "{\"error\":\"control not ready\"}");
					return;
				}
				s_control->stopOverride();
				p_request->send(200, "application/json", "{\"result\":\"ok\"}");
			}
		);
	}

	// --------------------------------------------------
	// 10. /api/sim/chart
	//      - S10 Chart 데이터 조회
	//      (CT10.toJson 내부에서 sim 상태는 이미 제공)
	// --------------------------------------------------
	static void routeSimulation() {
	// 기존 chart 라우트 유지
	s_server->on("/api/sim/chart", HTTP_GET, [](AsyncWebServerRequest* p_request) {
		if (!checkApiKey(p_request)) {
			p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
			return;
		}
		if (!s_control) {
			p_request->send(500, "application/json", "{\"error\":\"control not ready\"}");
			return;
		}
		JsonDocument v_doc;
		s_control->toChartJson(v_doc);
		sendJson(p_request, v_doc);
	});

	// ✅ 신규 추가: /api/sim/status
	s_server->on("/api/sim/status", HTTP_GET, [](AsyncWebServerRequest* p_request) {
		if (!checkApiKey(p_request)) {
			p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
			return;
		}
		if (!s_control) {
			p_request->send(500, "application/json", "{\"error\":\"control not ready\"}");
			return;
		}
		JsonDocument v_doc;
		s_control->toSummaryJson(v_doc);  // CT10의 새 함수 호출 ✅
		sendJson(p_request, v_doc);
	});
}

	// --------------------------------------------------
	// 11. /api/logs
	// --------------------------------------------------
	static void routeLogs() {
		s_server->on("/api/logs", HTTP_GET,
			[](AsyncWebServerRequest* p_request) {
				if (!checkApiKey(p_request)) {
					p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
					return;
				}
				String v_logs = CL_D10_Logger::getLogsJson();
				p_request->send(200, "application/json", v_logs);
			}
		);
	}

	// --------------------------------------------------
	// 12. /api/reload
	//      - 전체 Config 재로드 (C10_loadAll)
	// --------------------------------------------------
	static void routeReload() {
		s_server->on("/api/reload", HTTP_POST,
			[](AsyncWebServerRequest* p_request) {
				if (!checkApiKey(p_request)) {
					p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
					return;
				}
				ST_A10_ConfigRoot v_root;
				bool v_ok = CL_C10_ConfigManager::C10_loadAll(v_root);
				if (!v_ok) {
					p_request->send(500, "application/json", "{\"error\":\"reload failed\"}");
					return;
				}
				g_A10_config_root = v_root;
				p_request->send(200, "application/json", "{\"result\":\"ok\"}");
			}
		);
	}
};

// ------------------------------------------------------
// 정적 멤버 정의
// ------------------------------------------------------
AsyncWebServer*        CL_W10_WebAPI::s_server  = nullptr;
CL_CT10_ControlManager* CL_W10_WebAPI::s_control = nullptr;


