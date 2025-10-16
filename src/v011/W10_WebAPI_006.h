#pragma once
/*
 * W10_WebAPI_006.h
 * ------------------------------------------------------
 * WindScape Web API (ESPAsyncWebServer)
 * ------------------------------------------------------
 * 주요 기능
 * - 정적 자산 제공 (HTML/JS/CSS)
 * - 상태/설정/프리셋 API
 * - Wi-Fi 스캔, 시스템 진단, 로그 조회
 * - 설정 변경 / 공장 초기화 / 기본 설정 복구(init)
 * - OTA, 파일 업로드
 * - CORS / 캐시 제어 / 인증(API Key)
 * ------------------------------------------------------
 */

#include <ArduinoJson.h>
#include <AsyncJson.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <Update.h>

#include "A10_Const_006.h"
#include "C10_ConfigManager_006.h"
#include "D10_Logger_004.h"
#include "M10_WiFiManager_004.h"
#include "S10_Simulation_004.h"
#include "P10_PWM_ctrl_005.h"

class CL_W10_WebAPI {
public:
	// ======================================================
	// 초기화
	// ======================================================
	static void init(AsyncWebServer &p_srv, CL_S10_Simulation &p_sim, WiFiMulti &p_multi, CL_P10_PWM &p_pwm) {
		mountApi(p_srv, p_sim, p_multi, p_pwm);
		CL_D10_Logger::log(EN_L10_LOG_INFO, "[WebAPI] API mounted");

		mountStatic(p_srv);
		CL_D10_Logger::log(EN_L10_LOG_INFO, "[WebAPI] Static routes mounted");
	}

	// ======================================================
	// 정적 파일 서빙
	// ======================================================
	static void mountStatic(AsyncWebServer &p_srv) {
		struct StaticRoute {
			const char *uri;
			const char *path;
			const char *mime;
		};

		static const StaticRoute ROUTES[] = {
			{ A10_Const::HTML_URI, A10_Const::HTML_FILE, "text/html" },
			{ A10_Const::CSS_URI,  A10_Const::CSS_FILE,  "text/css" },
			{ A10_Const::JS_URI,   A10_Const::JS_FILE,   "application/javascript" }
		};

		auto &h = p_srv.serveStatic("/", LittleFS, "/html/")
						.setDefaultFile(A10_Const::HTML_FILE)
						.setCacheControl("max-age=86400");
		(void)h;

		auto serve = [&](const StaticRoute &r) {
			p_srv.on(r.uri, HTTP_GET, [=](AsyncWebServerRequest *req) {
				if (LittleFS.exists(r.path))
					req->send(LittleFS, r.path, r.mime);
				else
					req->send(404, "text/plain", String("Missing file: ") + r.path);
			});
			CL_D10_Logger::log(EN_L10_LOG_INFO, "[STATIC] %s -> %s", r.uri, r.path);
		};

		for (auto &r : ROUTES) serve(r);

		p_srv.onNotFound([](AsyncWebServerRequest *req) {
			if (req->method() == HTTP_OPTIONS) {
				auto *res = req->beginResponse(204);
				_addCors(res);
				req->send(res);
				return;
			}
			req->send(404, "text/plain", "Not found");
		});
	}

	// ======================================================
	// API 라우트
	// ======================================================
	static void mountApi(AsyncWebServer &p_srv, CL_S10_Simulation &p_sim, WiFiMulti &p_multi, CL_P10_PWM &p_pwm) {

		// --------------------------------------------------
		// /api/state : 현재 상태 조회
		// --------------------------------------------------
		p_srv.on("/api/state", HTTP_GET, [&p_sim, &p_pwm](AsyncWebServerRequest *req) {
			JsonDocument doc;
			JsonVariant root = doc.to<JsonVariant>();

			JsonObject status = root["status"].to<JsonObject>();
			status["sim_active"] = p_sim.wind_simulation_active;
			status["wind_speed"] = roundf(p_sim.current_wind_speed * 100.0f) / 100.0f;

			status["fan_pwm_raw"] = p_pwm.getDutyRaw();
			status["fan_pwm_percent"] = p_pwm.getDutyPercent();
			root["config"]["pwm"]["resolution"] = g_A10_config.pwm_resolution;

			status["phase_name"] = g_A10_WEATHER_PHASE_NAMES_Arr[p_sim.current_weather_phase];

			if (g_A10_config.wifi_mode == G_A10_WIFI_MODE_STA && WiFi.status() == WL_CONNECTED) {
				status["wifi_mode"] = "STA";
				status["ip_addr"] = WiFi.localIP().toString();
				status["ssid"] = WiFi.SSID();
			} else {
				status["wifi_mode"] = "AP";
				status["ip_addr"] = WiFi.softAPIP().toString();
				status["ssid"] = g_A10_config.ap_ssid;
			}

			// config + presets
			CL_C10_ConfigManager::toJson(g_A10_config, root["config"].to<JsonObject>());
			JsonArray presets = root["presets"].to<JsonArray>();
			for (int i = 0; i < EN_A10_PRESET_COUNT; i++)
				presets.add(g_A10_PRESET_MODE_NAMES_Arr[i]);

			auto *res = req->beginResponseStream("application/json");
			serializeJson(doc, *res);
			res->setCode(200);
			_applyHeaders(res, true);
			req->send(res);
		});

		// --------------------------------------------------
		// /api/config : 설정 변경
		// --------------------------------------------------
		p_srv.on("/api/config", HTTP_POST, nullptr, nullptr,
		[&p_sim, &p_multi, &p_pwm](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index, size_t total) {
			if (!_authorize(req)) {
				req->send(401, "application/json", "{\"error\":\"unauthorized\"}");
				return;
			}

			if (index == 0 && len == total) {
				JsonDocument doc;
				if (deserializeJson(doc, (const char*)data, len)) {
					req->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
					return;
				}

				// 기존 PWM/프리셋 비교
				int oldPreset = g_A10_config.preset_mode_index;
				int oldPin = g_A10_config.fan_pwm_pin;
				int oldFreq = g_A10_config.pwm_frequency;
				int oldRes = g_A10_config.pwm_resolution;
				int oldCh  = g_A10_config.pwm_channel;

				bool wifiChanged = false;
				CL_C10_ConfigManager::patchFromJson(g_A10_config, doc, wifiChanged);
				CL_C10_ConfigManager::save(g_A10_config);

				if (g_A10_config.preset_mode_index != oldPreset)
					p_sim.applyCurrentPreset(true);

				// PWM 재초기화
				if (g_A10_config.fan_pwm_pin != oldPin)
					p_pwm.set_pwmPin(g_A10_config.fan_pwm_pin);
				if (g_A10_config.pwm_channel != oldCh)
					p_pwm.set_pwmChannel(g_A10_config.pwm_channel);
				if (g_A10_config.pwm_frequency != oldFreq)
					p_pwm.set_pwmFrequency(g_A10_config.pwm_frequency);
				if (g_A10_config.pwm_resolution != oldRes)
					p_pwm.set_pwmResolution(g_A10_config.pwm_resolution);

				p_pwm.set_pwmDuty(0.0f);

				if (wifiChanged) {
					CL_D10_Logger::log(EN_L10_LOG_INFO, "WiFi config changed. Re-init WiFi");
					CL_M10_WiFiManager::init(g_A10_config, p_multi);
				}

				req->send(200, "application/json", "{\"message\":\"Config updated\"}");
			}
		});

		// --------------------------------------------------
		// /api/config/init : 기본 설정 생성
		// --------------------------------------------------
		p_srv.on("/api/config/init", HTTP_POST, [](AsyncWebServerRequest *req) {
			if (!_authorize(req)) {
				req->send(401, "application/json", "{\"error\":\"unauthorized\"}");
				return;
			}

			bool ok = CL_C10_ConfigManager::saveDefaultConfig();
			if (ok) {
				CL_D10_Logger::log(EN_L10_LOG_INFO, "[API] Default config created.");
				req->send(200, "application/json", "{\"message\":\"Default config created\"}");
			} else {
				CL_D10_Logger::log(EN_L10_LOG_ERROR, "[API] Default config creation failed!");
				req->send(500, "application/json", "{\"error\":\"Init failed\"}");
			}
		});

		// --------------------------------------------------
		// /api/reset : 공장 초기화
		// --------------------------------------------------
		p_srv.on("/api/reset", HTTP_POST, [](AsyncWebServerRequest *req) {
			if (!_authorize(req)) {
				req->send(401, "application/json", "{\"error\":\"unauthorized\"}");
				return;
			}
			CL_C10_ConfigManager::reset();
			req->send(200, "text/plain", "Factory reset... Reboot");
			ESP.restart();
		});

		// --------------------------------------------------
		// /api/scan : Wi-Fi 스캔
		// --------------------------------------------------
		p_srv.on("/api/scan", HTTP_GET, [](AsyncWebServerRequest *req) {
			bool async = req->hasParam("async");
			String json = CL_M10_WiFiManager::scanNetworksJson(async);
			auto *res = req->beginResponse(200, "application/json", json);
			_applyHeaders(res, true);
			req->send(res);
		});

		// --------------------------------------------------
		// /api/diag : 시스템 진단
		// --------------------------------------------------
		p_srv.on("/api/diag", HTTP_GET, [](AsyncWebServerRequest *req) {
			JsonDocument doc;
			doc["heap"] = ESP.getFreeHeap();
			doc["rssi"] = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0;
			doc["fs_total"] = LittleFS.totalBytes();
			doc["fs_used"] = LittleFS.usedBytes();

			String json;
			serializeJson(doc, json);
			auto *res = req->beginResponse(200, "application/json", json);
			_applyHeaders(res, true);
			req->send(res);
		});

		// --------------------------------------------------
		// /api/logs : 로그 조회
		// --------------------------------------------------
		p_srv.on("/api/logs", HTTP_GET, [](AsyncWebServerRequest *req) {
			String json = CL_D10_Logger::getLogsJson();
			auto *res = req->beginResponse(200, "application/json", json);
			_applyHeaders(res, true);
			req->send(res);
		});

		// --------------------------------------------------
		// /api/version : 펌웨어 버전
		// --------------------------------------------------
		p_srv.on("/api/version", HTTP_GET, [](AsyncWebServerRequest *req) {
			JsonDocument doc;
			doc["fw_version"] = A10_Const::FW_VERSION;
			doc["config_file"] = A10_Const::CONFIG_JSON_FILE;

			String json;
			serializeJson(doc, json);
			auto *res = req->beginResponse(200, "application/json", json);
			_applyHeaders(res, true);
			req->send(res);
		});
	}

private:
	// ======================================================
	// 공통 헤더/보안 유틸
	// ======================================================
	static void _applyHeaders(AsyncWebServerResponse *res, bool noCache = false) {
		if (noCache) _addNoCache(res);
		_addCors(res);
	}
	static void _addNoCache(AsyncWebServerResponse *res) {
		res->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
		res->addHeader("Pragma", "no-cache");
		res->addHeader("Expires", "0");
	}
	static void _addCors(AsyncWebServerResponse *res) {
		res->addHeader("Access-Control-Allow-Origin", "*");
		res->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
		res->addHeader("Access-Control-Allow-Headers", "Content-Type, X-API-Key");
	}

	static bool _authorize(AsyncWebServerRequest *req) {
		if (strlen(g_A10_config.api_key) == 0) return true;
		if (!req->hasHeader("X-API-Key")) return false;
		auto *h = req->getHeader("X-API-Key");
		return (h && h->value() == String(g_A10_config.api_key));
	}
};
