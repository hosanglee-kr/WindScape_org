#pragma once
/*
 * W10_WebAPI_006.h
 * ------------------------------------------------------
 * WindScape Web API (ESPAsyncWebServer 기반)
 * ------------------------------------------------------
 * 기능 요약:
 *  - LittleFS 정적 자산 서빙 (HTML/CSS/JS)
 *  - 상태/설정/프리셋 API
 *  - Wi-Fi 스캔, 시스템 진단, 로그 조회
 *  - Config 변경 / Reset / Default Init
 *  - OTA 및 File Upload
 *  - API Key 인증 / CORS 헤더 / 캐시 제어
 * ------------------------------------------------------
 */

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <Update.h>

#include "A10_Const_006.h"
#include "C10_ConfigManager_006.h"
#include "D10_Logger_004.h"
#include "M10_WiFiManager_006.h"
#include "S10_Simulation_006.h"
#include "P10_PWM_ctrl_005.h"

class CL_W10_WebAPI {
   public:
    // ======================================================
    // 초기화
    // ======================================================
    static void init(AsyncWebServer &p_srv, CL_S10_Simulation &p_sim, WiFiMulti &p_multi, CL_P10_PWM &p_P10_pwm) {
        mountApi(p_srv, p_sim, p_multi, p_P10_pwm);
        CL_D10_Logger::log(EN_L10_LOG_INFO, "W10_init_010::mountApi");

        mountStatic(p_srv);
        CL_D10_Logger::log(EN_L10_LOG_INFO, "W10_init_020::mountStatic");
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
            {A10_Const::HTML_URI, A10_Const::HTML_FILE, "text/html"},
            {A10_Const::CSS_URI, A10_Const::CSS_FILE, "text/css"},
            {A10_Const::JS_URI, A10_Const::JS_FILE, "application/javascript"}};

        auto v_serveFile = [&](const StaticRoute &p_staticRoute) {
            p_srv.on(p_staticRoute.uri, HTTP_GET, [=](AsyncWebServerRequest *p_request) {
                if (LittleFS.exists(p_staticRoute.path)) {
                    p_request->send(LittleFS, p_staticRoute.path, p_staticRoute.mime);
                } else {
                    String v_msg = String("/* missing file: ") + p_staticRoute.path + " */";
                    auto *v_response = p_request->beginResponse(200, p_staticRoute.mime, v_msg);
                    _applyHeaders(v_response, true);
                    p_request->send(v_response);
                }
            });
        };

        for (auto &v_staticRoute : ROUTES) {
            CL_D10_Logger::log(EN_L10_LOG_INFO, "[STATIC] %s -> %s (%s)",
                               v_staticRoute.uri, v_staticRoute.path, v_staticRoute.mime);
            v_serveFile(v_staticRoute);
        }

        // OPTIONS 요청 처리 (CORS)
        p_srv.onNotFound([](AsyncWebServerRequest *p_request) {
            if (p_request->method() == HTTP_OPTIONS) {
                auto *v_response = p_request->beginResponse(204);
                _addCors(v_response);
                p_request->send(v_response);
                return;
            }
            p_request->send(404, "text/plain", "Not found");
        });
    }

    // ======================================================
    // API 등록
    // ======================================================
    static void mountApi(AsyncWebServer &p_srv, CL_S10_Simulation &p_sim, WiFiMulti &p_multi, CL_P10_PWM &p_P10_pwm) {
        // ---------------------------
        // /api/state : 상태 조회
        // ---------------------------
        p_srv.on("/api/state", HTTP_GET, [&p_sim, &p_P10_pwm](AsyncWebServerRequest *p_request) {
            JsonDocument v_doc;
            JsonVariant v_root = v_doc.to<JsonVariant>();

            JsonObject v_status = v_root["status"].to<JsonObject>();
            v_status["sim_active"] = p_sim.wind_simulation_active;
            v_status["wind_speed"] = roundf(p_sim.current_wind_speed * 100.0f) / 100.0f;
            v_status["fan_pwm_raw"] = p_P10_pwm.getDutyRaw();
            v_status["fan_pwm_percent"] = p_P10_pwm.getDutyPercent();
            v_status["phase_name"] = g_A10_WEATHER_PHASE_NAMES_Arr[p_sim.current_weather_phase];

            if (g_A10_config.wifi_mode == G_A10_WIFI_MODE_STA && WiFi.status() == WL_CONNECTED) {
                v_status["wifi_mode"] = "STA";
                v_status["ip_addr"] = WiFi.localIP().toString();
                v_status["ssid"] = WiFi.SSID();
            } else {
                v_status["wifi_mode"] = "AP";
                v_status["ip_addr"] = WiFi.softAPIP().toString();
                v_status["ssid"] = g_A10_config.ap_ssid;
            }

            // config 직렬화
            CL_C10_ConfigManager::toJson(g_A10_config, v_root["config"].to<JsonObject>());

            // presets
            JsonArray v_presets = v_root["presets"].to<JsonArray>();
            for (int i = 0; i < EN_A10_PRESET_COUNT; ++i)
                v_presets.add(g_A10_PRESET_MODE_NAMES_Arr[i]);

            auto *v_response = p_request->beginResponseStream("application/json");
            serializeJson(v_doc, *v_response);
            v_response->setCode(200);
            _applyHeaders(v_response, true);
            p_request->send(v_response);
        });

        // ---------------------------
        // /api/config : 설정 변경
        // ---------------------------
        p_srv.on("/api/config", HTTP_POST, [](AsyncWebServerRequest *p_request) {}, nullptr,
                 [&p_sim, &p_multi, &p_P10_pwm](AsyncWebServerRequest *p_request,
                                               uint8_t *data, size_t len, size_t index, size_t total) {
                     if (!_authorize(p_request)) {
                         p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
                         return;
                     }

                     if (index == 0 && len == total) {
                         JsonDocument v_doc;
                         if (deserializeJson(v_doc, (const char *)data, len)) {
                             p_request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
                             return;
                         }

                         int v_oldPreset = g_A10_config.preset_mode_index;
                         int v_oldPin = g_A10_config.fan_pwm_pin;
                         int v_oldFreq = g_A10_config.pwm_frequency;
                         int v_oldRes = g_A10_config.pwm_resolution;
                         int v_oldCh = g_A10_config.pwm_channel;

                         bool v_wifiChanged = false;
                         CL_C10_ConfigManager::patchFromJson(g_A10_config, v_doc, v_wifiChanged);
                         CL_C10_ConfigManager::save(g_A10_config);

                         // 프리셋 변경 시 재적용
                         if (g_A10_config.preset_mode_index != v_oldPreset) {
                             p_sim.applyCurrentPreset(true);
                         }

                         // PWM 변경 반영
                         if (g_A10_config.fan_pwm_pin != v_oldPin)
                             p_P10_pwm.set_pwmPin(g_A10_config.fan_pwm_pin);
                         if (g_A10_config.pwm_channel != v_oldCh)
                             p_P10_pwm.set_pwmChannel(g_A10_config.pwm_channel);
                         if (g_A10_config.pwm_frequency != v_oldFreq)
                             p_P10_pwm.set_pwmFrequency(g_A10_config.pwm_frequency);
                         if (g_A10_config.pwm_resolution != v_oldRes)
                             p_P10_pwm.set_pwmResolution(g_A10_config.pwm_resolution);

                         p_P10_pwm.set_pwmDuty(0.0f);

                         if (v_wifiChanged) {
                             CL_D10_Logger::log(EN_L10_LOG_INFO, "Wi-Fi config changed. Re-init WiFi");
                             CL_M10_WiFiManager::init(g_A10_config, p_multi);
                         }

                         p_request->send(200, "application/json", "{\"message\":\"Config updated\"}");
                     }
                 });

        // ---------------------------
        // /api/config/init : 기본 설정 생성
        // ---------------------------
        p_srv.on("/api/config/init", HTTP_POST, [](AsyncWebServerRequest *p_request) {
            if (!_authorize(p_request)) {
                p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
                return;
            }
            bool v_ok = CL_C10_ConfigManager::saveDefaultConfig();
            if (v_ok)
                p_request->send(200, "application/json", "{\"message\":\"Default config created\"}");
            else
                p_request->send(500, "application/json", "{\"error\":\"Init failed or file exists\"}");
        });

        // ---------------------------
        // /api/reset : 공장 초기화
        // ---------------------------
        p_srv.on("/api/reset", HTTP_POST, [](AsyncWebServerRequest *p_request) {
            if (!_authorize(p_request)) {
                p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
                return;
            }
            CL_C10_ConfigManager::reset();
            p_request->send(200, "text/plain", "Factory reset... Reboot");
            ESP.restart();
        });

        // ---------------------------
        // /api/scan : Wi-Fi 스캔
        // ---------------------------
        p_srv.on("/api/scan", HTTP_GET, [](AsyncWebServerRequest *p_request) {
            bool v_async = p_request->hasParam("async");
            String v_json = CL_M10_WiFiManager::scanNetworksJson(v_async);
            auto *v_response = p_request->beginResponse(200, "application/json", v_json);
            _applyHeaders(v_response, true);
            p_request->send(v_response);
        });

        // ---------------------------
        // /api/diag : 진단 정보
        // ---------------------------
        p_srv.on("/api/diag", HTTP_GET, [](AsyncWebServerRequest *p_request) {
            JsonDocument v_doc;
            v_doc["heap"] = ESP.getFreeHeap();
            v_doc["rssi"] = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0;
            v_doc["fs_total"] = LittleFS.totalBytes();
            v_doc["fs_used"] = LittleFS.usedBytes();

            String v_json;
            serializeJson(v_doc, v_json);
            auto *v_response = p_request->beginResponse(200, "application/json", v_json);
            _applyHeaders(v_response, true);
            p_request->send(v_response);
        });

        // ---------------------------
        // /api/logs : 로그 조회
        // ---------------------------
        p_srv.on("/api/logs", HTTP_GET, [](AsyncWebServerRequest *p_request) {
            String v_json = CL_D10_Logger::getLogsJson();
            auto *v_response = p_request->beginResponse(200, "application/json", v_json);
            _applyHeaders(v_response, true);
            p_request->send(v_response);
        });

        // ---------------------------
        // /api/version : 버전 정보
        // ---------------------------
        p_srv.on("/api/version", HTTP_GET, [](AsyncWebServerRequest *p_request) {
            JsonDocument v_doc;
            v_doc["fw_version"] = A10_Const::FW_VERSION;
            v_doc["config_file"] = A10_Const::CONFIG_JSON_FILE;
            String v_json;
            serializeJson(v_doc, v_json);
            auto *v_response = p_request->beginResponse(200, "application/json", v_json);
            _applyHeaders(v_response, true);
            p_request->send(v_response);
        });
    }

   private:
    // ======================================================
    // 응답 헤더/보안 유틸
    // ======================================================
    static void _applyHeaders(AsyncWebServerResponse *p_response, bool noCache = false) {
        if (noCache) _addNoCache(p_response);
        _addCors(p_response);
    }
    static void _addNoCache(AsyncWebServerResponse *p_response) {
        p_response->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
        p_response->addHeader("Pragma", "no-cache");
        p_response->addHeader("Expires", "0");
    }
    static void _addCors(AsyncWebServerResponse *p_response) {
        p_response->addHeader("Access-Control-Allow-Origin", "*");
        p_response->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
        p_response->addHeader("Access-Control-Allow-Headers", "Content-Type, X-API-Key");
    }
    static bool _authorize(AsyncWebServerRequest *p_request) {
        if (strlen(g_A10_config.api_key) == 0) return true;
        if (!p_request->hasHeader("X-API-Key")) return false;
        auto *v_h = p_request->getHeader("X-API-Key");
        return (v_h && v_h->value() == String(g_A10_config.api_key));
    }
};
