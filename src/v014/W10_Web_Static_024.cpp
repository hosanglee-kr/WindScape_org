/*
 * ------------------------------------------------------
 * 소스명 : W10_Web_Static_024.cpp
 * 모듈 약어 : W10
 * 모듈명 : Smart Nature Wind Web API (v024) - Static Assets Implementation
 * ------------------------------------------------------
 * 기능 요약:
 * - LittleFS에 저장된 Web UI (HTML/CSS/JS) 파일 라우팅 구현
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

#include "W10_Web_024.h"

// ------------------------------------------------------
// 정적 페이지 경로 (v012 Const 복구)
// ------------------------------------------------------
namespace W10_Const {
constexpr char MAIN_HTML[] = "/html/SC10_main_021.html";
constexpr char MAIN_CSS[]  = "/html/SC10_main_021.css";
constexpr char MAIN_JS[]   = "/html/SC10_main_021.js";

constexpr char CHART_HTML[] = "/html/SC10_chart_003.html";
constexpr char CHART_CSS[]  = "/html/SC10_chart_003.css";
constexpr char CHART_JS[]   = "/html/SC10_chart_003.js";
}

// ------------------------------------------------------
// Static routing table (v012 복구)
// ------------------------------------------------------
struct ST_W10_Route_t {
	const char* uri;
	const char* file;
	const char* mime;
};

// 최대 16개 경로만 지원하는 v012 로직을 복구
static ST_W10_Route_t s_routes_static[16];
static uint8_t s_routeCnt_static = 0;

static void W10_pushRoute(const char* p_uri, const char* p_file, const char* p_mime) {
	if (s_routeCnt_static < 16)
		s_routes_static[s_routeCnt_static++] = {p_uri, p_file, p_mime};
}

// ------------------------------------------------------
// 정적 자산 라우팅 초기화
// ------------------------------------------------------
void CL_W10_WebAPI::routeStaticAssets() {
	// v012의 _buildRoutes 로직 복구
	auto& v_web = g_A10_config_root.system->web;

	auto v_addRoute = [&](const char* p_cfg, const char* p_def, const char* p_mime) {
		if (strlen(p_cfg) > 0)
			W10_pushRoute(p_cfg, p_cfg, p_mime);
		W10_pushRoute(p_def, p_def, p_mime);
	};

	v_addRoute(v_web.html, W10_Const::MAIN_HTML, "text/html");
	v_addRoute(v_web.css, W10_Const::MAIN_CSS, "text/css");
	v_addRoute(v_web.js, W10_Const::MAIN_JS, "application/javascript");

	// 차트 페이지는 설정에서 오버라이드 불가
	W10_pushRoute("/chart", W10_Const::CHART_HTML, "text/html");
	W10_pushRoute(W10_Const::CHART_HTML, W10_Const::CHART_HTML, "text/html");
	W10_pushRoute(W10_Const::CHART_CSS, W10_Const::CHART_CSS, "text/css");
	W10_pushRoute(W10_Const::CHART_JS, W10_Const::CHART_JS, "application/javascript");

	// 1. 루트 경로 '/' 리다이렉트
	s_server->on("/", HTTP_GET, [](AsyncWebServerRequest* r) {
		const char* f = g_A10_config_root.system->web.html;
		if (strlen(f) && LittleFS.exists(f))
			r->redirect(f);
		else
			r->redirect(W10_Const::MAIN_HTML);
	});

	// 2. 등록된 정적 파일 경로 마운트 (v012의 _mountStatic 로직 복구)
	for (uint8_t v_i = 0; v_i < s_routeCnt_static; v_i++) {
		s_server->on(s_routes_static[v_i].uri, HTTP_GET, [v_i](AsyncWebServerRequest* r) {
			const char* v_file = s_routes_static[v_i].file;
			const char* v_mime = s_routes_static[v_i].mime;
			
			// 인증 검사 생략 (정적 자산은 일반적으로 인증 불필요)
			
			if (LittleFS.exists(v_file)) {
				// 응답 헤더를 적용하기 위해 send 대신 beginResponse 사용
				auto* v_resp = r->beginResponse(LittleFS, v_file, v_mime);
				CL_W10_WebAPI::_applyHeaders(v_resp, false); // 정적 파일이므로 nocache=false
				r->send(v_resp);
				return;
			}
			
			// 파일이 없는 경우 404가 아닌 200/주석 처리된 메시지 반환 (v012 복구)
			String v_msg	 = "/* missing:" + String(v_file) + " */";
			auto* v_resp	 = r->beginResponse(200, v_mime, v_msg);
			CL_W10_WebAPI::_applyHeaders(v_resp, true); // 개발 중이므로 nocache=true
			r->send(v_resp);
		});
	}

	CL_D10_Logger::log(EN_L10_LOG_INFO, "[W10] Static assets routing initialized (%d routes)", s_routeCnt_static);
}

