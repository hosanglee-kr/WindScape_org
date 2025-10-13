
// SC10_WebAPI_003.h
// ------------------------------------------------------
// ESPAsyncWebServer 기반 Web API 라우팅 클래스
// ------------------------------------------------------
// 주요 기능
// - 정적 자산 제공 (HTML/JS/CSS) → LittleFS 기반
// - 상태/설정/프리셋 API
// - Wi-Fi 스캔, 시스템 진단, 로그 조회
// - 위험 작업 (Config 변경/Reset/Reboot/Upload/OTA) → API Key 인증 필요
// - OTA 업로드 안전성 강화 (용량 체크, 에러 리턴)
// - 업로드 파일 확장자/사이즈 제한
// - CORS 허용 헤더 처리, 캐시 제어
// ------------------------------------------------------

#pragma once
#include <ArduinoJson.h>
#include <AsyncJson.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <Update.h>

#include "SC10_ConfigManager_002.h"
#include "SC10_Const_002.h"
#include "SC10_Logger_002.h"
#include "SC10_Simulation_002.h"
#include "SC10_WiFiManager_003.h"

class SC10_WebAPI {
   public:
	// ======================================================
	// 유틸리티: 공통 헤더 설정 (보안/캐시)
	// ======================================================

	// 캐시 금지 (매번 최신값 받도록)
	static void addNoCache(AsyncWebServerResponse *res) {
		res->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
		res->addHeader("Pragma", "no-cache");
		res->addHeader("Expires", "0");
	}

	// CORS 허용 (브라우저에서 JS fetch 가능하도록)
	static void addCors(AsyncWebServerResponse *res) {
		res->addHeader("Access-Control-Allow-Origin", "*");
		res->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
		res->addHeader("Access-Control-Allow-Headers", "Content-Type, X-API-Key");
	}

	// API Key 검사 → g_SC10_config.api_key 가 비어 있지 않으면 반드시 헤더 필요
	static bool authorize(AsyncWebServerRequest *req) {
		if (strlen(g_SC10_config.api_key) == 0){
			return true;  // 설정이 비어 있으면 무조건 통과
		}
		if (!req->hasHeader("X-API-Key")){
			return false;  // 헤더 없으면 거부
		}
		
		auto *h = req->getHeader("X-API-Key");
		return (h && h->value() == String(g_SC10_config.api_key));	// 값이 일치해야 통과
	}

	// 업로드 파일명 정규화 → ../ 같은 경로 탈출, 금지 문자 제거
	static String sanitizeFilename(const String &in) {
		String out;
		for (size_t i = 0; i < in.length(); i++) {
			char c = in[i];
			if (c == '/' || c == '\\')
				continue;
			if (c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
				continue;
			out += c;
		}
		out.trim();
		return out;
	}

	// 허용 확장자만 필터 (html/js/css/json/이미지 등)
	static bool isAllowedExt(const String &name) {
		String n = name;
		n.toLowerCase();
		return n.endsWith(".html") || n.endsWith(".htm") || n.endsWith(".js") ||
			   n.endsWith(".css") || n.endsWith(".json") || n.endsWith(".txt") ||
			   n.endsWith(".gif") || n.endsWith(".png") || n.endsWith(".jpg") ||
			   n.endsWith(".jpeg") || n.endsWith(".svg") || n.endsWith(".ico") ||
			   n.endsWith(".gz");
	}

	// ======================================================
	// 정적 파일 서빙
	// ======================================================
	static void mountStatic(AsyncWebServer &p_srv) {
		// /html/ 폴더를 기본 루트로 서비스
		// → / 요청 시 index.html 자동 매핑
		auto &h = p_srv.serveStatic("/", LittleFS, "/html/")
					  .setDefaultFile("SC10_main_013.html")
					  .setCacheControl("max-age=86400");  // 하루 캐시
		(void)h;

		// (옵션) 개별 경로도 호환성 위해 남겨둠
		p_srv.on("/SC10_main_010.js", HTTP_GET, [](AsyncWebServerRequest *req) {
			if (LittleFS.exists(SC10_Const::JS_FILE)) {
				req->send(LittleFS, SC10_Const::JS_FILE, "application/javascript");
			} else {
				auto *res = req->beginResponse(200, "application/javascript", "console.log('SC10: no JS file');");
				addNoCache(res);
				addCors(res);
				req->send(res);
			}
		});

		p_srv.on("/SC10_main_011.css", HTTP_GET, [](AsyncWebServerRequest *req) {
			if (LittleFS.exists(SC10_Const::CSS_FILE)) {
				req->send(LittleFS, SC10_Const::CSS_FILE, "text/css");
			} else {
				auto *res = req->beginResponse(200, "text/css", "/* SC10: no CSS file */");
				addNoCache(res);
				addCors(res);
				req->send(res);
			}
		});

		// OPTIONS 프리플라이트 (브라우저 CORS 사전 요청 처리)
		p_srv.onNotFound([](AsyncWebServerRequest *req) {
			if (req->method() == HTTP_OPTIONS) {
				auto *res = req->beginResponse(204);
				addCors(res);
				req->send(res);
				return;
			}
			req->send(404, "text/plain", "Not found");
		});
	}

	// ======================================================
	// API 라우트 등록
	// ======================================================
	static void mountApi(AsyncWebServer &p_srv, SC10_Simulation &p_sim, WiFiMulti &p_multi) {
		// -------------------
		// /api/state : 현재 상태 조회
		// -------------------
		p_srv.on("/api/state", HTTP_GET, [&p_sim](AsyncWebServerRequest *req) {
			// 상태 + config + preset 전체 JSON으로
			AsyncJsonResponse *res	= new AsyncJsonResponse(false);
			////AsyncJsonResponse *res	= new AsyncJsonResponse(false, 4096);
			JsonVariant		   root = res->getRoot();

			JsonObject st	 = root["status"].to<JsonObject>();
			st["sim_active"] = p_sim.wind_simulation_active;
			st["wind_speed"] = roundf(p_sim.current_wind_speed * 100.0f) / 100.0f;
			
			// ▽▽ 추가/변경: PWM raw + percent 동시 제공 ▽▽
            const int duty_raw = ledcRead(g_SC10_config.pwm_channel);
            const int levels   = (1 << g_SC10_config.pwm_resolution) - 1;
            const float duty_percent = (levels > 0) ? (100.0f * duty_raw / (float)levels) : 0.0f;

            st["fan_pwm"]         = duty_raw;        // (기존 호환) raw duty 유지
            st["fan_pwm_percent"] = duty_percent;
			// (선택) 클라이언트 계산용으로 해상도도 내려주면 더 좋음
            root["config"]["pwm"]["resolution"] = g_SC10_config.pwm_resolution;
			
			st["phase_name"] = G_SC10_WEATHER_PHASE_NAMES[p_sim.current_weather_phase];

			if (g_SC10_config.wifi_mode == G_SC10_WIFI_MODE_STA && WiFi.status() == WL_CONNECTED) {
				st["wifi_mode"] = "STA";
				st["ip_addr"]	= WiFi.localIP().toString();
				st["ssid"]		= WiFi.SSID();
			} else {
				st["wifi_mode"] = "AP";
				st["ip_addr"]	= WiFi.softAPIP().toString();
				st["ssid"]		= g_SC10_config.ap_ssid;
			}

			// config 직렬화
			ConfigManager::toJson(g_SC10_config, root["config"].to<JsonObject>());

			// presets 추가
			JsonArray presets = root["presets"].to<JsonArray>();
			for (int i = 0; i < SC10_PRESET_COUNT; i++) {
				presets.add(G_SC10_PRESET_MODE_NAMES[i]);
			}

			res->setCode(200);
			addNoCache(res);
			addCors(res);
			req->send(res);
		});

		// -------------------
		// /api/config : 설정 변경 (Wi-Fi 재초기화 가능)
		// -------------------
		p_srv.on("/api/config", HTTP_POST, [](AsyncWebServerRequest *req) {}, nullptr, [&p_sim, &p_multi](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index, size_t total) {
        if (!authorize(req)) { req->send(401, "application/json", "{\"error\":\"unauthorized\"}"); return; }
        if (index == 0 && len == total) {
          JsonDocument v_doc;
          if (deserializeJson(v_doc, (const char*)data, len)) {
            req->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
          }
          bool v_wifiChanged = false;
          ConfigManager::patchFromJson(g_SC10_config, v_doc, v_wifiChanged);
          ConfigManager::save(g_SC10_config);
          p_sim.applyCurrentPreset(true);

          if (v_wifiChanged) {
            SC10_Logger::log(SC10_LOG_INFO, "WiFi config changed. Re-init WiFi");
            SC10_WiFiManager::init(g_SC10_config, p_multi);
          }
          req->send(200, "application/json", "{\"message\":\"Config updated\"}");
        } });

		// -------------------
		// /api/scan : 주변 Wi-Fi 스캔
		// -------------------
		p_srv.on("/api/scan", HTTP_GET, [](AsyncWebServerRequest *req) {
            bool async = req->hasParam("async");
            String j = SC10_WiFiManager::scanNetworksJson(async);
            auto *res = req->beginResponse(200, "application/json", j);
             addNoCache(res); addCors(res); req->send(res);
        });
		/*
		p_srv.on("/api/scan", HTTP_GET, [](AsyncWebServerRequest *req) {
			String j   = SC10_WiFiManager::scanNetworksJson();
			auto  *res = req->beginResponse(200, "application/json", j);
			addNoCache(res);
			addCors(res);
			req->send(res);
		});
		*/

		// -------------------
		// /api/diag : 메모리/FS/신호 강도 등 진단
		// -------------------
		p_srv.on("/api/diag", HTTP_GET, [](AsyncWebServerRequest *req) {
			JsonDocument v_doc;
			v_doc["heap"]	  = ESP.getFreeHeap();
			v_doc["rssi"]	  = WiFi.RSSI();
			v_doc["fs_total"] = LittleFS.totalBytes();
			v_doc["fs_used"]  = LittleFS.usedBytes();
			String out;
			serializeJson(v_doc, out);
			auto *res = req->beginResponse(200, "application/json", out);
			addNoCache(res);
			addCors(res);
			req->send(res);
		});

		// -------------------
		// /api/logs : 최근 로그 반환
		// -------------------
		p_srv.on("/api/logs", HTTP_GET, [](AsyncWebServerRequest *req) {
			String j   = SC10_Logger::getLogsJson();
			auto  *res = req->beginResponse(200, "application/json", j);
			addNoCache(res);
			addCors(res);
			req->send(res);
		});

		// -------------------
		// /api/reboot : 시스템 재부팅
		// -------------------
		p_srv.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest *req) {
			if (!authorize(req)) {
				req->send(401, "application/json", "{\"error\":\"unauthorized\"}");
				return;
			}
			req->send(200, "text/plain", "Rebooting...");
			ESP.restart();
		});

		// -------------------
		// /api/reset : 공장 초기화
		// -------------------
		p_srv.on("/api/reset", HTTP_POST, [](AsyncWebServerRequest *req) {
			if (!authorize(req)) {
				req->send(401, "application/json", "{\"error\":\"unauthorized\"}");
				return;
			}
			ConfigManager::reset();
			req->send(200, "text/plain", "Factory reset... Reboot");
			ESP.restart();
		});

		// -------------------
		// /api/version : 펌웨어 버전 정보
		// -------------------
		p_srv.on("/api/version", HTTP_GET, [](AsyncWebServerRequest *req) {
			JsonDocument v_doc;
			v_doc["fw_version"]	 = SC10_Const::FW_VERSION;
			v_doc["config_file"] = SC10_Const::CONFIG_FILE;
			String out;
			serializeJson(v_doc, out);
			auto *res = req->beginResponse(200, "application/json", out);
			addNoCache(res);
			addCors(res);
			req->send(res);
		});

		// -------------------
		// /upload : 정적 파일 업로드 (보안제한)
		// -------------------
		static bool s_uploadError = false;

p_srv.on("/upload", HTTP_POST,
  [](AsyncWebServerRequest *req) {
    if (!authorize(req)) {
      req->send(401, "application/json", "{\"error\":\"unauthorized\"}");
      return;
    }
    if (s_uploadError) {
      s_uploadError = false;
      req->send(500, "application/json", "{\"error\":\"upload failed\"}");
    } else {
      req->send(200, "application/json", "{\"message\":\"Upload OK\"}");
    }
  },
  [](AsyncWebServerRequest *req, const String &filename, size_t index, uint8_t *data, size_t len, bool final) {
    if (!authorize(req)) { s_uploadError = true; return; }
    static const size_t kMaxUpload = 4 * 1024 * 1024;
    if (index == 0) {
      s_uploadError = false;
      String safe = sanitizeFilename(filename);
      if (!isAllowedExt(safe)) { s_uploadError = true; return; }
      String v_path = "/" + safe;
      if (LittleFS.exists(v_path)) LittleFS.remove(v_path);
      req->_tempFile = LittleFS.open(v_path, "w");
      if (!req->_tempFile) { s_uploadError = true; return; }
    }
    if (s_uploadError) return;
    if (req->_tempFile) {
      if (req->_tempFile.size() + len > kMaxUpload) {
        req->_tempFile.close();
        LittleFS.remove(req->_tempFile.name());
        s_uploadError = true;
        return;
      }
      if (len) req->_tempFile.write(data, len);
      if (final) req->_tempFile.close();
    }
});

		/*
		p_srv.on("/upload", HTTP_POST, [](AsyncWebServerRequest *req) {
        if (!authorize(req)) { req->send(401, "application/json", "{\"error\":\"unauthorized\"}"); return; }
        req->send(200, "application/json", "{\"message\":\"Upload OK\"}"); }, [](AsyncWebServerRequest *req, const String &filename, size_t index, uint8_t *data, size_t len, bool final) {
        if (!authorize(req)) return;
        static const size_t kMaxUpload = 4 * 1024 * 1024; // 4MB 제한
        if (index == 0) {
          String safe = sanitizeFilename(filename);
          if (!isAllowedExt(safe)) { req->send(400, "application/json", "{\"error\":\"ext not allowed\"}"); return; }
          String v_path = "/" + safe;
          if (LittleFS.exists(v_path)) LittleFS.remove(v_path);
          req->_tempFile = LittleFS.open(v_path, "w");
          if (!req->_tempFile) { req->send(500, "application/json", "{\"error\":\"open failed\"}"); return; }
        }
        if (req->_tempFile) {
          if (req->_tempFile.size() + len > kMaxUpload) { req->_tempFile.close(); LittleFS.remove(req->_tempFile.name()); return; }
          if (len) req->_tempFile.write(data, len);
          if (final) req->_tempFile.close();
        } });
		*/

		// -------------------
		// /update : OTA 펌웨어 업로드
		// -------------------
		p_srv.on("/update", HTTP_POST, [](AsyncWebServerRequest *req) {
        if (!authorize(req)) { req->send(401, "application/json", "{\"error\":\"unauthorized\"}"); return; } }, [](AsyncWebServerRequest *req, const String &filename, size_t index, uint8_t *data, size_t len, bool final) {
        if (!authorize(req)) return;
        if (!index) {
          size_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
          if (!Update.begin(maxSketchSpace)) { Update.printError(Serial); req->send(500, "text/plain", "OTA begin failed"); return; }
        }
        if (len) {
          if (Update.write(data, len) != len) { Update.printError(Serial); req->send(500, "text/plain", "OTA write failed"); return; }
        }
        if (final) {
          if (!Update.end(true)) { String msg = "OTA end failed: "; msg += Update.errorString(); req->send(500, "text/plain", msg); return; }
          req->send(200, "text/plain", "OTA OK, rebooting");
          ESP.restart();
        } });
	}
};
