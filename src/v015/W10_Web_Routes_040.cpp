/*
 * ------------------------------------------------------
 * 소스명 : W10_Web_Routes_040.cpp
 * 모듈 약어 : W10
 * 모듈명 : Smart Nature Wind Web API (v029) - Routes Implementation
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

#include "CT10_Control_030.h"
#include "M10_MotionLogic_020.h"
#include "W10_Web_040.h"
#include "WF10_WiFiManager_030.h"

// ------------------------------------------------------
// 정적 멤버 정의
// ------------------------------------------------------
AsyncWebServer*			CL_W10_WebAPI::s_server	 = nullptr;
CL_CT10_ControlManager* CL_W10_WebAPI::s_control = nullptr;
WiFiMulti*				CL_W10_WebAPI::s_multi	 = nullptr;
File					CL_W10_WebAPI::s_upFile;

AsyncWebSocket			s_wsLogs(W10_Const::WS_API_LOG);
AsyncWebSocket			s_wsState(W10_Const::WS_API_STATE);
AsyncWebSocket			s_wsChart(W10_Const::WS_API_CHART);
AsyncWebSocket			s_wsMetrics(W10_Const::WS_API_METRICS);

/*
AsyncWebSocket          s_wsLogs("/ws/log");
AsyncWebSocket          s_wsState("/ws/state");
AsyncWebSocket          s_wsChart("/ws/chart");
AsyncWebSocket          s_wsMetrics("/ws/metrics");
*/

AsyncWebSocket*			CL_W10_WebAPI::s_wsServerState	 = &s_wsState;
AsyncWebSocket*			CL_W10_WebAPI::s_wsServerLogs	 = &s_wsLogs;
AsyncWebSocket*			CL_W10_WebAPI::s_wsServerChart	 = &s_wsChart;
AsyncWebSocket*			CL_W10_WebAPI::s_wsServerMetrics = &s_wsMetrics;

// --------------------------------------------------
// 초기화
// --------------------------------------------------
void CL_W10_WebAPI::begin(AsyncWebServer& p_server, CL_CT10_ControlManager& p_control, WiFiMulti& p_multi) {
	s_server  = &p_server;
	s_control = &p_control;
	s_multi	  = &p_multi;

	// 1. 시스템 정보 및 상태
	routeVersion();
	routeState();
	routeSystem();
	// routeWifi();
	routeDiag();
	routeScan();
	routeAuthTest();
	routeWifiConfig();
	routeTimeSet();
	routeFirmwareCheck();

	// 2. 설정 및 제어
	routeMotion();
	routeSimulation();
	routeSimState();
	routeControl();
	routeControlSummary();

	// 3. 설정 관리
	// routeConfigSave();
	routeConfigDirtySave();
	routeConfigInit();
	routeReload();

	// 4. Wind Profiles CRUD
	routeWindProfile();
	routeWindProfileID();

	// 5. Schedules CRUD
	routeSchedules();
	routeSchedulesID();

	// 6. User Profiles CRUD
	routeUserProfiles();
	routeUserProfilesID();
	routeUserProfilesPatch();

	// 7. 모니터링 및 로깅
	routeMetrics();
	routeLogs();

	// 8. 피드/Static/Upload/OTA/WS
	routeMotionFeed();
	routeStaticAssets();
	routeUpload();
	routeUpdate();
	routeWebSocket();

	// s_server.serveStatic(uriPath, fs, filePath);
	// s_server->serveStatic("/html_v2", LittleFS, "/html_v2");

	CL_D10_Logger::log(EN_L10_LOG_INFO, "[W10] WebAPI initialized (v029)");
}

// --------------------------------------------------
// 1. /api/version
// --------------------------------------------------
void CL_W10_WebAPI::routeVersion() {
	s_server->on(W10_Const::HTTP_API_VERSION, HTTP_GET, [](AsyncWebServerRequest* p_request) {
		if (!checkApiKey(p_request)) {
			p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
			return;
		}
		JsonDocument v_doc;
		v_doc["module"]	 = "SmartNatureWind";
		v_doc["fw"]		 = A20_Const::FW_VERSION;

		v_doc["control"] = "CT10_ControlManager_024";
		v_doc["config"]	 = "C10_ConfigManager_029";
		v_doc["api"]	 = "W10_WebAPI_029";

		sendJson(p_request, v_doc);
	});
}

// --------------------------------------------------
// 2. /api/state
// --------------------------------------------------
void CL_W10_WebAPI::routeState() {
	s_server->on(W10_Const::HTTP_API_STATE, HTTP_GET, [](AsyncWebServerRequest* p_request) {
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
		sendJson(p_request, v_doc);
	});
}

// --------------------------------------------------
// 3. /api/system (시스템 설정 GET/POST)
// --------------------------------------------------
void CL_W10_WebAPI::routeSystem() {
	// GET
	s_server->on(W10_Const::HTTP_API_SYSTEM, HTTP_GET, [](AsyncWebServerRequest* p_request) {
		if (!checkApiKey(p_request)) {
			p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
			return;
		}
		JsonDocument v_doc;
		if (g_A20_config_root.system) {
			CL_C10_ConfigManager::toJson_System(*g_A20_config_root.system, v_doc);
		}
		sendJson(p_request, v_doc);
	});

	// POST (패치)
	s_server->on(
		W10_Const::HTTP_API_SYSTEM, HTTP_POST, [](AsyncWebServerRequest* p_request) {}, nullptr, [](AsyncWebServerRequest* p_request, uint8_t* p_data, size_t p_len, size_t p_index, size_t p_total) {
            if (!checkApiKey(p_request)) {
                p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
                return;
            }
            if (p_index + p_len != p_total)
                return;

            JsonDocument v_doc;
            if (!parseJsonBody(p_request, p_data, p_len, v_doc)) {
                p_request->send(400, "application/json", "{\"error\":\"json parse\"}");
                return;
            }

            bool v_changed = false;
            if (g_A20_config_root.system) {
                v_changed = CL_C10_ConfigManager::patchSystemFromJson(*g_A20_config_root.system, v_doc);
            }

            JsonDocument v_res;
            v_res["updated"] = v_changed;
            sendJson(p_request, v_res); });
}

// --------------------------------------------------
// 5. /api/motion
// --------------------------------------------------
void CL_W10_WebAPI::routeMotion() {
	// GET
	s_server->on(W10_Const::HTTP_API_MOTION, HTTP_GET, [](AsyncWebServerRequest* p_request) {
		if (!checkApiKey(p_request)) {
			p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
			return;
		}
		JsonDocument v_doc;
		if (g_A20_config_root.motion) {
			CL_C10_ConfigManager::toJson_Motion(*g_A20_config_root.motion, v_doc);
		}
		sendJson(p_request, v_doc);
	});

	// POST
	s_server->on(
		W10_Const::HTTP_API_MOTION, HTTP_POST, [](AsyncWebServerRequest* p_request) {}, nullptr, [](AsyncWebServerRequest* p_request, uint8_t* p_data, size_t p_len, size_t p_index, size_t p_total) {
            if (!checkApiKey(p_request)) {
                p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
                return;
            }
            if (p_index + p_len != p_total)
                return;

            JsonDocument v_doc;
            if (!parseJsonBody(p_request, p_data, p_len, v_doc)) {
                p_request->send(400, "application/json", "{\"error\":\"json parse\"}");
                return;
            }

            bool v_changed = false;
            if (g_A20_config_root.motion) {
                v_changed = CL_C10_ConfigManager::patchMotionFromJson(*g_A20_config_root.motion, v_doc);
            }

            JsonDocument v_res;
            v_res["updated"] = v_changed;
            sendJson(p_request, v_res); });
}

// --------------------------------------------------
// 6. /api/windProfile (GET/POST)
// --------------------------------------------------
void CL_W10_WebAPI::routeWindProfile() {
	// GET: 전체 목록 조회
	s_server->on(W10_Const::HTTP_API_WIND_PROFILE, HTTP_GET, [](AsyncWebServerRequest* p_request) {
		if (!checkApiKey(p_request)) {
			p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
			return;
		}

		JsonDocument			 v_doc;
		ST_A20_WindProfileDict_t v_dict;
		memset(&v_dict, 0, sizeof(v_dict));

		if (CL_C10_ConfigManager::loadWindProfileDict(v_dict)) {
			CL_C10_ConfigManager::toJson_WindProfileDict(v_dict, v_doc);
			sendJson(p_request, v_doc);
		} else {
			p_request->send(500, "application/json", "{\"error\":\"load failed\"}");
		}
	});

	// POST: 신규 생성
	s_server->on(
		W10_Const::HTTP_API_WIND_PROFILE, HTTP_POST, [](AsyncWebServerRequest* p_request) {}, nullptr, [](AsyncWebServerRequest* p_request, uint8_t* p_data, size_t p_len, size_t p_index, size_t p_total) {
            if (!checkApiKey(p_request)) {
                p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
                return;
            }
            if (p_index + p_len != p_total)
                return;

            JsonDocument v_doc;
            if (!parseJsonBody(p_request, p_data, p_len, v_doc)) {
                p_request->send(400, "application/json", "{\"error\":\"json parse\"}");
                return;
            }

            int          v_new_id = CL_C10_ConfigManager::addWindProfileFromJson(v_doc);

            JsonDocument v_res;
            if (v_new_id > 0) {
                v_res["result"] = "created";
                v_res["id"]     = v_new_id;

                CL_C10_ConfigManager::saveDirtyConfigs();
                sendJson(p_request, v_res, 201);
            } else {
                v_res["error"] = "creation failed or validation error";
                sendJson(p_request, v_res, 400);
            } });
}

// --------------------------------------------------
// 6-1. /api/windProfile/{id} (PUT/DELETE)
// --------------------------------------------------
void CL_W10_WebAPI::routeWindProfileID() {
	// PUT: 수정
	s_server->on((String(W10_Const::HTTP_API_WIND_PROFILE) + "/([0-9]+)").c_str(), HTTP_PUT,
		// "/api/windProfile/([0-9]+)", HTTP_PUT,
		[](AsyncWebServerRequest* p_request) {},
		nullptr,
		[](AsyncWebServerRequest* p_request, uint8_t* p_data, size_t p_len, size_t p_index, size_t p_total) {
			if (!checkApiKey(p_request)) {
				p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
				return;
			}
			if (p_index + p_len != p_total)
				return;

			int			 v_id = p_request->pathArg(0).toInt();

			JsonDocument v_doc;
			if (!parseJsonBody(p_request, p_data, p_len, v_doc)) {
				p_request->send(400, "application/json", "{\"error\":\"json parse\"}");
				return;
			}

			bool		 v_updated = CL_C10_ConfigManager::updateWindProfileFromJson((uint16_t)v_id, v_doc);

			JsonDocument v_res;
			v_res["updated"] = v_updated;
			if (v_updated) {
				CL_C10_ConfigManager::saveDirtyConfigs();
			}
			sendJson(p_request, v_res, v_updated ? 200 : 404);
		});

	// DELETE: 삭제
	s_server->on((String(W10_Const::HTTP_API_WIND_PROFILE) + "/([0-9]+)").c_str(), HTTP_DELETE, [](AsyncWebServerRequest* p_request) {
		if (!checkApiKey(p_request)) {
			p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
			return;
		}
		int			 v_id	   = p_request->pathArg(0).toInt();
		bool		 v_deleted = CL_C10_ConfigManager::deleteWindProfile((uint16_t)v_id);

		JsonDocument v_res;
		v_res["deleted"] = v_deleted;
		if (v_deleted) {
			CL_C10_ConfigManager::saveDirtyConfigs();
		}
		sendJson(p_request, v_res, v_deleted ? 200 : 404);
	});
}

// --------------------------------------------------
// 7. /api/schedules (GET/POST)
// --------------------------------------------------
void CL_W10_WebAPI::routeSchedules() {
	// GET
	s_server->on(W10_Const::HTTP_API_SCHEDULES, HTTP_GET, [](AsyncWebServerRequest* p_request) {
		if (!checkApiKey(p_request)) {
			p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
			return;
		}
		JsonDocument v_doc;
		if (g_A20_config_root.schedules) {
			CL_C10_ConfigManager::toJson_Schedules(*g_A20_config_root.schedules, v_doc);
		}
		sendJson(p_request, v_doc);
	});

	// POST: 신규 생성
	s_server->on(
		W10_Const::HTTP_API_SCHEDULES, HTTP_POST, [](AsyncWebServerRequest* p_request) {}, nullptr, [](AsyncWebServerRequest* p_request, uint8_t* p_data, size_t p_len, size_t p_index, size_t p_total) {
            if (!checkApiKey(p_request)) {
                p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
                return;
            }
            if (p_index + p_len != p_total)
                return;

            JsonDocument v_doc;
            if (!parseJsonBody(p_request, p_data, p_len, v_doc)) {
                p_request->send(400, "application/json", "{\"error\":\"json parse\"}");
                return;
            }

            int          v_new_id = CL_C10_ConfigManager::addScheduleFromJson(v_doc);

            JsonDocument v_res;
            if (v_new_id > 0) {
                v_res["result"] = "created";
                v_res["id"]     = v_new_id;
                CL_C10_ConfigManager::saveDirtyConfigs();
                sendJson(p_request, v_res, 201);
            } else {
                v_res["error"] = "creation failed or validation error";
                sendJson(p_request, v_res, 400);
            } });
}

// --------------------------------------------------
// 7-1. /api/schedules/{id} (PUT/DELETE)
// --------------------------------------------------
void CL_W10_WebAPI::routeSchedulesID() {
	// PUT
	s_server->on((String(W10_Const::HTTP_API_SCHEDULES) + "/([0-9]+)").c_str(), HTTP_PUT, [](AsyncWebServerRequest* p_request) {}, nullptr, [](AsyncWebServerRequest* p_request, uint8_t* p_data, size_t p_len, size_t p_index, size_t p_total) {
            if (!checkApiKey(p_request)) {
                p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
                return;
            }
            if (p_index + p_len != p_total)
                return;

            int          v_id = p_request->pathArg(0).toInt();

            JsonDocument v_doc;
            if (!parseJsonBody(p_request, p_data, p_len, v_doc)) {
                p_request->send(400, "application/json", "{\"error\":\"json parse\"}");
                return;
            }

            bool         v_updated = CL_C10_ConfigManager::updateScheduleFromJson((uint16_t)v_id, v_doc);

            JsonDocument v_res;
            v_res["updated"] = v_updated;
            if (v_updated) {
                CL_C10_ConfigManager::saveDirtyConfigs();
            }
            sendJson(p_request, v_res, v_updated ? 200 : 404); });

	// DELETE
	s_server->on((String(W10_Const::HTTP_API_SCHEDULES) + "/([0-9]+)").c_str(), HTTP_DELETE, [](AsyncWebServerRequest* p_request) {
		if (!checkApiKey(p_request)) {
			p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
			return;
		}
		int			 v_id	   = p_request->pathArg(0).toInt();
		bool		 v_deleted = CL_C10_ConfigManager::deleteSchedule((uint16_t)v_id);

		JsonDocument v_res;
		v_res["deleted"] = v_deleted;
		if (v_deleted) {
			CL_C10_ConfigManager::saveDirtyConfigs();
		}
		sendJson(p_request, v_res, v_deleted ? 200 : 404);
	});
}

// --------------------------------------------------
// 8. /api/user_profiles (GET/POST - 목록 & 신규 생성)
// --------------------------------------------------
void CL_W10_WebAPI::routeUserProfiles() {
	// GET: 전체 목록
	s_server->on(W10_Const::HTTP_API_USER_PROFILES, HTTP_GET, [](AsyncWebServerRequest* p_request) {
		if (!checkApiKey(p_request)) {
			p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
			return;
		}
		JsonDocument v_doc;
		if (g_A20_config_root.userProfiles) {
			CL_C10_ConfigManager::toJson_UserProfiles(*g_A20_config_root.userProfiles, v_doc);
		}
		sendJson(p_request, v_doc);
	});

	// POST: 신규 생성
	s_server->on(
		W10_Const::HTTP_API_USER_PROFILES, HTTP_POST, [](AsyncWebServerRequest* p_request) {}, nullptr, [](AsyncWebServerRequest* p_request, uint8_t* p_data, size_t p_len, size_t p_index, size_t p_total) {
            if (!checkApiKey(p_request)) {
                p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
                return;
            }
            if (p_index + p_len != p_total)
                return;

            JsonDocument v_doc;
            if (!parseJsonBody(p_request, p_data, p_len, v_doc)) {
                p_request->send(400, "application/json", "{\"error\":\"json parse\"}");
                return;
            }

            // C10_Config_029 측에 다음 함수가 존재한다고 가정:
            // static int addUserProfilesFromJson(const JsonDocument&);
            int          v_new_id = CL_C10_ConfigManager::addUserProfilesFromJson(v_doc);

            JsonDocument v_res;
            if (v_new_id > 0) {
                v_res["result"] = "created";
                v_res["id"]     = v_new_id;
                CL_C10_ConfigManager::saveDirtyConfigs();
                sendJson(p_request, v_res, 201);
            } else {
                v_res["error"] = "creation failed or validation error";
                sendJson(p_request, v_res, 400);
            } });
}

// --------------------------------------------------
// 8-1. /api/user_profiles/{id} (PUT/DELETE)
// --------------------------------------------------
void CL_W10_WebAPI::routeUserProfilesID() {
	// PUT: 수정
	s_server->on((String(W10_Const::HTTP_API_USER_PROFILES) + "/([0-9]+)").c_str(), HTTP_PUT, [](AsyncWebServerRequest* p_request) {}, nullptr, [](AsyncWebServerRequest* p_request, uint8_t* p_data, size_t p_len, size_t p_index, size_t p_total) {
            if (!checkApiKey(p_request)) {
                p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
                return;
            }
            if (p_index + p_len != p_total)
                return;

            int          v_id = p_request->pathArg(0).toInt();

            JsonDocument v_doc;
            if (!parseJsonBody(p_request, p_data, p_len, v_doc)) {
                p_request->send(400, "application/json", "{\"error\":\"json parse\"}");
                return;
            }

            // C10_Config_029: static bool updateUserProfilesFromJson(uint16_t, const JsonDocument&);
            bool         v_updated = CL_C10_ConfigManager::updateUserProfilesFromJson((uint16_t)v_id, v_doc);

            JsonDocument v_res;
            v_res["updated"] = v_updated;
            if (v_updated) {
                CL_C10_ConfigManager::saveDirtyConfigs();
            }
            sendJson(p_request, v_res, v_updated ? 200 : 404); });

	// DELETE: 삭제
	s_server->on((String(W10_Const::HTTP_API_USER_PROFILES) + "/([0-9]+)").c_str(), HTTP_DELETE, [](AsyncWebServerRequest* p_request) {
		if (!checkApiKey(p_request)) {
			p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
			return;
		}
		int			 v_id	   = p_request->pathArg(0).toInt();
		bool		 v_deleted = CL_C10_ConfigManager::deleteUserProfiles((uint16_t)v_id);

		JsonDocument v_res;
		v_res["deleted"] = v_deleted;
		if (v_deleted) {
			CL_C10_ConfigManager::saveDirtyConfigs();
		}
		sendJson(p_request, v_res, v_deleted ? 200 : 404);
	});
}

// --------------------------------------------------
// 8-2. /api/user_profiles/patch (배치 패치, 기존 patch 유지)
// --------------------------------------------------
void CL_W10_WebAPI::routeUserProfilesPatch() {
	s_server->on(
		W10_Const::HTTP_API_USER_PROFILES_PATCH, HTTP_POST, [](AsyncWebServerRequest* p_request) {}, nullptr, [](AsyncWebServerRequest* p_request, uint8_t* p_data, size_t p_len, size_t p_index, size_t p_total) {
            if (!checkApiKey(p_request)) {
                p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
                return;
            }
            if (p_index + p_len != p_total)
                return;

            JsonDocument v_doc;
            if (!parseJsonBody(p_request, p_data, p_len, v_doc)) {
                p_request->send(400, "application/json", "{\"error\":\"json parse\"}");
                return;
            }

            bool v_changed = false;
            if (g_A20_config_root.userProfiles) {
                v_changed = CL_C10_ConfigManager::patchUserProfilesFromJson(*g_A20_config_root.userProfiles, v_doc);
            }

            if (v_changed) {
                CL_C10_ConfigManager::saveDirtyConfigs();
            }

            JsonDocument v_res;
            v_res["updated"] = v_changed;
            sendJson(p_request, v_res); });
}

// --------------------------------------------------
// 9. /api/control (프로필/오버라이드/리부트/팩토리리셋)
// --------------------------------------------------
void CL_W10_WebAPI::routeControl() {
	// profile/select
	s_server->on(
		W10_Const::HTTP_API_CTL_PROF_SEL, HTTP_POST, [](AsyncWebServerRequest* p_request) {}, nullptr, [](AsyncWebServerRequest* p_request, uint8_t* p_data, size_t p_len, size_t p_index, size_t p_total) {
            if (!checkApiKey(p_request)) {
                p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
                return;
            }
            if (p_index + p_len != p_total)
                return;

            JsonDocument v_doc;
            if (!parseJsonBody(p_request, p_data, p_len, v_doc)) {
                p_request->send(400, "application/json", "{\"error\":\"json parse\"}");
                return;
            }

            int          v_id = v_doc["id"] | -1;

            JsonDocument v_res;
            if (v_id > 0 && s_control && s_control->startUserProfileByNo((uint8_t)v_id)) {
                v_res["result"] = "profile_started";
                v_res["id"]     = v_id;
                sendJson(p_request, v_res, 200);
            } else {
                v_res["error"] = "invalid_profile_id";
                sendJson(p_request, v_res, 400);
            } });

	// reboot
	s_server->on(W10_Const::HTTP_API_CTL_REBOOT, HTTP_POST, [](AsyncWebServerRequest* p_request) {
		if (!checkApiKey(p_request)) {
			p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
			return;
		}
		p_request->send(200, "application/json", "{\"result\":\"rebooting\"}");
		delay(500);
		ESP.restart();
	});

	// factoryReset + reboot (ConfigManager 사용)
	s_server->on(W10_Const::HTTP_API_CTL_FACTORY, HTTP_POST, [](AsyncWebServerRequest* p_request) {
		if (!checkApiKey(p_request)) {
			p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
			return;
		}
		CL_D10_Logger::log(EN_L10_LOG_WARN, "[W10] Factory Reset requested via /api/control/factoryReset.");
		bool		 v_ok = CL_C10_ConfigManager::factoryResetFromDefault();

		JsonDocument v_doc;
		v_doc["factory"] = v_ok;
		sendJson(p_request, v_doc);

		if (v_ok) {
			delay(500);
			ESP.restart();
		}
	});

	// profile/stop
	s_server->on(W10_Const::HTTP_API_CTL_PROF_STOP, HTTP_POST, [](AsyncWebServerRequest* p_request) {
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

	// override/fixed
	s_server->on(W10_Const::HTTP_API_CTL_OVR_FIXED, HTTP_POST, [](AsyncWebServerRequest* p_request) {
		if (!checkApiKey(p_request)) {
			p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
			return;
		}
		if (!p_request->hasParam("percent", true) || !p_request->hasParam("seconds", true)) {
			p_request->send(400, "application/json", "{\"error\":\"missing param\"}");
			return;
		}
		float	 v_pct = p_request->getParam("percent", true)->value().toFloat();
		uint32_t v_sec = (uint32_t)p_request->getParam("seconds", true)->value().toInt();
		if (s_control) {
			s_control->startOverrideFixed(v_pct, v_sec);
		}
		p_request->send(200, "application/json", "{\"result\":\"ok\"}");
	});

	// override/preset (JSON Body)
	s_server->on(
		W10_Const::HTTP_API_CTL_OVR_PRESET, HTTP_POST, [](AsyncWebServerRequest* p_request) {}, nullptr, [](AsyncWebServerRequest* p_request, uint8_t* p_data, size_t p_len, size_t p_index, size_t p_total) {
            if (!checkApiKey(p_request)) {
                p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
                return;
            }
            if (p_index + p_len != p_total)
                return;

            JsonDocument v_doc;
            if (!parseJsonBody(p_request, p_data, p_len, v_doc)) {
                p_request->send(400, "application/json", "{\"error\":\"json parse\"}");
                return;
            }

            const char*          v_preset = v_doc["presetCode"] | "";
            const char*          v_style  = v_doc["styleCode"] | "BALANCE";
            uint32_t             v_sec    = v_doc["durationSec"] | 0;

            ST_A20_AdjustDelta_t v_adj;
            memset(&v_adj, 0, sizeof(v_adj));
            if (v_doc["adjust"].is<JsonObject>()) {
                JsonObject v_aj        = v_doc["adjust"];
                v_adj.wind_intensity   = v_aj["wind_intensity"] | 0.0f;
                v_adj.wind_variability = v_aj["wind_variability"] | 0.0f;
                v_adj.gust_frequency   = v_aj["gust_frequency"] | 0.0f;
                v_adj.fan_limit        = v_aj["fan_limit"] | 0.0f;
                v_adj.min_fan          = v_aj["min_fan"] | 0.0f;
            }

            if (s_control) {
                s_control->startOverridePreset(v_preset, v_style, &v_adj, v_sec);
            }
            p_request->send(200, "application/json", "{\"result\":\"ok\"}"); });

	// override/clear
	s_server->on(W10_Const::HTTP_API_CTL_OVR_CLEAR, HTTP_POST, [](AsyncWebServerRequest* p_request) {
		if (!checkApiKey(p_request)) {
			p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
			return;
		}
		if (s_control) {
			s_control->stopOverride();
		}
		p_request->send(200, "application/json", "{\"result\":\"ok\"}");
	});
}

// --------------------------------------------------
// 10. /api/simulation
// --------------------------------------------------
void CL_W10_WebAPI::routeSimulation() {
	s_server->on(W10_Const::HTTP_API_SIMULATION, HTTP_GET, [](AsyncWebServerRequest* p_request) {
		if (!checkApiKey(p_request)) {
			p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
			return;
		}
		JsonDocument v_doc;
		if (s_control) {
			s_control->sim.toJson(v_doc);
		}
		sendJson(p_request, v_doc);
	});

	s_server->on(
		W10_Const::HTTP_API_SIMULATION, HTTP_POST, [](AsyncWebServerRequest* p_request) {}, nullptr, [](AsyncWebServerRequest* p_request, uint8_t* p_data, size_t p_len, size_t p_index, size_t p_total) {
            if (!checkApiKey(p_request)) {
                p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
                return;
            }
            if (p_index + p_len != p_total)
                return;

            JsonDocument v_doc;
            if (!parseJsonBody(p_request, p_data, p_len, v_doc)) {
                p_request->send(400, "application/json", "{\"error\":\"json parse\"}");
                return;
            }

            bool v_changed = false;
            if (s_control) {
                v_changed = s_control->sim.patchFromJson(v_doc);
            }

            JsonDocument v_res;
            v_res["updated"] = v_changed;
            sendJson(p_request, v_res); });
}

// --------------------------------------------------
// 11. /api/control/summary
// --------------------------------------------------
void CL_W10_WebAPI::routeControlSummary() {
	s_server->on(W10_Const::HTTP_API_CONTROL_SUMMARY, HTTP_GET, [](AsyncWebServerRequest* p_request) {
		if (!checkApiKey(p_request)) {
			p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
			return;
		}
		JsonDocument v_doc;
		if (s_control) {
			s_control->toSummaryJson(v_doc);
			s_control->toMetricsJson(v_doc);
		}
		sendJson(p_request, v_doc);
	});
}

// --------------------------------------------------
// 12. /api/sim/state
// --------------------------------------------------
void CL_W10_WebAPI::routeSimState() {
	s_server->on(W10_Const::HTTP_API_SIM_STATE, HTTP_GET, [](AsyncWebServerRequest* p_request) {
		if (!checkApiKey(p_request)) {
			p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
			return;
		}
		JsonDocument v_doc;
		if (s_control) {
			s_control->sim.toJson(v_doc);
			s_control->toSummaryJson(v_doc);
			s_control->toMetricsJson(v_doc);
		}
		sendJson(p_request, v_doc);
	});
}

// --------------------------------------------------
// 13. /api/metrics
// --------------------------------------------------
void CL_W10_WebAPI::routeMetrics() {
	s_server->on(W10_Const::HTTP_API_METRICS, HTTP_GET, [](AsyncWebServerRequest* p_request) {
		if (!checkApiKey(p_request)) {
			p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
			return;
		}
		JsonDocument v_doc;
		if (s_control) {
			s_control->toMetricsJson(v_doc);
		}
		CL_W10_WebAPI::broadcastMetrics(v_doc, true);
		CL_W10_WebAPI::broadcastChart(v_doc, true);
		sendJson(p_request, v_doc);
	});
}

// --------------------------------------------------
// 14. /api/logs
// --------------------------------------------------
void CL_W10_WebAPI::routeLogs() {
	s_server->on(W10_Const::HTTP_API_LOGS, HTTP_GET, [](AsyncWebServerRequest* p_request) {
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
// 15. /api/reload
// --------------------------------------------------
void CL_W10_WebAPI::routeReload() {
	s_server->on(W10_Const::HTTP_API_RELOAD, HTTP_POST, [](AsyncWebServerRequest* p_request) {
		if (!checkApiKey(p_request)) {
			p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
			return;
		}
		ST_A20_ConfigRoot_t v_root;
		bool				v_ok = CL_C10_ConfigManager::loadAll(v_root);
		if (!v_ok) {
			p_request->send(500, "application/json", "{\"error\":\"reload failed\"}");
			return;
		}
		g_A20_config_root = v_root;
		p_request->send(200, "application/json", "{\"result\":\"ok\"}");
	});
}

// --------------------------------------------------
// 16. /api/diag
// --------------------------------------------------
void CL_W10_WebAPI::routeDiag() {
	s_server->on(W10_Const::HTTP_API_DIAG, HTTP_GET, [](AsyncWebServerRequest* p_request) {
		if (!checkApiKey(p_request)) {
			p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
			return;
		}
		JsonDocument v_doc;
		v_doc["heap"]	  = ESP.getFreeHeap();
		v_doc["fs_used"]  = LittleFS.usedBytes();
		v_doc["fs_total"] = LittleFS.totalBytes();
		sendJson(p_request, v_doc);
	});
}

// --------------------------------------------------
// 17. /api/scan
// --------------------------------------------------
void CL_W10_WebAPI::routeScan() {
	s_server->on(W10_Const::HTTP_API_WIFI_SCAN, HTTP_GET, [](AsyncWebServerRequest* p_request) {
		if (!checkApiKey(p_request)) {
			p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
			return;
		}
		JsonDocument v_doc;
		CL_WF10_WiFiManager::scanNetworksToJson(v_doc);
		sendJson(p_request, v_doc);
	});
}

// --------------------------------------------------
// 18. /api/config/init  (factoryResetFromDefault 통일)
// --------------------------------------------------
void CL_W10_WebAPI::routeConfigInit() {
	s_server->on(W10_Const::HTTP_API_CONFIG_INIT, HTTP_POST, [](AsyncWebServerRequest* p_request) {
		if (!checkApiKey(p_request)) {
			p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
			return;
		}
		CL_D10_Logger::log(EN_L10_LOG_WARN, "[W10] Config Init requested via /api/config/init.");
		bool		 v_ok = CL_C10_ConfigManager::factoryResetFromDefault();

		JsonDocument v_doc;
		v_doc["factory"] = v_ok;
		sendJson(p_request, v_doc);

		if (v_ok) {
			delay(200);
			ESP.restart();
		}
	});
}

// --------------------------------------------------
// 19. /api/motion/feed (PIR / BLE)
// --------------------------------------------------
void CL_W10_WebAPI::routeMotionFeed() {
	// PIR
	s_server->on(
		W10_Const::HTTP_API_FEED_PIR, HTTP_POST, [](AsyncWebServerRequest* p_request) {}, nullptr, [](AsyncWebServerRequest* p_request, uint8_t* p_data, size_t p_len, size_t p_index, size_t p_total) {
            if (!checkApiKey(p_request)) {
                p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
                return;
            }
            if (p_index + p_len != p_total)
                return;

            JsonDocument v_doc;
            if (!parseJsonBody(p_request, p_data, p_len, v_doc)) {
                p_request->send(400, "application/json", "{\"error\":\"json parse\"}");
                return;
            }

            if (s_control && s_control->motion) {
                bool v_pir = v_doc["pir"] | false;
                s_control->motion->feedPIR(v_pir);
            }

            JsonDocument v_res;
            v_res["fed"]  = true;
            v_res["type"] = "pir";
            sendJson(p_request, v_res); });

	// BLE
	s_server->on(
		W10_Const::HTTP_API_FEED_BLE, HTTP_POST, [](AsyncWebServerRequest* p_request) {}, nullptr, [](AsyncWebServerRequest* p_request, uint8_t* p_data, size_t p_len, size_t p_index, size_t p_total) {
            if (!checkApiKey(p_request)) {
                p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
                return;
            }
            if (p_index + p_len != p_total)
                return;

            JsonDocument v_doc;
            if (!parseJsonBody(p_request, p_data, p_len, v_doc)) {
                p_request->send(400, "application/json", "{\"error\":\"json parse\"}");
                return;
            }

            if (s_control && s_control->motion) {
                bool v_ble = v_doc["ble"] | false;
                s_control->motion->feedBLE(v_ble);
            }

            JsonDocument v_res;
            v_res["fed"]  = true;
            v_res["type"] = "ble";
            sendJson(p_request, v_res); });
}

// --------------------------------------------------
// 20. /api/auth/test
// --------------------------------------------------
void CL_W10_WebAPI::routeAuthTest() {
	s_server->on(W10_Const::HTTP_API_AUTH_TEST, HTTP_GET, [](AsyncWebServerRequest* p_request) {
		if (!checkApiKey(p_request)) {
			p_request->send(401, "application/json", "{\"result\":\"unauthorized\"}");
			return;
		}
		p_request->send(200, "application/json", "{\"result\":\"authorized\"}");
	});
}

// --------------------------------------------------
// 21. /api/config/save & /api/config/dirty
// --------------------------------------------------
// void CL_W10_WebAPI::routeConfigSave() {

//}

void CL_W10_WebAPI::routeConfigDirtySave() {
	s_server->on(W10_Const::HTTP_API_CONFIG_SAVE, HTTP_POST, [](AsyncWebServerRequest* p_request) {
		if (!checkApiKey(p_request)) {
			p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
			return;
		}
		CL_C10_ConfigManager::saveDirtyConfigs();
		p_request->send(200, "application/json", "{\"result\":\"saved\",\"status\":\"clean\"}");
	});

	s_server->on(W10_Const::HTTP_API_CONFIG_DIRTY, HTTP_GET, [](AsyncWebServerRequest* p_request) {
		if (!checkApiKey(p_request)) {
			p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
			return;
		}
		JsonDocument v_doc;
		CL_C10_ConfigManager::getDirtyStatus(v_doc);
		sendJson(p_request, v_doc);
	});
}

// --------------------------------------------------
// 통합된 /api/network/wifi/config (GET/POST)
// --------------------------------------------------
void CL_W10_WebAPI::routeWifiConfig() {
	// GET: 현재 설정 조회 (기존 routeWifi의 GET 기능 통합)
	s_server->on(W10_Const::HTTP_API_WIFI_CONFIG, HTTP_GET, [](AsyncWebServerRequest* p_request) {
		if (!checkApiKey(p_request)) {
			p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
			return;
		}
		JsonDocument v_doc;
		if (g_A20_config_root.wifi) {
			CL_C10_ConfigManager::toJson_Wifi(*g_A20_config_root.wifi, v_doc);
		}
		sendJson(p_request, v_doc);
	});

	// POST: 설정 변경 및 시스템 즉시 적용 (기존 routeWifiConfig의 POST)
	s_server->on(
		W10_Const::HTTP_API_WIFI_CONFIG, HTTP_POST, [](AsyncWebServerRequest* p_request) {}, nullptr, [](AsyncWebServerRequest* p_request, uint8_t* p_data, size_t p_len, size_t p_index, size_t p_total) {
            if (!checkApiKey(p_request)) {
                p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
                return;
            }
            if (p_index + p_len != p_total)
                return;

            JsonDocument v_doc;
            if (!parseJsonBody(p_request, p_data, p_len, v_doc)) {
                p_request->send(400, "application/json", "{\"error\":\"json parse\"}");
                return;
            }

            bool v_changed = false;
            if (g_A20_config_root.wifi) {
                v_changed = CL_C10_ConfigManager::patchWifiFromJson(*g_A20_config_root.wifi, v_doc);
            }

            JsonDocument v_res;
            v_res["updated"] = v_changed;

            if (v_changed) {
                // 1. 변경된 설정을 비휘발성 메모리에 마킹(Save)
                CL_C10_ConfigManager::saveDirtyConfigs();
                // 2. WiFiManager 모듈에 실제 설정 즉시 투입
                CL_WF10_WiFiManager::applyConfig(*g_A20_config_root.wifi);

                v_res["status"]      = "applied";
                v_res["need_reboot"] = true;
                CL_D10_Logger::log(EN_L10_LOG_INFO, "[W10] WiFi config integrated & applied via config endpoint.");
            } else {
                v_res["status"]      = "no_change";
                v_res["need_reboot"] = false;
            }

            sendJson(p_request, v_res); });
}

/*
// --------------------------------------------------
// 22. /api/network/wifi/config (WiFi 설정 저장 + WiFiManager 적용)
// --------------------------------------------------
void CL_W10_WebAPI::routeWifiConfig() {
	s_server->on("/api/network/wifi/config", HTTP_POST,
				 [](AsyncWebServerRequest* p_request) {},
				 nullptr,
				 [](AsyncWebServerRequest* p_request, uint8_t* p_data,
					size_t p_len, size_t p_index, size_t p_total) {
					 if (!checkApiKey(p_request)) {
						 p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
						 return;
					 }
					 if (p_index + p_len != p_total)
						 return;

					 JsonDocument v_doc;
					 if (!parseJsonBody(p_request, p_data, p_len, v_doc)) {
						 p_request->send(400, "application/json", "{\"error\":\"json parse\"}");
						 return;
					 }

					 bool v_changed = false;
					 if (g_A20_config_root.wifi) {
						 v_changed = CL_C10_ConfigManager::patchWifiFromJson(
							 *g_A20_config_root.wifi,
							 v_doc);
					 }

					 JsonDocument v_res;
					 v_res["updated"] = v_changed;

					 if (v_changed) {
						 CL_C10_ConfigManager::saveDirtyConfigs();
						 // WiFiManager에 실제 설정 적용
						 CL_WF10_WiFiManager::applyConfig(*g_A20_config_root.wifi);
						 v_res["status"]	  = "applied";
						 v_res["need_reboot"] = true;
						 CL_D10_Logger::log(EN_L10_LOG_INFO,
											"[W10] WiFi config updated and applied via /api/network/wifi/config.");
					 } else {
						 v_res["status"]	  = "no_change";
						 v_res["need_reboot"] = false;
					 }

					 sendJson(p_request, v_res);
				 });
}
*/
// --------------------------------------------------
// 23. /api/system/time/set (시간 설정 저장 + TimeManager 적용)
// --------------------------------------------------
void CL_W10_WebAPI::routeTimeSet() {
	s_server->on(
		W10_Const::HTTP_API_TIME_SET, HTTP_POST, [](AsyncWebServerRequest* p_request) {}, nullptr, [](AsyncWebServerRequest* p_request, uint8_t* p_data, size_t p_len, size_t p_index, size_t p_total) {
            if (!checkApiKey(p_request)) {
                p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
                return;
            }
            if (p_index + p_len != p_total)
                return;

            JsonDocument v_doc;
            if (!parseJsonBody(p_request, p_data, p_len, v_doc)) {
                p_request->send(400, "application/json", "{\"error\":\"json parse\"}");
                return;
            }

            bool v_changed = false;
            if (g_A20_config_root.system) {
                v_changed = CL_C10_ConfigManager::patchSystemFromJson(*g_A20_config_root.system, v_doc);
            }

            JsonDocument v_res;
            v_res["updated"] = v_changed;

            if (v_changed && g_A20_config_root.system) {
                CL_C10_ConfigManager::saveDirtyConfigs();

                WF10_applyTimeConfigFromSystem(*g_A20_config_root.system);
                v_res["status"] = "applied";
                CL_D10_Logger::log(EN_L10_LOG_INFO, "[W10] Time config updated and applied via TimeManager.");
            } else {
                v_res["status"] = "no_change";
            }

            sendJson(p_request, v_res); });
}

// --------------------------------------------------
// 24. /api/system/firmware/check
// --------------------------------------------------
void CL_W10_WebAPI::routeFirmwareCheck() {
	s_server->on(W10_Const::HTTP_API_FW_CHECK, HTTP_GET, [](AsyncWebServerRequest* p_request) {
		if (!checkApiKey(p_request)) {
			p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
			return;
		}
		JsonDocument v_doc;

		const char*	 v_current_version = A20_Const::FW_VERSION;
		const char*	 v_latest_version  = "V1.0.1";	// TODO: 실제 OTA 서버 연동

		v_doc["current_version"]	   = v_current_version;

		if (strcmp(v_current_version, v_latest_version) < 0) {
			v_doc["status"]			= "available";
			v_doc["latest_version"] = v_latest_version;
			v_doc["url"]			= "/api/update/latest";
		} else {
			v_doc["status"]			= "latest";
			v_doc["latest_version"] = v_current_version;
		}

		sendJson(p_request, v_doc);
	});
}
