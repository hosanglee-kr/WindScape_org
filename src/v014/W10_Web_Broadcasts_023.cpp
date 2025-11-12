/*
 * ------------------------------------------------------
 * 소스명 : W10_Web_Broadcasts_023.cpp
 * 모듈 약어 : W10
 * 모듈명 : Smart Nature Wind Web API (v023) - Broadcast Implementation
 * ------------------------------------------------------
 * 기능 요약:
 * - WebSocket을 통한 실시간 상태(State, Metrics, Chart) 브로드캐스트 구현
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
// 상태 브로드캐스트 (diffOnly 비교 적용)
// --------------------------------------------------
void CL_W10_WebAPI::broadcastState(JsonDocument& p_doc, bool p_diffOnly) {
	if (!s_wsServerState)
		return;

	static String s_lastStateJson;	// 이전 상태 스냅샷
	String		  v_msg;
	serializeJson(p_doc, v_msg);

	// diffOnly 모드일 때 동일 데이터는 송신 생략
	if (p_diffOnly && v_msg == s_lastStateJson) {
		CL_D10_Logger::log(EN_L10_LOG_DEBUG, "[W10] broadcastState() diffOnly skip (identical)");
		return;
	}
	s_lastStateJson = v_msg;

	for (auto& c : s_wsServerState->getClients()) {
		if (c.canSend())
			c.text(v_msg);
	}

	CL_D10_Logger::log(EN_L10_LOG_DEBUG,
					   "[W10] broadcastState(diffOnly=%d) → %d clients",
					   p_diffOnly ? 1 : 0,
					   s_wsServerState->count());
}

// --------------------------------------------------
// Metrics 브로드캐스트 (diffOnly 비교 적용)
// --------------------------------------------------
void CL_W10_WebAPI::broadcastMetrics(JsonDocument& p_doc, bool p_diffOnly) {
	if (!s_wsServerMetrics)
		return;

	static String s_lastMetricsJson;  // 이전 메트릭 상태
	String		  v_msg;
	serializeJson(p_doc, v_msg);

	if (p_diffOnly && v_msg == s_lastMetricsJson) {
		CL_D10_Logger::log(EN_L10_LOG_DEBUG, "[W10] broadcastMetrics() skip (no diff)");
		return;
	}
	s_lastMetricsJson = v_msg;

	for (auto& c : s_wsServerMetrics->getClients()) {
		if (c.canSend())
			c.text(v_msg);
	}
	CL_D10_Logger::log(EN_L10_LOG_DEBUG,
					   "[W10] broadcastMetrics(diffOnly=%d) → %d clients",
					   p_diffOnly ? 1 : 0,
					   s_wsServerMetrics->count());
}

// --------------------------------------------------
// Chart 브로드캐스트 (diffOnly 비교 적용)
// --------------------------------------------------
void CL_W10_WebAPI::broadcastChart(JsonDocument& p_doc, bool p_diffOnly) {
	if (!s_wsServerChart)
		return;

	static String s_lastChartJson;
	String		  v_msg;
	serializeJson(p_doc, v_msg);

	if (p_diffOnly && v_msg == s_lastChartJson) {
		CL_D10_Logger::log(EN_L10_LOG_DEBUG, "[W10] broadcastChart() skip (no diff)");
		return;
	}
	s_lastChartJson = v_msg;

	for (auto& c : s_wsServerChart->getClients()) {
		if (c.canSend())
			c.text(v_msg);
	}
}

