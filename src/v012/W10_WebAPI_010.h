
#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : W10_WebAPI_010.h
 * 모듈명 : Smart Nature Wind Web API Manager
 * ------------------------------------------------------
 * 기능 요약:
 *  - LittleFS 정적 자산 서빙(코어 설정 기반 + 기본 경로)
 *  - /api/state, /api/chart, /api/config(GET/POST), /api/reset,
 *    /api/version, /api/logs, /api/diag, /api/scan
 *  - 시뮬레이션 제어 API: /api/sim/start, /api/sim/stop, /api/sim/preset
 *  - 정적 파일 업로드(/upload), OTA 업데이트(/update)
 *  - API Key 인증(X-API-Key) / CORS / No-Cache 헤더 적용
 *  - A10_Const_010 / C10_ConfigManager_010 / S10_Simulation_010 연동
 * ------------------------------------------------------
 * [코드 네이밍 규칙]
 *      - 현재 파일 모듈약어    : W10
 *      - 전역 상수,매크로      : G_모듈약어_ 접두사
 *      - 전역 변수             : g_모듈약어_ 접두사
 *      - 전역 함수             : 모듈약어_ 접두사
 *      - type                  : T_모듈약어_ 접두사
 *      - enum 상수             : EN_모듈약어_ 접두사
 *      - 구조체                : ST_모듈약어_ 접두사
 *      - 클래스명              : CL_모듈약어_ 접두사
 *      - 클래스 private 멤버   : _ 접두사,
 *      - 클래스 정적 멤버      : s_ 접두사
 *      - 로컬 변수             : v_ 접두사
 *      - 함수 인자             : p_ 접두사
 *      - 람다함수 인자          : p_ 접두사
 *      - 람다함수 내 로컬변수     : v_ 접두사
 */

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <Update.h>
#include <WiFi.h>
#include <WiFiMulti.h>

#include "A10_Const_010.h"
#include "C10_ConfigManager_010.h"
#include "D10_Logger_010.h"
#include "M10_WiFiManager_010.h"
#include "S10_Simulation_010.h"
#include "P10_PWM_ctrl_010.h"

// ------------------------------------------------------
// 기본 정적 자산 경로 상수
// ------------------------------------------------------
namespace W10_Const {
	constexpr char MAIN_PAGE_HTML_FILE[] = "/html/SC10_main_021.html";
	constexpr char MAIN_PAGE_HTML_URI[]  = "/SC10_main_021.html";
	constexpr char MAIN_PAGE_HTML_MIME[] = "text/html";

	constexpr char MAIN_PAGE_CSS_FILE[]  = "/html/SC10_main_021.css";
	constexpr char MAIN_PAGE_CSS_URI[]   = "/SC10_main_021.css";
	constexpr char MAIN_PAGE_CSS_MIME[]  = "text/css";

	constexpr char MAIN_PAGE_JS_FILE[]   = "/html/SC10_main_021.js";
	constexpr char MAIN_PAGE_JS_URI[]    = "/SC10_main_021.js";
	constexpr char MAIN_PAGE_JS_MIME[]   = "application/javascript";

	constexpr char CHART_PAGE_HTML_FILE[] = "/html/SC10_chart_003.html";
	constexpr char CHART_PAGE_HTML_URI[]  = "/SC10_chart_003.html";
	constexpr char CHART_PAGE_HTML_MIME[] = "text/html";

	constexpr char CHART_PAGE_CSS_FILE[]  = "/html/SC10_chart_003.css";
	constexpr char CHART_PAGE_CSS_URI[]   = "/SC10_chart_003.css";
	constexpr char CHART_PAGE_CSS_MIME[]  = "text/css";

	constexpr char CHART_PAGE_JS_FILE[]   = "/html/SC10_chart_003.js";
	constexpr char CHART_PAGE_JS_URI[]    = "/SC10_chart_003.js";
	constexpr char CHART_PAGE_JS_MIME[]   = "application/javascript";

	constexpr char CHART_ALIAS_URI[]      = "/chart";
	constexpr char CHART_ALIAS_FILE[]     = "/SC10_chart_003.html";
	constexpr char CHART_ALIAS_MIME[]     = "text/html";
}

// ------------------------------------------------------
// Web API Manager
// ------------------------------------------------------
class CL_W10_WebAPI {
public:
	static void W10_init(AsyncWebServer &p_srv,
	                     CL_S10_Simulation &p_sim,
	                     WiFiMulti &p_multi,
	                     CL_P10_PWM &p_pwmCtrl)
	{
		s_pSim     = &p_sim;
		s_pWiMulti = &p_multi;
		s_pPwm     = &p_pwmCtrl;

		_W10_mountStatic(p_srv);
		_W10_mountApi(p_srv);
	}

private:
	// --------------------------------------------------
	// 정적 자산
	// --------------------------------------------------
	static void _W10_mountStatic(AsyncWebServer &p_srv) {
		p_srv.on("/", HTTP_GET, [](AsyncWebServerRequest *req){
			req->redirect(W10_Const::MAIN_PAGE_HTML_URI);
		});

		struct Route { const char* uri; const char* file; const char* mime; };
		const Route routes[] = {
			{ W10_Const::MAIN_PAGE_HTML_URI, W10_Const::MAIN_PAGE_HTML_FILE, W10_Const::MAIN_PAGE_HTML_MIME },
			{ W10_Const::MAIN_PAGE_CSS_URI , W10_Const::MAIN_PAGE_CSS_FILE , W10_Const::MAIN_PAGE_CSS_MIME  },
			{ W10_Const::MAIN_PAGE_JS_URI  , W10_Const::MAIN_PAGE_JS_FILE  , W10_Const::MAIN_PAGE_JS_MIME   },
			{ W10_Const::CHART_PAGE_HTML_URI, W10_Const::CHART_PAGE_HTML_FILE, W10_Const::CHART_PAGE_HTML_MIME },
			{ W10_Const::CHART_PAGE_CSS_URI , W10_Const::CHART_PAGE_CSS_FILE , W10_Const::CHART_PAGE_CSS_MIME  },
			{ W10_Const::CHART_PAGE_JS_URI  , W10_Const::CHART_PAGE_JS_FILE  , W10_Const::CHART_PAGE_JS_MIME   },
			{ W10_Const::CHART_ALIAS_URI    , W10_Const::CHART_ALIAS_FILE    , W10_Const::CHART_ALIAS_MIME     },
		};
		for (auto &r : routes) {
			p_srv.on(r.uri, HTTP_GET, [=](AsyncWebServerRequest *req){
				if (LittleFS.exists(r.file))
					req->send(LittleFS, r.file, r.mime);
				else {
					auto *resp = req->beginResponse(200, r.mime, String("/* missing ") + r.file + " */");
					_W10_applyHeaders(resp, true);
					req->send(resp);
				}
			});
		}

		p_srv.onNotFound([](AsyncWebServerRequest *req){
			if (req->method() == HTTP_OPTIONS) {
				auto *resp = req->beginResponse(204);
				_W10_applyHeaders(resp, false);
				// CL_W10_WebAPI::_W10_applyHeaders(v_resp, false);
				// _W10_addCors(resp);
				req->send(resp);
				return;
			}
			req->send(404, "text/plain", "Not found");
		});
	}

	// --------------------------------------------------
	// API 라우팅
	// --------------------------------------------------
	static void _W10_mountApi(AsyncWebServer &p_srv) {
		// --- /api/state ---
		p_srv.on("/api/state", HTTP_GET, [](AsyncWebServerRequest *req){
			JsonDocument doc;
			if (s_pSim) s_pSim->S10_toJson(doc);

			JsonObject status = doc["status"].to<JsonObject>();
			if (WiFi.status() == WL_CONNECTED) {
				status["wifi"]["mode"] = "STA";
				status["wifi"]["ip"]   = WiFi.localIP().toString();
				status["wifi"]["ssid"] = WiFi.SSID();
				status["wifi"]["rssi"] = WiFi.RSSI();
			} else {
				status["wifi"]["mode"] = "AP";
				status["wifi"]["ip"]   = WiFi.softAPIP().toString();
				status["wifi"]["ssid"] = g_A10_config_root.core.meta.device_name;
			}

			if (s_pPwm) {
				status["pwm"]["percent"] = s_pPwm->P10_getDutyPercent();
				status["pwm"]["raw"]     = s_pPwm->P10_getDutyRaw();
			}

			String out; serializeJson(doc, out);
			auto *resp = req->beginResponse(200, "application/json", out);
			_W10_applyHeaders(resp, true);
			req->send(resp);
		});

		// --- /api/chart_data ---
		auto chartHandler = [](AsyncWebServerRequest *req){
			JsonDocument doc;
			if (s_pSim) s_pSim->S10_toChartJson(doc);
			String out; serializeJson(doc, out);
			auto *resp = req->beginResponse(200, "application/json", out);
			_W10_applyHeaders(resp, true);
			req->send(resp);
		};
		p_srv.on("/api/chart", HTTP_GET, chartHandler);
		p_srv.on("/api/chart_data", HTTP_GET, chartHandler);

		// --- /api/config [GET] ---
		p_srv.on("/api/config", HTTP_GET, [](AsyncWebServerRequest *req){
			JsonDocument doc;
			JsonObject root = doc.to<JsonObject>();
			_W10_toJsonWifi(root["wifi"].to<JsonObject>());
			_W10_toJsonSim(root["sim"].to<JsonObject>());
			_W10_toJsonSchedule(root["schedule"].to<JsonObject>());
			_W10_toJsonMotion(root["motion"].to<JsonObject>());

			String out; serializeJson(doc, out);
			auto *resp = req->beginResponse(200, "application/json", out);
			_W10_applyHeaders(resp, true);
			req->send(resp);
		});

		// --- /api/config [POST] ---
		p_srv.on("/api/config", HTTP_POST, nullptr, nullptr,
			[](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index, size_t total){
				if (!_W10_authorize(req)) {
					req->send(401, "application/json", "{\"error\":\"unauthorized\"}");
					return;
				}
				if (index == 0 && len == total) {
					JsonDocument doc;
					if (deserializeJson(doc, (const char*)data, len)) {
						req->send(400, "application/json", "{\"error\":\"invalid json\"}");
						return;
					}
					bool needWifiReinit = false;
					bool changed = CL_C10_ConfigManager::patchFromJson(g_A10_config_root, doc, needWifiReinit);
					if (changed) {
						CL_C10_ConfigManager::saveAll(g_A10_config_root);
						if (needWifiReinit && g_A10_config_root.wifi)
							CL_M10_WiFiManager::M10_init(*g_A10_config_root.wifi, *s_pWiMulti);
						if (s_pSim && g_A10_config_root.sim)
							s_pSim->S10_applyPreset(g_A10_config_root.sim->preset);
						req->send(200, "application/json", "{\"message\":\"updated\"}");
					} else req->send(200, "application/json", "{\"message\":\"no change\"}");
				}
			}
		);

		// --- /api/scan ---
		p_srv.on("/api/scan", HTTP_GET, [](AsyncWebServerRequest *req){
			bool async = req->hasParam("async");
			String json = CL_M10_WiFiManager::M10_scanNetworksJson(async);
			auto *resp = req->beginResponse(200, "application/json", json);
			_W10_applyHeaders(resp, true);
			req->send(resp);
		});

		// --- /api/version ---
		p_srv.on("/api/version", HTTP_GET, [](AsyncWebServerRequest *req){
			JsonDocument doc;
			doc["fw_version"]   = A10_Const::FW_VERSION;
			doc["cfg_json_ver"] = G_A10_CFG_JSON_FILE_VER;
			String out; serializeJson(doc, out);
			auto *resp = req->beginResponse(200, "application/json", out);
			_W10_applyHeaders(resp, true);
			req->send(resp);
		});

		// --- /api/sim/start ---
		p_srv.on("/api/sim/start", HTTP_POST, [](AsyncWebServerRequest *req){
			if (!_W10_authorize(req)) { req->send(401, "application/json", "{\"error\":\"unauthorized\"}"); return; }
			if (s_pSim && s_pPwm) {
				s_pSim->S10_begin(*s_pPwm);
				req->send(200, "application/json", "{\"message\":\"started\"}");
			} else req->send(500, "application/json", "{\"error\":\"not ready\"}");
		});

		// --- /api/sim/stop ---
		p_srv.on("/api/sim/stop", HTTP_POST, [](AsyncWebServerRequest *req){
			if (!_W10_authorize(req)) { req->send(401, "application/json", "{\"error\":\"unauthorized\"}"); return; }
			if (s_pSim) {
				s_pSim->S10_active = false;
				req->send(200, "application/json", "{\"message\":\"stopped\"}");
			} else req->send(500, "application/json", "{\"error\":\"not ready\"}");
		});

		// --- /api/sim/preset ---
		p_srv.on("/api/sim/preset", HTTP_POST, nullptr, nullptr,
			[](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index, size_t total){
				if (!_W10_authorize(req)) { req->send(401, "application/json", "{\"error\":\"unauthorized\"}"); return; }
				if (!s_pSim) { req->send(500, "application/json", "{\"error\":\"not ready\"}"); return; }
				if (index==0 && len==total) {
					JsonDocument doc;
					if (deserializeJson(doc, (const char*)data, len)) {
						req->send(400, "application/json", "{\"error\":\"invalid json\"}");
						return;
					}
					const char* preset = doc["preset"] | "OCEAN";
					s_pSim->S10_applyPreset(preset);
					req->send(200, "application/json", "{\"message\":\"preset applied\"}");
				}
			}
		);
	}

	// --------------------------------------------------
	// 공통 유틸
	// --------------------------------------------------
	static void _W10_applyHeaders(AsyncWebServerResponse *r, bool nocache=false) {
		if (nocache) {
			r->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
			r->addHeader("Pragma", "no-cache");
		}
		r->addHeader("Access-Control-Allow-Origin", "*");
		r->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
		r->addHeader("Access-Control-Allow-Headers", "Content-Type, X-API-Key");
	}

	static bool _W10_authorize(AsyncWebServerRequest *req) {
		if (strlen(g_A10_config_root.core.security.api_key) == 0) return true;
		if (!req->hasHeader("X-API-Key")) return false;
		auto *h = req->getHeader("X-API-Key");
		return (h && h->value() == String(g_A10_config_root.core.security.api_key));
	}

	// --------------------------------------------------
	// 설정 → JSON 보조 직렬화
	// --------------------------------------------------
	static void _W10_toJsonWifi(JsonObject o) {
		ST_A10_WifiConfig tmp;
		ST_A10_WifiConfig *cfg = g_A10_config_root.wifi;
		if (!cfg) {
			if (CL_C10_ConfigManager::loadWifi(tmp)) cfg = &tmp;
		}
		if (!cfg) return;
		o["mode"] = cfg->mode;
		o["ap"]["ssid"] = cfg->ap.ssid;
		JsonArray sta = o["sta"].to<JsonArray>();
		for (uint8_t i=0;i<cfg->sta_count;i++){
			JsonObject n = sta.add<JsonObject>();
			n["ssid"] = cfg->sta[i].ssid;
		}
	}

	static void _W10_toJsonSim(JsonObject o) {
		ST_A10_SimConfig tmp;
		ST_A10_SimConfig *cfg = g_A10_config_root.sim;
		if (!cfg) {
			if (CL_C10_ConfigManager::loadSim(tmp)) cfg = &tmp;
		}
		if (!cfg) return;
		o["preset"] = cfg->preset;
		o["wind_intensity"]   = cfg->wind_intensity;
		o["gust_frequency"]   = cfg->gust_frequency;
		o["wind_variability"] = cfg->wind_variability;
		o["fan_limit"]        = cfg->fan_limit;
		o["min_fan"]          = cfg->min_fan;
		o["turbulence"]["length_scale"]    = cfg->turbulence.length_scale;
		o["turbulence"]["intensity_sigma"] = cfg->turbulence.intensity_sigma;
		o["thermal"]["bubble_strength"]    = cfg->thermal.bubble_strength;
		o["thermal"]["bubble_radius"]      = cfg->thermal.bubble_radius;
	}

	static void _W10_toJsonSchedule(JsonObject o) {
		ST_A10_ScheduleConfig tmp;
		ST_A10_ScheduleConfig *cfg = g_A10_config_root.schedule;
		if (!cfg) {
			if (CL_C10_ConfigManager::loadSchedule(tmp)) cfg = &tmp;
		}
		if (!cfg) return;
		o["count"] = cfg->count;
	}

	static void _W10_toJsonMotion(JsonObject o) {
		ST_A10_MotionConfig tmp;
		ST_A10_MotionConfig *cfg = g_A10_config_root.motion;
		if (!cfg) {
			if (CL_C10_ConfigManager::loadMotion(tmp)) cfg = &tmp;
		}
		if (!cfg) return;
		o["enabled"] = cfg->enabled;
		o["pir"]["enabled"] = cfg->pir.enabled;
		o["ble"]["enabled"] = cfg->ble.enabled;
		o["ble"]["rssi_threshold"] = cfg->ble.rssi_threshold;
		o["ble"]["hold_sec"] = cfg->ble.hold_sec;

		JsonArray dev = o["ble"]["devices"].to<JsonArray>();
		for (uint8_t i = 0; i < cfg->ble.device_count; i++) {
			const auto &d = cfg->ble.devices[i];
			JsonObject n = dev.add<JsonObject>();
			n["mac"]     = d.mac;
			n["alias"]   = d.alias;
			n["enabled"] = d.enabled;
		}
	}

private:
	static inline CL_S10_Simulation *s_pSim     = nullptr;
	static inline WiFiMulti          *s_pWiMulti= nullptr;
	static inline CL_P10_PWM         *s_pPwm    = nullptr;
};
            
