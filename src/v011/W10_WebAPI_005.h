
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

    // 공통 응답 헤더 유틸
    static void applyHeaders(AsyncWebServerResponse *res, bool noCache = false) {
	    if (noCache) addNoCache(res);
	    addCors(res);
    }

	// API Key 검사 → g_A10_config.api_key 가 비어 있지 않으면 반드시 헤더 필요
	static bool authorize(AsyncWebServerRequest *req) {
		if (strlen(g_A10_config.api_key) == 0) {
			return true;  // 설정이 비어 있으면 무조건 통과
		}
		if (!req->hasHeader("X-API-Key")) {
			return false;  // 헤더 없으면 거부
		}

		auto *h = req->getHeader("X-API-Key");
		return (h && h->value() == String(g_A10_config.api_key));	// 값이 일치해야 통과
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

    static void init(AsyncWebServer &p_srv, CL_S10_Simulation &p_sim, WiFiMulti &p_multi, CL_P10_PWM &p_P10_pwm) {
        
        mountApi(p_srv, p_sim, p_multi, p_P10_pwm);
		//mountApi(g_WS10_asyncWeb, g_WS10_sim, g_WS10_wifiMulti);
		CL_D10_Logger::log(EN_L10_LOG_INFO, "W10_init_010::mountApi");

		mountStatic(p_srv);
		// mountStatic(g_WS10_asyncWeb);
		CL_D10_Logger::log(EN_L10_LOG_INFO, "W10_init_020::mountStatic");
		
	}
		//
	// ======================================================
	// 정적 파일 서빙
	// ======================================================
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
	    auto serveFile = [&](const StaticRoute &r) {
		    p_srv.on(r.uri, HTTP_GET, [=](AsyncWebServerRequest *req) {
			    if (LittleFS.exists(r.path)) {
				    req->send(LittleFS, r.path, r.mime);
			    } else {
				    String msg = String("/* missing file: ") + r.path + " */";
				    auto *res  = req->beginResponse(200, r.mime, msg);
				    applyHeaders(res, true);
				    req->send(res);
			    }
		    });
	    };
    
	    // ROUTES 등록 (HTML/JS/CSS)
	    for (auto &r : ROUTES) {
		    CL_D10_Logger::log(EN_L10_LOG_INFO,
			    "[STATIC] mount: %s -> %s (%s)", r.uri, r.path, r.mime);
		    serveFile(r);
	    }
    
	    // OPTIONS 프리플라이트 (브라우저 CORS 사전 요청 대응)
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
	
    
	    
	static void mountStatic_old(AsyncWebServer &p_srv) {
		// /html/ 폴더를 기본 루트로 서비스
		// → / 요청 시 index.html 자동 매핑
		auto &h = p_srv.serveStatic("/", LittleFS, "/html/")
					  .setDefaultFile("SC10_main_014.html")
					  .setCacheControl("max-age=86400");  // 하루 캐시
		(void)h;

		// (옵션) 개별 경로도 호환성 위해 남겨둠
		p_srv.on(A10_Const::JS_URI, HTTP_GET, [](AsyncWebServerRequest *req) {
		// p_srv.on("/SC10_main_014.js", HTTP_GET, [](AsyncWebServerRequest *req) {
			if (LittleFS.exists(A10_Const::JS_FILE)) {
				req->send(LittleFS, A10_Const::JS_FILE, "application/javascript");
			} else {
				auto *res = req->beginResponse(200, "application/javascript", "console.log('SC10: no JS file');");
				addNoCache(res);
				addCors(res);
				req->send(res);
			}
		});

		p_srv.on(A10_Const::CSS_URI, HTTP_GET, [](AsyncWebServerRequest *req) {
		// p_srv.on("/SC10_main_014.css", HTTP_GET, [](AsyncWebServerRequest *req) {
			if (LittleFS.exists(A10_Const::CSS_FILE)) {
				req->send(LittleFS, A10_Const::CSS_FILE, "text/css");
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
	static void mountApi(AsyncWebServer &p_srv, CL_S10_Simulation &p_sim, WiFiMulti &p_multi, CL_P10_PWM &p_P10_pwm) {
		// -------------------
		// /api/state : 현재 상태 조회
		// -------------------
		p_srv.on("/api/state", HTTP_GET, [&p_sim, &p_P10_pwm](AsyncWebServerRequest *req) {
				
            JsonDocument doc;

			// 2. doc.to<JsonVariant>()를 사용하여 root 객체에 접근
    		JsonVariant root = doc.to<JsonVariant>(); 

			JsonObject st	 = root["status"].to<JsonObject>();
			st["sim_active"] = p_sim.wind_simulation_active;
			st["wind_speed"] = roundf(p_sim.current_wind_speed * 100.0f) / 100.0f;

           

			// PWM raw + percent 동시 제공 ▽▽
            st["fan_pwm"] = p_P10_pwm.getDutyRaw();
            st["fan_pwm_percent"] = p_P10_pwm.getDutyPercent();
				
            /*
			const int	duty_raw	 = ledcRead(g_A10_config.pwm_channel);
			const int	levels		 = (1 << g_A10_config.pwm_resolution) - 1;
			const float duty_percent = (levels > 0) ? (100.0f * duty_raw / (float)levels) : 0.0f;

			st["fan_pwm"]		  = duty_raw;  // (기존 호환) raw duty 유지
			st["fan_pwm_percent"] = duty_percent;
			*/

			
			// (선택) 클라이언트 계산용으로 해상도도 내려주면 더 좋음
			root["config"]["pwm"]["resolution"] = g_A10_config.pwm_resolution;

			st["phase_name"] = g_A10_WEATHER_PHASE_NAMES_Arr[p_sim.current_weather_phase];


			if (g_A10_config.wifi_mode == G_A10_WIFI_MODE_STA && WiFi.status() == WL_CONNECTED) {
				st["wifi_mode"] = "STA";
				st["ip_addr"]	= WiFi.localIP().toString();
				st["ssid"]		= WiFi.SSID();
			} else {
				st["wifi_mode"] = "AP";
				st["ip_addr"]	= WiFi.softAPIP().toString();
				st["ssid"]		= g_A10_config.ap_ssid;
			}

			// config 직렬화
			CL_C10_ConfigManager::toJson(g_A10_config, root["config"].to<JsonObject>());

			// presets 추가
			JsonArray presets = root["presets"].to<JsonArray>();
			for (int i = 0; i < EN_A10_PRESET_COUNT; i++) {
				presets.add(g_A10_PRESET_MODE_NAMES_Arr[i]);
			}

			// 3. StaticJsonDocument를 AsyncResponseStream에 직접 직렬화하여 응답 생성
			// (AsyncJson.h 종속성 없이 AsyncWebServer의 기본 스트림 기능 사용)
			AsyncResponseStream *res = req->beginResponseStream("application/json");
			
			// 응답 스트림에 JSON 직렬화
			serializeJson(doc, *res); 

			res->setCode(200);
			addNoCache(res);
			addCors(res);
			req->send(res);

		});

		// -------------------
		// /api/config : 설정 변경 (Wi-Fi 재초기화 가능)
		// -------------------
		p_srv.on("/api/config", HTTP_POST, [](AsyncWebServerRequest *req) {}, nullptr, [&p_sim, &p_multi, &p_P10_pwm](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index, size_t total) {
        if (!authorize(req)) { req->send(401, "application/json", "{\"error\":\"unauthorized\"}"); return; }
        if (index == 0 && len == total) {
          JsonDocument v_doc;
          if (deserializeJson(v_doc, (const char*)data, len)) {
            req->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
          }

			// 패치 전 스냅샷
			
          int oldPreset = g_A10_config.preset_mode_index;
          int oldPin    = g_A10_config.fan_pwm_pin;
          int oldFreq   = g_A10_config.pwm_frequency;
          int oldRes    = g_A10_config.pwm_resolution;
		  int oldChannel = g_A10_config.pwm_channel;

			
          bool v_wifiChanged = false;
          CL_C10_ConfigManager::patchFromJson(g_A10_config, v_doc, v_wifiChanged);
          CL_C10_ConfigManager::save(g_A10_config);

		  // 1) 프리셋 바뀌면 시뮬만 재시작
          if (g_A10_config.preset_mode_index != oldPreset) {
              p_sim.applyCurrentPreset(true);
          }

          // 2) PWM 파라미터 변경 감지 → LEDC 재초기화
			if(g_A10_config.fan_pwm_pin != oldPin){
				p_P10_pwm.set_pwmPin(g_A10_config.fan_pwm_pin);
				delay(10);  // PWM 재부착 안정화 (ESP32 ghost pulse 방지)
			}
			if(g_A10_config.pwm_channel != oldChannel){
				p_P10_pwm.set_pwmChannel(g_A10_config.pwm_channel);
			}
			if(g_A10_config.pwm_frequency != oldFreq){
				p_P10_pwm.set_pwmFrequency(g_A10_config.pwm_frequency);
			}
			if(g_A10_config.pwm_resolution != oldRes){
				p_P10_pwm.set_pwmResolution(g_A10_config.pwm_resolution);
			}
			
			// 현재 요구되는 팬 출력을 재적용 (예: 0%로 안정화하거나, 직전 상태 유지)
            // 여기서는 안전하게 0%로 초기화 후 시뮬 tick에서 다시 설정되게 함
		    p_P10_pwm.set_pwmDuty(0.0f);

			CL_D10_Logger::log(EN_L10_LOG_INFO,
                "PWM reinit: pin %d->%d, ch %d->%d, freq %d->%d, res %d->%d",
                 oldPin    , g_A10_config.fan_pwm_pin,
				 oldChannel, g_A10_config.pwm_channel,
                 oldFreq   , g_A10_config.pwm_frequency,
                 oldRes    , g_A10_config.pwm_resolution
				);



          if (v_wifiChanged) {
            CL_D10_Logger::log(EN_L10_LOG_INFO, "WiFi config changed. Re-init WiFi");
            CL_M10_WiFiManager::init(g_A10_config, p_multi);
          }
          req->send(200, "application/json", "{\"message\":\"Config updated\"}");
        } });

		// -------------------
		// /api/scan : 주변 Wi-Fi 스캔
		// -------------------
		p_srv.on("/api/scan", HTTP_GET, [](AsyncWebServerRequest *req) {
			bool   async = req->hasParam("async");
			String j	 = CL_M10_WiFiManager::scanNetworksJson(async);
			auto  *res	 = req->beginResponse(200, "application/json", j);
			addNoCache(res);
			addCors(res);
			req->send(res);
		});

		// -------------------
		// /api/diag : 메모리/FS/신호 강도 등 진단
		// -------------------
		p_srv.on("/api/diag", HTTP_GET, [](AsyncWebServerRequest *req) {
			JsonDocument v_doc;
			v_doc["heap"] = ESP.getFreeHeap();
			if (WiFi.status() == WL_CONNECTED) {
				v_doc["rssi"] = WiFi.RSSI();
			}
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
			String j   = CL_D10_Logger::getLogsJson();
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
			CL_C10_ConfigManager::reset();
			req->send(200, "text/plain", "Factory reset... Reboot");
			ESP.restart();
		});

		// -------------------
		// /api/version : 펌웨어 버전 정보
		// -------------------
		p_srv.on("/api/version", HTTP_GET, [](AsyncWebServerRequest *req) {
			JsonDocument v_doc;
			v_doc["fw_version"]	 = A10_Const::FW_VERSION;
			v_doc["config_file"] = A10_Const::CONFIG_FILE;
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

		p_srv.on("/upload", HTTP_POST, [](AsyncWebServerRequest *req) {
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
		}, [](AsyncWebServerRequest *req, const String &filename, size_t index, uint8_t *data, size_t len, bool final) {
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

		// -------------------
		// /update : OTA 펌웨어 업로드
		// -------------------
		p_srv.on("/update", HTTP_POST, [](AsyncWebServerRequest *req) {
			if (!authorize(req)) {
				req->send(401, "application/json", "{\"error\":\"unauthorized\"}");
				return;
			} }, [](AsyncWebServerRequest *req, const String &filename, size_t index, uint8_t *data, size_t len, bool final) {
			if (!authorize(req))
				return;
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
			} });
	}
};
