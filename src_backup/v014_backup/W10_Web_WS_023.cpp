/*
 * ------------------------------------------------------
 * 소스명 : W10_WebAPI_WebSockets_023.cpp
 * 모듈 약어 : W10
 * 모듈명 : Smart Nature Wind Web API (v023) - WebSocket Implementation
 * ------------------------------------------------------
 * 기능 요약:
 * - WebSocket 엔드포인트 설정 및 이벤트 핸들링 구현
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

#include "W10_Web_023.h"

// --------------------------------------------------
// WebSocket 초기화 및 라우팅
// --------------------------------------------------
void CL_W10_WebAPI::routeWebSocket() {
	// 1. 로그 WS 핸들러
	s_wsLogs.onEvent([](AsyncWebSocket*, AsyncWebSocketClient* client,
						AwsEventType type, void*, uint8_t*, size_t) {
		if (type == WS_EVT_CONNECT)
			CL_D10_Logger::log(EN_L10_LOG_INFO, "[W10] WS /logs connected (id=%u)", client->id());
	});
	s_server->addHandler(&s_wsLogs);

	// 2. 상태 WS 핸들러
	s_wsState.onEvent([](AsyncWebSocket*, AsyncWebSocketClient* client,
						 AwsEventType type, void*, uint8_t*, size_t) {
		if (type == WS_EVT_CONNECT) {
			CL_D10_Logger::log(EN_L10_LOG_INFO, "[W10] WS /state connected (id=%u)", client->id());
			JsonDocument v_doc;
			if (s_control)
				s_control->toJson(v_doc);
			String v_json;
			serializeJson(v_doc, v_json);
			client->text(v_json); // 초기 상태 전송
		}
	});
	s_server->addHandler(&s_wsState);

	// 3. 차트 WS 핸들러
	s_wsChart.onEvent([](AsyncWebSocket*, AsyncWebSocketClient* client,
						 AwsEventType type, void*, uint8_t*, size_t) {
		if (type == WS_EVT_CONNECT)
			CL_D10_Logger::log(EN_L10_LOG_INFO, "[W10] WS /chart connected (id=%u)", client->id());
	});
	s_server->addHandler(&s_wsChart);

	// 4. 메트릭 WS 핸들러
	s_wsMetrics.onEvent([](AsyncWebSocket*, AsyncWebSocketClient* client,
						   AwsEventType type, void*, uint8_t*, size_t) {
		if (type == WS_EVT_CONNECT) {
			CL_D10_Logger::log(EN_L10_LOG_INFO, "[W10] WS /metrics connected (id=%u)", client->id());
			if (s_control) {
				JsonDocument v_doc;
				s_control->toMetricsJson(v_doc);
				String v_json;
				serializeJson(v_doc, v_json);
				client->text(v_json); // 초기 메트릭스 전송
			}
		}
	});
	s_server->addHandler(&s_wsMetrics);

	// WebSocket 포인터 연결
	s_wsServerLog	  = &s_wsLogs;
	s_wsServerState	  = &s_wsState;
	s_wsServerChart	  = &s_wsChart;
	s_wsServerMetrics = &s_wsMetrics;

	// Logger 모듈에 WebSocket 연결
	CL_D10_Logger::attachWebSocket(s_wsServerLog);
	CL_D10_Logger::log(EN_L10_LOG_INFO, "[W10] WebSocket routes initialized");
}

