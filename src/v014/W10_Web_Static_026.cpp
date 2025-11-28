/*
 * ------------------------------------------------------
 * 소스명 : W10_Web_Static_026.cpp
 * 모듈 약어 : W10
 * 모듈명 : Smart Nature Wind Web API (v025) - Static Assets & Menu API Implementation
 * ------------------------------------------------------
 * 기능 요약:
 * - LittleFS에 저장된 Web UI 파일 라우팅을 구조체 배열 기반으로 효율화
 * - Web UI 메뉴 정보를 JSON으로 반환하는 API (/api/v1/menu) 구현
 * - [리팩토링]: 페이지 목록을 간소화된 구조체 배열로 통합 및 경로 동적 생성
 * - [규칙 적용]: 모든 HTML/CSS/JS 파일명이 동일하다는 규칙을 기반으로 구현
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

#include <LittleFS.h> 
#include <ArduinoJson.h> // ArduinoJson V7.4.x 사용

#include "W10_Web_025.h"


// ------------------------------------------------------
// 정적 페이지 관리 구조체 및 목록
// ------------------------------------------------------

// 페이지 파일 경로 정의 구조체 (레이블 추가 및 경로 간소화)
typedef struct {
	const char* uri;   // HTTP 요청 단축 URI (예: "/dashboard")
	const char* base;  // 파일의 기본 이름 (예: "SC10_dashboard_001")
    const char* label; // 페이지 레이블 (메뉴 표시 이름)
	bool isMain;       // 설정 오버라이드 대상 여부
} ST_W10_Page_t;

// [공통 자산 경로]
constexpr char G_W10_COMMON_CSS[] = "/html/SC10_common_001.css";
constexpr char G_W10_COMMON_JS[]  = "/html/SC10_common_001.js";

// 정적 페이지 목록: 기본 파일 이름과 레이블 포함
static const ST_W10_Page_t s_pages_static[] = {
	// uri         , base							, label                , isMain
	{"/"				, "SC10_main_019"			, "Home"				, true  },
	{"/chart_t1"		, "SC10_chart_t1_006"		, "차트 모니터링 T1"	, false }, 
	{"/schedules_t1"	, "SC30_schedules_t1_005"	, "스케줄 관리 T1"		, false }, 

	{"/dashboard"		, "SC10_dashboard_001"		, "대시보드"			, false },
    {"/chart_t2"		, "SC10_chart_t2_007"		, "차트 모니터링 T2"	, false },
	{"/sim_details"		, "SC10_sim_details_001"	, "시뮬 설정"			, false },
	{"/profiles"		, "SC10_profile_001"		, "프로파일 관리"		, false },
	{"/schedules_t2"	, "SC10_schedules_t2_001"	, "스케줄 관리 T2"		, false },
	{"/user_profiles"	, "SC10_user_001"			, "사용자 프로필"		, false },
	{"/settings"		, "SC10_settings_001"		, "시스템 설정"			, false },
	{"/config"			, "SC10_config_025"			, "Config 설정"			, false }, 
	{"/diag"			, "SC10_diag_025"			, "시스템 진단"			, false } 
};
// 페이지 목록 크기 계산
static const uint8_t G_W10_PAGE_COUNT = sizeof(s_pages_static) / sizeof(ST_W10_Page_t);


// ------------------------------------------------------
// Static routing table 및 헬퍼 함수
// ------------------------------------------------------
// 정적 자산 라우팅 정보를 저장하기 위한 구조체
struct ST_W10_Route_t {
	const char* uri; // HTTP 요청 URI 
	const char* file; // LittleFS의 실제 파일 경로 
	const char* mime; // MIME 타입 
};

// 정적 라우팅 테이블 배열 크기 정의
#define G_W10_MAX_ROUTES (G_W10_PAGE_COUNT * 5)
static ST_W10_Route_t s_routes_static[G_W10_MAX_ROUTES];
static uint8_t s_routeCnt_static = 0; // 등록된 라우트 개수

/**
 * @brief 정적 라우팅 테이블에 새 경로를 추가합니다. 배열 크기를 초과하지 않도록 검사합니다.
 */
static void W10_pushRoute(const char* p_uri, const char* p_file, const char* p_mime) {
	if (s_routeCnt_static < G_W10_MAX_ROUTES)
		s_routes_static[s_routeCnt_static++] = {p_uri, p_file, p_mime};
	else
		CL_D10_Logger::log(EN_L10_LOG_ERROR, "[W10] Max static routes reached!");
}

/**
 * @brief 기본 이름(base)과 확장자(ext)를 사용하여 파일 시스템 경로를 동적으로 생성합니다.
 * @return String /html/base.ext 형식의 경로
 */
static String W10_buildPath(const char* p_base, const char* p_ext) {
    // String 객체를 사용하면 String이 자동으로 메모리 관리
    String path = "/html/";
    path += p_base;
    path += ".";
    path += p_ext;
    return path;
}


// ------------------------------------------------------
// 메뉴 Web API 구현
// ------------------------------------------------------

/**
 * @brief 정적 페이지 목록을 JSON 형태의 메뉴 데이터로 반환합니다.
 */
static void W10_getMenuJson(AsyncWebServerRequest* r) {
	// 메뉴 항목이 G_W10_PAGE_COUNT 이므로 적절한 버퍼 크기를 계산
	// 항목당 약 64바이트 가정: {"label":"...", "path":"..."}
	
	// JsonDocument 단일 타입만 사용 규칙 준수
    JsonDocument v_doc; 

	// 페이지 목록을 순회하며 메뉴 항목 추가
	for (uint8_t v_i = 0; v_i < G_W10_PAGE_COUNT; v_i++) {
		const auto& v_page = s_pages_static[v_i];
        
        // isMain 페이지(URI가 "/")는 메뉴에서 제외 (옵션)
        if (v_page.isMain) continue; 
        
        // HTML 파일 경로 동적 생성 (예: /html/SC10_dashboard_001.html)
        String v_html_path = W10_buildPath(v_page.base, "html");

        // 메뉴 항목 구성
        // createNestedObject 사용 금지 규칙 준수: v_doc이 배열이 아니므로 Array 타입으로 강제 변환 후 사용
        JsonArray v_array = v_doc.to<JsonArray>();
        JsonObject v_item = v_array.add<JsonObject>();
        
        v_item["label"] = v_page.label;
        // 메뉴 HTML의 상대 경로를 위해 "/html/" 접두사 제거 (substring(6) 사용)
        v_item["path"]  = v_html_path.substring(6); 
	}
    
	// JSON 직렬화 및 응답 전송
	String v_json_output;
	if (serializeJson(v_doc, v_json_output) > 0) {
		auto* v_resp = r->beginResponse(200, "application/json", v_json_output);

		CL_W10_WebAPI::_applyHeaders(v_resp, true); // API 응답은 캐시 방지
		r->send(v_resp);
	} else {
		CL_D10_Logger::log(EN_L10_LOG_ERROR, "[W10] Menu API serialization failed.");
		r->send(500, "application/json", "{\"error\":\"Serialization Failed\"}");
	}
}



// ------------------------------------------------------
// 정적 자산 라우팅 초기화
// ------------------------------------------------------
/**
 * @brief 웹 서버에 정적 자산 및 API 라우팅을 등록합니다.
 */
void CL_W10_WebAPI::routeStaticAssets() {
	auto& v_web = g_A10_config_root.system->system.web;
	s_routeCnt_static = 0; // 라우팅 테이블 카운트 초기화

	// 1. 페이지 목록 순회하며 라우팅 테이블 구축
	for (uint8_t v_i = 0; v_i < G_W10_PAGE_COUNT; v_i++) {
		const auto& v_page = s_pages_static[v_i];

		// 경로 동적 생성
		String v_html_path = W10_buildPath(v_page.base, "html");
		String v_css_path  = W10_buildPath(v_page.base, "css");
		String v_js_path   = W10_buildPath(v_page.base, "js");

		// 1-1. HTML 파일 라우팅 등록
		if (v_page.isMain) {
			// 메인 페이지: 설정값 오버라이드 로직 적용
			const char* v_cfg_html = v_web.html;
			if (strlen(v_cfg_html) > 0)
				W10_pushRoute(v_cfg_html, v_cfg_html, "text/html"); // 설정 오버라이드 경로 등록
			W10_pushRoute(v_html_path.c_str(), v_html_path.c_str(), "text/html"); // 기본 파일 경로 등록
		} else {
			// 그 외 페이지: 단축 URI와 파일 경로 모두 등록
			W10_pushRoute(v_page.uri, v_html_path.c_str(), "text/html");
			W10_pushRoute(v_html_path.c_str(), v_html_path.c_str(), "text/html");
		}

		// 1-2. CSS/JS 파일 라우팅 등록
		W10_pushRoute(v_css_path.c_str(), v_css_path.c_str(), "text/css");
		W10_pushRoute(v_js_path.c_str(), v_js_path.c_str(), "application/javascript");

	} // end for

	// 2. 공통 CSS/JS 파일 라우팅 등록
	W10_pushRoute(G_W10_COMMON_CSS, G_W10_COMMON_CSS, "text/css");
	W10_pushRoute(G_W10_COMMON_JS, G_W10_COMMON_JS, "application/javascript");
	
    // 3. 루트 경로 '/' 리다이렉트 핸들러 등록
	s_server->on("/", HTTP_GET, [](AsyncWebServerRequest* r) {
		const char* f = g_A10_config_root.system->system.web.html;
		// 첫 번째 페이지(MAIN)의 기본 HTML 경로를 동적으로 생성
		String v_default_html_path = W10_buildPath(s_pages_static[0].base, "html"); 
		const char* v_default_html = v_default_html_path.c_str();
		
		if (strlen(f) && LittleFS.exists(f))
			r->redirect(f); // 설정된 경로로 리다이렉트
		else
			r->redirect(v_default_html); // 기본 HTML 경로로 리다이렉트
	});

	// 4. 등록된 정적 파일 경로 마운트 (HTTP GET 핸들러 등록)
    for (uint8_t v_i = 0; v_i < s_routeCnt_static; v_i++) {
        s_server->on(s_routes_static[v_i].uri, HTTP_GET, [v_i](AsyncWebServerRequest* r) {
            const char* v_file = s_routes_static[v_i].file;
            const char* v_mime = s_routes_static[v_i].mime;
            
            if (LittleFS.exists(v_file)) {
                auto* v_resp = r->beginResponse(LittleFS, v_file, v_mime);
                CL_W10_WebAPI::_applyHeaders(v_resp, false); // 정적 파일 캐싱 허용
                r->send(v_resp);
                return;
            }
            
            // 파일 부재 시 200 OK와 주석 메시지 반환 (개발 편의)
            String v_msg	 = "/* missing:" + String(v_file) + " */";
            auto* v_resp	 = r->beginResponse(200, v_mime, v_msg);
            CL_W10_WebAPI::_applyHeaders(v_resp, true); // 캐시 방지 헤더 적용
            r->send(v_resp);
        });
    }
    
    // 5. [추가] 메뉴 API 라우팅 등록
    s_server->on("/api/v1/menu", HTTP_GET, W10_getMenuJson);


	CL_D10_Logger::log(EN_L10_LOG_INFO, "[W10] Web routing initialized (%d routes, %d pages)", s_routeCnt_static, G_W10_PAGE_COUNT);
}
