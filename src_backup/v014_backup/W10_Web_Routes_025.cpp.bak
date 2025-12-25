/*
 * ------------------------------------------------------
 * 소스명 : W10_Web_Routes_025.cpp
 * 모듈 약어 : W10
 * 모듈명 : Smart Nature Wind Web API (v025) - Routes Implementation
 * ------------------------------------------------------
 * 기능 요약:
 * - Web UI / REST API 엔드포인트 라우팅 로직 구현
 * ------------------------------------------------------
 * [구현 규칙]
 * - 항상 소스 시작 주석 부분 체계 유지 및 내용 업데이트
 * - 소스 시작 주석 부분 구현규칙, 코드네이밍규칙 내용 그대로 유지, 수정금지
 * - ArduinoJson v7.x.x 사용 (v6 이하 사용 금지)
 * - JsonDocument 단일 타입만 사용
 * - createNestedArray/Object/containsKey 사용 금지
 * - memset + strlcpy 기반 안전 초기화
 * - 주석/필드명은 JSON 구조와 동일하게 유지
 * ------------------------------------------------------
 * [코드 네이밍 규칙]
 * - 전역 상수,매크로      : G_모듈약어_ 접두사
 * - 전역 변수             : g_모듈약어_ 접두사
 * - 전역 함수             : 모듈약어_ 접두사
 * - type                  : T_모듈약어_ 접두사
 * - typedef               : _t  접미사
 * - enum 상수             : EN_모듈약어_ 접두사
 * - 구조체                : ST_모듈약어_ 접두사
 * - 클래스명              : CL_모듈약어_ 접두사
 * - 클래스 private 멤버   : _ 접두사
 * - 클래스 멤버(함수/변수) : 모듈약어 접두사 미사용
 * - 클래스 정적 멤버      : s_ 접두사
 * ------------------------------------------------------
 */

#include "W10_Web_025.h"
#include "M10_MotionLogic_016.h"
#include "CT10_ControlManager_022.h"
#include "WF10_WiFiManager_023.h" // CL_WF10_WiFiManager::scanNetworksToJson 사용을 위해 포함


// ------------------------------------------------------
// 정적 멤버 정의 (Routes/Broadcasts/WebSockets 파일 중 하나에만 정의)
// ------------------------------------------------------
AsyncWebServer*			CL_W10_WebAPI::s_server	 		= nullptr;
CL_CT10_ControlManager* CL_W10_WebAPI::s_control 		= nullptr;
WiFiMulti*				CL_W10_WebAPI::s_multi	 		= nullptr;	 	// v012 복구
File					CL_W10_WebAPI::s_upFile;			 			// v012 복구

// AsyncWebSocket 객체 자체는 CL_W10_WebAPI 클래스 외부에서 정의합니다.
AsyncWebSocket 			s_wsLogs("/ws/log");
AsyncWebSocket 			s_wsState("/ws/state");
AsyncWebSocket 			s_wsChart("/ws/chart");
AsyncWebSocket 			s_wsMetrics("/ws/metrics");

// AsyncWebSocket 포인터 멤버를 정의합니다.
AsyncWebSocket* 		CL_W10_WebAPI::s_wsServerState	 = nullptr;
AsyncWebSocket* 		CL_W10_WebAPI::s_wsServerLogs	 = nullptr;
AsyncWebSocket* 		CL_W10_WebAPI::s_wsServerChart	 = nullptr;
AsyncWebSocket* 		CL_W10_WebAPI::s_wsServerMetrics = nullptr;

// --------------------------------------------------
// 초기화: WebServer + ControlManager + WiFiMulti 연결
// --------------------------------------------------
void CL_W10_WebAPI::begin(AsyncWebServer& p_server, CL_CT10_ControlManager& p_control, WiFiMulti& p_multi) { // ✅ WiFiMulti 인자 추가
	s_server  = &p_server;
	s_control = &p_control;
	s_multi   = &p_multi;

	s_wsServerLogs	  = &s_wsLogs;
	s_wsServerState	  = &s_wsState;
	s_wsServerChart	  = &s_wsChart;
	s_wsServerMetrics = &s_wsMetrics;


	routeVersion();
	routeState();
	routeSystem();
	routeWifi();
	routeMotion();
	routeWindProfile();
	routeSchedules();
	routeUserProfiles();
	routeControl();
	routeSimulation();		// /api/sim/chart
	routeSimState();		// /api/sim/state
	routeControlSummary();	// /api/control/summary
	routeMetrics();			// /api/metrics
	routeLogs();
	routeReload();
	routeConfigSave(); 

	// --- v012 복구 라우트 추가 ---
	routeDiag();		 // /api/diag
	routeScan();		 // /api/scan
	routeConfigInit();	 // /api/config/init
	routeMotionFeed();	 // /api/motion/feed
	routeStaticAssets(); // W10_Web_Static_024.cpp
	routeUpload();		 // W10_Web_Upload_024.cpp
	routeUpdate();		 // W10_Web_Upload_024.cpp


	
	routeWebSocket();

	CL_D10_Logger::log(EN_L10_LOG_INFO, "[W10] WebAPI initialized (v023)");
}


// --------------------------------------------------
// 1. /api/version
// --------------------------------------------------
void CL_W10_WebAPI::routeVersion() {
	s_server->on("/api/version", HTTP_GET,
				 [](AsyncWebServerRequest* p_request) {
					 if (!checkApiKey(p_request)) {
						 p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
						 return;
					 }
					 JsonDocument v_doc;
					 v_doc["module"] = "SmartNatureWind";
					 v_doc["fw"]	 = A10_Const::FW_VERSION;

					 v_doc["logger"]  = "D10_Logger_016";
					 v_doc["control"] = "CT10_ControlManager_021";
					 v_doc["config"]  = "C10_ConfigManager_021";
					 v_doc["sim"]	  = "S10_Simulation_018";
					 v_doc["nvs"]	  = "N10_NvsManager_017";
					 v_doc["logger"]  = "D10_Logger_016";
					 v_doc["api"]	  = "W10_WebAPI_023"; // ✅ v023

					 sendJson(p_request, v_doc);
				 });
}

// --------------------------------------------------
// 2. /api/state
// --------------------------------------------------
void CL_W10_WebAPI::routeState() {
	s_server->on("/api/state", HTTP_GET,
				 [](AsyncWebServerRequest* p_request) {
					 if (!checkApiKey(p_request)) {
						 p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
						 return;
					 }
					 if (!s_control) {
						 p_request->send(500, "application/json", "{\"error\":\"control not ready\"}");
						 return;
					 }
					 JsonDocument v_doc;
					 s_control->toJson(v_doc);

					 // Motion 상태 직렬화 추가
					 if (g_A10_config_root.motion) {
						 JsonObject v_motion = v_doc["motion"].to<JsonObject>();
						 CL_C10_ConfigManager::toJson_Motion(*g_A10_config_root.motion, v_motion);
					 }

					 sendJson(p_request, v_doc);
				 });
}

// --------------------------------------------------
// 3. /api/system
// --------------------------------------------------
void CL_W10_WebAPI::routeSystem() {
	s_server->on("/api/system", HTTP_GET,
				 [](AsyncWebServerRequest* p_request) {
					 if (!checkApiKey(p_request)) {
						 p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
						 return;
					 }
					 JsonDocument v_doc;
					 CL_C10_ConfigManager::toJson_System(*g_A10_config_root.system, v_doc);
					 sendJson(p_request, v_doc);
				 });
	// ✅ HTTP_POST 핸들러 추가: 시스템 설정 수정 및 저장
	s_server->on("/api/system", HTTP_POST, [](AsyncWebServerRequest* p_request) {}, nullptr,
				 [](AsyncWebServerRequest* p_request, uint8_t* p_data, size_t p_len, size_t p_index, size_t p_total) {
		if (!checkApiKey(p_request)) {
			p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
			return;
		}
		if (p_index + p_len != p_total) return;

		JsonDocument v_doc;
		if (!parseJsonBody(p_request, p_data, p_len, v_doc)) {
			p_request->send(400, "application/json", "{\"error\":\"json parse\"}");
			return;
		}

		bool v_changed = false;
		if (g_A10_config_root.system) {
		    v_changed = CL_C10_ConfigManager::patchSystemFromJson(
                  *g_A10_config_root.system,
                  v_doc
            );
		}
		
		JsonDocument v_res;
		v_res["updated"] = v_changed;
		// 시스템 설정 변경은 재부팅이 필요할 수 있으므로, 재부팅 플래그를 추가할 수도 있음.
		// v_res["need_reboot"] = true; 
		sendJson(p_request, v_res);
	});
}

// --------------------------------------------------
// 4. /api/wifi
// --------------------------------------------------
void CL_W10_WebAPI::routeWifi() {
	s_server->on("/api/wifi", HTTP_GET,
				 [](AsyncWebServerRequest* p_request) {
					 if (!checkApiKey(p_request)) {
						 p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
						 return;
					 }
					 JsonDocument v_doc;
					 if (g_A10_config_root.wifi) {
						 CL_C10_ConfigManager::toJson_Wifi(*g_A10_config_root.wifi, v_doc);
					 }
					 sendJson(p_request, v_doc);
				 });
	// ✅ HTTP_POST 핸들러 추가: Wi-Fi 설정 수정 및 저장
	s_server->on("/api/wifi", HTTP_POST, [](AsyncWebServerRequest* p_request) {}, nullptr,
				 [](AsyncWebServerRequest* p_request, uint8_t* p_data, size_t p_len, size_t p_index, size_t p_total) {
		if (!checkApiKey(p_request)) {
			p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
			return;
		}
		if (p_index + p_len != p_total) return;

		JsonDocument v_doc;
		if (!parseJsonBody(p_request, p_data, p_len, v_doc)) {
			p_request->send(400, "application/json", "{\"error\":\"json parse\"}");
			return;
		}

		bool v_changed = false;
		if (g_A10_config_root.wifi) {
		    v_changed = CL_C10_ConfigManager::patchWifiFromJson(
                  *g_A10_config_root.wifi,
                  v_doc
            );
		}
		
		JsonDocument v_res;
		v_res["updated"] = v_changed;
		// Wi-Fi 설정 변경은 재부팅이 필요함
		v_res["need_reboot"] = true; 
		sendJson(p_request, v_res);
	});
}

// --------------------------------------------------
// 5. /api/motion
// --------------------------------------------------
void CL_W10_WebAPI::routeMotion() {
	s_server->on("/api/motion", HTTP_GET,
				 [](AsyncWebServerRequest* p_request) {
					 if (!checkApiKey(p_request)) {
						 p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
						 return;
					 }
					 JsonDocument v_doc;
					 if (g_A10_config_root.motion) {
						 JsonObject v_motion = v_doc["motion"].to<JsonObject>();
						 CL_C10_ConfigManager::toJson_Motion(*g_A10_config_root.motion, v_motion);
					 }
					 sendJson(p_request, v_doc);
				 });
}

// --------------------------------------------------
// 6. /api/windProfile
// --------------------------------------------------
void CL_W10_WebAPI::routeWindProfile() {
	s_server->on("/api/windProfile", HTTP_GET,
				 [](AsyncWebServerRequest* p_request) {
					 if (!checkApiKey(p_request)) {
						 p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
						 return;
					 }
					 JsonDocument			  v_doc;
					 ST_A10_WindProfileDict_t v_dict;
					 memset(&v_dict, 0, sizeof(v_dict));

					 if (!CL_C10_ConfigManager::loadWindProfileDict(v_dict)) {
						 p_request->send(500, "application/json", "{\"error\":\"load failed\"}");
						 return;
					 }

					 for (uint8_t v_i = 0; v_i < v_dict.preset_count; v_i++) {
						 const ST_A10_PresetEntry_t& v_p			= v_dict.presets[v_i];
						 JsonObject					   v_jp			= v_doc["windProfile"]["presets"][v_i];
						 v_jp["name"]								= v_p.name;
						 v_jp["code"]								= v_p.code;
						 v_jp["base"]["wind_intensity"]				= v_p.base.wind_intensity;
						 v_jp["base"]["gust_frequency"]				= v_p.base.gust_frequency;
						 v_jp["base"]["wind_variability"]			= v_p.base.wind_variability;
						 v_jp["base"]["fan_limit"]					= v_p.base.fan_limit;
						 v_jp["base"]["min_fan"]					= v_p.base.min_fan;
						 v_jp["base"]["turbulence_length_scale"]	= v_p.base.turbulence_length_scale;
						 v_jp["base"]["turbulence_intensity_sigma"] = v_p.base.turbulence_intensity_sigma;
						 v_jp["base"]["thermal_bubble_strength"]	= v_p.base.thermal_bubble_strength;
						 v_jp["base"]["thermal_bubble_radius"]		= v_p.base.thermal_bubble_radius;
					 }

					 for (uint8_t v_i = 0; v_i < v_dict.style_count; v_i++) {
						 const ST_A10_StyleEntry_t& v_s	   = v_dict.styles[v_i];
						 JsonObject					  v_js	   = v_doc["windProfile"]["styles"][v_i];
						 v_js["name"]						   = v_s.name;
						 v_js["code"]						   = v_s.code;
						 v_js["factors"]["intensity_factor"]   = v_s.factors.intensity_factor;
						 v_js["factors"]["variability_factor"] = v_s.factors.variability_factor;
						 v_js["factors"]["gust_factor"]		   = v_s.factors.gust_factor;
						 v_js["factors"]["thermal_factor"]	   = v_s.factors.thermal_factor;
					 }

					 sendJson(p_request, v_doc);
				 });
}


// --------------------------------------------------
// 7. /api/schedules (GET/POST)
// --------------------------------------------------
void CL_W10_WebAPI::routeSchedules() {
	// HTTP_GET 핸들러: 스케줄 설정 조회
	s_server->on("/api/schedules", HTTP_GET,
				 [](AsyncWebServerRequest* p_request) {
					 if (!checkApiKey(p_request)) {
						 p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
						 return;
					 }
					 JsonDocument v_doc;
					 CL_C10_ConfigManager::toJson_Schedules(*g_A10_config_root.schedules, v_doc);
					 sendJson(p_request, v_doc);
				 });

	// HTTP_POST 핸들러: 스케줄 설정 수정 및 저장
	s_server->on("/api/schedules", HTTP_POST, [](AsyncWebServerRequest* p_request) {}, nullptr,
				 [](AsyncWebServerRequest* p_request, uint8_t* p_data, size_t p_len, size_t p_index, size_t p_total) {
		if (!checkApiKey(p_request)) {
			p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
			return;
		}
		if (p_index + p_len != p_total) return;

		JsonDocument v_doc;
		if (!parseJsonBody(p_request, p_data, p_len, v_doc)) {
			p_request->send(400, "application/json", "{\"error\":\"json parse\"}");
			return;
		}

		bool v_changed = false;
		if (g_A10_config_root.schedules) {
		    v_changed = CL_C10_ConfigManager::patchSchedulesFromJson(
                  *g_A10_config_root.schedules,
                  v_doc
            );
		}
		
		// ✅ v024 일관성: sendJson 유틸리티 사용
		JsonDocument v_res;
		v_res["updated"] = v_changed;
		sendJson(p_request, v_res);
	});
}


// --------------------------------------------------
// 8. /api/user_profiles (GET/POST)
// --------------------------------------------------
void CL_W10_WebAPI::routeUserProfiles() {
	// HTTP_GET 핸들러: 사용자 프로필 설정 조회
	s_server->on("/api/user_profiles", HTTP_GET,
				 [](AsyncWebServerRequest* p_request) {
					 if (!checkApiKey(p_request)) {
						 p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
						 return;
					 }
					 JsonDocument v_doc;
					 CL_C10_ConfigManager::toJson_UserProfiles(*g_A10_config_root.userProfiles, v_doc);
					 sendJson(p_request, v_doc);
				 });
	
	// HTTP_POST 핸들러: 사용자 프로필 설정 수정 및 저장
	s_server->on("/api/user_profiles", HTTP_POST, [](AsyncWebServerRequest* p_request) {}, nullptr,
				 [](AsyncWebServerRequest* p_request, uint8_t* p_data, size_t p_len, size_t p_index, size_t p_total) {
		if (!checkApiKey(p_request)) {
			p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
			return;
		}
		if (p_index + p_len != p_total) return;

		JsonDocument v_doc;
		if (!parseJsonBody(p_request, p_data, p_len, v_doc)) {
			p_request->send(400, "application/json", "{\"error\":\"json parse\"}");
			return;
		}

		bool v_changed = false;
		if (g_A10_config_root.userProfiles) {
            // ✅ CL_C10_ConfigManager를 통해 패치 및 저장
            v_changed = CL_C10_ConfigManager::patchUserProfilesFromJson(
                *g_A10_config_root.userProfiles, // 현재 메모리상의 구조체
                v_doc                              // 웹에서 받은 JSON 데이터
            );
       }
		
		// ✅ v024 일관성: sendJson 유틸리티 사용
		JsonDocument v_res;
		v_res["updated"] = v_changed;
		sendJson(p_request, v_res);
	});
}

// 2. [신규] Config 저장 라우트 추가
void CL_W10_WebAPI::routeConfigSave() {
    // /api/config/save (POST)
    // Dirty 상태인 모든 설정을 파일로 저장합니다.
    s_server->on("/api/config/save", HTTP_POST, 
        [](AsyncWebServerRequest* p_request) {
            if (!checkApiKey(p_request)) {
                p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
                return;
            }
            
            CL_C10_ConfigManager::saveDirtyConfigs();
            p_request->send(200, "application/json", "{\"result\":\"saved\", \"status\":\"clean\"}");
        }
    );

    // /api/config/dirty (GET)
    // 현재 저장되지 않은 변경 사항 확인
    s_server->on("/api/config/dirty", HTTP_GET, 
        [](AsyncWebServerRequest* p_request) {
            if (!checkApiKey(p_request)) {
                p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
                return;
            }
            JsonDocument v_doc;
            CL_C10_ConfigManager::getDirtyStatus(v_doc);
            sendJson(p_request, v_doc);
        }
    );
}

// --------------------------------------------------
// 9. /api/control
// --------------------------------------------------
void CL_W10_WebAPI::routeControl() {
	// UserProfile 선택 (로직 생략)
	s_server->on("/api/control/profile/select", HTTP_POST,
				 [](AsyncWebServerRequest* p_request) {
					 if (!checkApiKey(p_request)) {
						 p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
						 return;
					 }
					 if (!s_control) {
						 p_request->send(500, "application/json", "{\"error\":\"control not ready\"}");
						 return;
					 }
					 if (!p_request->hasParam("id", true)) {
						 p_request->send(400, "application/json", "{\"error\":\"missing id\"}");
						 return;
					 }
					 int v_id = p_request->getParam("id", true)->value().toInt();
					 if (!s_control->startUserProfileByNo((uint8_t)v_id)) {
						 p_request->send(400, "application/json", "{\"error\":\"invalid profile\"}");
						 return;
					 }
					 p_request->send(200, "application/json", "{\"result\":\"ok\"}");
				 });

	// UserProfile 정지 (로직 생략)
	s_server->on("/api/control/profile/stop", HTTP_POST,
				 [](AsyncWebServerRequest* p_request) {
					 if (!checkApiKey(p_request)) {
						 p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
						 return;
					 }
					 if (!s_control) {
						 p_request->send(500, "application/json", "{\"error\":\"control not ready\"}");
						 return;
					 }
					 s_control->stopUserProfile();
					 p_request->send(200, "application/json", "{\"result\":\"ok\"}");
				 });

	// Override: fixed (로직 생략)
	s_server->on("/api/control/override/fixed", HTTP_POST,
				 [](AsyncWebServerRequest* p_request) {
					 if (!checkApiKey(p_request)) {
						 p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
						 return;
					 }
					 if (!s_control) {
						 p_request->send(500, "application/json", "{\"error\":\"control not ready\"}");
						 return;
					 }
					 if (!p_request->hasParam("percent", true) ||
						 !p_request->hasParam("seconds", true)) {
						 p_request->send(400, "application/json", "{\"error\":\"missing param\"}");
						 return;
					 }
					 float	  v_pct = p_request->getParam("percent", true)->value().toFloat();
					 uint32_t v_sec = (uint32_t)p_request->getParam("seconds", true)->value().toInt();
					 s_control->startOverrideFixed(v_pct, v_sec);
					 p_request->send(200, "application/json", "{\"result\":\"ok\"}");
				 });

	// Override: preset+style+adjust (JSON Body) (로직 생략)
	s_server->on("/api/control/override/preset", HTTP_POST, [](AsyncWebServerRequest* p_request) {}, nullptr, [](AsyncWebServerRequest* p_request, uint8_t* p_data, size_t p_len, size_t p_index, size_t p_total) {
		if (!checkApiKey(p_request)) {
			p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
			return;
		}
		if (!s_control) {
			p_request->send(500, "application/json", "{\"error\":\"control not ready\"}");
			return;
		}
		if (p_index + p_len != p_total) return;

		JsonDocument v_doc;
		if (!parseJsonBody(p_request, p_data, p_len, v_doc)) {
			p_request->send(400, "application/json", "{\"error\":\"json parse\"}");
			return;
		}

		const char* v_preset = v_doc["presetCode"] | "";
		const char* v_style  = v_doc["styleCode"]  | "BALANCE";
		uint32_t v_sec       = v_doc["durationSec"] | 0;

		ST_A10_AdjustDelta_t v_adj;
		memset(&v_adj, 0, sizeof(v_adj));
		if (v_doc["adjust"].is<JsonObject>()) {
			JsonObject v_aj = v_doc["adjust"];
			v_adj.wind_intensity   = v_aj["wind_intensity"]   | 0.0f;
			v_adj.wind_variability = v_aj["wind_variability"] | 0.0f;
			v_adj.gust_frequency   = v_aj["gust_frequency"]   | 0.0f;
			v_adj.fan_limit        = v_aj["fan_limit"]        | 0.0f;
			v_adj.min_fan          = v_aj["min_fan"]          | 0.0f;
		}

		s_control->startOverridePreset(v_preset, v_style, &v_adj, v_sec);
		p_request->send(200, "application/json", "{\"result\":\"ok\"}"); });

	// Override 해제 (로직 생략)
	s_server->on("/api/control/override/clear", HTTP_POST,
				 [](AsyncWebServerRequest* p_request) {
					 if (!checkApiKey(p_request)) {
						 p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
						 return;
					 }
					 if (!s_control) {
						 p_request->send(500, "application/json", "{\"error\":\"control not ready\"}");
						 return;
					 }
					 s_control->stopOverride();
					 p_request->send(200, "application/json", "{\"result\":\"ok\"}");
				 });
}

// --------------------------------------------------
// 10. /api/sim/chart
// --------------------------------------------------
void CL_W10_WebAPI::routeSimulation() {
	s_server->on("/api/sim/chart", HTTP_GET, [](AsyncWebServerRequest* p_request) {
		if (!checkApiKey(p_request)) {
			p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
			return;
		}
		if (!s_control) {
			p_request->send(500, "application/json", "{\"error\":\"control not ready\"}");
			return;
		}
		JsonDocument v_doc;
		s_control->toChartJson(v_doc);
		sendJson(p_request, v_doc);
	});
}

// --------------------------------------------------
// 11. /api/control/summary (신규 라우트)
// --------------------------------------------------
void CL_W10_WebAPI::routeControlSummary() {
	s_server->on("/api/control/summary", HTTP_GET,
				 [](AsyncWebServerRequest* p_request) {
					 if (!checkApiKey(p_request)) {
						 p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
						 return;
					 }
					 if (!s_control) {
						 p_request->send(500, "application/json", "{\"error\":\"control not ready\"}");
						 return;
					 }
					 JsonDocument v_doc;
					 s_control->toSummaryJson(v_doc);
					 s_control->toMetricsJson(v_doc);
					 sendJson(p_request, v_doc);
				 });
}

// --------------------------------------------------
// 12. /api/sim/state (신규 라우트)
// --------------------------------------------------
void CL_W10_WebAPI::routeSimState() {
	s_server->on("/api/sim/state", HTTP_GET,
				 [](AsyncWebServerRequest* p_request) {
					 if (!checkApiKey(p_request)) {
						 p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
						 return;
					 }
					 if (!s_control) {
						 p_request->send(500, "application/json", "{\"error\":\"control not ready\"}");
						 return;
					 }

					 JsonDocument v_doc;
					 // 시뮬레이션 상태 + 요약 통합
					 JsonObject v_sim = v_doc["sim"].to<JsonObject>();
					 s_control->sim.toJson(v_sim);

					 s_control->toSummaryJson(v_doc);
					 s_control->toMetricsJson(v_doc);

					 sendJson(p_request, v_doc);
				 });
}

// --------------------------------------------------
// 13. /api/metrics (신규 라우트)
// --------------------------------------------------
void CL_W10_WebAPI::routeMetrics() {
	s_server->on("/api/metrics", HTTP_GET,
				 [](AsyncWebServerRequest* p_request) {
					 if (!checkApiKey(p_request)) {
						 p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
						 return;
					 }
					 if (!s_control) {
						 p_request->send(500, "application/json", "{\"error\":\"control not ready\"}");
						 return;
					 }

					 JsonDocument v_doc;
					 s_control->toMetricsJson(v_doc);

					 // API 호출 시 WS로도 브로드캐스트
					 CL_W10_WebAPI::broadcastMetrics(v_doc, true);
					 CL_W10_WebAPI::broadcastChart(v_doc, true);

					 sendJson(p_request, v_doc);
				 });
}

// --------------------------------------------------
// 14. /api/logs
// --------------------------------------------------
void CL_W10_WebAPI::routeLogs() {
	s_server->on("/api/logs", HTTP_GET,
				 [](AsyncWebServerRequest* p_request) {
					 if (!checkApiKey(p_request)) {
						 p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
						 return;
					 }
					 JsonDocument v_doc;
					 CL_D10_Logger::getLogsAsJson(v_doc);
					 String v_json;
					 serializeJson(v_doc, v_json);
					 p_request->send(200, "application/json", v_json);
				 });
}

// --------------------------------------------------
// 15. /api/reload
// --------------------------------------------------
void CL_W10_WebAPI::routeReload() {
	s_server->on("/api/reload", HTTP_POST,
				 [](AsyncWebServerRequest* p_request) {
					 if (!checkApiKey(p_request)) {
						 p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
						 return;
					 }

					 ST_A10_ConfigRoot_t v_root;
					 bool			   v_ok = CL_C10_ConfigManager::loadAll(v_root);
					 if (!v_ok) {
						 p_request->send(500, "application/json", "{\"error\":\"reload failed\"}");
						 return;
					 }
					 g_A10_config_root = v_root;
					 p_request->send(200, "application/json", "{\"result\":\"ok\"}");
				 });
}


// --------------------------------------------------
// 16. /api/diag (v012 복구)
// --------------------------------------------------
void CL_W10_WebAPI::routeDiag() {
	s_server->on("/api/diag", HTTP_GET,
				 [](AsyncWebServerRequest* p_request) {
					 if (!checkApiKey(p_request)) {
						 p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
						 return;
					 }
					 JsonDocument v_doc;
					 v_doc["heap"]	  = ESP.getFreeHeap();
					 v_doc["fs_used"] = LittleFS.usedBytes();
					 v_doc["fs_total"] = LittleFS.totalBytes();

					 sendJson(p_request, v_doc);
				 });
}

// --------------------------------------------------
// 17. /api/scan (v012 복구)
// --------------------------------------------------
void CL_W10_WebAPI::routeScan() {
	s_server->on("/api/scan", HTTP_GET,
				 [](AsyncWebServerRequest* p_request) {
					 if (!checkApiKey(p_request)) {
						 p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
						 return;
					 }
					 JsonDocument v_doc;
                     CL_WF10_WiFiManager::scanNetworksToJson(v_doc); // ✅ JsonDocument를 인자로 전달
    
                     String v_json;
                     serializeJson(v_doc, v_json);

					 sendJson(p_request, v_doc);
    
				 });
}

// --------------------------------------------------
// 18. /api/config/init (v012 복구)
// --------------------------------------------------
void CL_W10_WebAPI::routeConfigInit() {
	s_server->on("/api/config/init", HTTP_POST,
				 [](AsyncWebServerRequest* p_request) {
					 if (!checkApiKey(p_request)) {
						 p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
						 return;
					 }
					 A10_resetToDefault(g_A10_config_root);
					 CL_C10_ConfigManager::saveAll(g_A10_config_root);

					 JsonDocument v_doc;
					 v_doc["factory"] = true;
					 sendJson(p_request, v_doc);

					 delay(200);
					 ESP.restart();
				 });
}

// --------------------------------------------------
// 19. /api/motion/feed (v012 복구)
// --------------------------------------------------
void CL_W10_WebAPI::routeMotionFeed() {
    // 1. PIR Feed API 등록: /api/motion/pir/feed (POST)
    s_server->on("/api/motion/pir/feed", HTTP_POST, [](AsyncWebServerRequest* p_request) {}, nullptr,
                 [](AsyncWebServerRequest* p_request, uint8_t* p_data, size_t p_len, size_t p_index, size_t p_total) {
        if (!checkApiKey(p_request)) {
            p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
            return;
        }
        if (p_index + p_len != p_total) return;

        JsonDocument v_doc;
        if (!parseJsonBody(p_request, p_data, p_len, v_doc)) {
            p_request->send(400, "application/json", "{\"error\":\"json parse\"}");
            return;
        }

        // PIR 상태만 피드
        if (s_control && s_control->motion) {
            s_control->motion->feedPIR(v_doc["pir"] | false);
        }

        JsonDocument v_res;
        v_res["fed"] = true;
        v_res["type"] = "pir";
        sendJson(p_request, v_res);
    });

    // 2. BLE Feed API 등록: /api/motion/ble/feed (POST)
    s_server->on("/api/motion/ble/feed", HTTP_POST, [](AsyncWebServerRequest* p_request) {}, nullptr,
                 [](AsyncWebServerRequest* p_request, uint8_t* p_data, size_t p_len, size_t p_index, size_t p_total) {
        if (!checkApiKey(p_request)) {
            p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
            return;
        }
        if (p_index + p_len != p_total) return;

        JsonDocument v_doc;
        if (!parseJsonBody(p_request, p_data, p_len, v_doc)) {
            p_request->send(400, "application/json", "{\"error\":\"json parse\"}");
            return;
        }

        // BLE 상태만 피드
        // NOTE: JSON 키는 "ble" 또는 "detected" 등 적절한 키를 사용해야 합니다.
        // 현재 로직을 단순화하여 "ble" 키를 사용합니다.
        if (s_control && s_control->motion) {
            s_control->motion->feedBLE(v_doc["ble"] | false);
        }

        JsonDocument v_res;
        v_res["fed"] = true;
        v_res["type"] = "ble";
        sendJson(p_request, v_res);
    });
}



// TODO 완성 필요 ... (routeSchedules와 routeUserProfiles 등의 POST 핸들러 내에서 sendJson/sendText로 변경 필요 - 이 부분은 생략하고, 다음 단계에서 필요 시 수정 검토) ...


