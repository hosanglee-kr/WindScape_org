

#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : W10_WebAPI_008.h
 * 모듈명 : Smart Nature Wind Web API Manager
 * ------------------------------------------------------
 * 기능 요약:
 *  - LittleFS 정적 자산 서빙(설정 기반 동적/정적 전환 가능)
 *  - /api/state, /api/config, /api/reset, /api/version 등
 *  - Wi-Fi 스캔/진단/로그
 *  - OTA/업로드 확장 가능
 *  - API Key 인증 / CORS / No-Cache 헤더 적용
 * ------------------------------------------------------
 * [코드 네이밍 규칙]
 * 		- 현재 파일 모듈약어    : W10
 * 		- 전역 상수,매크로      : G_모듈약어_ 접두사
 * 		- 전역 변수             : g_모듈약어_ 접두사
 * 		- 전역 함수             : 모듈약어_ 접두사
 * 		- type                  : T_모듈약어_ 접두사
 * 		- enum 상수             : EN_모듈약어_ 접두사
 * 		- 구조체                : ST_모듈약어_ 접두사
 * 		- 클래스명              : CL_모듈약어_ 접두사
 * 		- 클래스 private 멤버   : _ 접두사,
 * 		- 클래스 정적 멤버      : s_ 접두사
 * 		- 로컬 변수             : v_ 접두사
 * 		- 함수 인자             : p_ 접두사
 */

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <Update.h>

#include "A10_Const_008.h"
#include "C10_ConfigManager_008.h"
#include "D10_Logger_008.h"
#include "M10_WiFiManager_008.h"
#include "S10_Simulation_008.h"
#include "P10_PWM_ctrl_008.h"


namespace W10_Const {
    constexpr char MAIN_PAGE_HTML_FILE[]              = "/html/SC10_main_017.html";
    constexpr char MAIN_PAGE_HTML_URI[]               = "/main.html";
    constexpr char MAIN_PAGE_HTML_MIME[]              = "text/html";

    constexpr char MAIN_PAGE_CSS_FILE[]               = "/html/SC10_main_017.css";
    constexpr char MAIN_PAGE_CSS_URI[]                = "/SC10_main_017.css";
    constexpr char MAIN_PAGE_CSS_MIME[]               = "text/css";

    constexpr char MAIN_PAGE_JS_FILE[]                = "/html/SC10_main_017.js";
    constexpr char MAIN_PAGE_JS_URI[]                 = "/SC10_main_017.js";
    constexpr char MAIN_PAGE_JS_MIME[]                = "application/javascript";


    constexpr char CHART_PAGE_HTML_FILE[]            = "/html/SC10_chart_003.html";
    constexpr char CHART_PAGE_HTML_URI[]             = "/SC10_chart_003.html";
    constexpr char CHART_PAGE_HTML_MIME[]            = "text/html";

    constexpr char CHART_PAGE_CSS_FILE[]             = "/html/SC10_chart_003.css";
    constexpr char CHART_PAGE_CSS_URI[]              = "/SC10_chart_003.css";
    constexpr char CHART_PAGE_CSS_MIME[]             = "text/css";

    constexpr char CHART_PAGE_JS_FILE[]              = "/html/SC10_chart_003.js";
    constexpr char CHART_PAGE_JS_URI[]               = "/SC10_chart_003.js";
    constexpr char CHART_PAGE_JS_MIME[]              = "application/javascript";

    constexpr char CHART_PAGE_DEFAULTURI_FILE[]     = "/SC10_chart_003.html";
    constexpr char CHART_PAGE_DEFAULTURI_URI[]      = "/chart";
    constexpr char CHART_PAGE_DEFAULTURI_MIME[]     = "text/html";

}


class CL_W10_WebAPI {
public:
    static void init(AsyncWebServer &p_srv, CL_S10_Simulation &p_sim, WiFiMulti &p_multi, CL_P10_PWM &p_P10_pwm) 
{
        mountApi(p_srv, p_sim, p_multi, p_P10_pwm);
        mountStatic(p_srv);
    }

    // ------------------------------------------------------
    // 정적 파일(설정 기반 or 고정값) 서빙
    // ------------------------------------------------------
    static void mountStatic(AsyncWebServer &p_srv) {
        // 루트 리다이렉트
        p_srv.on("/", HTTP_GET, [](AsyncWebServerRequest *p_req){
            #ifdef G_A10_DYNIM_WEB_STATIC_FILE_USE
                if (strlen(g_A10_config.web.html_file.uri) > 0)
                    p_req->redirect(String(g_A10_config.web.html_file.uri));
                else
                    p_req->redirect(String(A10_Const::MAIN_PAGE_HTML_URI));
            #else
                p_req->redirect(String(A10_Const::MAIN_PAGE_HTML_URI));
            #endif
        });

        // --------------------------
        // 정적 자산 라우트 등록
        // --------------------------
        struct ST_W10_WebStatic_Route { 
            const char* uri; 
            const char* file; 
            const char* mime; 
        };

        #ifdef G_A10_DYNIM_WEB_STATIC_FILE_USE
            ST_W10_WebStatic_Route v_webStatic_Routes_arr[3] = {
                { g_A10_config.web.html_file.uri[0]? g_A10_config.web.html_file.uri : A10_Const::MAIN_PAGE_HTML_URI,
                g_A10_config.web.html_file.file[0]? g_A10_config.web.html_file.file : A10_Const::MAIN_PAGE_HTML_FILE,
                g_A10_config.web.html_file.mime[0]? g_A10_config.web.html_file.mime : A10_Const::MAIN_PAGE_HTML_MIME },

                { g_A10_config.web.css_file.uri[0]? g_A10_config.web.css_file.uri : A10_Const::MAIN_PAGE_CSS_URI,
                g_A10_config.web.css_file.file[0]? g_A10_config.web.css_file.file : A10_Const::MAIN_PAGE_CSS_FILE,
                g_A10_config.web.css_file.mime[0]? g_A10_config.web.css_file.mime : A10_Const::MAIN_PAGE_CSS_MIME },

                { g_A10_config.web.js_file.uri[0]? g_A10_config.web.js_file.uri : A10_Const::MAIN_PAGE_JS_URI,
                g_A10_config.web.js_file.file[0]? g_A10_config.web.js_file.file : A10_Const::MAIN_PAGE_JS_FILE,
                g_A10_config.web.js_file.mime[0]? g_A10_config.web.js_file.mime : A10_Const::MAIN_PAGE_JS_MIME }
            };
        #else
            ST_W10_WebStatic_Route v_webStatic_Routes_arr[7] = {
                { A10_Const::MAIN_PAGE_HTML_URI             , A10_Const::MAIN_PAGE_HTML_FILE            , A10_Const::MAIN_PAGE_HTML_MIME            },
                { A10_Const::MAIN_PAGE_CSS_URI              , A10_Const::MAIN_PAGE_CSS_FILE             , A10_Const::MAIN_PAGE_CSS_MIME             },
                { A10_Const::MAIN_PAGE_JS_URI               , A10_Const::MAIN_PAGE_JS_FILE              , A10_Const::MAIN_PAGE_JS_MIME              },
                // --- 추가 ---

                { W10_Const::CHART_PAGE_HTML_URI            , W10_Const::CHART_PAGE_HTML_FILE           , W10_Const::CHART_PAGE_HTML_MIME           },
                { W10_Const::CHART_PAGE_CSS_URI             , W10_Const::CHART_PAGE_CSS_FILE            , W10_Const::CHART_PAGE_CSS_MIME            },
                { W10_Const::CHART_PAGE_JS_URI              , W10_Const::CHART_PAGE_JS_FILE             , W10_Const::CHART_PAGE_JS_MIME             },
                { W10_Const::CHART_PAGE_DEFAULTURI_URI      , W10_Const::CHART_PAGE_DEFAULTURI_FILE     , W10_Const::CHART_PAGE_DEFAULTURI_MIME     }
               
                
                // { "/chart", "/html/SC10_chart_002.html", "text/html" },
                // { "/SC10_chart_002.html", "/html/SC10_chart_002.html", "text/html" },
                // { "/SC10_chart_002.css",  "/html/SC10_chart_002.css",  "text/css" },
                // { "/SC10_chart_002.js",   "/html/SC10_chart_002.js",   "application/javascript" },
        
            };
        #endif

        for (auto &v_webStatic_Route : v_webStatic_Routes_arr) {
            p_srv.on(v_webStatic_Route.uri, HTTP_GET, [=](AsyncWebServerRequest *p_request) {
                if (LittleFS.exists(v_webStatic_Route.file)) {
                    p_request->send(LittleFS, v_webStatic_Route.file, v_webStatic_Route.mime);
                } else {
                    String v_msg = String("/* missing file: ") + v_webStatic_Route.file + " */";
                    auto *v_resp = p_request->beginResponse(200, v_webStatic_Route.mime, v_msg);
                    _applyHeaders(v_resp, true);
                    p_request->send(v_resp);
                }
            });
        }

        // OPTIONS (CORS preflight)
        p_srv.onNotFound([](AsyncWebServerRequest *p_request){
            if (p_request->method()==HTTP_OPTIONS) {
                auto *v_resp = p_request->beginResponse(204);
                _addCors(v_resp);
                p_request->send(v_resp);
                return;
            }
            p_request->send(404, "text/plain", "Not found");
        });
    }

    // ------------------------------------------------------
    // API 라우트
    // ------------------------------------------------------
    static void mountApi(AsyncWebServer &p_srv, CL_S10_Simulation &p_sim, WiFiMulti &p_multi, CL_P10_PWM &p_P10_pwm) {

#ifdef G_A10_DYNIM_WEB_STATIC_FILE_USE
        // /api/web : 현재 web 설정 반환 (동적 모드에서만)
        p_srv.on("/api/web", HTTP_GET, [](AsyncWebServerRequest *p_req){
            JsonDocument v_doc;
            JsonObject v_root = v_doc.to<JsonObject>();

            v_root["web"]["html_file"]["file"] = g_A10_config.web.html_file.file;
            v_root["web"]["html_file"]["uri"]  = g_A10_config.web.html_file.uri;
            v_root["web"]["html_file"]["mime"] = g_A10_config.web.html_file.mime;

            v_root["web"]["js_file"]["file"] = g_A10_config.web.js_file.file;
            v_root["web"]["js_file"]["uri"]  = g_A10_config.web.js_file.uri;
            v_root["web"]["js_file"]["mime"] = g_A10_config.web.js_file.mime;

            v_root["web"]["css_file"]["file"] = g_A10_config.web.css_file.file;
            v_root["web"]["css_file"]["uri"]  = g_A10_config.web.css_file.uri;
            v_root["web"]["css_file"]["mime"] = g_A10_config.web.css_file.mime;

            String v_out; serializeJson(v_doc, v_out);
            auto *v_rsp = p_req->beginResponse(200, "application/json", v_out);
            _applyHeaders(v_rsp, true);
            p_req->send(v_rsp);
        });
#endif

        // /api/state : 현재 풍속 설정 및 config 상태
        p_srv.on("/api/state", HTTP_GET, [&p_sim, &p_P10_pwm](AsyncWebServerRequest *p_req){
            JsonDocument v_doc;
            JsonVariant v_root = v_doc.to<JsonVariant>();

            JsonObject v_status = v_root["status"].to<JsonObject>();
            v_status["sim_active"]      = p_sim.wind_simulation_active;
            v_status["wind_speed"]      = roundf(p_sim.current_wind_speed * 100.0f) / 100.0f;
            v_status["fan_pwm_raw"]     = p_P10_pwm.getDutyRaw();
            v_status["fan_pwm_percent"] = p_P10_pwm.getDutyPercent();
            v_status["phase_name"]      = g_A10_WEATHER_PHASE_NAMES_Arr[p_sim.current_weather_phase];

            if (g_A10_config.wifi_mode == EN_A10_WIFI_MODE_STA && WiFi.status()==WL_CONNECTED) {
                v_status["wifi_mode"] = "STA";
                v_status["ip_addr"]   = WiFi.localIP().toString();
                v_status["ssid"]      = WiFi.SSID();
            } else if (g_A10_config.wifi_mode == EN_A10_WIFI_MODE_AP) {
                v_status["wifi_mode"] = "AP";
                v_status["ip_addr"]   = WiFi.softAPIP().toString();
                v_status["ssid"]      = g_A10_config.ap_ssid;
            } else {
                v_status["wifi_mode"] = "AP+STA";
                v_status["ip_addr"]   = WiFi.softAPIP().toString();
                v_status["ssid"]      = g_A10_config.ap_ssid;
            }

            // config 직렬화
            CL_C10_ConfigManager::toJson(g_A10_config, v_root["config"].to<JsonObject>());

            // presets 배열
            JsonArray v_presets = v_root["presets"].to<JsonArray>();
            for (int v_i=0; v_i<EN_A10_PRESET_COUNT; ++v_i) v_presets.add(g_A10_PRESET_MODE_NAMES_Arr[v_i]);

            auto *v_resp = p_req->beginResponseStream("application/json");
            serializeJson(v_doc, *v_resp);
            v_resp->setCode(200);
            _applyHeaders(v_resp, true);
            p_req->send(v_resp);
        });

        // /api/config : 설정 변경 (PATCH)
        p_srv.on("/api/config", HTTP_POST, [](AsyncWebServerRequest *p_request){}, nullptr,
                 [&p_sim, &p_multi, &p_P10_pwm](AsyncWebServerRequest *p_request, uint8_t *data, size_t len, size_t index, size_t total){
            if (!_authorize(p_request)) { p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}"); return; }
            if (index==0 && len==total) {
                JsonDocument v_doc;
                if (deserializeJson(v_doc, (const char*)data, len)) {
                    p_request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
                    return;
                }
                int v_oldPreset = g_A10_config.preset_mode_index;
                int v_oldPin    = g_A10_config.fan_pwm_pin;
                int v_oldFreq   = g_A10_config.pwm_frequency;
                int v_oldRes    = g_A10_config.pwm_resolution;
                int v_oldCh     = g_A10_config.pwm_channel;

                bool v_wifiChanged = false;
                CL_C10_ConfigManager::patchFromJson(g_A10_config, v_doc, v_wifiChanged);
                CL_C10_ConfigManager::save(g_A10_config);

                if (g_A10_config.preset_mode_index != v_oldPreset) p_sim.applyCurrentPreset(true);
                if (g_A10_config.fan_pwm_pin != v_oldPin)       p_P10_pwm.set_pwmPin(g_A10_config.fan_pwm_pin);
                if (g_A10_config.pwm_channel != v_oldCh)        p_P10_pwm.set_pwmChannel(g_A10_config.pwm_channel);
                if (g_A10_config.pwm_frequency != v_oldFreq)    p_P10_pwm.set_pwmFrequency(g_A10_config.pwm_frequency);
                if (g_A10_config.pwm_resolution != v_oldRes)    p_P10_pwm.set_pwmResolution(g_A10_config.pwm_resolution);
                p_P10_pwm.set_pwmDuty(0.0f);

                if (v_wifiChanged) {
                    CL_D10_Logger::log(EN_L10_LOG_INFO, "Wi-Fi changed, re-init");
                    CL_M10_WiFiManager::init(g_A10_config, p_multi);
                }
                p_request->send(200, "application/json", "{\"message\":\"Config updated\"}");
            }
        });

        // /api/config/init
        p_srv.on("/api/config/init", HTTP_POST, [](AsyncWebServerRequest *p_request){
            if (!_authorize(p_request)) { p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}"); return; }
            bool v_ok = CL_C10_ConfigManager::saveDefaultConfig();
            if (v_ok) p_request->send(200, "application/json", "{\"message\":\"Default created\"}");
            else      p_request->send(500, "application/json", "{\"error\":\"Init failed or exists\"}");
        });

        // /api/reset
        p_srv.on("/api/reset", HTTP_POST, [](AsyncWebServerRequest *p_request){
            if (!_authorize(p_request)) { p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}"); return; }
            CL_C10_ConfigManager::reset();
            p_request->send(200, "text/plain", "Factory reset... Reboot");
            ESP.restart();
        });

        // /api/scan
        p_srv.on("/api/scan", HTTP_GET, [](AsyncWebServerRequest *p_request){
            bool v_async = p_request->hasParam("async");
            String v_json = CL_M10_WiFiManager::scanNetworksJson(v_async);
            auto *v_resp = p_request->beginResponse(200, "application/json", v_json);
            _applyHeaders(v_resp, true);
            p_request->send(v_resp);
        });

        // /api/diag
        p_srv.on("/api/diag", HTTP_GET, [](AsyncWebServerRequest *p_request){
            JsonDocument v_doc;
            v_doc["heap"]     = ESP.getFreeHeap();
            v_doc["rssi"]     = (WiFi.status()==WL_CONNECTED)? WiFi.RSSI() : 0;
            v_doc["fs_total"] = LittleFS.totalBytes();
            v_doc["fs_used"]  = LittleFS.usedBytes();
            String v_out; serializeJson(v_doc, v_out);
            auto *v_resp = p_request->beginResponse(200, "application/json", v_out);
            _applyHeaders(v_resp, true);
            p_request->send(v_resp);
        });

        // /api/logs
        p_srv.on("/api/logs", HTTP_GET, [](AsyncWebServerRequest *p_request){
            String v_json = CL_D10_Logger::getLogsJson();
            auto *v_resp = p_request->beginResponse(200, "application/json", v_json);
            _applyHeaders(v_resp, true);
            p_request->send(v_resp);
        });

        // /api/version
        p_srv.on("/api/version", HTTP_GET, [](AsyncWebServerRequest *p_request){
            JsonDocument v_doc;
            v_doc["fw_version"]  = A10_Const::FW_VERSION;
            v_doc["config_file"] = A10_Const::CONFIG_JSON_FILE;
            String v_out; serializeJson(v_doc, v_out);
            auto *v_resp = p_request->beginResponse(200, "application/json", v_out);
            _applyHeaders(v_resp, true);
            p_request->send(v_resp);
        });

        // ------------------------------------------------------
        // /api/chart_data : 실시간 풍속 / PWM duty 로그 반환
        // ------------------------------------------------------
        p_srv.on("/api/chart_data", HTTP_GET, [&p_sim](AsyncWebServerRequest *p_req){
            JsonDocument v_doc;
            JsonArray v_arr = v_doc["records"].to<JsonArray>();

            for (auto &e : CL_S10_Simulation::s_chartBuffer) {
                JsonObject v_o = v_arr.add<JsonObject>();
                v_o["t"]         = e.timestamp;
                v_o["wind"]      = e.wind_speed;
                v_o["pwm"]       = e.pwm_duty;
                v_o["intensity"] = e.intensity;
                v_o["variability"]= e.variability;
                v_o["turbulence"] = e.turbulence;
                v_o["preset"]    = e.preset_id;
                v_o["gust"]      = e.gust_active;
                v_o["thermal"]   = e.thermal_active;
            }

            String v_out;
            serializeJson(v_doc, v_out);
            auto *v_resp = p_req->beginResponse(200, "application/json", v_out);
            _applyHeaders(v_resp, true);
            p_req->send(v_resp);
        });

        // -------------------
		// /upload : 정적 파일 업로드 (보안제한)
		// -------------------
		static bool s_uploadError = false;

		p_srv.on("/upload", HTTP_POST, [](AsyncWebServerRequest *p_request) {
			if (!_authorize(p_request)) {
				p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
				return;
			}

			if (s_uploadError) {
				s_uploadError = false;
				p_request->send(500, "application/json", "{\"error\":\"upload failed\"}");
			} else {
				p_request->send(200, "application/json", "{\"message\":\"Upload OK\"}");
			} 
		}, [](AsyncWebServerRequest *p_request, const String &filename, size_t index, uint8_t *data, size_t len, bool final) {
			if (!_authorize(p_request)) { s_uploadError = true; return; }

			static const size_t v_kMaxUpload = 4 * 1024 * 1024;
			if (index == 0) {
				s_uploadError = false;
				String v_safe = _sanitizeFilename(filename);
				if (!_isAllowedExt(v_safe)) { s_uploadError = true; return; }

				String v_path = "/" + v_safe;
				if (LittleFS.exists(v_path)) {
					LittleFS.remove(v_path);
				}

				p_request->_tempFile = LittleFS.open(v_path, "w");
				if (!p_request->_tempFile) { s_uploadError = true; return; }
			}

			if (s_uploadError) return;
			if (p_request->_tempFile) {
				if (p_request->_tempFile.size() + len > v_kMaxUpload) {
					p_request->_tempFile.close();
					LittleFS.remove(p_request->_tempFile.name());
					s_uploadError = true;
					return;
				}
				if (len) p_request->_tempFile.write(data, len);
				if (final) p_request->_tempFile.close();
			} 
		});

		// -------------------
		// /update : OTA 펌웨어 업로드
		// -------------------
		p_srv.on("/update", HTTP_POST, [](AsyncWebServerRequest *p_request) {
			if (!_authorize(p_request)) {
				p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
				return;
			} }, [](AsyncWebServerRequest *p_request, const String &filename, size_t index, uint8_t *data, size_t len, bool final) {
			if (!_authorize(p_request))
				return;
			if (!index) {
				size_t v_maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
				if (!Update.begin(v_maxSketchSpace)) {
					Update.printError(Serial);
					p_request->send(500, "text/plain", "OTA begin failed");
					return;
				}
			}
			if (len) {
				if (Update.write(data, len) != len) {
					Update.printError(Serial);
					p_request->send(500, "text/plain", "OTA write failed");
					return;
				}
			}
			if (final) {
				if (!Update.end(true)) {
					String v_msg = "OTA end failed: ";
					v_msg += Update.errorString();
					p_request->send(500, "text/plain", v_msg);
					return;
				}
				p_request->send(200, "text/plain", "OTA OK, rebooting");
				ESP.restart();
			} });
    }

private:
    // ------------------------------------------------------
    // 내부 공통 유틸
    // ------------------------------------------------------
    static void _applyHeaders(AsyncWebServerResponse *p_resp, bool p_noCache=false) {
        if (p_noCache) _addNoCache(p_resp);
        _addCors(p_resp);
    }
    static void _addNoCache(AsyncWebServerResponse *p_resp) {
        p_resp->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
        p_resp->addHeader("Pragma", "no-cache");
        p_resp->addHeader("Expires", "0");
    }
    static void _addCors(AsyncWebServerResponse *p_resp) {
        p_resp->addHeader("Access-Control-Allow-Origin", "*");
        p_resp->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
        p_resp->addHeader("Access-Control-Allow-Headers", "Content-Type, X-API-Key");
    }
    static bool _authorize(AsyncWebServerRequest *p_req) {
        if (strlen(g_A10_config.api_key)==0) return true;
        if (!p_req->hasHeader("X-API-Key")) return false;
        auto *v_h = p_req->getHeader("X-API-Key");
        return (v_h && v_h->value()==String(g_A10_config.api_key));
    }

    // 업로드 파일명 정규화 → ../ 같은 경로 탈출, 금지 문자 제거
	static String _sanitizeFilename(const String &p_in) {
		String v_out;
		for (size_t i = 0; i < p_in.length(); i++) {
			char v_char = p_in[i];
			if (v_char == '/' || v_char == '\\')
				continue;
			if (v_char == ':' || v_char == '*' || v_char == '?' || v_char == '"' || v_char == '<' || v_char == '>' || v_char == '|')
				continue;
			v_out += v_char;
		}
		v_out.trim();
		return v_out;
	}

	// 허용 확장자만 필터 (html/js/css/json/이미지 등)
	static bool _isAllowedExt(const String &p_name) {
		String v_name = p_name;
		v_name.toLowerCase();
		return v_name.endsWith(".html") || v_name.endsWith(".htm") || v_name.endsWith(".js") ||
			   v_name.endsWith(".css") || v_name.endsWith(".json") || v_name.endsWith(".txt") ||
			   v_name.endsWith(".gif") || v_name.endsWith(".png") || v_name.endsWith(".jpg") ||
			   v_name.endsWith(".jpeg") || v_name.endsWith(".svg") || v_name.endsWith(".ico") ||
			   v_name.endsWith(".gz");
	}
};
