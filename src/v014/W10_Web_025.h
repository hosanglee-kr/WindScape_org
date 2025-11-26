/*
 * ------------------------------------------------------
 * 소스명 : W10_Web_025.h
 * 모듈약어 : W10
 * 모듈명 : Smart Nature Wind Web API Manager (v024)
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

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <WiFiMulti.h> // WiFiMulti 추가
#include <Update.h>    // OTA를 위해 추가

#include "A10_Const_015.h"
#include "C10_ConfigManager_024.h"
#include "D10_Logger_016.h"
#include "CT10_ControlManager_022.h"
#include "N10_NvsManager_019.h"
#include "WF10_WiFiManager_023.h"

// ------------------------------------------------------
// WebAPI Manager
// ------------------------------------------------------
class CL_W10_WebAPI {
public:
	// --------------------------------------------------
	// 초기화
	// --------------------------------------------------
	// v024: WiFiMulti 인자 추가 (v012 복구)
	static void begin(AsyncWebServer& p_server, CL_CT10_ControlManager& p_control, WiFiMulti& p_multi);

	// --------------------------------------------------
	// 브로드캐스트 (Broadcasts.cpp)
	// --------------------------------------------------
	static void broadcastState(JsonDocument& p_doc, bool p_diffOnly = true);
	static void broadcastMetrics(JsonDocument& p_doc, bool p_diffOnly = true);
	static void broadcastChart(JsonDocument& p_doc, bool p_diffOnly = true);

private:
	// --------------------------------------------------
	// 정적 멤버 (Routes.cpp, WebSockets.cpp, Broadcasts.cpp 공유)
	// --------------------------------------------------
	static AsyncWebServer*		   s_server;
	static CL_CT10_ControlManager* s_control;
	static WiFiMulti*			   s_multi;	 // v012 복구: /api/scan 처리를 위해 필요
	static File					   s_upFile; // v012 복구: /upload 처리를 위해 필요

	// WebSocket Servers (WebSockets.cpp 정의, Broadcasts.cpp 사용)
	static AsyncWebSocket* s_wsServerState;
	static AsyncWebSocket* s_wsServerLogs;
	static AsyncWebSocket* s_wsServerChart;
	static AsyncWebSocket* s_wsServerMetrics;

	// --------------------------------------------------
	// 라우팅 선언 (Routes.cpp)
	// --------------------------------------------------
	// --------------------------------------------------
	// 1. 시스템 정보 조회 및 진단 (GET)
	// --------------------------------------------------
	static void routeVersion();      // GET /api/version: 펌웨어/모듈 버전 정보 조회
	static void routeState();        // GET /api/state: 장치의 현재 구동 상태 요약 조회
	static void routeDiag();	     // GET /api/diag: 힙 메모리, 파일 시스템 사용량 등 진단 정보 조회
	static void routeMetrics();      // GET /api/metrics: 성능 지표 및 메모리 상태 조회
	static void routeLogs();         // GET /api/logs: 시스템 로그 목록 조회
	static void routeAuthTest();     // GET /api/auth/test: API Key 인증 테스트

	// --------------------------------------------------
	// 2. 설정 조회 및 패치 (GET/POST)
	// --------------------------------------------------
	static void routeSystem();       // GET/POST /api/system: 시스템 공통 설정 조회/수정 (패치)
	static void routeWifi();         // GET/POST /api/wifi: Wi-Fi 연결 설정 조회/수정 (패치)
	static void routeMotion();       // GET/POST /api/motion: 모션 감지 설정 조회/수정 (패치)
	static void routeUserProfiles(); // GET/POST /api/user_profiles: 사용자 프로필 설정 조회/수정 (패치)

    // --------------------------------------------------
	// 3. 설정 관리 및 저장/적용 (POST/GET)
	// --------------------------------------------------
	static void routeConfigSave();   // POST /api/config/save: 메모리 설정값을 파일에 저장
    static void routeConfigDirty();  // GET /api/config/dirty: 저장되지 않은 변경 사항(Dirty) 목록 조회
	static void routeReload();       // POST /api/reload: 설정 파일 전체 재로드 (메모리 적용)
	static void routeConfigInit();   // POST /api/config/init: 모든 설정을 기본값으로 초기화 및 재부팅

    // --------------------------------------------------
	// 4. 네트워크 및 펌웨어 관리 (GET/POST)
	// --------------------------------------------------
	static void routeScan();	     // GET /api/scan: 주변 Wi-Fi 네트워크 스캔 결과 조회
    static void routeWifiConfig();   // POST /api/network/wifi/config: Wi-Fi 설정 상세 저장 및 적용
	static void routeTimeSet();      // POST /api/system/time/set: NTP 및 시간대 설정 저장 및 적용
	static void routeFirmwareCheck();// GET /api/system/firmware/check: 펌웨어 업데이트 여부 확인
	static void routeUpload();		 // POST /upload: 파일 업로드 처리 (FS/OTA 파일 업로드)
	static void routeUpdate();		 // POST /update: OTA 펌웨어 업데이트 실행

	// --------------------------------------------------
	// 5. 제어 및 상태 요약 (POST/GET)
	// --------------------------------------------------
	static void routeControl();      // POST /api/control/*: 프로필 시작/정지, 오버라이드, 재부팅 등 제어 명령
	static void routeControlSummary();// GET /api/control/summary: 대시보드 표시용 핵심 요약 정보 조회
	static void routeMotionFeed();   // POST /api/motion/feed: 외부 센서(PIR/BLE) 데이터 수신

	// --------------------------------------------------
	// 6. 시뮬레이션 제어 (GET/POST)
	// --------------------------------------------------
	static void routeSimulation();   // GET/POST /api/simulation: 시뮬레이션 설정 조회/수정 (패치)
	static void routeSimState();     // GET/POST /api/sim/state: 시뮬레이션 상세 상태 및 시작/정지 제어

	// --------------------------------------------------
	// 7. CRUD: Wind Profiles (바람 프로파일)
	// --------------------------------------------------
	static void routeWindProfile();  // GET /api/windProfile: 전체 목록 조회 | POST /api/windProfile: 신규 생성
	static void routeWindProfileID();// PUT/DELETE /api/windProfile/{id}: 프로파일 수정/삭제

	// --------------------------------------------------
	// 8. CRUD: Schedules (스케줄)
	// --------------------------------------------------
	static void routeSchedules();    // GET /api/schedules: 전체 목록 조회 | POST /api/schedules: 신규 생성
	static void routeSchedulesID();  // PUT/DELETE /api/schedules/{id}: 스케줄 수정/삭제

	// --------------------------------------------------
	// 9. 정적 파일 및 웹소켓 (분리된 파일에서 구현)
	// --------------------------------------------------
	static void routeStaticAssets(); // GET /*: 정적 웹 파일 (HTML, JS, CSS 등) 제공
	static void routeWebSocket();	 // GET /ws/*: 웹소켓 연결 처리


/*
	static void routeVersion();
	static void routeState();
	static void routeSystem();
	static void routeWifi();
	static void routeMotion();
	static void routeWindProfile();
	static void routeSchedules();
	static void routeUserProfiles();
	static void routeControl();
	static void routeSimulation();
	static void routeSimState();
	static void routeControlSummary();
	static void routeMetrics();
	static void routeLogs();
	static void routeReload();
    static void routeConfigSave(); 

	// --- v012에서 복구된 REST API 라우트 선언 ---
	static void routeDiag();	  // /api/diag
	static void routeScan();	  // /api/scan
	static void routeConfigInit(); // /api/config/init
	static void routeMotionFeed(); // /api/motion/feed

    static void routeWifiConfig();    // 23. /api/network/wifi/config (POST)
	static void routeTimeSet();       // 24. /api/system/time/set (POST)
	static void routeFirmwareCheck(); // 25. /api/system/firmware/check (GET)

	// --- 신규 분리 모듈 라우트 선언 ---
	static void routeWebSocket();		// (WebSockets.cpp)
	static void routeStaticAssets();	// (W10_Web_Static_024.cpp)
	static void routeUpload();			// (W10_Web_Upload_024.cpp - /upload)
	static void routeUpdate();			// (W10_Web_Upload_024.cpp - /update)
*/
	// --------------------------------------------------
	// 유틸리티 함수
	// --------------------------------------------------
    static void _broadcast(AsyncWebSocket* p_ws, JsonDocument& p_doc, bool p_diffOnly);
	
	
	// v012의 _applyHeaders 로직 복구 및 send 함수에 적용
	static inline void _applyHeaders(AsyncWebServerResponse* p_response, bool p_nocache) {
		if (p_nocache) {
			p_response->addHeader("Cache-Control", "no-store");
			p_response->addHeader("Pragma", "no-cache");
		}
		p_response->addHeader("Access-Control-Allow-Origin", "*");
		p_response->addHeader("Access-Control-Allow-Headers", "Content-Type, X-API-Key");
		p_response->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
	}

	static inline void sendJson(AsyncWebServerRequest* p_request, JsonDocument& p_doc, int p_code = 200) {
		String v_out;
		serializeJson(p_doc, v_out);
		auto* v_resp = p_request->beginResponse(p_code, "application/json", v_out);
		_applyHeaders(v_resp, true);
		p_request->send(v_resp);
	}

	static inline void sendText(AsyncWebServerRequest* p_request, const String& p_msg, int p_code = 200) {
		auto* v_resp = p_request->beginResponse(p_code, "application/json", p_msg);
		_applyHeaders(v_resp, true);
		p_request->send(v_resp);
	}

    static inline bool checkApiKey(AsyncWebServerRequest* p_request) {
		// system.security.api_key 가 비어있으면 검사 생략
		const char* v_key = g_A10_config_root.system->security.api_key;
		if (!v_key || v_key[0] == '\0')
			return true;

		if (!p_request->hasHeader("X-API-Key"))
			return false;
		String v_val = p_request->getHeader("X-API-Key")->value();
		return (v_val == v_key);
	}

    static inline bool parseJsonBody(AsyncWebServerRequest* p_request,
									 uint8_t* p_data, size_t p_len,
									 JsonDocument& p_doc) {
		auto v_err = deserializeJson(p_doc, (const char*)p_data, p_len);
		if (v_err) {
			CL_D10_Logger::log(EN_L10_LOG_WARN,
							   "[W10] JSON parse error: %s",
							   v_err.c_str());
			return false;
		}
		return true;
	}


};
