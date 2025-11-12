#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : W10_WebAPI_023.h
 * 모듈 약어 : W10
 * 모듈명 : Smart Nature Wind Web API (v023)
 * ------------------------------------------------------
 * 기능 요약:
 * - Web UI / REST API 엔드포인트 집약 모듈
 * - System / WiFi / Motion / WindProfile / Schedules / UserProfiles 조회 및 일부 관리
 * - CT10(ControlManager), C10(ConfigManager), S10(Simulation), N10(NVS), Logger 연동
 * - Preset × Style × Adjust 기반 바람 프로파일 Override 제어
 * - UserProfile 선택 / AutoOff 포함 운전제어 상태 조회
 * ------------------------------------------------------
 * [구현 규칙]
 * - 항상 소스 시작 주석 부분 체계 유지 및 내용 업데이트
 * - 소스 시작 주석 부분 구현규칙, 코드네이밍규칙 내용 그대로 유지, 수정금지
 * - ArduinoJson v7.x.x 사용 (v6 이하 사용 금지)
 * - JsonDocument 단일 타입만 사용
 * - createNestedArray/Object/containsKey 사용 금지
 * - memset + strlcpy 기반 안전 초기화
 * - 주석/필드명은 JSON 구조와 동일하게 유지
 * - 모듈별 단일 헤더(h)파일로만 구성 (cpp 없음) (→ 이번 요청으로 분리)
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
#include <WiFi.h>
#include <WiFiMulti.h>

// 필요한 모듈 헤더 선언
#include "A10_Const_015.h"
#include "C10_ConfigManager_023.h"
#include "CT10_ControlManager_021.h"
#include "D10_Logger_016.h"
#include "M10_MotionLogic_016.h"
#include "N10_NvsManager_017.h"
#include "P10_PWM_ctrl_014.h"
#include "S10_Simulation_018.h"

class CL_W10_WebAPI {
   public:
	// --------------------------------------------------
	// 초기화
	// --------------------------------------------------
	static void begin(AsyncWebServer& p_server, CL_CT10_ControlManager& p_control);

	// --------------------------------------------------
	// WebSocket 브로드캐스트 함수 (구현은 CPP)
	// --------------------------------------------------
	static void broadcastState(JsonDocument& p_doc, bool p_diffOnly = false);
	static void broadcastMetrics(JsonDocument& p_doc, bool p_diffOnly = false);
	static void broadcastChart(JsonDocument& p_doc, bool p_diffOnly = false);
	// (기존 broadcastLog는 D10_Logger에서 처리하므로 주석 처리 또는 D10의 attachWebSocket 확인)
	// static void broadcastLog(const char* p_msg); 

   private:
	// --------------------------------------------------
	// 정적 멤버 변수 선언
	// --------------------------------------------------
	static AsyncWebServer*		   s_server;
	static CL_CT10_ControlManager* s_control;

	// WebSocket 객체 (정의 및 경로 초기화는 CPP에서)
	static AsyncWebSocket s_wsLogs;
	static AsyncWebSocket s_wsState;
	static AsyncWebSocket s_wsChart;
	static AsyncWebSocket s_wsMetrics;

	// WebSocket 포인터 (begin()에서 연결)
	static AsyncWebSocket* s_wsServerState;
	static AsyncWebSocket* s_wsServerLog;
	static AsyncWebSocket* s_wsServerChart;
	static AsyncWebSocket* s_wsServerMetrics;

	// --------------------------------------------------
	// 공통 유틸리티 함수 선언
	// --------------------------------------------------
	static bool checkApiKey(AsyncWebServerRequest* p_request);
	static void sendJson(AsyncWebServerRequest* p_request, JsonDocument& p_doc, int p_code = 200);
	static bool parseJsonBody(AsyncWebServerRequest* p_request,
							  uint8_t* p_data, size_t p_len,
							  JsonDocument& p_doc);

	// --------------------------------------------------
	// REST API 라우팅 함수 선언
	// --------------------------------------------------
	static void routeVersion();
	static void routeState();
	static void routeSystem();
	static void routeWifi();
	static void routeMotion();
	static void routeWindProfile();
	static void routeSchedules();
	static void routeUserProfiles();
	static void routeControl();
	static void routeSimulation();		// /api/sim/chart
	static void routeSimState();		// /api/sim/state
	static void routeControlSummary();	// /api/control/summary
	static void routeMetrics();			// /api/metrics
	static void routeLogs();
	static void routeReload();

	// --------------------------------------------------
	// WebSocket 라우팅 함수 선언
	// --------------------------------------------------
	static void routeWebSocket();
};
