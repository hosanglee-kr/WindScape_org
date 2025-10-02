// SC10_WebAPI_003.h


#pragma once
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Update.h>

#include "SC10_ConfigManager_002.h"
#include "SC10_Const_002.h"
#include "SC10_Logger_002.h"
#include "SC10_Simulation_002.h"
#include "SC10_WiFiManager_002.h"

class SC10_WebAPI {
  public:
  // === 공통: CORS/보안/캐시 유틸 ===
  static void addNoCache(AsyncWebServerResponse* res) {
    res->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    res->addHeader("Pragma", "no-cache");
    res->addHeader("Expires", "0");
  }

  static void addCors(AsyncWebServerResponse* res) {
    // 필요 시 도메인 제한
    res->addHeader("Access-Control-Allow-Origin", "*");
    res->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    res->addHeader("Access-Control-Allow-Headers", "Content-Type, X-API-Key");
  }

  // 위험 엔드포인트 보호: g_SC10_config.api_key 가 비어있지 않으면 검사
  static bool authorize(AsyncWebServerRequest* req) {
    if (strlen(g_SC10_config.api_key) == 0) return true; // 키 미설정 → 통과
    if (!req->hasHeader("X-API-Key")) return false;
    auto* h = req->getHeader("X-API-Key");
    return (h && h->value() == String(g_SC10_config.api_key));
  }

  // 파일명 정규화(상대경로/역슬래시 차단, 단순 파일명만 허용)
  static String sanitizeFilename(const String& in) {
    String out;
    for (size_t i = 0; i < in.length(); i++) {
      char c = in[i];
      if (c == '/' || c == '\\') continue;
      if (c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') continue;
      out += c;
    }
    out.trim();
    return out;
  }

  static bool isAllowedExt(const String& name) {
    String n = name; n.toLowerCase();
    return n.endsWith(".html") || n.endsWith(".htm") || n.endsWith(".js") ||
           n.endsWith(".css") || n.endsWith(".json") || n.endsWith(".txt") ||
           n.endsWith(".gif") || n.endsWith(".png") || n.endsWith(".jpg") ||
           n.endsWith(".jpeg") || n.endsWith(".svg") || n.endsWith(".ico") ||
           n.endsWith(".gz");
  }

  // === 정적 파일 마운트 ===
  static void mountStatic(AsyncWebServer &p_srv) {
    // 정적 루트: /html/ 아래를 사이트 루트로 서비스 (gzip 자동)
    // 예) / -> /html/index.html, /SC10_main_010.js -> /html/SC10_main_010.js
    auto& h = p_srv.serveStatic("/", LittleFS, "/html/")
                 .setDefaultFile("index.html")
                 .setCacheControl("max-age=86400"); // 1일 캐시 (필요시 조정)
    (void)h;

    // 개별 파일(이전 호환 경로). 존재 시 LittleFS에서, 없으면 안전한 대체 응답
    p_srv.on("/SC10_main_010.js", HTTP_GET, [](AsyncWebServerRequest *req) {
      if (LittleFS.exists(SC10_Const::JS_FILE)) {
        req->send(LittleFS, SC10_Const::JS_FILE, "application/javascript");
      } else {
        auto* res = req->beginResponse(200, "application/javascript", "console.log('SC10: no JS file');");
        addNoCache(res); addCors(res); req->send(res);
      }
    });

    p_srv.on("/SC10_main_011.css", HTTP_GET, [](AsyncWebServerRequest *req) {
      if (LittleFS.exists(SC10_Const::CSS_FILE)) {
        req->send(LittleFS, SC10_Const::CSS_FILE, "text/css");
      } else {
        auto* res = req->beginResponse(200, "text/css", "/* SC10: no CSS file */");
        addNoCache(res); addCors(res); req->send(res);
      }
    });

    // OPTIONS preflight (CORS)
    p_srv.onNotFound([](AsyncWebServerRequest *req) {
      if (req->method() == HTTP_OPTIONS) {
        auto* res = req->beginResponse(204);
        addCors(res);
        req->send(res);
        return;
      }
      req->send(404, "text/plain", "Not found");
    });
  }

  // === API 라우트 마운트 ===
  static void mountApi(AsyncWebServer &p_srv, SC10_Simulation &p_sim, WiFiMulti &p_multi) {
    // /api/state — 상태+설정+프리셋
    p_srv.on("/api/state", HTTP_GET, [&p_sim](AsyncWebServerRequest *req) {
      // 여유 용량 산정(상태+설정)
      const size_t v_capacity = 4096 + 1024;
      auto* res = new AsyncJsonResponse(false, v_capacity);
      JsonVariant root = res->getRoot();

      // status
      JsonObject st = root["status"].to<JsonObject>();
      st["sim_active"] = p_sim.wind_simulation_active;
      st["wind_speed"] = roundf(p_sim.current_wind_speed * 100.0f) / 100.0f;
      st["fan_pwm"]    = ledcRead(g_SC10_config.pwm_channel);
      st["phase_name"] = G_SC10_WEATHER_PHASE_NAMES[p_sim.current_weather_phase];

      if (g_SC10_config.wifi_mode == G_SC10_WIFI_MODE_STA && WiFi.status() == WL_CONNECTED) {
        st["wifi_mode"] = "STA";
        st["ip_addr"]   = WiFi.localIP().toString();
        st["ssid"]      = WiFi.SSID();
      } else {
        st["wifi_mode"] = "AP";
        st["ip_addr"]   = WiFi.softAPIP().toString();
        st["ssid"]      = g_SC10_config.ap_ssid;
      }

      // config
      JsonObject cfg = root["config"].to<JsonObject>();
      ConfigManager::toJson(g_SC10_config, cfg);

      // presets
      JsonArray presets = root["presets"].to<JsonArray>();
      for (int i = 0; i < SC10_PRESET_COUNT; i++) presets.add(G_SC10_PRESET_MODE_NAMES[i]);

      res->setCode(200);
      addNoCache(res); addCors(res);
      req->send(res);
    });

    // /api/config — 설정 갱신 및 저장(+Wi-Fi 재초기화)
    p_srv.on("/api/config", HTTP_POST,
      [](AsyncWebServerRequest *req) {
        // body 핸들러에서 응답
      },
      nullptr,
      [&p_sim, &p_multi](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index, size_t total) {
        if (!authorize(req)) { req->send(401, "application/json", "{\"error\":\"unauthorized\"}"); return; }
        if (index == 0 && len == total) {
          DynamicJsonDocument v_doc(4096);
          auto v_err = deserializeJson(v_doc, (const char*)data, len);
          if (v_err) { req->send(400, "application/json", "{\"error\":\"Invalid JSON\"}"); return; }

          bool v_wifiChanged = false;
          ConfigManager::patchFromJson(g_SC10_config, v_doc, v_wifiChanged);
          ConfigManager::save(g_SC10_config);
          p_sim.applyCurrentPreset(true);

          if (v_wifiChanged) {
            SC10_Logger::log(SC10_LOG_INFO, "WiFi config changed. Re-init WiFi");
            SC10_WiFiManager::init(g_SC10_config, p_multi);
          }
          req->send(200, "application/json", "{\"message\":\"Config updated\"}");
        }
      }
    );

    // /api/scan — 주변 SSID 스캔
    p_srv.on("/api/scan", HTTP_GET, [](AsyncWebServerRequest *req) {
      String j = SC10_WiFiManager::scanNetworksJson();
      auto* res = req->beginResponse(200, "application/json", j);
      addNoCache(res); addCors(res);
      req->send(res);
    });

    // /api/diag — 진단 정보
    p_srv.on("/api/diag", HTTP_GET, [](AsyncWebServerRequest *req) {
      DynamicJsonDocument v_doc(1024);
      v_doc["heap"]    = ESP.getFreeHeap();
      v_doc["rssi"]    = WiFi.RSSI();
      v_doc["fs_total"]= LittleFS.totalBytes();
      v_doc["fs_used"] = LittleFS.usedBytes();
      String out; serializeJson(v_doc, out);
      auto* res = req->beginResponse(200, "application/json", out);
      addNoCache(res); addCors(res);
      req->send(res);
    });

    // /api/logs — 최근 로그
    p_srv.on("/api/logs", HTTP_GET, [](AsyncWebServerRequest *req) {
      // 로그가 커질 수 있으니 바로 String 생성 후 전송
      String j = SC10_Logger::getLogsJson();
      auto* res = req->beginResponse(200, "application/json", j);
      addNoCache(res); addCors(res);
      req->send(res);
    });

    // /api/reboot — 재부팅
    p_srv.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest *req) {
      if (!authorize(req)) { req->send(401, "application/json", "{\"error\":\"unauthorized\"}"); return; }
      req->send(200, "text/plain", "Rebooting...");
      // 약간의 지연 후 재부팅을 원하면 타이머 사용 권장
      ESP.restart();
    });

    // /api/reset — 공장 초기화
    p_srv.on("/api/reset", HTTP_POST, [](AsyncWebServerRequest *req) {
      if (!authorize(req)) { req->send(401, "application/json", "{\"error\":\"unauthorized\"}"); return; }
      ConfigManager::reset();
      req->send(200, "text/plain", "Factory reset... Reboot");
      ESP.restart();
    });

    // /api/version
    p_srv.on("/api/version", HTTP_GET, [](AsyncWebServerRequest *req) {
      DynamicJsonDocument v_doc(256);
      v_doc["fw_version"]  = SC10_Const::FW_VERSION;
      v_doc["config_file"] = SC10_Const::CONFIG_FILE;
      String out; serializeJson(v_doc, out);
      auto* res = req->beginResponse(200, "application/json", out);
      addNoCache(res); addCors(res);
      req->send(res);
    });

    // 정적 파일 업로드 (html/js/json 등) — 안전장치 포함
    // X-API-Key 필요, 확장자 화이트리스트, 4MB 제한(예시)
    p_srv.on("/upload", HTTP_POST,
      [](AsyncWebServerRequest *req) {
        if (!authorize(req)) { req->send(401, "application/json", "{\"error\":\"unauthorized\"}"); return; }
        req->send(200, "application/json", "{\"message\":\"Upload OK\"}");
      },
      [](AsyncWebServerRequest *req, const String &filename, size_t index, uint8_t *data, size_t len, bool final) {
        if (!authorize(req)) return;
        static const size_t kMaxUpload = 4 * 1024 * 1024;
        if (index == 0) {
          String safe = sanitizeFilename(filename);
          if (!isAllowedExt(safe)) { req->send(400, "application/json", "{\"error\":\"ext not allowed\"}"); return; }
          String v_path = "/" + safe; // 필요시 "/html/" + safe;
          if (LittleFS.exists(v_path)) LittleFS.remove(v_path);
          req->_tempFile = LittleFS.open(v_path, "w");
          if (!req->_tempFile) { req->send(500, "application/json", "{\"error\":\"open failed\"}"); return; }
          req->client()->setRxTimeout(15);
        }
        if (req->_tempFile) {
          if (req->_tempFile.size() + len > kMaxUpload) { req->_tempFile.close(); LittleFS.remove(req->_tempFile.name()); return; }
          if (len) req->_tempFile.write(data, len);
          if (final) req->_tempFile.close();
        }
      }
    );

    // 펌웨어 OTA 업로드 — 에러처리 강화
    p_srv.on("/update", HTTP_POST,
      [](AsyncWebServerRequest *req) {
        if (!authorize(req)) { req->send(401, "application/json", "{\"error\":\"unauthorized\"}"); return; }
        // final 콜백에서 응답/재부팅
      },
      [](AsyncWebServerRequest *req, const String &filename, size_t index, uint8_t *data, size_t len, bool final) {
        if (!authorize(req)) return;

        if (!index) {
          size_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
          if (!Update.begin(maxSketchSpace)) {
            Update.printError(Serial);
            req->send(500, "text/plain", "OTA begin failed");
            return;
          }
        }

        if (len) {
          if (Update.write(data, len) != len) {
            Update.printError(Serial);
            req->send(500, "text/plain", "OTA write failed");
            return;
          }
        }

        if (final) {
          if (!Update.end(true)) {
            String msg = "OTA end failed: ";
            msg += Update.errorString();
            req->send(500, "text/plain", msg);
            return;
          }
          req->send(200, "text/plain", "OTA OK, rebooting");
          ESP.restart();
        }
      }
    );

    // (이미 mountStatic에서 처리) 정적 루트 대체: 필요 시 아래 라인은 유지 가능
    // p_srv.serveStatic("/", LittleFS, "/html/");
  }
};
