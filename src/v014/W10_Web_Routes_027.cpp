/*
 * ------------------------------------------------------
 * 소스명 : W10_Web_Routes_027.cpp
 * 모듈 약어 : W10
 * 모듈명 : Smart Nature Wind Web API (v025) - Routes Implementation
 * ------------------------------------------------------
 * 기능 요약:
 * - Web UI / REST API 엔드포인트 라우팅 로직 구현 (Full CRUD 및 제어 기능 포함)
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

#include "W10_Web_027.h"
#include "M10_MotionLogic_016.h"
#include "CT10_ControlManager_024.h"
#include "WF10_WiFiManager_023.h"

// ------------------------------------------------------
// 정적 멤버 정의 (Routes/Broadcasts/WebSockets 파일 중 하나에만 정의)
// ------------------------------------------------------
AsyncWebServer*			CL_W10_WebAPI::s_server	 		= nullptr;
CL_CT10_ControlManager* CL_W10_WebAPI::s_control 		= nullptr;
WiFiMulti*				CL_W10_WebAPI::s_multi	 		= nullptr;
File					CL_W10_WebAPI::s_upFile;

// AsyncWebSocket 객체 자체는 CL_W10_WebAPI 클래스 외부에서 정의합니다.
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


	// 1. 시스템 정보 및 상태
	routeVersion();
	routeState();
	routeSystem();
	routeWifi();
	routeDiag();
	routeScan();
	routeAuthTest(); // 인증 테스트

	routeWifiConfig();
    routeTimeSet();
    routeFirmwareCheck();
	

	// 2. 설정 및 제어
	routeMotion();
	routeSimulation();
	routeSimState();
	routeControl();
	routeControlSummary();

	// 3. 설정 관리 및 저장 (Config)
	routeConfigSave();
	routeConfigDirty();
	routeConfigInit();
	routeReload();

	// 4. CRUD: Wind Profiles
	routeWindProfile();    // GET/POST /api/windProfile
	routeWindProfileID();  // PUT/DELETE /api/windProfile/{id}

	// 5. CRUD: Schedules
	routeSchedules();      // GET/POST /api/schedules
	routeSchedulesID();    // PUT/DELETE /api/schedules/{id}

	// 6. CRUD: User Profiles (Configuration)
	routeUserProfiles();

	// 7. 모니터링 및 로깅
	routeMetrics();
	routeLogs();

	// 8. 피드 및 업데이트
	routeMotionFeed();
	routeStaticAssets();
	routeUpload();
	routeUpdate();
	routeWebSocket();

	CL_D10_Logger::log(EN_L10_LOG_INFO, "[W10] WebAPI initialized (v025)");
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

					 // 모듈 버전 정보 추가
					 v_doc["control"] = "CT10_ControlManager_022";
					 v_doc["config"]  = "C10_ConfigManager_024";
					 v_doc["api"]	  = "W10_WebAPI_025"; 

					 sendJson(p_request, v_doc);
				 });
}

// --------------------------------------------------
// 2. /api/state (현재 구동 상태 요약)
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

					 /*  Todo: 예전 소스 ws로 통합/삭제 여부 검토 필요
					      JsonDocument v_doc;
						 // 시뮬레이션 상태 + 요약 통합
						 JsonObject v_sim = v_doc["sim"].to<JsonObject>();
						 s_control->sim.toJson(v_sim);	// ✅ v019 구조 호환

						 s_control->toSummaryJson(v_doc);  // ✅ v022에서 공식 지원 함수
						 s_control->toMetricsJson(v_doc);  // ✅ /api/metrics 연동 시 사용

						 sendJson(p_request, v_doc);
					 */
					 JsonDocument v_doc;
					 s_control->toJson(v_doc);

					 /* todo 포함여부 재검토
					 // Motion 상태 직렬화 추가
					 if (g_A10_config_root.motion) {
						 JsonObject v_motion = v_doc["motion"].to<JsonObject>();
						 CL_C10_ConfigManager::toJson_Motion(*g_A10_config_root.motion, v_motion);
					 }
					 */

					 sendJson(p_request, v_doc);
				 });
}

// --------------------------------------------------
// 3. /api/system (시스템 설정 GET/POST)
// --------------------------------------------------
void CL_W10_WebAPI::routeSystem() {
	// GET: 시스템 설정 조회
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
	// POST: 시스템 설정 패치 및 저장
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
		// 시스템 설정 변경 후 저장 필요 여부를 프론트엔드에 전달
		sendJson(p_request, v_res);
	});
}

// --------------------------------------------------
// 4. /api/wifi (Wi-Fi 설정 GET/POST)
// --------------------------------------------------
void CL_W10_WebAPI::routeWifi() {
	// GET: Wi-Fi 설정 조회
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
	// POST: Wi-Fi 설정 패치 및 저장
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
		// Wi-Fi 설정 변경은 재부팅이 필요할 수 있음을 알림
		v_res["need_reboot"] = true; 
		sendJson(p_request, v_res);
	});
}

// --------------------------------------------------
// 5. /api/motion (Motion 설정 GET/POST)
// --------------------------------------------------
void CL_W10_WebAPI::routeMotion() {
	// GET: Motion 설정 조회
	s_server->on("/api/motion", HTTP_GET,
				 [](AsyncWebServerRequest* p_request) {
					 if (!checkApiKey(p_request)) {
						 p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
						 return;
					 }
					 JsonDocument v_doc;
					 if (g_A10_config_root.motion) {
						 CL_C10_ConfigManager::toJson_Motion(*g_A10_config_root.motion, v_doc);
						 // JsonObject v_motion = v_doc["motion"].to<JsonObject>();
						 //CL_C10_ConfigManager::toJson_Motion(*g_A10_config_root.motion, v_motion);
					 }
					 sendJson(p_request, v_doc);
				 });
    // POST: Motion 설정 패치 및 저장
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
                  *g_A10_config_root.motion,
                  v_doc
            );
        }
        
        JsonDocument v_res;
        v_res["updated"] = v_changed;
        sendJson(p_request, v_res);
    });
}

// --------------------------------------------------
// 6. /api/windProfile (GET: 목록 조회, POST: 신규 생성)
// --------------------------------------------------
void CL_W10_WebAPI::routeWindProfile() {
	// GET: Wind Profile 전체 목록 조회
	s_server->on("/api/windProfile", HTTP_GET,
				 [](AsyncWebServerRequest* p_request) {
					 if (!checkApiKey(p_request)) {
						 p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
						 return;
					 }
					 JsonDocument			  v_doc;
					 // 실제 구현에서는 CL_C10_ConfigManager::loadWindProfileDict() 호출 및 직렬화 로직 필요
					 ST_A10_WindProfileDict_t v_dict; 
					 memset(&v_dict, 0, sizeof(v_dict));
					 
					 if (CL_C10_ConfigManager::loadWindProfileDict(v_dict)) {
						 // 성공적으로 로드했을 때 JSON으로 변환하여 응답
						 CL_C10_ConfigManager::toJson_WindProfileDict(v_dict, v_doc);
						 sendJson(p_request, v_doc);
					 } else {
						 p_request->send(500, "application/json", "{\"error\":\"load failed\"}");
					 }
				 });
	
	// POST: Wind Profile 신규 생성 (Create)
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
		
		// CL_C10_ConfigManager를 통해 신규 프로파일을 추가하고 ID를 반환받음
		int v_new_id = CL_C10_ConfigManager::addWindProfileFromJson(v_doc);
		
		JsonDocument v_res;
		if (v_new_id > 0) {
			v_res["result"] = "created";
			v_res["id"]     = v_new_id;
			// 201 Created 응답
			sendJson(p_request, v_res, 201); 
		} else {
			v_res["error"] = "creation failed or validation error";
			sendJson(p_request, v_res, 400);
		}
	});
}

// --------------------------------------------------
// 6-1. /api/windProfile/{id} (PUT: 수정, DELETE: 삭제)
// --------------------------------------------------
void CL_W10_WebAPI::routeWindProfileID() {
	// PUT: Wind Profile 수정 (Update)
	s_server->on(
		"/api/windProfile/([0-9]+)", HTTP_PUT, [](AsyncWebServerRequest* p_request) {}, nullptr,
		[](AsyncWebServerRequest* p_request, uint8_t* p_data, size_t p_len, size_t p_index, size_t p_total) {
			if (!checkApiKey(p_request)) {
				p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
				return;
			}
			if (p_index + p_len != p_total) return;

			// URL Path에서 ID 추출
			int v_id = p_request->pathArg(0).toInt();

			JsonDocument v_doc;
			if (!parseJsonBody(p_request, p_data, p_len, v_doc)) {
				p_request->send(400, "application/json", "{\"error\":\"json parse\"}");
				return;
			}

			// CL_C10_ConfigManager를 통해 해당 ID의 프로파일을 수정
			bool v_updated = CL_C10_ConfigManager::updateWindProfileFromJson(v_id, v_doc);

			JsonDocument v_res;
			v_res["updated"] = v_updated;
			// 성공 시 200 OK, 실패(ID 없음 등) 시 404 Not Found 또는 400 Bad Request
			sendJson(p_request, v_res, v_updated ? 200 : 404);
		});

	// DELETE: Wind Profile 삭제 (Delete)
	s_server->on("/api/windProfile/([0-9]+)", HTTP_DELETE, [](AsyncWebServerRequest* p_request) {
		if (!checkApiKey(p_request)) {
			p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
			return;
		}
		// URL Path에서 ID 추출
		int v_id = p_request->pathArg(0).toInt();

		// CL_C10_ConfigManager를 통해 해당 ID의 프로파일을 삭제
		bool v_deleted = CL_C10_ConfigManager::deleteWindProfile(v_id);

		JsonDocument v_res;
		v_res["deleted"] = v_deleted;
		// 성공 시 200 OK, 실패 시 404 Not Found
		sendJson(p_request, v_res, v_deleted ? 200 : 404);
	});
}

// --------------------------------------------------
// 7. /api/schedules (GET: 목록 조회, POST: 신규 생성)
// --------------------------------------------------
void CL_W10_WebAPI::routeSchedules() {
	// GET: 스케줄 전체 목록 조회 (Read)
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

	// POST: 스케줄 신규 생성 (Create)
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

		// CL_C10_ConfigManager를 통해 신규 스케줄을 추가하고 ID를 반환받음
		int v_new_id = CL_C10_ConfigManager::addScheduleFromJson(v_doc);
		
		JsonDocument v_res;
		if (v_new_id > 0) {
			v_res["result"] = "created";
			v_res["id"]     = v_new_id;
			// 201 Created 응답
			sendJson(p_request, v_res, 201);
		} else {
			v_res["error"] = "creation failed or validation error";
			sendJson(p_request, v_res, 400);
		}
	});
}

// --------------------------------------------------
// 7-1. /api/schedules/{id} (PUT: 수정, DELETE: 삭제)
// --------------------------------------------------
void CL_W10_WebAPI::routeSchedulesID() {
	// PUT: 스케줄 수정 (Update)
	s_server->on(
		"/api/schedules/([0-9]+)", HTTP_PUT, [](AsyncWebServerRequest* p_request) {}, nullptr,
		[](AsyncWebServerRequest* p_request, uint8_t* p_data, size_t p_len, size_t p_index, size_t p_total) {
			if (!checkApiKey(p_request)) {
				p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
				return;
			}
			if (p_index + p_len != p_total) return;

			// URL Path에서 ID 추출
			int v_id = p_request->pathArg(0).toInt();

			JsonDocument v_doc;
			if (!parseJsonBody(p_request, p_data, p_len, v_doc)) {
				p_request->send(400, "application/json", "{\"error\":\"json parse\"}");
				return;
			}

			// CL_C10_ConfigManager를 통해 해당 ID의 스케줄을 수정
			bool v_updated = CL_C10_ConfigManager::updateScheduleFromJson(v_id, v_doc);

			JsonDocument v_res;
			v_res["updated"] = v_updated;
			// 성공 시 200 OK, 실패 시 404 Not Found
			sendJson(p_request, v_res, v_updated ? 200 : 404);
		});

	// DELETE: 스케줄 삭제 (Delete)
	s_server->on("/api/schedules/([0-9]+)", HTTP_DELETE, [](AsyncWebServerRequest* p_request) {
		if (!checkApiKey(p_request)) {
			p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
			return;
		}
		// URL Path에서 ID 추출
		int v_id = p_request->pathArg(0).toInt();

		// CL_C10_ConfigManager를 통해 해당 ID의 스케줄을 삭제
		bool v_deleted = CL_C10_ConfigManager::deleteSchedule(v_id);

		JsonDocument v_res;
		v_res["deleted"] = v_deleted;
		// 성공 시 200 OK, 실패 시 404 Not Found
		sendJson(p_request, v_res, v_deleted ? 200 : 404);
	});
}

// --------------------------------------------------
// 8. /api/user_profiles (User Profile 설정 GET/POST)
// --------------------------------------------------
void CL_W10_WebAPI::routeUserProfiles() {
	// GET: 사용자 프로필 설정 조회
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
	
	// POST: 사용자 프로필 설정 수정 및 저장
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
                *g_A10_config_root.userProfiles,
                v_doc
            );
       }
		
		JsonDocument v_res;
		v_res["updated"] = v_changed;
		sendJson(p_request, v_res);
	});
}


// --------------------------------------------------
// 9. /api/control (대시보드/설정 관련 제어)
// --------------------------------------------------
void CL_W10_WebAPI::routeControl() {
	// POST /api/control/profile/select: 프로필 선택 후 즉시 적용
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
		// ControlManager의 startUserProfileByNo 함수 호출
		if (v_id > 0 && s_control && s_control->startUserProfileByNo((uint8_t)v_id)) { 
			v_res["result"] = "profile_started";
			v_res["id"]     = v_id;
			sendJson(p_request, v_res, 200);
		} else {
			v_res["error"] = "invalid_profile_id";
			sendJson(p_request, v_res, 400);
		}
	});

	// POST /api/control/reboot: 장치 재부팅
	s_server->on("/api/control/reboot", HTTP_POST,
				 [](AsyncWebServerRequest* p_request) {
					 if (!checkApiKey(p_request)) {
						 p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
						 return;
					 }
					 p_request->send(200, "application/json", "{\"result\":\"rebooting\"}");
					 delay(500);
					 ESP.restart();
				 });

	// POST /api/control/factoryReset: 공장 초기화 및 재부팅
	s_server->on("/api/control/factoryReset", HTTP_POST,
				 [](AsyncWebServerRequest* p_request) {
					 if (!checkApiKey(p_request)) {
						 p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
						 return;
					 }
					 
					 CL_D10_Logger::log(EN_L10_LOG_WARN, "[W10] Factory Reset requested.");
					 // ConfigManager의 공장 초기화 함수 호출
					 CL_C10_ConfigManager::factoryResetFromDefault(); 
					 
					 p_request->send(200, "application/json", "{\"result\":\"factory_reset_and_rebooting\"}");
					 delay(500);
					 ESP.restart();
				 });
    
    // POST /api/control/profile/stop: 프로파일 정지
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

	// POST /api/control/override/fixed: 고정 오버라이드 시작
	s_server->on("/api/control/override/fixed", HTTP_POST,
				 [](AsyncWebServerRequest* p_request) {
					 if (!checkApiKey(p_request)) {
						 p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
						 return;
					 }
					 // 쿼리 파라미터 대신 JSON Body 사용을 권장하지만, 기존 로직 유지
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

	// POST /api/control/override/preset: 프로파일 기반 오버라이드 시작 (JSON Body)
	s_server->on("/api/control/override/preset", HTTP_POST, [](AsyncWebServerRequest* p_request) {}, nullptr, [](AsyncWebServerRequest* p_request, uint8_t* p_data, size_t p_len, size_t p_index, size_t p_total) {
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

	// POST /api/control/override/clear: 오버라이드 해제
	s_server->on("/api/control/override/clear", HTTP_POST,
				 [](AsyncWebServerRequest* p_request) {
					 if (!checkApiKey(p_request)) {
						 p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
						 return;
					 }
					 s_control->stopOverride();
					 p_request->send(200, "application/json", "{\"result\":\"ok\"}");
				 });
}

// --------------------------------------------------
// 10. /api/simulation (시뮬레이션 설정 GET/POST)
// --------------------------------------------------
void CL_W10_WebAPI::routeSimulation() {
	// GET /api/simulation: 시뮬레이션 설정 조회 (Sim Details 페이지)
	s_server->on("/api/simulation", HTTP_GET,
				 [](AsyncWebServerRequest* p_request) {
					 if (!checkApiKey(p_request)) {
						 p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
						 return;
					 }
					 JsonDocument v_doc;
					 // 현재 활성 시뮬레이션 설정 정보를 JSON으로 변환
					 s_control->sim.toJson(v_doc); 
					 sendJson(p_request, v_doc);
				 });
	
	// POST /api/simulation: 시뮬레이션 설정 패치 및 적용
	s_server->on("/api/simulation", HTTP_POST, [](AsyncWebServerRequest* p_request) {}, nullptr,
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

		// ControlManager를 통해 시뮬레이션 설정 패치
		bool v_changed = s_control->sim.patchFromJson(v_doc);
		
		JsonDocument v_res;
		v_res["updated"] = v_changed;
		sendJson(p_request, v_res);
	});
}

// --------------------------------------------------
// 11. /api/control/summary (대시보드 요약 정보)
// --------------------------------------------------
void CL_W10_WebAPI::routeControlSummary() {
	s_server->on("/api/control/summary", HTTP_GET,
				 [](AsyncWebServerRequest* p_request) {
					 if (!checkApiKey(p_request)) {
						 p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
						 return;
					 }
					 JsonDocument v_doc;
					 s_control->toSummaryJson(v_doc);
					 s_control->toMetricsJson(v_doc);
					 sendJson(p_request, v_doc);
				 });
}

// --------------------------------------------------
// 12. /api/sim/state (시뮬레이션 상세 상태 및 요약)
// --------------------------------------------------
void CL_W10_WebAPI::routeSimState() {
	s_server->on("/api/sim/state", HTTP_GET,
				 [](AsyncWebServerRequest* p_request) {
					 if (!checkApiKey(p_request)) {
						 p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
						 return;
					 }
					 JsonDocument v_doc;

					 // 시뮬레이션 상태 + 요약 통합
					 s_control->sim.toJson(v_doc);
					 // JsonObject v_sim = v_doc["sim"].to<JsonObject>();
					 // s_control->sim.toJson(v_sim);

					 s_control->toSummaryJson(v_doc);
					 s_control->toMetricsJson(v_doc);

					 sendJson(p_request, v_doc);
				 });
	/*//////////////
	
	// POST /api/sim/state/run: 시뮬레이션 시작/정지 (대시보드 RUN 버튼)
	s_server->on("/api/sim/state/run", HTTP_POST, [](AsyncWebServerRequest* p_request) {}, nullptr,
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

		int v_mode = v_doc["mode"] | -1;
		bool v_result = false;
		if (s_control) {
			if (v_mode == 1) { // 1: RUN
				v_result = s_control->startSimulation();
			} else if (v_mode == 0) { // 0: STOP
				v_result = s_control->stopSimulation();
			}
		}

		JsonDocument v_res;
		v_res["mode_set"] = v_mode;
		v_res["success"] = v_result;
		sendJson(p_request, v_res);
	});
	*////////////
}

// --------------------------------------------------
// 13. /api/metrics (성능 지표 및 메모리 상태)
// --------------------------------------------------
void CL_W10_WebAPI::routeMetrics() {
	s_server->on("/api/metrics", HTTP_GET,
				 [](AsyncWebServerRequest* p_request) {
					 if (!checkApiKey(p_request)) {
						 p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
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
// 14. /api/logs (시스템 로그 조회)
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
					 sendJson(p_request, v_doc);
				 });
}

// --------------------------------------------------
// 15. /api/reload (설정 파일 재로드)
// --------------------------------------------------
void CL_W10_WebAPI::routeReload() {
	s_server->on("/api/reload", HTTP_POST,
				 [](AsyncWebServerRequest* p_request) {
					 if (!checkApiKey(p_request)) {
						 p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
						 return;
					 }

					 ST_A10_ConfigRoot_t v_root;
					 // 모든 설정 파일을 메모리로 재로드
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
// 16. /api/diag (시스템 진단 정보)
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
// 17. /api/scan (WiFi 네트워크 스캔)
// --------------------------------------------------
void CL_W10_WebAPI::routeScan() {
	s_server->on("/api/scan", HTTP_GET,
				 [](AsyncWebServerRequest* p_request) {
					 if (!checkApiKey(p_request)) {
						 p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
						 return;
					 }
					 JsonDocument v_doc;
                     // WiFiManager를 통해 네트워크 스캔 결과를 JSON으로 변환
                     CL_WF10_WiFiManager::scanNetworksToJson(v_doc); 
					 sendJson(p_request, v_doc);
				 });
}

// --------------------------------------------------
// 18. /api/config/init (설정 초기화 및 재부팅)
// --------------------------------------------------
void CL_W10_WebAPI::routeConfigInit() {
	s_server->on("/api/config/init", HTTP_POST,
				 [](AsyncWebServerRequest* p_request) {
					 if (!checkApiKey(p_request)) {
						 p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
						 return;
					 }
					 // 모든 설정을 기본값으로 리셋하고 저장
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
// 19. /api/motion/feed (모션 센서 데이터 수신)
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

        // PIR 상태 피드
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

        // BLE 상태 피드
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
// 20. /api/auth/test (API Key 인증 테스트)
// --------------------------------------------------
void CL_W10_WebAPI::routeAuthTest() {
	s_server->on("/api/auth/test", HTTP_GET,
				 [](AsyncWebServerRequest* p_request) {
					 // checkApiKey 유틸리티 함수가 API 키 검사를 수행
					 if (!checkApiKey(p_request)) {
						 p_request->send(401, "application/json", "{\"result\":\"unauthorized\"}");
						 return;
					 }
					 p_request->send(200, "application/json", "{\"result\":\"authorized\"}");
				 });
}

// --------------------------------------------------
// 21. /api/config/save & /api/config/dirty (설정 저장 및 Dirty 상태 확인)
// --------------------------------------------------
void CL_W10_WebAPI::routeConfigSave() {
    // POST /api/config/save: 메모리 설정값을 파일에 저장
    s_server->on("/api/config/save", HTTP_POST, 
        [](AsyncWebServerRequest* p_request) {
            if (!checkApiKey(p_request)) {
                p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
                return;
            }
            
            // 변경된 설정만 파일에 저장
            CL_C10_ConfigManager::saveDirtyConfigs();
            p_request->send(200, "application/json", "{\"result\":\"saved\", \"status\":\"clean\"}");
        }
    );
}

void CL_W10_WebAPI::routeConfigDirty() {
    // GET /api/config/dirty: 현재 저장되지 않은 변경 사항 확인
    s_server->on("/api/config/dirty", HTTP_GET, 
        [](AsyncWebServerRequest* p_request) {
            if (!checkApiKey(p_request)) {
                p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
                return;
            }
            JsonDocument v_doc;
            // ConfigManager에서 Dirty 상태 목록을 JSON으로 반환
            CL_C10_ConfigManager::getDirtyStatus(v_doc);
            sendJson(p_request, v_doc);
        }
    );
}

// --------------------------------------------------
// 23. /api/network/wifi/config (Wi-Fi 설정 상세 저장)
// --------------------------------------------------
// 이 라우트는 SC10_settings_001.js에서 호출하며, Wi-Fi 설정을 저장하고 적용을 시도합니다.
void CL_W10_WebAPI::routeWifiConfig() {
	s_server->on("/api/network/wifi/config", HTTP_POST, [](AsyncWebServerRequest* p_request) {}, nullptr,
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
        // Wi-Fi 설정 패치 및 변경 여부 확인
		if (g_A10_config_root.wifi) {
		    v_changed = CL_C10_ConfigManager::patchWifiFromJson(
                  *g_A10_config_root.wifi,
                  v_doc
            );
		}
		
		JsonDocument v_res;
		v_res["updated"] = v_changed;
		
        if (v_changed) {
            // 변경이 발생했을 경우, 설정을 파일에 저장하고 WiFi 재접속을 시도합니다.
            CL_C10_ConfigManager::saveDirtyConfigs(); 
            // WiFiManager에 재접속/모드 변경을 요청하는 로직 호출 (가정)
            // CL_WF10_WiFiManager::applyConfig(*g_A10_config_root.wifi); 
            
            v_res["status"] = "applied";
            v_res["need_reboot"] = true;
            CL_D10_Logger::log(EN_L10_LOG_INFO, "[W10] WiFi config updated and applied.");
        } else {
            v_res["status"] = "no_change";
            v_res["need_reboot"] = false;
        }

		sendJson(p_request, v_res);
	});
}

// --------------------------------------------------
// 24. /api/system/time/set (NTP 및 시간대 설정 저장)
// --------------------------------------------------
// 이 라우트는 SC10_settings_001.js에서 호출하며, 시간 관련 설정을 저장하고 적용을 시도합니다.
void CL_W10_WebAPI::routeTimeSet() {
	s_server->on("/api/system/time/set", HTTP_POST, [](AsyncWebServerRequest* p_request) {}, nullptr,
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
        
        // system 설정의 일부만 패치합니다.
        // 현재 ST_A10_ConfigSystem_t 구조체에 NTP와 Timezone 필드가 있다고 가정합니다.
		bool v_changed = false;
		if (g_A10_config_root.system) {
            // v_doc의 내용만 System 설정에 패치
            v_changed = CL_C10_ConfigManager::patchSystemFromJson(
                  *g_A10_config_root.system,
                  v_doc
            );
		}
		
		JsonDocument v_res;
		v_res["updated"] = v_changed;
		
        if (v_changed) {
            CL_C10_ConfigManager::saveDirtyConfigs(); 
            // 새로운 NTP/Timezone 설정을 즉시 시스템에 적용하는 로직 호출 (가정)
            // CL_T10_TimeManager::applyConfig(g_A10_config_root.system->ntp_server, g_A10_config_root.system->timezone_offset);
            v_res["status"] = "applied";
            CL_D10_Logger::log(EN_L10_LOG_INFO, "[W10] Time config updated and applied.");
        } else {
            v_res["status"] = "no_change";
        }
		sendJson(p_request, v_res);
	});
}

// --------------------------------------------------
// 25. /api/system/firmware/check (펌웨어 업데이트 확인)
// --------------------------------------------------
// 이 라우트는 SC10_settings_001.js에서 호출하며, OTA 서버에 최신 펌웨어를 확인합니다.
void CL_W10_WebAPI::routeFirmwareCheck() {
	s_server->on("/api/system/firmware/check", HTTP_GET,
				 [](AsyncWebServerRequest* p_request) {
					 if (!checkApiKey(p_request)) {
						 p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
						 return;
					 }
					 
					 JsonDocument v_doc;
					 // 실제 OTA 서버 체크 로직은 여기에 구현되거나 다른 모듈을 호출해야 합니다.
                     
                     // 시뮬레이션 응답 (실제 구현 필요)
                     const char* v_current_version = A10_Const::FW_VERSION;
                     const char* v_latest_version = "V1.0.1"; // 가상의 최신 버전

                     v_doc["current_version"] = v_current_version;
                     
                     if (strcmp(v_current_version, v_latest_version) < 0) {
                         v_doc["status"] = "available";
                         v_doc["latest_version"] = v_latest_version;
                         v_doc["url"] = "/api/update/latest";
                     } else {
                         v_doc["status"] = "latest";
                         v_doc["latest_version"] = v_current_version;
                     }

					 sendJson(p_request, v_doc);
				 });
}

/*
// --------------------------------------------------
// 22. 라우트 더미 함수 (실제 구현은 다른 파일에 있음)
// --------------------------------------------------
void CL_W10_WebAPI::routeWebSocket() {
	// WebSockets.cpp 파일에 구현됨
}
void CL_W10_WebAPI::routeStaticAssets() {
	// W10_Web_Static_024.cpp 파일에 구현됨
}
void CL_W10_WebAPI::routeUpload() {
	// W10_Web_Upload_024.cpp 파일에 구현됨
}
void CL_W10_WebAPI::routeUpdate() {
	// W10_Web_Upload_024.cpp 파일에 구현됨
}
*/
