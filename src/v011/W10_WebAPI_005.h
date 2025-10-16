
// W10_WebAPI_005.h
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

#include "A10_Const_004.h"
#include "C10_ConfigManager_004.h"
#include "D10_Logger_004.h"
#include "M10_WiFiManager_004.h"
#include "S10_Simulation_004.h"

#include "P10_PWM_ctrl_005.h"

class CL_W10_WebAPI {
   public:


    static void init(AsyncWebServer &p_srv, CL_S10_Simulation &p_sim, WiFiMulti &p_multi, CL_P10_PWM &p_P10_pwm) {
        
        mountApi(p_srv, p_sim, p_multi, p_P10_pwm);
		CL_D10_Logger::log(EN_L10_LOG_INFO, "W10_init_010::mountApi");

		mountStatic(p_srv);
		CL_D10_Logger::log(EN_L10_LOG_INFO, "W10_init_020::mountStatic");
		
	}
	 
	// ======================================================
    // 정적 파일 서빙 (유지보수성 강화 + 로깅 개선)
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
    
	    // 루트 HTML 서빙 (기본 페이지)
	    if (!LittleFS.exists(A10_Const::HTML_FILE)) {
		    CL_D10_Logger::log(EN_L10_LOG_ERROR, "[STATIC] Missing main HTML: %s", A10_Const::HTML_FILE);
	    }
	    auto &h = p_srv.serveStatic("/", LittleFS, "/html/")
				      .setDefaultFile(A10_Const::HTML_FILE)
				      .setCacheControl("max-age=86400");
	    (void)h;
    
	    // 파일 응답 헬퍼
	    auto v_serveFile = [&](const StaticRoute &p_staticRoute) {
		    p_srv.on(p_staticRoute.uri, HTTP_GET, [=](AsyncWebServerRequest *p_request) {
			    if (LittleFS.exists(p_staticRoute.path)) {
				    p_request->send(LittleFS, p_staticRoute.path, p_staticRoute.mime);
			    } else {
				    String v_msg = String("/* missing file: ") + p_staticRoute.path + " */";
				    auto *v_response  = p_request->beginResponse(200, p_staticRoute.mime, v_msg);
				    _applyHeaders(v_response, true);
				    p_request->send(v_response);
			    }
		    });
	    };
    
	    // ROUTES 등록 (HTML/JS/CSS)
	    for (auto &v_staticRoute : ROUTES) {
		    CL_D10_Logger::log(EN_L10_LOG_INFO, "[STATIC] mount: %s -> %s (%s)", v_staticRoute.uri, v_staticRoute.path, v_staticRoute.mime);

		    v_serveFile(v_staticRoute);
	    }
    
	    // OPTIONS 프리플라이트 (브라우저 CORS 사전 요청 대응)
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
	// API 라우트 등록
	// ======================================================
	static void mountApi(AsyncWebServer &p_srv, CL_S10_Simulation &p_sim, WiFiMulti &p_multi, CL_P10_PWM &p_P10_pwm) {
		// -------------------
		// /api/state : 현재 상태 조회
		// -------------------
		p_srv.on("/api/state", HTTP_GET, [&p_sim, &p_P10_pwm](AsyncWebServerRequest *p_request) {
				
            JsonDocument v_doc;

			// 2. v_doc.to<JsonVariant>()를 사용하여 root 객체에 접근
    		JsonVariant v_jsonVar_root = v_doc.to<JsonVariant>(); 

			JsonObject v_jsonObj_status	 = v_jsonVar_root["status"].to<JsonObject>();
			v_jsonObj_status["sim_active"] = p_sim.wind_simulation_active;
			v_jsonObj_status["wind_speed"] = roundf(p_sim.current_wind_speed * 100.0f) / 100.0f;

           

			// PWM raw + percent 동시 제공 ▽▽
            v_jsonObj_status["fan_pwm"] = p_P10_pwm.getDutyRaw();
            v_jsonObj_status["fan_pwm_percent"] = p_P10_pwm.getDutyPercent();
				
            /*
			const int	duty_raw	 = ledcRead(g_A10_config.pwm_channel);
			const int	levels		 = (1 << g_A10_config.pwm_resolution) - 1;
			const float duty_percent = (levels > 0) ? (100.0f * duty_raw / (float)levels) : 0.0f;

			v_jsonObj_status["fan_pwm"]		  = duty_raw;  // (기존 호환) raw duty 유지
			v_jsonObj_status["fan_pwm_percent"] = duty_percent;
			*/

			
			// (선택) 클라이언트 계산용으로 해상도도 내려주면 더 좋음
			v_jsonVar_root["config"]["pwm"]["resolution"] = g_A10_config.pwm_resolution;

			v_jsonObj_status["phase_name"] = g_A10_WEATHER_PHASE_NAMES_Arr[p_sim.current_weather_phase];


			if (g_A10_config.wifi_mode == G_A10_WIFI_MODE_STA && WiFi.status() == WL_CONNECTED) {
				v_jsonObj_status["wifi_mode"] 	= "STA";
				v_jsonObj_status["ip_addr"]		= WiFi.localIP().toString();
				v_jsonObj_status["ssid"]		= WiFi.SSID();
			} else {
				v_jsonObj_status["wifi_mode"] 	= "AP";
				v_jsonObj_status["ip_addr"]		= WiFi.softAPIP().toString();
				v_jsonObj_status["ssid"]		= g_A10_config.ap_ssid;
			}

			// config 직렬화
			CL_C10_ConfigManager::toJson(g_A10_config, v_jsonVar_root["config"].to<JsonObject>());

			// presets 추가
			JsonArray v_jsonArray_presets = v_jsonVar_root["presets"].to<JsonArray>();
			for (int i = 0; i < EN_A10_PRESET_COUNT; i++) {
				v_jsonArray_presets.add(g_A10_PRESET_MODE_NAMES_Arr[i]);
			}

			// 3. StaticJsonDocument를 AsyncResponseStream에 직접 직렬화하여 응답 생성
			// (AsyncJson.h 종속성 없이 AsyncWebServer의 기본 스트림 기능 사용)
			AsyncResponseStream *v_response = p_request->beginResponseStream("application/json");
			
			// 응답 스트림에 JSON 직렬화
			serializeJson(v_doc, *v_response); 

			v_response->setCode(200);
			
			_applyHeaders(v_response, true);
			// _addNoCache(v_response);
			// _addCors(v_response);

			p_request->send(v_response);

		});

		// -------------------
		// /api/config : 설정 변경 (Wi-Fi 재초기화 가능)
		// -------------------
		p_srv.on("/api/config", HTTP_POST, [](AsyncWebServerRequest *p_request) {}, nullptr, [&p_sim, &p_multi, &p_P10_pwm](AsyncWebServerRequest *p_request, uint8_t *data, size_t len, size_t index, size_t total) {
			if (!_authorize(p_request)) { 
				p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}"); 
				return; 
			}

			if (index == 0 && len == total) {
				JsonDocument v_doc;
				if (deserializeJson(v_doc, (const char*)data, len)) {
					p_request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
					return;
				}

					// 패치 전 스냅샷
					
				int v_oldPreset 	= g_A10_config.preset_mode_index;
				int v_oldPin    	= g_A10_config.fan_pwm_pin;
				int v_oldFreq   	= g_A10_config.pwm_frequency;
				int v_oldRes    	= g_A10_config.pwm_resolution;
				int v_oldChannel 	= g_A10_config.pwm_channel;

					
				bool v_wifiChanged = false;
				CL_C10_ConfigManager::patchFromJson(g_A10_config, v_doc, v_wifiChanged);
				CL_C10_ConfigManager::save(g_A10_config);

				// 1) 프리셋 바뀌면 시뮬만 재시작
				if (g_A10_config.preset_mode_index != v_oldPreset) {
					p_sim.applyCurrentPreset(true);
				}

				// 2) PWM 파라미터 변경 감지 → LEDC 재초기화
				if(g_A10_config.fan_pwm_pin != v_oldPin){
					p_P10_pwm.set_pwmPin(g_A10_config.fan_pwm_pin);
					delay(10);  // PWM 재부착 안정화 (ESP32 ghost pulse 방지)
				}
				if(g_A10_config.pwm_channel != v_oldChannel){
					p_P10_pwm.set_pwmChannel(g_A10_config.pwm_channel);
				}
				if(g_A10_config.pwm_frequency != v_oldFreq){
					p_P10_pwm.set_pwmFrequency(g_A10_config.pwm_frequency);
				}
				if(g_A10_config.pwm_resolution != v_oldRes){
					p_P10_pwm.set_pwmResolution(g_A10_config.pwm_resolution);
				}
				
				// 현재 요구되는 팬 출력을 재적용 (예: 0%로 안정화하거나, 직전 상태 유지)
				// 여기서는 안전하게 0%로 초기화 후 시뮬 tick에서 다시 설정되게 함
				p_P10_pwm.set_pwmDuty(0.0f);

				CL_D10_Logger::log(EN_L10_LOG_INFO,
					"PWM reinit: pin %d->%d, ch %d->%d, freq %d->%d, res %d->%d",
					v_oldPin    , g_A10_config.fan_pwm_pin,
					v_oldChannel, g_A10_config.pwm_channel,
					v_oldFreq   , g_A10_config.pwm_frequency,
					v_oldRes    , g_A10_config.pwm_resolution
					);

				if (v_wifiChanged) {
					CL_D10_Logger::log(EN_L10_LOG_INFO, "WiFi config changed. Re-init WiFi");
					CL_M10_WiFiManager::init(g_A10_config, p_multi);
				}
				p_request->send(200, "application/json", "{\"message\":\"Config updated\"}");
        	} 
	    });

		// -------------------
		// /api/scan : 주변 Wi-Fi 스캔
		// -------------------
		p_srv.on("/api/scan", HTTP_GET, [](AsyncWebServerRequest *p_request) {
			bool   v_async = p_request->hasParam("async");
			String v_scanNtwroks_jsonString	 = CL_M10_WiFiManager::scanNetworksJson(v_async);
			auto  *v_response	 = p_request->beginResponse(200, "application/json", v_scanNtwroks_jsonString);

			_applyHeaders(v_response, true);
			// _addNoCache(v_response);
			// _addCors(v_response);
			p_request->send(v_response);
		});

		// -------------------
		// /api/diag : 메모리/FS/신호 강도 등 진단
		// -------------------
		p_srv.on("/api/diag", HTTP_GET, [](AsyncWebServerRequest *p_request) {
			JsonDocument v_doc;
			v_doc["heap"] = ESP.getFreeHeap();

			if (WiFi.status() == WL_CONNECTED) {
				v_doc["rssi"] = WiFi.RSSI();
			}
			v_doc["fs_total"] = LittleFS.totalBytes();
			v_doc["fs_used"]  = LittleFS.usedBytes();

			String v_resp_jsonstring;
			serializeJson(v_doc, v_resp_jsonstring);
			auto *v_response = p_request->beginResponse(200, "application/json", v_resp_jsonstring);

			_applyHeaders(v_response, true);
			// _addNoCache(v_response);
			// _addCors(v_response);
			p_request->send(v_response);
		});

		// -------------------
		// /api/logs : 최근 로그 반환
		// -------------------
		p_srv.on("/api/logs", HTTP_GET, [](AsyncWebServerRequest *p_request) {
			String v_logs_jsonString   = CL_D10_Logger::getLogsJson();
			auto  *v_response = p_request->beginResponse(200, "application/json", v_logs_jsonString);

			_applyHeaders(v_response, true);
			// _addNoCache(v_response);
			// _addCors(v_response);
			p_request->send(v_response);
		});

		// -------------------
		// /api/reboot : 시스템 재부팅
		// -------------------
		p_srv.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest *p_request) {
			if (!_authorize(p_request)) {
				p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
				return;
			}
			p_request->send(200, "text/plain", "Rebooting...");
			ESP.restart();
		});

		// -------------------
		// /api/reset : 공장 초기화
		// -------------------
		p_srv.on("/api/reset", HTTP_POST, [](AsyncWebServerRequest *p_request) {
			if (!_authorize(p_request)) {
				p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
				return;
			}
			CL_C10_ConfigManager::reset();
			p_request->send(200, "text/plain", "Factory reset... Reboot");
			ESP.restart();
		});

		// -------------------
		// /api/fsinfo : LittleFS 정보
		// -------------------
		p_srv.on("/api/fsinfo", HTTP_GET, [](AsyncWebServerRequest *p_req) {
			JsonDocument v_doc;
			v_doc["fs_total"] = LittleFS.totalBytes();
			v_doc["fs_used"]  = LittleFS.usedBytes();
			v_doc["fs_free"]  = LittleFS.totalBytes() - LittleFS.usedBytes();

			String v_resp_jsonstring;
			serializeJson(v_doc, v_resp_jsonstring);
			
			auto *v_response = p_req->beginResponse(200, "application/json", v_resp_jsonstring);
			
			_applyHeaders(v_response, true);

			p_req->send(v_response);
		});

		// -------------------
		// /api/version : 펌웨어 버전 정보
		// -------------------
		p_srv.on("/api/version", HTTP_GET, [](AsyncWebServerRequest *p_request) {
			JsonDocument v_doc;

			v_doc["fw_version"]	 = A10_Const::FW_VERSION;
			v_doc["config_file"] = A10_Const::CONFIG_FILE;
			
			String v_resp_jsonstring;
			serializeJson(v_doc, v_resp_jsonstring);
			
			auto *v_response = p_request->beginResponse(200, "application/json", v_resp_jsonstring);

			_applyHeaders(v_response, true);
			// _addNoCache(v_response);
			// _addCors(v_response);
			
			p_request->send(v_response);
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

	// ======================================================
	// 유틸리티: 공통 헤더 설정 (보안/캐시)
	// ======================================================

	    // 공통 응답 헤더 유틸
    static void _applyHeaders(AsyncWebServerResponse *p_response, bool noCache = false) {
	    
		if (noCache) {
			_addNoCache(p_response);
		}

	    _addCors(p_response);
    }

	// 캐시 금지 (매번 최신값 받도록)
	static void _addNoCache(AsyncWebServerResponse *p_response) {
		p_response->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
		p_response->addHeader("Pragma", "no-cache");
		p_response->addHeader("Expires", "0");
	}

	// CORS 허용 (브라우저에서 JS fetch 가능하도록)
	static void _addCors(AsyncWebServerResponse *p_response) {
		p_response->addHeader("Access-Control-Allow-Origin", "*");
		p_response->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
		p_response->addHeader("Access-Control-Allow-Headers", "Content-Type, X-API-Key");
	}


	// API Key 검사 → g_A10_config.api_key 가 비어 있지 않으면 반드시 헤더 필요
	static bool _authorize(AsyncWebServerRequest *p_request) {

		if (strlen(g_A10_config.api_key) == 0) {
			return true;  // 설정이 비어 있으면 무조건 통과
		}
		if (!p_request->hasHeader("X-API-Key")) {
			return false;  // 헤더 없으면 거부
		}

		auto *v_req_header_apiKey = p_request->getHeader("X-API-Key");
		return (v_req_header_apiKey && v_req_header_apiKey->value() == String(g_A10_config.api_key));	// 값이 일치해야 통과
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
