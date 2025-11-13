/*
 * ------------------------------------------------------
 * 소스명 : W10_Web_Broadcasts_025.cpp
 * 모듈 약어 : W10
 * 모듈명 : Smart Nature Wind Web API (v025) - Broadcast Implementation
 * ------------------------------------------------------
 * 기능 요약:
 * - WebSocket을 통한 상태/메트릭/차트 데이터 브로드캐스팅 구현
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
 * - 클래스명              : CL_모듈약어_ 클래스명
 * - 클래스 private 멤버   : _ 접두사
 * - 클래스 멤버(함수/변수) : 모듈약어 접두사 미사용
 * - 클래스 정적 멤버      : s_ 접두사
 * ------------------------------------------------------
 */

#include "W10_Web_025.h" // ✅ v024로 변경

// --------------------------------------------------
// 브로드캐스트 유틸리티 (로직 변경 없음)
// --------------------------------------------------
namespace {
void W10_broadcast(AsyncWebSocket* p_ws, JsonDocument& p_doc, bool p_diffOnly) {
	if (!p_ws || !p_ws->count())
		return;

	String v_json;
	serializeJson(p_doc, v_json);

	// 차후 p_diffOnly 로직 추가 시 이 부분을 수정
	if (!p_diffOnly || v_json.length() > 5) {
		p_ws->textAll(v_json);
	}
}
} // namespace

// --------------------------------------------------
// 상태 브로드캐스트
// --------------------------------------------------
void CL_W10_WebAPI::broadcastState(JsonDocument& p_doc, bool p_diffOnly) {
	W10_broadcast(s_wsServerState, p_doc, p_diffOnly);
}

// --------------------------------------------------
// 메트릭스 브로드캐스트
// --------------------------------------------------
void CL_W10_WebAPI::broadcastMetrics(JsonDocument& p_doc, bool p_diffOnly) {
	W10_broadcast(s_wsServerMetrics, p_doc, p_diffOnly);
}

// --------------------------------------------------
// 차트 브로드캐스트
// --------------------------------------------------
void CL_W10_WebAPI::broadcastChart(JsonDocument& p_doc, bool p_diffOnly) {
	W10_broadcast(s_wsServerChart, p_doc, p_diffOnly);
}

