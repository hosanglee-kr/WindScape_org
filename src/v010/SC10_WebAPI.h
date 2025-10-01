// SC10_WebAPI.h

#pragma once
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <Update.h>
#include "SC10_Const.h"
#include "SC10_Logger.h"
#include "SC10_ConfigManager.h"
#include "SC10_WiFiManager.h"
#include "SC10_Simulation.h"

// WebServer 라우팅/핸들러 묶음
class SC10_WebAPI {
public:
  static void mountStatic(AsyncWebServer &p_srv) {
    // 루트/스크립트 정적 파일 (원본 호환)
    p_srv.on("/", HTTP_GET, [](AsyncWebServerRequest *req){
      if (LittleFS.begin() && LittleFS.exists(SC10_Const::HTML_FILE)) {
        req->send(LittleFS, SC10_Const::HTML_FILE, "text/html");
      } else {
        req->send(200,"text/html","<h3>SC10 Web UI</h3><p>LittleFS에 HTML이 없습니다.</p>");
      }
    });
    p_srv.on("/script.js", HTTP_GET, [](AsyncWebServerRequest *req){
      if (LittleFS.begin() && LittleFS.exists(SC10_Const::JS_FILE)) {
        req->send(LittleFS, SC10_Const::JS_FILE, "application/javascript");
      } else {
        req->send(200,"application/javascript","console.log('SC10: no JS file');");
      }
    });
  }

  static void mountApi(AsyncWebServer &p_srv, SC10_Simulation &p_sim, WiFiMulti &p_multi) {
    // /api/state — 상태+설정+프리셋
    p_srv.on("/api/state", HTTP_GET, [&p_sim](AsyncWebServerRequest *req){
      JsonDocument doc;

      // status
      JsonObject st = doc["status"].to<JsonObject>();
      st["sim_active"] = p_sim.wind_simulation_active;
      st["wind_speed"] = roundf(p_sim.current_wind_speed*100.0f)/100.0f;
      st["fan_pwm"]    = ledcRead(g_SC10_config.pwm_channel);
      st["phase_name"] = G_SC10_WEATHER_PHASE_NAMES[p_sim.current_weather_phase];

      if (g_SC10_config.wifi_mode == G_SC10_WIFI_MODE_STA && WiFi.status()==WL_CONNECTED) {
        st["wifi_mode"] = "STA";
        st["ip_addr"]   = WiFi.localIP().toString();
        st["ssid"]      = WiFi.SSID();
      } else {
        st["wifi_mode"] = "AP";
        st["ip_addr"]   = WiFi.softAPIP().toString();
        st["ssid"]      = g_SC10_config.ap_ssid;
      }

      // config (모든 시뮬/타이밍/Wi-Fi)
      JsonObject cfgObj = doc["config"].to<JsonObject>();
      ConfigManager::toJson(g_SC10_config, cfgObj);
      // ConfigManager::toJson(g_SC10_config, doc["config"].to<JsonDocument>());

      // presets
      JsonArray p = doc["presets"].to<JsonArray>();
      for (int i=0;i<SC10_PRESET_COUNT;i++) p.add(G_SC10_PRESET_MODE_NAMES[i]);

      String res; serializeJson(doc, res);
      req->se
        nd(200, "application/json", res);
    });

    // /api/config — 설정 갱신 및 저장(+Wi-Fi 재초기화)
    p_srv.on("/api/config", HTTP_POST,
      [](AsyncWebServerRequest *req){ /* no-op body in handler */ },
      nullptr,
      [&p_sim,&p_multi](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index, size_t total){
        if (index==0 && len==total) {
          JsonDocument doc; DeserializationError err = deserializeJson(doc, (const char*)data, len);
          if (err) { req->send(400,"application/json","{\"error\":\"Invalid JSON\"}"); return; }
          bool wifiChanged=false;
          ConfigManager::patchFromJson(g_SC10_config, doc, wifiChanged);
          ConfigManager::save(g_SC10_config);
          // 프리셋 즉시 재적용
          p_sim.applyCurrentPreset(true);
          // Wi-Fi 변경되었으면 재초기화
          if (wifiChanged) {
            SC10_Logger::log(SC10_LOG_INFO,"WiFi config changed. Re-init WiFi");
            SC10_WiFiManager::init(g_SC10_config, p_multi);
          }
          req->send(200,"application/json","{\"message\":\"Config updated\"}");
        }
      }
    );

    // /api/scan — 주변 SSID 스캔
    p_srv.on("/api/scan", HTTP_GET, [](AsyncWebServerRequest *req){
      req->send(200,"application/json",SC10_WiFiManager::scanNetworksJson());
    });

    // /api/diag — 진단 정보
    p_srv.on("/api/diag", HTTP_GET, [](AsyncWebServerRequest *req){
      JsonDocument d;
      d["heap"]    = ESP.getFreeHeap();
      d["rssi"]    = WiFi.RSSI();
      d["fs_total"]= LittleFS.totalBytes();
      d["fs_used"] = LittleFS.usedBytes();
      String res; serializeJson(d,res);
      req->send(200,"application/json",res);
    });

    // /api/logs — 최근 로그
    p_srv.on("/api/logs", HTTP_GET, [](AsyncWebServerRequest *req){
      req->send(200, "application/json", SC10_Logger::getLogsJson());
    });

    // /api/reboot — 재부팅
    p_srv.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest *req){
      req->send(200,"text/plain","Rebooting...");
      ESP.restart();
    });

    // /api/reset — 공장 초기화
    p_srv.on("/api/reset", HTTP_POST, [](AsyncWebServerRequest *req){
      ConfigManager::reset();
      req->send(200,"text/plain","Factory reset... Reboot");
      ESP.restart();
    });

    // /api/version
    p_srv.on("/api/version", HTTP_GET, [](AsyncWebServerRequest *req){
      JsonDocument d; d["fw_version"] = SC10_Const::FW_VERSION;
      d["config_file"] = SC10_Const::CONFIG_FILE;
      String res; serializeJson(d, res);
      req->send(200,"application/json",res);
    });

    // 정적 파일 업로드 (html/js/json 등)
    p_srv.on("/upload", HTTP_POST,
      [](AsyncWebServerRequest *req){ req->send(200,"text/plain","Upload OK"); },
      [](AsyncWebServerRequest *req, const String &filename, size_t index, uint8_t *data, size_t len, bool final){
        if (!index) {
          String path = "/" + filename; // 필요 시 "/html/" + filename 로 강제 가능
          if (LittleFS.exists(path)) LittleFS.remove(path);
          req->_tempFile = LittleFS.open(path, "w");
        }
        if (len && req->_tempFile) req->_tempFile.write(data, len);
        if (final && req->_tempFile) req->_tempFile.close();
      }
    );

    // 펌웨어 OTA 업로드
    p_srv.on("/update", HTTP_POST,
      [](AsyncWebServerRequest *req){ /* will restart after final */ },
      [](AsyncWebServerRequest *req, const String &filename, size_t index, uint8_t *data, size_t len, bool final) {
        if (!index) Update.begin();
        if (len) Update.write(data, len);
        if (final) { Update.end(true); req->send(200,"text/plain","OTA OK, rebooting"); ESP.restart(); }
      }
    );

    // NotFound
    p_srv.onNotFound([](AsyncWebServerRequest *req){
      req->send(404,"text/plain","Not found");
    });
  }
};


