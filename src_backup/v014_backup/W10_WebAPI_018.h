#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : W10_WebAPI_018.h
 * 모듈 약어 : W10
 * 모듈명 : Smart Nature Wind Web API (v018)
 * ------------------------------------------------------
 * 기능 요약:
 *  - Web UI / REST API 엔드포인트 집약 모듈
 *  - Schedules / UserProfiles / WindProfile / System 상태 조회 및 제어
 *  - CT10(Control), S10(Simulation), ConfigManager, NVS 연동
 *  - Preset × Style × Adjust 기반 바람 프로파일 제어
 *  - Manual Override / Profile 선택 / Schedule 기반 운전제어
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
#include "CT10_ControlManager_018.h"
#include "S10_Simulation_017.h"
#include "M10_MotionLogic_014.h"
#include "P10_PWM_ctrl_014.h"
#include "N10_NvsManager_016.h"
#include "D10_Logger_014.h"

class CL_W10_WebAPI {
public:
	// Web 초기화
	static void begin(AsyncWebServer& p_server) {
		server = &p_server;

		// 라우트 구성
		routeVersion();
		routeState();
		routeWindProfile();
		routeSchedules();
		routeUserProfiles();
		routeControl();
		routeSimulation();
		routeLogs();
		routeReload();

		CL_D10_Logger::log(EN_L10_LOG_INFO, "[W10] WebAPI v018 initialized");
	}

private:
	static AsyncWebServer* server;

	// --------------------------------------------------
	// 공통 유틸
	// --------------------------------------------------
	static void sendJson(AsyncWebServerRequest* request, JsonDocument& doc, int code = 200) {
		String out;
		serializeJson(doc, out);
		request->send(code, "application/json", out);
	}

	static bool parseJsonBody(AsyncWebServerRequest* request, JsonDocument& doc) {
		if (!request->hasParam("plain", true)) return false;
		auto* p = request->getParam("plain", true);
		auto err = deserializeJson(doc, p->value());
		if (err) {
			CL_D10_Logger::log(EN_L10_LOG_WARN, "[W10] JSON parse error: %s", err.c_str());
			return false;
		}
		return true;
	}

	// ========== 1. /api/version ==========
	static void routeVersion() {
		server->on("/api/version", HTTP_GET, [](AsyncWebServerRequest* request) {
			JsonDocument doc;
			doc["module"]  = "SmartNatureWind";
			doc["api"]     = "W10_WebAPI_018";
			doc["ct10"]    = "CT10_ControlManager_016";
			doc["s10"]     = "S10_Simulation_014";
			doc["config"]  = "C10_ConfigManager_014";
			sendJson(request, doc);
		});
	}

	// ========== 2. /api/state ==========
	// 현재 동작 모드, 활성 세그먼트, wind 상태, motion/autoOff 상태 조회
	static void routeState() {
		server->on("/api/state", HTTP_GET, [](AsyncWebServerRequest* request) {
			JsonDocument doc;

			// CT10 상태
			CL_CT10_ControlManager::toJson(doc); // 반드시 CT10에 toJson(JsonDocument&) 구현

			// S10 상태
			CL_S10_Simulation::toJson(doc);  // sim → doc["sim"] 섹션 채우는 함수

			// Motion 상태
			auto motionState = M10_MotionLogic_014::getState();
			doc["motion"]["present"]    = motionState.present;
			doc["motion"]["pir"]        = motionState.pirActive;
			doc["motion"]["ble"]        = motionState.bleActive;
			doc["motion"]["lastChange"] = motionState.lastChangeSec;

			sendJson(request, doc);
		});
	}

	// ========== 3. /api/windProfile ==========
	// windProfile dict 조회, 전체 preset/style 목록 제공
	static void routeWindProfile() {
		server->on("/api/windProfile", HTTP_GET, [](AsyncWebServerRequest* request) {
			JsonDocument doc;
			ST_A10_WindProfileDict_t dict;
			memset(&dict, 0, sizeof(dict));
			if (!CL_C10_ConfigManager::loadWindProfileDict(dict)) {
				request->send(500, "application/json", "{\"error\":\"load failed\"}");
				return;
			}

			doc["version"]  = dict.version;
			doc["jsonFile"] = dict.jsonFile;

			for (uint8_t i = 0; i < dict.presetCount; i++) {
				const auto& p = dict.presets[i];
				auto jp = doc["presets"][i];
				jp["name"]   = p.name;
				jp["code"]   = p.code;
				jp["wind_intensity"]             = p.base.wind_intensity;
				jp["gust_frequency"]             = p.base.gust_frequency;
				jp["wind_variability"]           = p.base.wind_variability;
				jp["fan_limit"]                  = p.base.fan_limit;
				jp["min_fan"]                    = p.base.min_fan;
				jp["turbulence_length_scale"]    = p.base.turbulence_length_scale;
				jp["turbulence_intensity_sigma"] = p.base.turbulence_intensity_sigma;
				jp["thermal_bubble_strength"]    = p.base.thermal_bubble_strength;
				jp["thermal_bubble_radius"]      = p.base.thermal_bubble_radius;
			}

			for (uint8_t i = 0; i < dict.styleCount; i++) {
				const auto& s = dict.styles[i];
				auto js = doc["styles"][i];
				js["name"] = s.name;
				js["code"] = s.code;
				js["intensity_factor"]  = s.factors.intensity_factor;
				js["variability_factor"]= s.factors.variability_factor;
				js["gust_factor"]       = s.factors.gust_factor;
				js["thermal_factor"]    = s.factors.thermal_factor;
			}

			sendJson(request, doc);
		});
	}

	// ========== 4. /api/schedules [GET] ==========
	static void routeSchedules() {
		// GET: 현재 cfg_schedules_xxx.json 전체 조회
		server->on("/api/schedules", HTTP_GET, [](AsyncWebServerRequest* request) {
			JsonDocument doc;
			if (!CL_C10_ConfigManager::toJson_Schedules(doc)) {
				request->send(500, "application/json", "{\"error\":\"load schedules failed\"}");
				return;
			}
			sendJson(request, doc);
		});

		// POST: 전체 스케줄 교체 저장
		server->on("/api/schedules", HTTP_POST,
			[](AsyncWebServerRequest* request) {},
			nullptr,
			[](AsyncWebServerRequest* request, uint8_t* data, size_t len,
			   size_t index, size_t total) {
				if (index + len != total) return;
				JsonDocument doc;
				auto err = deserializeJson(doc, (const char*)data, len);
				if (err) {
					request->send(400, "application/json", "{\"error\":\"json parse\"}");
					return;
				}
				if (!CL_C10_ConfigManager::fromJson_Schedules(doc)) {
					request->send(400, "application/json", "{\"error\":\"invalid data\"}");
					return;
				}
				if (!CL_C10_ConfigManager::saveSchedules()) {
					request->send(500, "application/json", "{\"error\":\"save failed\"}");
					return;
				}
				CL_N10_NvsManager::markDirty("schedules", true);
				request->send(200, "application/json", "{\"result\":\"ok\"}");
			}
		);
	}

	// ========== 5. /api/userProfiles ==========
	static void routeUserProfiles() {
		// GET: userProfiles 조회
		server->on("/api/userProfiles", HTTP_GET, [](AsyncWebServerRequest* request) {
			JsonDocument doc;
			if (!CL_C10_ConfigManager::toJson_UserProfiles(doc)) {
				request->send(500, "application/json", "{\"error\":\"load userProfiles failed\"}");
				return;
			}
			sendJson(request, doc);
		});

		// POST: 전체 userProfiles 교체 저장
		server->on("/api/userProfiles", HTTP_POST,
			[](AsyncWebServerRequest* request) {},
			nullptr,
			[](AsyncWebServerRequest* request, uint8_t* data, size_t len,
			   size_t index, size_t total) {
				if (index + len != total) return;
				JsonDocument doc;
				auto err = deserializeJson(doc, (const char*)data, len);
				if (err) {
					request->send(400, "application/json", "{\"error\":\"json parse\"}");
					return;
				}
				if (!CL_C10_ConfigManager::fromJson_UserProfiles(doc)) {
					request->send(400, "application/json", "{\"error\":\"invalid data\"}");
					return;
				}
				if (!CL_C10_ConfigManager::saveUserProfiles()) {
					request->send(500, "application/json", "{\"error\":\"save failed\"}");
					return;
				}
				CL_N10_NvsManager::markDirty("userProfiles", true);
				request->send(200, "application/json", "{\"result\":\"ok\"}");
			}
		);

		// POST: /api/userProfiles/select?id=n  → 해당 프로파일 모드로 전환
		server->on("/api/userProfiles/select", HTTP_POST, [](AsyncWebServerRequest* request) {
			if (!request->hasParam("id", true)) {
				request->send(400, "application/json", "{\"error\":\"missing id\"}");
				return;
			}
			int id = request->getParam("id", true)->value().toInt();
			if (!CL_CT10_ControlManager::setActiveUserProfile(id)) {
				request->send(400, "application/json", "{\"error\":\"invalid profile\"}");
				return;
			}
			request->send(200, "application/json", "{\"result\":\"ok\"}");
		});
	}

	// ========== 6. /api/control ==========
	// - mode 전환 (schedule/userProfile)
	// - manual override (fixed / preset+style)
	// - override clear
	static void routeControl() {
		server->on("/api/control/mode", HTTP_POST,
			[](AsyncWebServerRequest* request) {
				if (!request->hasParam("mode", true)) {
					request->send(400, "application/json", "{\"error\":\"missing mode\"}");
					return;
				}
				String m = request->getParam("mode", true)->value();
				if (m == "schedule") {
					CL_CT10_ControlManager::setMode(false);
				} else if (m == "profile") {
					CL_CT10_ControlManager::setMode(true);
				} else {
					request->send(400, "application/json", "{\"error\":\"invalid mode\"}");
					return;
				}
				request->send(200, "application/json", "{\"result\":\"ok\"}");
			}
		);

		// manual fixed
		server->on("/api/control/override/fixed", HTTP_POST,
			[](AsyncWebServerRequest* request) {
				if (!request->hasParam("speed", true)) {
					request->send(400,"application/json","{\"error\":\"missing speed\"}");
					return;
				}
				float sp = request->getParam("speed", true)->value().toFloat();
				ST_A10_ResolvedWind_t w;
				memset(&w,0,sizeof(w));
				w.valid = true;
				w.fixedMode = true;
				w.fixedSpeed = sp;
				CL_CT10_ControlManager::applyManual(w);
				request->send(200, "application/json", "{\"result\":\"ok\"}");
			}
		);

		// manual preset+style
		server->on("/api/control/override/preset", HTTP_POST,
			[](AsyncWebServerRequest* request) {},
			nullptr,
			[](AsyncWebServerRequest* request, uint8_t* data, size_t len,
			   size_t index, size_t total) {
				if (index + len != total) return;
				JsonDocument doc;
				auto err = deserializeJson(doc, (const char*)data, len);
				if (err) {
					request->send(400,"application/json","{\"error\":\"json parse\"}");
					return;
				}
				const char* presetCode = doc["presetCode"] | "";
				const char* styleCode  = doc["styleCode"]  | "BALANCE";

				ST_A10_AdjustDelta_t adj;
				memset(&adj,0,sizeof(adj));
				if (doc["adjust"].is<JsonObjectConst>()) {
					adj.valid = true;
					adj.wind_intensity   = doc["adjust"]["wind_intensity"]   | 0;
					adj.gust_frequency   = doc["adjust"]["gust_frequency"]   | 0;
					adj.wind_variability = doc["adjust"]["wind_variability"] | 0;
				}

				ST_A10_WindProfileDict_t dict;
				memset(&dict,0,sizeof(dict));
				if (!CL_C10_ConfigManager::loadWindProfileDict(dict)) {
					request->send(500,"application/json","{\"error\":\"load dict failed\"}");
					return;
				}

				ST_A10_ResolvedWind_t w;
				if (!CL_C10_ConfigManager::resolveWindParams(dict,
						presetCode, styleCode,
						adj.valid ? &adj : nullptr, w)) {
					request->send(400,"application/json","{\"error\":\"resolve failed\"}");
					return;
				}
				CL_CT10_ControlManager::applyManual(w);
				request->send(200,"application/json","{\"result\":\"ok\"}");
			}
		);

		// override clear
		server->on("/api/control/override/clear", HTTP_POST,
			[](AsyncWebServerRequest* request) {
				CL_CT10_ControlManager::clearManual();
				request->send(200,"application/json","{\"result\":\"ok\"}");
			}
		);
	}

	// ========== 7. /api/sim ==========
	static void routeSimulation() {
		// Chart 데이터 조회
		server->on("/api/sim/chart", HTTP_GET, [](AsyncWebServerRequest* request) {
			JsonDocument doc;
			CL_S10_Simulation::toChartJson(doc);
			sendJson(request, doc);
		});
	}

	// ========== 8. /api/logs ==========
	static void routeLogs() {
		server->on("/api/logs", HTTP_GET, [](AsyncWebServerRequest* request) {
			String logs = CL_D10_Logger::getLogsJson();
			request->send(200, "application/json", logs);
		});
	}

	// ========== 9. /api/reload ==========
	// Config 전체 재로드 (개발/장애 대응용)
	static void routeReload() {
		server->on("/api/reload", HTTP_POST, [](AsyncWebServerRequest* request) {
			bool ok = CL_CT10_ControlManager::begin()
				&& CL_CT10_ControlManager::reloadAll();
			if (!ok) {
				request->send(500,"application/json","{\"error\":\"reload failed\"}");
				return;
			}
			request->send(200,"application/json","{\"result\":\"ok\"}");
		});
	}
};

// ------------------------------------------------------
// 정적 멤버 정의
// ------------------------------------------------------
AsyncWebServer* CL_W10_WebAPI::server = nullptr;
