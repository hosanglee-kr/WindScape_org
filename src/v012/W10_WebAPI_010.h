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
// 기본 정적 자산 경로 상수 (코어 설정(system.web.*)과 함께 사용)
// ------------------------------------------------------
namespace W10_Const {
    // 메인 페이지
    constexpr char MAIN_PAGE_HTML_FILE[] = "/html/SC10_main_021.html";
    constexpr char MAIN_PAGE_HTML_URI[]  = "/SC10_main_021.html";
    constexpr char MAIN_PAGE_HTML_MIME[] = "text/html";

    constexpr char MAIN_PAGE_CSS_FILE[]  = "/html/SC10_main_021.css";
    constexpr char MAIN_PAGE_CSS_URI[]   = "/SC10_main_021.css";
    constexpr char MAIN_PAGE_CSS_MIME[]  = "text/css";

    constexpr char MAIN_PAGE_JS_FILE[]   = "/html/SC10_main_021.js";
    constexpr char MAIN_PAGE_JS_URI[]    = "/SC10_main_021.js";
    constexpr char MAIN_PAGE_JS_MIME[]   = "application/javascript";

    // 차트 페이지
    constexpr char CHART_PAGE_HTML_FILE[] = "/html/SC10_chart_003.html";
    constexpr char CHART_PAGE_HTML_URI[]  = "/SC10_chart_003.html";
    constexpr char CHART_PAGE_HTML_MIME[] = "text/html";

    constexpr char CHART_PAGE_CSS_FILE[]  = "/html/SC10_chart_003.css";
    constexpr char CHART_PAGE_CSS_URI[]   = "/SC10_chart_003.css";
    constexpr char CHART_PAGE_CSS_MIME[]  = "text/css";

    constexpr char CHART_PAGE_JS_FILE[]   = "/html/SC10_chart_003.js";
    constexpr char CHART_PAGE_JS_URI[]    = "/SC10_chart_003.js";
    constexpr char CHART_PAGE_JS_MIME[]   = "application/javascript";

    // /chart → 차트 HTML
    constexpr char CHART_ALIAS_URI[]      = "/chart";
    constexpr char CHART_ALIAS_FILE[]     = "/SC10_chart_003.html";
    constexpr char CHART_ALIAS_MIME[]     = "text/html";
}

// ------------------------------------------------------
// Web API 관리자
// ------------------------------------------------------
class CL_W10_WebAPI {
public:
    // ======================================================
    // 초기화 (라우팅 등록)
    // ======================================================
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
    // ------------------------------------------------------
    // 정적 파일 서빙
    //  - 우선순위: 코어 설정(system.web.*) 경로 → 기본 상수
    // ------------------------------------------------------
    static void _W10_mountStatic(AsyncWebServer &p_srv) {
        // 루트 → 메인 HTML로 리다이렉트
        p_srv.on("/", HTTP_GET, [](AsyncWebServerRequest *p_req){
            const char *v_uri = W10_Const::MAIN_PAGE_HTML_URI;
            if (strlen(g_A10_config_root.core.system.web.html) > 0) {
                v_uri = g_A10_config_root.core.system.web.html; // 예: "/html/SC10_main_021.html" (파일 경로)
                // 브라우저 접근용 URI가 필요한 경우 기본 URI로 리디렉트
                // 파일 경로를 바로 열 수 없는 환경이면 기본 URI를 사용
                v_uri = W10_Const::MAIN_PAGE_HTML_URI;
            }
            p_req->redirect(String(v_uri));
        });

        // 라우트 테이블
        struct ST_W10_StaticRoute { const char* uri; const char* file; const char* mime; };

        ST_W10_StaticRoute v_routes[] = {
            // Main
            { W10_Const::MAIN_PAGE_HTML_URI, W10_Const::MAIN_PAGE_HTML_FILE, W10_Const::MAIN_PAGE_HTML_MIME },
            { W10_Const::MAIN_PAGE_CSS_URI , W10_Const::MAIN_PAGE_CSS_FILE , W10_Const::MAIN_PAGE_CSS_MIME  },
            { W10_Const::MAIN_PAGE_JS_URI  , W10_Const::MAIN_PAGE_JS_FILE  , W10_Const::MAIN_PAGE_JS_MIME   },

            // Chart
            { W10_Const::CHART_PAGE_HTML_URI, W10_Const::CHART_PAGE_HTML_FILE, W10_Const::CHART_PAGE_HTML_MIME },
            { W10_Const::CHART_PAGE_CSS_URI , W10_Const::CHART_PAGE_CSS_FILE , W10_Const::CHART_PAGE_CSS_MIME  },
            { W10_Const::CHART_PAGE_JS_URI  , W10_Const::CHART_PAGE_JS_FILE  , W10_Const::CHART_PAGE_JS_MIME   },

            // /chart 별칭
            { W10_Const::CHART_ALIAS_URI    , W10_Const::CHART_ALIAS_FILE    , W10_Const::CHART_ALIAS_MIME     },
        };

        for (auto &v : v_routes) {
            p_srv.on(v.uri, HTTP_GET, [=](AsyncWebServerRequest *p_request) {
                if (LittleFS.exists(v.file)) {
                    p_request->send(LittleFS, v.file, v.mime);
                } else {
                    String v_msg = String("/* missing file: ") + v.file + " */";
                    auto *v_resp = p_request->beginResponse(200, v.mime, v_msg);
                    _W10_applyHeaders(v_resp, true);
                    p_request->send(v_resp);
                }
            });
        }

        // OPTIONS (CORS preflight)
        p_srv.onNotFound([](AsyncWebServerRequest *p_request){
            if (p_request->method()==HTTP_OPTIONS) {
                auto *v_resp = p_request->beginResponse(204);
                _W10_addCors(v_resp);
                p_request->send(v_resp);
                return;
            }
            p_request->send(404, "text/plain", "Not found");
        });
    }

    // ------------------------------------------------------
    // API 라우트 등록
    // ------------------------------------------------------
    static void _W10_mountApi(AsyncWebServer &p_srv) {
        // ---------- /api/web : 현재 코어 Web 경로 반환 ----------
        p_srv.on("/api/web", HTTP_GET, [](AsyncWebServerRequest *p_req){
            JsonDocument v_doc;
            JsonObject v_root = v_doc.to<JsonObject>();
            v_root["system"]["web"]["html"] = g_A10_config_root.core.system.web.html;
            v_root["system"]["web"]["css"]  = g_A10_config_root.core.system.web.css;
            v_root["system"]["web"]["js"]   = g_A10_config_root.core.system.web.js;

            String v_out; serializeJson(v_doc, v_out);
            auto *v_resp = p_req->beginResponse(200, "application/json", v_out);
            _W10_applyHeaders(v_resp, true);
            p_req->send(v_resp);
        });

        // ---------- /api/state : 현재 상태 + 설정 요약 ----------
        p_srv.on("/api/state", HTTP_GET, [](AsyncWebServerRequest *p_req){
            JsonDocument v_doc;

            // sim 상태(JSON) - S10에서 직렬화
            if (s_pSim) s_pSim->S10_toJson(v_doc);

            // 상태: Wi-Fi / IP / SSID
            JsonObject v_status = v_doc["status"].to<JsonObject>();
            if (WiFi.status() == WL_CONNECTED) {
                v_status["wifi"]["mode"] = "STA";
                v_status["wifi"]["ip"]   = WiFi.localIP().toString();
                v_status["wifi"]["ssid"] = WiFi.SSID();
                v_status["wifi"]["rssi"] = WiFi.RSSI();
            } else {
                v_status["wifi"]["mode"] = "AP/IDLE";
                v_status["wifi"]["ip"]   = WiFi.softAPIP().toString();
                v_status["wifi"]["ssid"] = g_A10_config_root.core.meta.device_name;
            }

            // 코어 설정 일부 포함
            JsonObject v_core = v_doc["core"].to<JsonObject>();
            v_core["meta"]["version"]      = g_A10_config_root.core.meta.version;
            v_core["meta"]["device_name"]  = g_A10_config_root.core.meta.device_name;
            v_core["system"]["logging"]["level"]       = g_A10_config_root.core.system.logging.level;
            v_core["system"]["logging"]["max_entries"] = g_A10_config_root.core.system.logging.max_entries;

            // pwm 상태
            if (s_pPwm) {
                // 008/010 혼용 지원: getDutyPercent() or P10_getDutyPercent() 둘 다 시도
                float v_pwmPercent = 0.0f;
                #if defined(__cpp_generic_lambdas)
                #endif
                // 가장 보편적인 이름 (008 호환)
                v_pwmPercent = s_pPwm->getDutyPercent();
                v_status["pwm"]["percent"] = v_pwmPercent;
                // raw는 008 호환 (없으면 0)
                v_status["pwm"]["raw"] = s_pPwm->getDutyRaw();
            } else {
                v_status["pwm"]["percent"] = 0.0f;
                v_status["pwm"]["raw"]     = 0;
            }

            // 프리셋 이름 배열
            JsonArray v_presets = v_doc["presets"].to<JsonArray>();
            for (uint8_t v_i=0; v_i<EN_A10_PRESET_COUNT; ++v_i) v_presets.add(g_A10_PRESET_MODE_NAMES_Arr[v_i]);

            // 응답
            String v_out; serializeJson(v_doc, v_out);
            auto *v_resp = p_req->beginResponse(200, "application/json", v_out);
            _W10_applyHeaders(v_resp, true);
            p_req->send(v_resp);
        });

        // ---------- /api/chart, /api/chart_data : 차트 데이터 ----------
        auto chartHandler = [](AsyncWebServerRequest *p_req){
            JsonDocument v_doc;
            if (s_pSim) s_pSim->S10_toChartJson(v_doc);
            String v_out; serializeJson(v_doc, v_out);
            auto *v_resp = p_req->beginResponse(200, "application/json", v_out);
            _W10_applyHeaders(v_resp, true);
            p_req->send(v_resp);
        };
        p_srv.on("/api/chart",       HTTP_GET, chartHandler);
        p_srv.on("/api/chart_data",  HTTP_GET, chartHandler); // 호환 경로

        // ---------- /api/config [GET] : 전체 설정 조회 ----------
        p_srv.on("/api/config", HTTP_GET, [](AsyncWebServerRequest *p_req){
            JsonDocument v_doc;
            JsonObject v_root = v_doc.to<JsonObject>();

            // Core 직렬화
            {
                JsonDocument v_c;
                CL_C10_ConfigManager::toCoreJson(g_A10_config_root.core, v_c);
                v_root["core"] = v_c.as<JsonObject>();
            }

            // WiFi/Sim/Schedule/Motion (로딩되어 있지 않다면 임시 로딩)
            _W10_toJsonWifi(v_root["wifi"].to<JsonObject>());
            _W10_toJsonSim(v_root["sim"].to<JsonObject>());
            _W10_toJsonSchedule(v_root["schedule"].to<JsonObject>());
            _W10_toJsonMotion(v_root["motion"].to<JsonObject>());

            String v_out; serializeJson(v_doc, v_out);
            auto *v_resp = p_req->beginResponse(200, "application/json", v_out);
            _W10_applyHeaders(v_resp, true);
            p_req->send(v_resp);
        });

        // ---------- /api/config [POST] : 부분 패치 / 저장 ----------
        p_srv.on("/api/config", HTTP_POST,
            [](AsyncWebServerRequest *p_request){}, nullptr,
            [](AsyncWebServerRequest *p_request, uint8_t *data, size_t len, size_t index, size_t total){
                if (!_W10_authorize(p_request)) {
                    p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
                    return;
                }
                if (index==0 && len==total) {
                    JsonDocument v_doc;
                    DeserializationError v_err = deserializeJson(v_doc, (const char*)data, len);
                    if (v_err) {
                        p_request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
                        return;
                    }
                    bool v_needWifiReinit = false;
                    bool v_changed = CL_C10_ConfigManager::patchFromJson(g_A10_config_root, v_doc, v_needWifiReinit);
                    if (v_changed) {
                        CL_C10_ConfigManager::saveAll(g_A10_config_root);
                        if (v_needWifiReinit) {
                            CL_D10_Logger::log(EN_L10_LOG_INFO, "Wi-Fi config changed, re-init");
                            CL_M10_WiFiManager::init(g_A10_config_root, *s_pWiMulti);
                        }
                        // 프리셋이 바뀐 경우 즉시 적용 (sim.preset)
                        if (s_pSim && g_A10_config_root.sim) {
                            s_pSim->S10_applyPreset(g_A10_config_root.sim->preset);
                        }
                        p_request->send(200, "application/json", "{\"message\":\"Config updated\"}");
                    } else {
                        p_request->send(200, "application/json", "{\"message\":\"No changes\"}");
                    }
                }
            }
        );

        // ---------- /api/config/init : 공장 초기화 & 저장 ----------
        p_srv.on("/api/config/init", HTTP_POST, [](AsyncWebServerRequest *p_req){
            if (!_W10_authorize(p_req)) {
                p_req->send(401, "application/json", "{\"error\":\"unauthorized\"}");
                return;
            }
            A10_resetToDefault(g_A10_config_root);
            CL_C10_ConfigManager::saveAll(g_A10_config_root);
            p_req->send(200, "application/json", "{\"message\":\"Factory defaults written\"}");
        });

        // ---------- /api/reset : 공장 초기화 후 재부팅 ----------
        p_srv.on("/api/reset", HTTP_POST, [](AsyncWebServerRequest *p_req){
            if (!_W10_authorize(p_req)) {
                p_req->send(401, "application/json", "{\"error\":\"unauthorized\"}");
                return;
            }
            CL_C10_ConfigManager::resetAll(g_A10_config_root);
            p_req->send(200, "text/plain", "Factory reset... Rebooting");
            delay(200);
            ESP.restart();
        });

        // ---------- /api/scan : Wi-Fi 스캔 ----------
        p_srv.on("/api/scan", HTTP_GET, [](AsyncWebServerRequest *p_req){
            bool v_async = p_req->hasParam("async");
            String v_json = CL_M10_WiFiManager::scanNetworksJson(v_async);
            auto *v_resp = p_req->beginResponse(200, "application/json", v_json);
            _W10_applyHeaders(v_resp, true);
            p_req->send(v_resp);
        });

        // ---------- /api/diag : 간단 진단 ----------
        p_srv.on("/api/diag", HTTP_GET, [](AsyncWebServerRequest *p_req){
            JsonDocument v_doc;
            v_doc["heap"]     = ESP.getFreeHeap();
            v_doc["rssi"]     = (WiFi.status()==WL_CONNECTED)? WiFi.RSSI() : 0;
            v_doc["fs_total"] = LittleFS.totalBytes();
            v_doc["fs_used"]  = LittleFS.usedBytes();
            String v_out; serializeJson(v_doc, v_out);
            auto *v_resp = p_req->beginResponse(200, "application/json", v_out);
            _W10_applyHeaders(v_resp, true);
            p_req->send(v_resp);
        });

        // ---------- /api/logs : 최근 로그 ----------
        p_srv.on("/api/logs", HTTP_GET, [](AsyncWebServerRequest *p_req){
            String v_json = CL_D10_Logger::getLogsJson();
            auto *v_resp = p_req->beginResponse(200, "application/json", v_json);
            _W10_applyHeaders(v_resp, true);
            p_req->send(v_resp);
        });

        // ---------- /api/version ----------
        p_srv.on("/api/version", HTTP_GET, [](AsyncWebServerRequest *p_req){
            JsonDocument v_doc;
            v_doc["fw_version"]   = A10_Const::FW_VERSION;
            v_doc["cfg_json_ver"] = G_A10_CFG_JSON_FILE_VER;
            String v_out; serializeJson(v_doc, v_out);
            auto *v_resp = p_req->beginResponse(200, "application/json", v_out);
            _W10_applyHeaders(v_resp, true);
            p_req->send(v_resp);
        });

        // ---------- 시뮬 제어: /api/sim/start ----------
        p_srv.on("/api/sim/start", HTTP_POST, [](AsyncWebServerRequest *p_req){
            if (!_W10_authorize(p_req)) { p_req->send(401, "application/json", "{\"error\":\"unauthorized\"}"); return; }
            if (s_pSim && s_pPwm) {
                s_pSim->S10_begin(*s_pPwm);
                p_req->send(200, "application/json", "{\"message\":\"Simulation started\"}");
            } else {
                p_req->send(500, "application/json", "{\"error\":\"Simulation/PWM not ready\"}");
            }
        });

        // ---------- 시뮬 제어: /api/sim/stop ----------
        p_srv.on("/api/sim/stop", HTTP_POST, [](AsyncWebServerRequest *p_req){
            if (!_W10_authorize(p_req)) { p_req->send(401, "application/json", "{\"error\":\"unauthorized\"}"); return; }
            if (s_pSim) {
                s_pSim->S10_stop();
                p_req->send(200, "application/json", "{\"message\":\"Simulation stopped\"}");
            } else {
                p_req->send(500, "application/json", "{\"error\":\"Simulation not ready\"}");
            }
        });

        // ---------- 시뮬 제어: /api/sim/preset {"preset":"OCEAN"} ----------
        p_srv.on("/api/sim/preset", HTTP_POST,
            [](AsyncWebServerRequest *p_req){}, nullptr,
            [](AsyncWebServerRequest *p_req, uint8_t *data, size_t len, size_t index, size_t total){
                if (!_W10_authorize(p_req)) { p_req->send(401, "application/json", "{\"error\":\"unauthorized\"}"); return; }
                if (!s_pSim) { p_req->send(500, "application/json", "{\"error\":\"Simulation not ready\"}"); return; }
                if (index==0 && len==total) {
                    JsonDocument v_doc;
                    if (deserializeJson(v_doc, (const char*)data, len)) {
                        p_req->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
                        return;
                    }
                    const char* v_name = v_doc["preset"] | "OCEAN";
                    s_pSim->S10_applyPreset(v_name);
                    p_req->send(200, "application/json", "{\"message\":\"Preset applied\"}");
                    return;
                }
                p_req->send(400, "application/json", "{\"error\":\"Invalid request\"}");
            }
        );

        // ---------- /upload : 정적파일 업로드 (보안) ----------
        static bool s_uploadError = false;
        p_srv.on("/upload", HTTP_POST,
            [](AsyncWebServerRequest *p_req) {
                if (!_W10_authorize(p_req)) {
                    p_req->send(401, "application/json", "{\"error\":\"unauthorized\"}");
                    return;
                }
                if (s_uploadError) {
                    s_uploadError = false;
                    p_req->send(500, "application/json", "{\"error\":\"upload failed\"}");
                } else {
                    p_req->send(200, "application/json", "{\"message\":\"Upload OK\"}");
                }
            },
            [](AsyncWebServerRequest *p_req, const String &filename, size_t index, uint8_t *data, size_t len, bool final) {
                if (!_W10_authorize(p_req)) { s_uploadError = true; return; }

                static const size_t v_kMaxUpload = 4 * 1024 * 1024;

                if (index == 0) {
                    s_uploadError = false;

                    String v_safe = _W10_sanitizeFilename(filename);
                    if (!_W10_isAllowedExt(v_safe)) { s_uploadError = true; return; }

                    String v_path;
                    String v_lower = v_safe; v_lower.toLowerCase();
                    if (v_lower.endsWith(".json")) v_path = "/json/" + v_safe;
                    else                           v_path = "/html/" + v_safe;

                    if (LittleFS.exists(v_path)) LittleFS.remove(v_path);

                    p_req->_tempFile = LittleFS.open(v_path, "w");
                    if (!p_req->_tempFile) { s_uploadError = true; return; }
                }

                if (s_uploadError) return;
                if (p_req->_tempFile) {
                    if (p_req->_tempFile.size() + len > v_kMaxUpload) {
                        p_req->_tempFile.close();
                        LittleFS.remove(p_req->_tempFile.name());
                        s_uploadError = true;
                        return;
                    }
                    if (len) p_req->_tempFile.write(data, len);
                    if (final) p_req->_tempFile.close();
                }
            }
        );

        // ---------- /update : OTA ----------
        p_srv.on("/update", HTTP_POST,
            [](AsyncWebServerRequest *p_req) {
                if (!_W10_authorize(p_req)) {
                    p_req->send(401, "application/json", "{\"error\":\"unauthorized\"}");
                    return;
                }
            },
            [](AsyncWebServerRequest *p_req, const String &filename, size_t index, uint8_t *data, size_t len, bool final){
                if (!_W10_authorize(p_req)) return;

                if (!index) {
                    size_t v_maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
                    if (!Update.begin(v_maxSketchSpace)) {
                        Update.printError(Serial);
                        p_req->send(500, "text/plain", "OTA begin failed");
                        return;
                    }
                }
                if (len) {
                    if (Update.write(data, len) != len) {
                        Update.printError(Serial);
                        p_req->send(500, "text/plain", "OTA write failed");
                        return;
                    }
                }
                if (final) {
                    if (!Update.end(true)) {
                        String v_msg = "OTA end failed: ";
                        v_msg += Update.errorString();
                        p_req->send(500, "text/plain", v_msg);
                        return;
                    }
                    p_req->send(200, "text/plain", "OTA OK, rebooting");
                    ESP.restart();
                }
            }
        );
    }

    // ======================================================
    // 내부 유틸: 공통 헤더/CORS/인증
    // ======================================================
    static void _W10_applyHeaders(AsyncWebServerResponse *p_resp, bool p_noCache=false) {
        if (p_noCache) _W10_addNoCache(p_resp);
        _W10_addCors(p_resp);
    }
    static void _W10_addNoCache(AsyncWebServerResponse *p_resp) {
        p_resp->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
        p_resp->addHeader("Pragma", "no-cache");
        p_resp->addHeader("Expires", "0");
    }
    static void _W10_addCors(AsyncWebServerResponse *p_resp) {
        p_resp->addHeader("Access-Control-Allow-Origin", "*");
        p_resp->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
        p_resp->addHeader("Access-Control-Allow-Headers", "Content-Type, X-API-Key");
    }
    static bool _W10_authorize(AsyncWebServerRequest *p_req) {
        // API Key는 core.security.api_key에 저장
        if (strlen(g_A10_config_root.core.security.api_key)==0) return true;
        if (!p_req->hasHeader("X-API-Key")) return false;
        auto *v_h = p_req->getHeader("X-API-Key");
        return (v_h && v_h->value()==String(g_A10_config_root.core.security.api_key));
    }

    // 파일명 정규화
    static String _W10_sanitizeFilename(const String &p_in) {
        String v_out;
        for (size_t i = 0; i < p_in.length(); i++) {
            char v_ch = p_in[i];
            if (v_ch == '/' || v_ch == '\\') continue;
            if (v_ch == ':' || v_ch == '*' || v_ch == '?' || v_ch == '"' || v_ch == '<' || v_ch == '>' || v_ch == '|') continue;
            v_out += v_ch;
        }
        v_out.trim();
        return v_out;
    }
    static bool _W10_isAllowedExt(const String &p_name) {
        String v = p_name; v.toLowerCase();
        return v.endsWith(".html") || v.endsWith(".htm") || v.endsWith(".js") ||
               v.endsWith(".css")  || v.endsWith(".json")|| v.endsWith(".txt")||
               v.endsWith(".gif")  || v.endsWith(".png") || v.endsWith(".jpg")||
               v.endsWith(".jpeg") || v.endsWith(".svg") || v.endsWith(".ico")||
               v.endsWith(".gz");
    }

    // ======================================================
    // 내부 유틸: 설정 → JSON (W10에서 보조 직렬화)
    // ======================================================
    static void _W10_toJsonWifi(JsonObject v_obj) {
        // 로드되어 있지 않다면 임시 로딩
        ST_A10_WifiConfig v_tmp;
        ST_A10_WifiConfig *v_wifi = g_A10_config_root.wifi;
        if (!v_wifi) {
            if (CL_C10_ConfigManager::loadWifi(v_tmp)) v_wifi = &v_tmp;
        }
        if (!v_wifi) return;

        v_obj["mode"] = v_wifi->mode;
        JsonObject ap = v_obj["ap"].to<JsonObject>();
        ap["ssid"] = v_wifi->ap.ssid;
        // password는 보안상 마스킹(필요 시 정책에 따라 노출)
        if (strlen(v_wifi->ap.password)>0) ap["password"] = "***";

        JsonArray sta = v_obj["sta"].to<JsonArray>();
        for (uint8_t i=0;i<v_wifi->sta_count;i++){
            JsonObject n = sta.add<JsonObject>();
            n["ssid"] = v_wifi->sta[i].ssid;
            if (strlen(v_wifi->sta[i].pass)>0) n["pass"] = "***";
        }
    }

    static void _W10_toJsonSim(JsonObject v_obj) {
        ST_A10_SimConfig v_tmp;
        ST_A10_SimConfig *v_sim = g_A10_config_root.sim;
        if (!v_sim) {
            if (CL_C10_ConfigManager::loadSim(v_tmp)) v_sim = &v_tmp;
        }
        if (!v_sim) return;

        v_obj["preset"]           = v_sim->preset;
        v_obj["wind_intensity"]   = v_sim->wind_intensity;
        v_obj["gust_frequency"]   = v_sim->gust_frequency;
        v_obj["wind_variability"] = v_sim->wind_variability;
        v_obj["fan_limit"]        = v_sim->fan_limit;
        v_obj["min_fan"]          = v_sim->min_fan;

        v_obj["turbulence"]["length_scale"]    = v_sim->turbulence.length_scale;
        v_obj["turbulence"]["intensity_sigma"] = v_sim->turbulence.intensity_sigma;
        v_obj["thermal"]["bubble_strength"]    = v_sim->thermal.bubble_strength;
        v_obj["thermal"]["bubble_radius"]      = v_sim->thermal.bubble_radius;
    }

    static void _W10_toJsonSchedule(JsonObject v_obj) {
        ST_A10_ScheduleConfig v_tmp;
        ST_A10_ScheduleConfig *v_sc = g_A10_config_root.schedule;
        if (!v_sc) {
            if (CL_C10_ConfigManager::loadSchedule(v_tmp)) v_sc = &v_tmp;
        }
        if (!v_sc) return;

        JsonArray arr = v_obj["items"].to<JsonArray>();
        for (uint8_t i=0;i<v_sc->count;i++){
            const auto &s = v_sc->items[i];
            JsonObject o = arr.add<JsonObject>();
            o["no"]      = s.no;
            o["name"]    = s.name;
            o["enabled"] = s.enabled;

            JsonArray d = o["days"].to<JsonArray>();
            for (uint8_t k=0;k<7;k++) d.add(s.days[k]);

            o["start_time"] = s.start_time;
            o["end_time"]   = s.end_time;

            JsonArray seg = o["segments"].to<JsonArray>();
            for (uint8_t j=0;j<s.seg_count;j++){
                const auto &g = s.seg[j];
                JsonObject gg = seg.add<JsonObject>();
                gg["no"]           = g.no;
                gg["on_minutes"]   = g.on_minutes;
                gg["off_minutes"]  = g.off_minutes;
                gg["mode"]         = g.mode;
                gg["preset_name"]  = g.preset_name;
                gg["fixed_speed"]  = g.fixed_speed;
                gg["preset_adjust"]["intensity"]   = g.adj_intensity;
                gg["preset_adjust"]["variability"] = g.adj_variability;
            }
        }
        v_obj["count"] = v_sc->count;
    }

    static void _W10_toJsonMotion(JsonObject v_obj) {
        ST_A10_MotionConfig v_tmp;
        ST_A10_MotionConfig *v_mo = g_A10_config_root.motion;
        if (!v_mo) {
            if (CL_C10_ConfigManager::loadMotion(v_tmp)) v_mo = &v_tmp;
        }
        if (!v_mo) return;

        v_obj["enabled"]           = v_mo->enabled;
        v_obj["pir"]["enabled"]    = v_mo->pir.enabled;
        v_obj["pir"]["hold_sec"]   = v_mo->pir.hold_sec;
        v_obj["ble"]["enabled"]    = v_mo->ble.enabled;
        v_obj["ble"]["rssi_threshold"] = v_mo->ble.rssi_threshold;
        v_obj["ble"]["hold_sec"]   = v_mo->ble.hold_sec;

        JsonArray dev = v_obj["ble"]["devices"].to<JsonArray>();
        for (uint8_t i=0;i<v_mo->ble.device_count;i++){
            const auto &d = v_mo->ble.devices[i];
            JsonObject o = dev.add<JsonObject>();
            o["mac"]     = d.mac;
            o["alias"]   = d.alias;
            o["enabled"] = d.enabled;
        }
    }

private:
    // 정적 멤버(참조 보관)
    static inline CL_S10_Simulation *s_pSim     = nullptr;
    static inline WiFiMulti          *s_pWiMulti= nullptr;
    static inline CL_P10_PWM         *s_pPwm    = nullptr;
};
