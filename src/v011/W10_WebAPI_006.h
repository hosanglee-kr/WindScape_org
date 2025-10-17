#pragma once
/*
 * W10_WebAPI_006.h
 * ------------------------------------------------------
 * WindScape Web API (ESPAsyncWebServer)
 * ------------------------------------------------------
 * 주요 기능
 *  - 정적 자산(HTML/CSS/JS) 제공
 *  - 현재 상태(/api/state)
 *  - 설정 변경(/api/config)
 *  - 기본 설정 생성(/api/config/init)
 *  - 공장 초기화(/api/reset)
 *  - Wi-Fi 스캔(/api/scan)
 *  - 진단(/api/diag)
 *  - 로그(/api/logs)
 *  - 버전(/api/version)
 *  - OTA / File Upload
 * ------------------------------------------------------
 */

#include <ArduinoJson.h>
#include <AsyncJson.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <Update.h>
#include <WiFiMulti.h>

#include "A10_Const_006.h"
#include "C10_ConfigManager_006.h"
#include "D10_Logger_004.h"
#include "M10_WiFiManager_006.h"
#include "S10_Simulation_006.h"
#include "P10_PWM_ctrl_005.h"

class CL_W10_WebAPI {
public:
	// ======================================================
	// 초기화: API 및 정적 자원 등록
	// ======================================================
	static void init(AsyncWebServer &srv, CL_S10_Simulation &sim, WiFiMulti &multi, CL_P10_PWM &pwm) {
		mountApi(srv, sim, multi, pwm);
		CL_D10_Logger::log(EN_L10_LOG_INFO, "[WebAPI] mountApi complete");
		mountStatic(srv);
		CL_D10_Logger::log(EN_L10_LOG_INFO, "[WebAPI] mountStatic complete");
	}

	// ======================================================
	// 정적 파일 서빙
	// ======================================================
	static void mountStatic(AsyncWebServer &srv) {
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

		srv.serveStatic("/", LittleFS, "/html/")
		   .setDefaultFile(A10_Const::HTML_FILE)
		   .setCacheControl("max-age=86400");

		for (auto &r : ROUTES) {
			srv.on(r.uri, HTTP_GET, [=](AsyncWebServerRequest *req) {
				if (LittleFS.exists(r.path))
					req->send(LittleFS, r.path, r.mime);
				else
					req->send(404, "text/plain", String("Missing file: ") + r.path);
			});
			CL_D10_Logger::log(EN_L10_LOG_INFO, "[STATIC] %s -> %s", r.uri, r.path);
		}

		srv.onNotFound([](AsyncWebServerRequest *req) {
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
	// API 라우트 등록
	// ======================================================
	static void mountApi(AsyncWebServer &srv, CL_S10_Simulation &sim, WiFiMulti &multi, CL_P10_PWM &pwm) {

		// --------------------------------------------------
		// /api/state : 현재 상태 조회
		// --------------------------------------------------
		srv.on("/api/state", HTTP_GET, [&sim, &pwm](AsyncWebServerRequest *req) {
			JsonDocument doc;
			JsonVariant root = doc.to<JsonVariant>();

			JsonObject st = root["status"].to<JsonObject>();
			st["sim_active"] = sim.wind_simulation_active;
			st["wind_speed"] = roundf(sim.current_wind_speed * 100.0f) / 100.0f;
			st["fan_pwm_raw"] = pwm.getDutyRaw();
			st["fan_pwm_percent"] = pwm.getDutyPercent();
			st["phase_name"] = g_A10_WEATHER_PHASE_NAMES_Arr[sim.current_weather_phase];

			if (g_A10_config.wifi_mode == G_A10_WIFI_MODE_STA && WiFi.status() == WL_CONNECTED) {
				st["wifi_mode"] = "STA";
				st["ip_addr"] = WiFi.localIP().toString();
				st["ssid"] = WiFi.SSID();
			} else {
				st["wifi_mode"] = "AP";
				st["ip_addr"] = WiFi.softAPIP().toString();
				st["ssid"] = g_A10_config.ap_ssid;
			}

			// config 직렬화
			CL_C10_ConfigManager::toJson(g_A10_config, root["config"].to<JsonObject>());

			// presets 목록
			JsonArray presets = root["presets"].to<JsonArray>();
			for (int i = 0; i < EN_A10_PRESET_COUNT; i++)
				presets.add(g_A10_PRESET_MODE_NAMES_Arr[i]);

			auto *res = req->beginResponseStream("application/json");
			serializeJson(doc, *res);
			_applyHeaders(res, true);
			req->send(res);
		});

		// --------------------------------------------------
		// /api/config : 설정 변경 (patchFromJson 사용)
		// --------------------------------------------------
		srv.on("/api/config", HTTP_POST, nullptr, nullptr,
		[&sim, &multi, &pwm](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index, size_t total) {
			if (!_authorize(req)) {
				req->send(401, "application/json", "{\"error\":\"unauthorized\"}");
				return;
			}

			if (index == 0 && len == total) {
				JsonDocument doc;
				DeserializationError err = deserializeJson(doc, (const char*)data, len);
				if (err) {
					req->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
					return;
				}

				int oldPreset = g_A10_config.preset_mode_index;
				int oldPin = g_A10_config.fan_pwm_pin;
				int oldFreq = g_A10_config.pwm_frequency;
				int oldRes = g_A10_config.pwm_resolution;
				int oldCh  = g_A10_config.pwm_channel;

				bool wifiChanged = false;
				CL_C10_ConfigManager::patchFromJson(g_A10_config, doc, wifiChanged);
				CL_C10_ConfigManager::save(g_A10_config);

				if (g_A10_config.preset_mode_index != oldPreset)
					sim.applyCurrentPreset(true);

				// PWM 변경사항 즉시 반영
				if (g_A10_config.fan_pwm_pin != oldPin)
					pwm.set_pwmPin(g_A10_config.fan_pwm_pin);
				if (g_A10_config.pwm_channel != oldCh)
					pwm.set_pwmChannel(g_A10_config.pwm_channel);
				if (g_A10_config.pwm_frequency != oldFreq)
					pwm.set_pwmFrequency(g_A10_config.pwm_frequency);
				if (g_A10_config.pwm_resolution != oldRes)
					pwm.set_pwmResolution(g_A10_config.pwm_resolution);

				pwm.set_pwmDuty(0.0f); // 안정화

				if (wifiChanged) {
					CL_D10_Logger::log(EN_L10_LOG_INFO, "Wi-Fi changed, reinit...");
					CL_M10_WiFiManager::init(g_A10_config, multi);
				}

				req->send(200, "application/json", "{\"message\":\"Config updated\"}");
			}
		});

		// --------------------------------------------------
		// /api/config/init : 기본 설정 생성
		// --------------------------------------------------
		srv.on("/api/config/init", HTTP_POST, [](AsyncWebServerRequest *req) {
			if (!_authorize(req)) {
				req->send(401, "application/json", "{\"error\":\"unauthorized\"}");
				return;
			}
			bool ok = CL_C10_ConfigManager::saveDefaultConfig();
			if (ok)
				req->send(200, "application/json", "{\"message\":\"Default config created\"}");
			else
				req->send(500, "application/json", "{\"error\":\"Init failed\"}");
		});

		// --------------------------------------------------
		// /api/reset : 공장 초기화
		// --------------------------------------------------
		srv.on("/api/reset", HTTP_POST, [](AsyncWebServerRequest *req) {
			if (!_authorize(req)) {
				req->send(401, "application/json", "{\"error\":\"unauthorized\"}");
				return;
			}
			CL_C10_ConfigManager::reset();
			req->send(200, "text/plain", "Factory reset, rebooting...");
			delay(500);
			ESP.restart();
		});

		// --------------------------------------------------
		// /api/scan : Wi-Fi 스캔
		// --------------------------------------------------
		srv.on("/api/scan", HTTP_GET, [](AsyncWebServerRequest *req) {
			bool async = req->hasParam("async");
			String json = CL_M10_WiFiManager::scanNetworksJson(async);
			auto *res = req->beginResponse(200, "application/json", json);
			_applyHeaders(res, true);
			req->send(res);
		});

		// --------------------------------------------------
		// /api/diag : 진단
		// --------------------------------------------------
		srv.on("/api/diag", HTTP_GET, [](AsyncWebServerRequest *req) {
			JsonDocument doc;
			doc["heap"] = ESP.getFreeHeap();
			doc["rssi"] = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0;
			doc["fs_total"] = LittleFS.totalBytes();
			doc["fs_used"]  = LittleFS.usedBytes();

			String json;
			serializeJson(doc, json);
			auto *res = req->beginResponse(200, "application/json", json);
			_applyHeaders(res, true);
			req->send(res);
		});

		// --------------------------------------------------
		// /api/logs : 로그 조회
		// --------------------------------------------------
		srv.on("/api/logs", HTTP_GET, [](AsyncWebServerRequest *req) {
			String json = CL_D10_Logger::getLogsJson();
			auto *res = req->beginResponse(200, "application/json", json);
			_applyHeaders(res, true);
			req->send(res);
		});

		// --------------------------------------------------
		// /api/version : 펌웨어 정보
		// --------------------------------------------------
		srv.on("/api/version", HTTP_GET, [](AsyncWebServerRequest *req) {
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
	// 공통 헤더 유틸
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
		if (strlen(g_A10_config.api_key) == 0)
			return true;
		if (!req->hasHeader("X-API-Key"))
			return false;
		auto *h = req->getHeader("X-API-Key");
		return (h && h->value() == String(g_A10_config.api_key));
	}
};
