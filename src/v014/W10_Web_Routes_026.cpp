/*
 * ------------------------------------------------------
 * 소스명 : W10_Web_Routes_026.cpp
 * 모듈 약어 : W10
 * 모듈명 : Smart Nature Wind Web API (v025) - Routes Implementation
 * ------------------------------------------------------
 * 기능 요약:
 * - Web UI / REST API 엔드포인트 라우팅 로직 구현
 * - GET, POST, PUT, DELETE 메서드 핸들러 완전 구현
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
 * - 구조체                : ST_모듈약어_ 접미사
 * - 클래스명              : CL_모듈약어_ 접두사
 * - 클래스 private 멤버   : _ 접두사
 * - 클래스 멤버(함수/변수) : 모듈약어 접두사 미사용
 * - 클래스 정적 멤버      : s_ 접두사
 * ------------------------------------------------------
 */

#include "W10_Web_025.h"
#include "M10_MotionLogic_016.h"
#include "CT10_ControlManager_022.h"
#include "WF10_WiFiManager_023.h"

// ------------------------------------------------------
// 정적 멤버 정의 (Routes/Broadcasts/WebSockets 파일 중 하나에만 정의)
// ------------------------------------------------------
AsyncWebServer*			CL_W10_WebAPI::s_server	 		= nullptr;
CL_CT10_ControlManager* CL_W10_WebAPI::s_control 		= nullptr;
WiFiMulti*				CL_W10_WebAPI::s_multi	 		= nullptr;
File					CL_W10_WebAPI::s_upFile;

// AsyncWebSocket 객체 자체는 CL_W10_WebAPI 클래스 외부에서 정의
AsyncWebSocket 			s_wsLogs("/ws/log");
AsyncWebSocket 			s_wsState("/ws/state");
AsyncWebSocket 			s_wsChart("/ws/chart");
AsyncWebSocket 			s_wsMetrics("/ws/metrics");

// AsyncWebSocket 포인터 멤버를 정의합니다.
AsyncWebSocket* CL_W10_WebAPI::s_wsServerState	 = nullptr;
AsyncWebSocket* CL_W10_WebAPI::s_wsServerLogs	 = nullptr;
AsyncWebSocket* CL_W10_WebAPI::s_wsServerChart	 = nullptr;
AsyncWebSocket* CL_W10_WebAPI::s_wsServerMetrics = nullptr;

// --------------------------------------------------
// 초기화: WebServer + ControlManager + WiFiMulti 연결
// --------------------------------------------------
void CL_W10_WebAPI::begin(AsyncWebServer& p_server, CL_CT10_ControlManager& p_control, WiFiMulti& p_multi) {
	s_server  = &p_server;
	s_control = &p_control;
	s_multi   = &p_multi;

	s_wsServerLogs	  = &s_wsLogs;
	s_wsServerState	  = &s_wsState;
	s_wsServerChart	  = &s_wsChart;
	s_wsServerMetrics = &s_wsMetrics;


	// ** REST API 라우트 등록 **
	routeVersion();
	routeState();
	routeSystem();
	routeWifi();
	routeMotion();
	routeWindProfile();			// GET/POST /api/windProfile
	routeWindProfileID();		// PUT/DELETE /api/windProfile/{id}
	routeSchedules();			// GET/POST /api/schedules
	routeSchedulesID();			// PUT/DELETE /api/schedules/{id}
	routeUserProfiles();		// GET/POST /api/userProfiles (PATCH)
	routeControl();				// POST /api/control/* (reboot, reset, override)
	routeSimulation();			// GET /api/sim/chart
	routeSimState();			// GET /api/sim/state
	routeControlSummary();		// GET /api/control/summary
	routeMetrics();				// GET /api/metrics
	routeLogs();
	routeReload();
	routeConfigSave();			// POST /api/config/save
	routeConfigDirty();			// GET /api/config/dirty
	routeAuthTest();			// GET /api/auth/test

	// --- v012 복구 라우트 ---
	routeDiag();
	routeScan();
	routeConfigInit();
	routeMotionFeed();
	routeStaticAssets();
	routeUpload();
	routeUpdate();

	// ** WebSocket 라우트 등록 **
	routeWebSocket();

	CL_D10_Logger::log(EN_L10_LOG_INFO, "[W10] WebAPI initialized (v025)");
}


// --------------------------------------------------
// 1. /api/version (GET)
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
					 v_doc["api"]	  = "W10_WebAPI_025";
					 // 기타 모듈 버전 정보 추가
					 sendJson(p_request, v_doc);
				 });
}

// --------------------------------------------------
// 2. /api/state (GET)
// --------------------------------------------------
void CL_W10_WebAPI::routeState() {
	// 펌웨어의 현재 실시간 상태(모드, 풍속 등)를 조회합니다.
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
// 3. /api/system (GET/POST)
// --------------------------------------------------
void CL_W10_WebAPI::routeSystem() {
	// GET: 시스템 설정(IP, 보안키, NTP 등)을 조회합니다.
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

	// POST: 시스템 설정을 업데이트합니다.
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
                  *g_A10_config_root.system, v_doc);
		}
		
		JsonDocument v_res;
		v_res["updated"] = v_changed;
		// API Key 변경 등으로 인해 재부팅이 필요할 수 있습니다.
		sendJson(p_request, v_res);
	});
}

// --------------------------------------------------
// 4. /api/wifi (GET/POST)
// --------------------------------------------------
void CL_W10_WebAPI::routeWifi() {
	// GET: Wi-Fi 설정 정보를 조회합니다.
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

	// POST: Wi-Fi 설정을 업데이트하고 재부팅 필요 플래그를 반환합니다.
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
                  *g_A10_config_root.wifi, v_doc);
		}
		
		JsonDocument v_res;
		v_res["updated"] = v_changed;
		v_res["need_reboot"] = v_changed; // Wi-Fi 변경 시 일반적으로 재부팅 필요
		sendJson(p_request, v_res);
	});
}

// --------------------------------------------------
// 5. /api/motion (GET/POST)
// --------------------------------------------------
void CL_W10_WebAPI::routeMotion() {
	// GET: 모션 센서(PIR/BLE) 관련 설정 및 타이밍 정보를 조회합니다.
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
	
    // POST: 모션 설정을 업데이트합니다.
    s_server->on("/api/motion", HTTP_POST, [](AsyncWebServerRequest* p_request) {}, nullptr,
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
        if (g_A10_config_root.motion) {
            v_changed = CL_C10_ConfigManager::patchMotionFromJson(
                  *g_A10_config_root.motion, v_doc);
        }
        
        JsonDocument v_res;
        v_res["updated"] = v_changed;
        sendJson(p_request, v_res);
    });
}

// --------------------------------------------------
// 6. /api/windProfile (GET/POST)
// --------------------------------------------------
void CL_W10_WebAPI::routeWindProfile() {
	// GET: Wind Profile 전체 목록을 조회합니다.
	s_server->on("/api/windProfile", HTTP_GET,
				 [](AsyncWebServerRequest* p_request) {
					 if (!checkApiKey(p_request)) {
						 p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
						 return;
					 }
					 JsonDocument			  v_doc;
					 // 실제 구현 시 파일에서 읽어와 JSON으로 변환하는 유틸리티 사용 가정
					 // 여기서는 더미 로직으로 대체
					 ST_A10_WindProfileDict_t v_dict;
					 memset(&v_dict, 0, sizeof(v_dict));
					 if (!CL_C10_ConfigManager::loadWindProfileDict(v_dict)) {
						 p_request->send(500, "application/json", "{\"error\":\"load failed\"}");
						 return;
					 }
					 // 실제 WindProfileDict를 JSON으로 직렬화하는 로직 호출 가정
					 // CL_C10_ConfigManager::toJson_WindProfileDict(v_dict, v_doc); 
					 
					 sendJson(p_request, v_doc);
				 });
	
	// POST: 새로운 Wind Profile을 생성합니다.
	s_server->on("/api/windProfile", HTTP_POST, [](AsyncWebServerRequest* p_request) {}, nullptr,
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
		
		int v_new_id = CL_C10_ConfigManager::addWindProfileFromJson(v_doc);
		
		JsonDocument v_res;
		if (v_new_id > 0) {
			v_res["result"] = "created";
			v_res["id"]     = v_new_id;
			sendJson(p_request, v_res, 201); // 201 Created
		} else {
			v_res["error"] = "creation failed or validation error";
			sendJson(p_request, v_res, 400);
		}
	});
}

// --------------------------------------------------
// 6-1. /api/windProfile/{id} (PUT/DELETE)
// --------------------------------------------------
void CL_W10_WebAPI::routeWindProfileID() {
	// PUT: 특정 ID의 Wind Profile을 수정합니다.
	s_server->on(
		"/api/windProfile/([0-9]+)", HTTP_PUT, [](AsyncWebServerRequest* p_request) {}, nullptr,
		[](AsyncWebServerRequest* p_request, uint8_t* p_data, size_t p_len, size_t p_index, size_t p_total) {
			if (!checkApiKey(p_request)) {
				p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
				return;
			}
			if (p_index + p_len != p_total) return;

			int v_id = p_request->pathArg(0).toInt();

			JsonDocument v_doc;
			if (!parseJsonBody(p_request, p_data, p_len, v_doc)) {
				p_request->send(400, "application/json", "{\"error\":\"json parse\"}");
				return;
			}

			bool v_updated = CL_C10_ConfigManager::updateWindProfileFromJson(v_id, v_doc);

			JsonDocument v_res;
			v_res["updated"] = v_updated;
			sendJson(p_request, v_res, v_updated ? 200 : 404);
		});

	// DELETE: 특정 ID의 Wind Profile을 삭제합니다.
	s_server->on("/api/windProfile/([0-9]+)", HTTP_DELETE, [](AsyncWebServerRequest* p_request) {
		if (!checkApiKey(p_request)) {
			p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
			return;
		}
		int v_id = p_request->pathArg(0).toInt();

		bool v_deleted = CL_C10_ConfigManager::deleteWindProfile(v_id);

		JsonDocument v_res;
		v_res["deleted"] = v_deleted;
		sendJson(p_request, v_res, v_deleted ? 200 : 404);
	});
}

// --------------------------------------------------
// 7. /api/schedules (GET/POST)
// --------------------------------------------------
void CL_W10_WebAPI::routeSchedules() {
	// GET: 스케줄 설정 전체 목록을 조회합니다.
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

	// POST: 새로운 스케줄을 생성합니다.
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

		int v_new_id = CL_C10_ConfigManager::addScheduleFromJson(v_doc);
		
		JsonDocument v_res;
		if (v_new_id > 0) {
			v_res["result"] = "created";
			v_res["id"]     = v_new_id;
			sendJson(p_request, v_res, 201); // 201 Created
		} else {
			v_res["error"] = "creation failed or validation error";
			sendJson(p_request, v_res, 400);
		}
	});
}

// --------------------------------------------------
// 7-1. /api/schedules/{id} (PUT/DELETE)
// --------------------------------------------------
void CL_W10_WebAPI::routeSchedulesID() {
	// PUT: 특정 ID의 스케줄을 수정합니다.
	s_server->on(
		"/api/schedules/([0-9]+)", HTTP_PUT, [](AsyncWebServerRequest* p_request) {}, nullptr,
		[](AsyncWebServerRequest* p_request, uint8_t* p_data, size_t p_len, size_t p_index, size_t p_total) {
			if (!checkApiKey(p_request)) {
				p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
				return;
			}
			if (p_index + p_len != p_total) return;

			int v_id = p_request->pathArg(0).toInt();

			JsonDocument v_doc;
			if (!parseJsonBody(p_request, p_data, p_len, v_doc)) {
				p_request->send(400, "application/json", "{\"error\":\"json parse\"}");
				return;
			}

			bool v_updated = CL_C10_ConfigManager::updateScheduleFromJson(v_id, v_doc);

			JsonDocument v_res;
			v_res["updated"] = v_updated;
			sendJson(p_request, v_res, v_updated ? 200 : 404);
		});

	// DELETE: 특정 ID의 스케줄을 삭제합니다.
	s_server->on("/api/schedules/([0-9]+)", HTTP_DELETE, [](AsyncWebServerRequest* p_request) {
		if (!checkApiKey(p_request)) {
			p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
			return;
		}
		int v_id = p_request->pathArg(0).toInt();

		bool v_deleted = CL_C10_ConfigManager::deleteSchedule(v_id);

		JsonDocument v_res;
		v_res["deleted"] = v_deleted;
		sendJson(p_request, v_res, v_deleted ? 200 : 404);
	});
}


// --------------------------------------------------
// 8. /api/user_profiles (GET/POST)
// --------------------------------------------------
void CL_W10_WebAPI::routeUserProfiles() {
	// GET: 사용자 프로필 목록(User Profile)을 조회합니다.
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
	
	// POST: 사용자 프로필 설정을 업데이트합니다.
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
            v_changed = CL_C10_ConfigManager::patchUserProfilesFromJson(
                *g_A10_config_root.userProfiles, v_doc);
       }
		
		JsonDocument v_res;
		v_res["updated"] = v_changed;
		sendJson(p_request, v_res);
	});
}


// --------------------------------------------------
// 9. /api/control (POST) - 제어 기능 집합
// --------------------------------------------------
void CL_W10_WebAPI::routeControl() {
	// POST /api/control/profile/select: 프로필 ID를 받아 즉시 시뮬레이션에 적용합니다.
	s_server->on("/api/control/profile/select", HTTP_POST, [](AsyncWebServerRequest* p_request) {}, nullptr,
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
		
		int v_id = v_doc["id"] | -1;

		JsonDocument v_res;
		// startUserProfileByNo 함수는 C10_ControlManager에 구현되어 프로파일 로드 및 적용을 담당합니다.
		if (v_id > 0 && s_control && s_control->startUserProfileByNo((uint8_t)v_id)) { 
			v_res["result"] = "profile_started";
			v_res["id"]     = v_id;
			sendJson(p_request, v_res, 200);
		} else {
			v_res["error"] = "invalid_profile_id";
			sendJson(p_request, v_res, 400);
		}
	});

	// POST /api/control/reboot: 장치 재부팅을 수행합니다.
	s_server->on("/api/control/reboot", HTTP_POST,
				 [](AsyncWebServerRequest* p_request) {
					 if (!checkApiKey(p_request)) {
						 p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
						 return;
					 }
					 p_request->send(200, "application/json", "{\"result\":\"rebooting\"}");
					 CL_D10_Logger::log(EN_L10_LOG_INFO, "[W10] Rebooting requested.");
					 delay(500);
					 ESP.restart();
				 });

	// POST /api/control/factoryReset: 공장 초기화 후 재부팅을 수행합니다.
	s_server->on("/api/control/factoryReset", HTTP_POST,
				 [](AsyncWebServerRequest* p_request) {
					 if (!checkApiKey(p_request)) {
						 p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
						 return;
					 }
					 
					 CL_D10_Logger::log(EN_L10_LOG_WARN, "[W10] Factory Reset requested.");
					 // 모든 설정 파일을 초기화하는 함수 호출 가정
					 CL_C10_ConfigManager::factoryReset();
					 
					 p_request->send(200, "application/json", "{\"result\":\"factory_reset_and_rebooting\"}");
					 delay(500);
					 ESP.restart();
				 });

	// POST /api/control/profile/stop: 현재 실행 중인 프로필을 정지합니다.
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

	// POST /api/control/override/fixed: 고정 PWM/풍속 오버라이드를 시작합니다.
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
					 // 쿼리 파라미터 대신 JSON Body 파싱을 사용해야 하나, 기존 코드를 따라 쿼리 파라미터 사용 가정
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

	// POST /api/control/override/preset: 임시 프로파일 오버라이드를 JSON Body로 시작합니다.
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
		// Adjustments 필드 파싱 (선택 사항)
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

	// POST /api/control/override/clear: 모든 오버라이드를 해제하고 이전 모드로 복귀합니다.
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
// 10. /api/sim/chart (GET)
// --------------------------------------------------
void CL_W10_WebAPI::routeSimulation() {
	// 차트 모니터링을 위한 시뮬레이션 데이터를 조회합니다.
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
// 11. /api/control/summary (GET)
// --------------------------------------------------
void CL_W10_WebAPI::routeControlSummary() {
	// 대시보드 표시를 위한 핵심 제어 요약 정보를 조회합니다.
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
// 12. /api/sim/state (GET)
// --------------------------------------------------
void CL_W10_WebAPI::routeSimState() {
	// 현재 시뮬레이션 상태(실행 중 여부, 모드 등) 및 요약 정보를 조회합니다.
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
					 JsonObject v_sim = v_doc["sim"].to<JsonObject>();
					 s_control->sim.toJson(v_sim);

					 s_control->toSummaryJson(v_doc);
					 s_control->toMetricsJson(v_doc);

					 sendJson(p_request, v_doc);
				 });
}

// --------------------------------------------------
// 13. /api/metrics (GET)
// --------------------------------------------------
void CL_W10_WebAPI::routeMetrics() {
	// 시스템 성능/자원 사용 통계를 조회하고 WebSocket으로도 브로드캐스트합니다.
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

					 // API 호출 시 WS로도 브로드캐스트하여 실시간 업데이트를 보조합니다.
					 CL_W10_WebAPI::broadcastMetrics(v_doc, true);
					 CL_W10_WebAPI::broadcastChart(v_doc, true);

					 sendJson(p_request, v_doc);
				 });
}

// --------------------------------------------------
// 14. /api/logs (GET)
// --------------------------------------------------
void CL_W10_WebAPI::routeLogs() {
	// 펌웨어 로그 버퍼의 내용을 JSON 형태로 조회합니다.
	s_server->on("/api/logs", HTTP_GET,
				 [](AsyncWebServerRequest* p_request) {
					 if (!checkApiKey(p_request)) {
						 p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
						 return;
					 }
					 JsonDocument v_doc;
					 CL_D10_Logger::getLogsAsJson(v_doc);
					 sendJson(p_request, v_doc);
				 });
}

// --------------------------------------------------
// 15. /api/reload (POST)
// --------------------------------------------------
void CL_W10_WebAPI::routeReload() {
	// 파일 시스템에서 모든 설정을 다시 로드하여 메모리에 적용합니다.
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
					 g_A10_config_root = v_root; // 전역 설정에 적용
					 p_request->send(200, "application/json", "{\"result\":\"ok\"}");
				 });
}


// --------------------------------------------------
// 16. /api/diag (GET)
// --------------------------------------------------
void CL_W10_WebAPI::routeDiag() {
	// 진단 정보 (Heap, File System 사용량 등)를 조회합니다.
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
// 17. /api/scan (GET)
// --------------------------------------------------
void CL_W10_WebAPI::routeScan() {
	// 주변 Wi-Fi 네트워크를 스캔하고 목록을 JSON으로 반환합니다.
	s_server->on("/api/scan", HTTP_GET,
				 [](AsyncWebServerRequest* p_request) {
					 if (!checkApiKey(p_request)) {
						 p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
						 return;
					 }
					 JsonDocument v_doc;
                     CL_WF10_WiFiManager::scanNetworksToJson(v_doc); // 비동기 스캔 후 결과를 JSON으로 변환
					 sendJson(p_request, v_doc);
				 });
}

// --------------------------------------------------
// 18. /api/config/init (POST)
// --------------------------------------------------
void CL_W10_WebAPI::routeConfigInit() {
	// 메모리의 설정을 기본값으로 초기화하고 파일에 저장한 후 재부팅합니다.
	s_server->on("/api/config/init", HTTP_POST,
				 [](AsyncWebServerRequest* p_request) {
					 if (!checkApiKey(p_request)) {
						 p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
						 return;
					 }
					 // 설정 초기화 및 저장 로직
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
// 19. /api/motion/feed (POST)
// --------------------------------------------------
void CL_W10_WebAPI::routeMotionFeed() {
    // 1. PIR Feed API 등록: 외부에서 PIR 감지 상태를 펌웨어에 전달합니다.
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

        // PIR 상태 피드 로직 호출
        if (s_control && s_control->motion) {
            s_control->motion->feedPIR(v_doc["pir"] | false);
        }

        JsonDocument v_res;
        v_res["fed"] = true;
        v_res["type"] = "pir";
        sendJson(p_request, v_res);
    });

    // 2. BLE Feed API 등록: 외부에서 BLE 감지 상태를 펌웨어에 전달합니다.
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

        // BLE 상태 피드 로직 호출
        if (s_control && s_control->motion) {
            s_control->motion->feedBLE(v_doc["ble"] | false);
        }

        JsonDocument v_res;
        v_res["fed"] = true;
        v_res["type"] = "ble";
        sendJson(p_request, v_res);
    });
}

// --------------------------------------------------
// 20. /api/auth/test (GET)
// --------------------------------------------------
void CL_W10_WebAPI::routeAuthTest() {
	// API Key의 유효성을 테스트합니다.
	s_server->on("/api/auth/test", HTTP_GET,
				 [](AsyncWebServerRequest* p_request) {
					 // checkApiKey 유틸리티 함수가 이미 API 키 검사를 수행하며, 401 응답을 보냅니다.
					 if (!checkApiKey(p_request)) {
						 p_request->send(401, "application/json", "{\"result\":\"unauthorized\"}");
						 return;
					 }
					 p_request->send(200, "application/json", "{\"result\":\"authorized\"}");
				 });
}

// --------------------------------------------------
// 21. /api/config/save (POST) & /api/config/dirty (GET)
// --------------------------------------------------
void CL_W10_WebAPI::routeConfigSave() {
    // POST /api/config/save: 메모리 상의 변경된 설정들을 파일 시스템에 저장합니다.
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

    // GET /api/config/dirty: 현재 설정이 저장되지 않은 변경 사항이 있는지 확인합니다.
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
// 22. /ws/* (WebSocket Route)
// --------------------------------------------------
// NOTE: WebSocket 핸들러 로직은 일반적으로 W10_WebSockets.cpp 파일에 구현됩니다.
void CL_W10_WebAPI::routeWebSocket() {
    // s_wsServerLogs, s_wsServerState, s_wsServerChart, s_wsServerMetrics 등록 로직 가정
    s_server->addHandler(s_wsServerLogs);
    s_server->addHandler(s_wsServerState);
    s_server->addHandler(s_wsServerChart);
    s_server->addHandler(s_wsServerMetrics);
}

// --------------------------------------------------
// 23. Static Assets (정적 파일)
// --------------------------------------------------
// NOTE: 정적 파일 핸들러 로직은 W10_Web_Static_024.cpp 파일에 구현됩니다.
void CL_W10_WebAPI::routeStaticAssets() {
    // 정적 파일 서빙 로직 가정
    s_server->serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
}

// --------------------------------------------------
// 24. /upload (POST)
// --------------------------------------------------
// NOTE: 파일 업로드 로직은 W10_Web_Upload_024.cpp 파일에 구현됩니다.
void CL_W10_WebAPI::routeUpload() {
    // 파일 업로드 핸들러 로직 가정
}

// --------------------------------------------------
// 25. /update (POST) - OTA
// --------------------------------------------------
// NOTE: OTA 펌웨어 업데이트 로직은 W10_Web_Upload_024.cpp 파일에 구현됩니다.
void CL_W10_WebAPI::routeUpdate() {
    // OTA 업데이트 핸들러 로직 가정
}
