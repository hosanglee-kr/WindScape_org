#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : W10_Web_050.h
 * 모듈약어 : W10
 * 모듈명 : Smart Nature Wind Web API Manager (v030)
 * ------------------------------------------------------
 * 기능 요약:
 * - REST API 엔드포인트 선언 및 유틸리티 함수 포함.
 * - WebSocket 정적 멤버 포인터 포함.
 * - ArduinoJson v7.4.x 사용
 * ------------------------------------------------------
 * [구현 규칙]
 * - 항상 소스 시작 주석 체계 유지 및 내용 업데이트
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

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <Update.h>		// OTA
#include <WiFiMulti.h>	// WiFiMulti

#include <vector>

#include "A20_Const_040.h"
#include "C10_Config_040.h"
#include "CT10_Control_040.h"
#include "D10_Logger_040.h"
#include "N10_NvsManager_040.h"
#include "W10_Web_Const_040.h"
#include "WF10_WiFiManager_040.h"

// ------------------------------------------------------
// WebAPI Manager
// ------------------------------------------------------
class CL_W10_WebAPI {
  public:
	// --------------------------------------------------
	// 초기화
	// --------------------------------------------------
	// v029: WiFiMulti 인자 포함
	static void begin(AsyncWebServer& p_server, CL_CT10_ControlManager& p_control, WiFiMulti& p_multi);

	// --------------------------------------------------
	// 브로드캐스트 (WebSockets.cpp)
	// --------------------------------------------------
	static void broadcastState(JsonDocument& p_doc, bool p_diffOnly = true);
	static void broadcastMetrics(JsonDocument& p_doc, bool p_diffOnly = true);
	static void broadcastChart(JsonDocument& p_doc, bool p_diffOnly = true);

  private:
	// --------------------------------------------------
	// 정적 멤버 (Routes.cpp, WebSockets.cpp, Static.cpp, Upload.cpp 공유)
	// --------------------------------------------------
	static AsyncWebServer*		   s_server;
	static CL_CT10_ControlManager* s_control;
	static WiFiMulti*			   s_multi;

	static fs::File				   s_upFile;
	// static File					   s_upFile;

	// WebSocket Servers
	static AsyncWebSocket*		   s_wsServerState;
	static AsyncWebSocket*		   s_wsServerLogs;
	static AsyncWebSocket*		   s_wsServerChart;
	static AsyncWebSocket*		   s_wsServerMetrics;

	// --------------------------------------------------
	// Static Assets & Menu (W10_Web_Static_032.cpp)
	// --------------------------------------------------

	// JSON 파일 경로 (v032)
	static const char*			   G_W10_PAGES_JSON;

	// 페이지 엔트리 구조체 (메뉴 및 메타 정보용)
	struct ST_W10_PageEntry_t {
		int	   order;
		String uri;
		String path;
		String label;
		bool   isMain;
		bool   enable;
	};

	// 메뉴 API에 필요한 전역 상태 (정적 멤버로 관리)
	struct ST_W10_MenuState_t {
		std::vector<ST_W10_PageEntry_t> pages_sorted;  // /api/v1/menu 응답용 정렬된 리스트
		String							main_path;	   // isMain == true 인 첫 번째 페이지 path

		ST_W10_MenuState_t() : pages_sorted(), main_path() {
		}
	};

	// 메뉴 상태 (Static.cpp에서만 사용)
	static ST_W10_MenuState_t s_menu_state;

	// 헬퍼 함수 (Static.cpp 내부에서만 사용)
	static char* W10_allocCString(const char* p_src);
	static const char* W10_guessMime(const char* p_path);
	static bool W10_loadPagesJson(JsonDocument& p_doc, uint16_t& p_page_count, uint16_t& p_asset_count);
	static void W10_registerStaticRoute(const char* p_uri, const char* p_file, const char* p_mime);
	static void W10_getMenuJson(AsyncWebServerRequest* r);

	static String getUploadPath(const String& p_filename);

	// --------------------------------------------------
	// 라우팅 선언 (Routes.cpp)
	// --------------------------------------------------
	// 1. 시스템 정보 조회 및 진단 (GET)
	static void routeVersion();	  // GET /api/version
	static void routeState();	  // GET /api/state
	static void routeDiag();	  // GET /api/diag
	static void routeMetrics();	  // GET /api/metrics
	static void routeLogs();	  // GET /api/logs
	static void routeAuthTest();  // GET /api/auth/test

	// 2. 설정 조회 및 패치/CRUD (GET/POST/PUT/DELETE)
	static void routeSystem();	// GET/POST /api/system
	// static void routeWifi();          // GET/POST /api/wifi
	static void routeMotion();			   // GET/POST /api/motion
	static void routeUserProfiles();	   // GET/POST /api/user_profiles (목록조회/신규생성)
	static void routeUserProfilesID();	   // PUT/DELETE /api/user_profiles/{id}
	static void routeUserProfilesPatch();  // POST /api/user_profiles/patch (배치 패치)

	// 3. 설정 관리 및 저장/적용 (POST/GET)
	// static void routeConfigSave();    // POST /api/config/save
	static void routeConfigDirtySave();	 // GET /api/config/dirty
	static void routeReload();			 // POST /api/reload
	static void routeConfigInit();		 // POST /api/config/init

	// 4. 네트워크 및 펌웨어 관리 (GET/POST)
	static void routeScan();		   // GET /api/scan
	static void routeWifiConfig();	   // POST /api/network/wifi/config
	static void routeTimeSet();		   // POST /api/system/time/set
	static void routeFirmwareCheck();  // GET /api/system/firmware/check
	static void routeUpload();		   // POST /upload
	static void routeUpdate();		   // POST /update

	// 5. 제어 및 상태 요약 (POST/GET)
	static void routeControl();			// 여러 제어용 /api/control/*
	static void routeControlSummary();	// GET /api/control/summary
	static void routeMotionFeed();		// POST /api/motion/pir/feed, /api/motion/ble/feed

	// 6. 시뮬레이션 제어 (GET/POST)
	static void routeSimulation();	// GET/POST /api/simulation
	static void routeSimState();	// GET /api/sim/state

	// 7. CRUD: Wind Profiles
	static void routeWindProfile();	   // GET/POST /api/windProfile
	static void routeWindProfileID();  // PUT/DELETE /api/windProfile/{id}

	// 8. CRUD: Schedules
	static void routeSchedules();	 // GET/POST /api/schedules
	static void routeSchedulesID();	 // PUT/DELETE /api/schedules/{id}

	// 9. 정적 파일 및 웹소켓
	static void routeStaticAssets();  // JSON 기반 static routes
	static void routeWebSocket();	  // WS 라우트 초기화

  public:
	// --------------------------------------------------
	// 유틸리티 함수
	// --------------------------------------------------
	static void _broadcast(AsyncWebSocket* p_ws, JsonDocument& p_doc, bool p_diffOnly);

	// 공통 헤더 적용
	static inline void _applyHeaders(AsyncWebServerResponse* p_response, bool p_nocache) {
		if (p_nocache) {
			p_response->addHeader("Cache-Control", "no-store");
			p_response->addHeader("Pragma", "no-cache");
		}
		p_response->addHeader("Access-Control-Allow-Origin", "*");
		p_response->addHeader("Access-Control-Allow-Headers", "Content-Type, X-API-Key");
		p_response->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
	}

	static inline void sendJson(AsyncWebServerRequest* p_request, JsonDocument& p_doc, int p_code = 200) {
		String v_out;
		serializeJson(p_doc, v_out);

		// ✅ UTF-8 강제
		auto* v_resp = p_request->beginResponse(p_code, "application/json; charset=utf-8", v_out);

		_applyHeaders(v_resp, true);
		p_request->send(v_resp);
	}

	// W10_Web_040.h 안에서 기존 sendText 교체
	static inline void sendText(AsyncWebServerRequest* p_request, const String& p_msg, int p_code = 200, const char* p_mime = "text/plain; charset=utf-8") {
		auto* v_resp = p_request->beginResponse(p_code, p_mime, p_msg);
		_applyHeaders(v_resp, true);
		p_request->send(v_resp);
	}

	// API Key 검사
	static inline bool checkApiKey(AsyncWebServerRequest* p_request) {
		const char* v_key = nullptr;
		if (g_A20_config_root.system && g_A20_config_root.system->security.api_key[0] != '\0') {
			v_key = g_A20_config_root.system->security.api_key;
		}
		if (!v_key || v_key[0] == '\0') {
			return true;  // API 키 비활성화 상태
		}

		if (!p_request->hasHeader("X-API-Key"))
			return false;
		String v_val = p_request->getHeader("X-API-Key")->value();
		return (v_val == v_key);
	}

	// JSON Body 파싱
	static inline bool parseJsonBody(AsyncWebServerRequest* p_request, uint8_t* p_data, size_t p_len, JsonDocument& p_doc) {
		auto v_err = deserializeJson(p_doc, (const char*)p_data, p_len);
		if (v_err) {
			CL_D10_Logger::log(EN_L10_LOG_WARN, "[W10] JSON parse error: %s", v_err.c_str());
			return false;
		}
		return true;
	}
};
