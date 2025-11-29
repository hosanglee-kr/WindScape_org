/*
 * ------------------------------------------------------
 * 소스명 : W10_Web_Static_028.cpp
 * 모듈 약어 : W10
 * 모듈명 : Smart Nature Wind Web API (v025) - Static Assets & Menu Implementation
 * ------------------------------------------------------
 * 기능 요약:
 * - **LittleFS 기반 JSON 파일** (`G_W10_PAGES_JSON`)에서 웹 페이지(`pages`)와 정적 자산(`assets`) 정보를 분리하여 로드.
 * - **`pages` 배열은 `order` 필드**를 기준으로 정렬되어 메뉴 및 라우팅 등록에 사용.
 * - 로드된 JSON 데이터를 기반으로 **모든 정적 자산(HTML, CSS, JS)**의 라우팅 초기화 및 등록.
 * - **Web UI 메뉴 정보를 JSON으로 반환**하는 API (`/api/v1/menu`) 구현.
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
#include <ArduinoJson.h> 

#include <algorithm> // std::sort 사용
#include <vector>    // std::vector 사용

#include "W10_Web_027.h"


// ------------------------------------------------------
// 전역 상수 및 변수
// ------------------------------------------------------

// [JSON 파일 경로]
constexpr char G_W10_PAGES_JSON[] = "/json/cfg_pages_028.json";

// [페이지 데이터 저장]
// JSON 파일에서 로드한 페이지 목록 (전역 JsonDocument 사용)
static JsonDocument s_pages_doc; // 전체 JSON (pages, assets 포함)
static uint16_t s_page_count = 0; // 'pages' 배열의 항목 수
static uint16_t s_asset_count = 0; // 'assets' 배열의 항목 수


// ------------------------------------------------------
// Static routing table 및 헬퍼 함수
// ------------------------------------------------------

// 정적 자산 라우팅 정보를 저장하기 위한 구조체
struct ST_W10_Route_t {
	const char* uri; // HTTP 요청 URI 
	const char* file; // LittleFS의 실제 파일 경로 
	const char* mime; // MIME 타입 
};

// 정적 라우팅 테이블 배열 크기 정의 (최대 예상 경로 수)
// (페이지 수 * 3 라우트) + (자산 수 * 2 라우트) + 예비 공간
#define G_W10_PAGE_ROUTES_MAX 80 
static ST_W10_Route_t s_routes_static[G_W10_PAGE_ROUTES_MAX];
static uint8_t s_routeCnt_static = 0; // 등록된 라우트 개수

/**
 * @brief 정적 라우팅 테이블에 새 경로를 추가합니다. 배열 크기를 초과하지 않도록 검사합니다.
 */
static void W10_pushRoute(const char* p_uri, const char* p_file, const char* p_mime) {
	if (s_routeCnt_static < G_W10_PAGE_ROUTES_MAX)
		s_routes_static[s_routeCnt_static++] = {p_uri, p_file, p_mime};
	else
		CL_D10_Logger::log(EN_L10_LOG_ERROR, "[W10] Max static routes reached!");
}

/**
 * @brief LittleFS에서 페이지 정보 JSON 파일을 읽고 파싱합니다.
 * 성공적으로 로드되면 'pages' 배열을 'order' 필드를 기준으로 정렬합니다.
 * @return 로드 성공 시 true
 */
static bool W10_loadPagesJson() {
	File v_file = LittleFS.open(G_W10_PAGES_JSON, "r");
	if (!v_file) {
		CL_D10_Logger::log(EN_L10_LOG_ERROR, "[W10] Failed to open pages JSON: %s", G_W10_PAGES_JSON);
		return false;
	}

    // 파일 스트림에서 JsonDocument로 역직렬화
	DeserializationError v_err = deserializeJson(s_pages_doc, v_file);
	v_file.close();

	if (v_err) {
		CL_D10_Logger::log(EN_L10_LOG_ERROR, "[W10] Pages JSON deserialize failed: %s", v_err.c_str());
		return false;
	}
    
    // 로드된 데이터가 JsonObject이고 'pages' 배열을 포함하는지 확인
    if (!s_pages_doc.is<JsonObject>()) {
        CL_D10_Logger::log(EN_L10_LOG_ERROR, "[W10] Pages JSON is not a valid object.");
        return false;
    }
    
    JsonArray v_pages_array = s_pages_doc["pages"].as<JsonArray>();
    JsonArray v_assets_array = s_pages_doc["assets"].as<JsonArray>();

    if (v_pages_array.isNull()) {
        CL_D10_Logger::log(EN_L10_LOG_ERROR, "[W10] Pages array missing or invalid in JSON.");
        return false;
    }


    // 수정 후 (std::sort 사용):
    std::sort(
        v_pages_array.begin(), 
        v_pages_array.end(),
        [](const JsonVariant& a, const JsonVariant& b) {
            // 비교 로직: JsonVariant에서 비교에 필요한 값을 추출합니다.
            // 예를 들어, JSON 객체의 "order" 필드를 비교한다면:
            return a["order"].as<int>() < b["order"].as<int>();
        }
    );

    /*
	// **[정렬 로직 반영]** 'pages' 배열을 'order' 필드를 기준으로 정렬
    v_pages_array.sort([](const JsonVariant& a, const JsonVariant& b) {
        return a["order"].as<int>() < b["order"].as<int>();
    });
	*/
    
    s_page_count = v_pages_array.size();
    s_asset_count = v_assets_array.size(); // assets 배열은 정렬 불필요
    
    CL_D10_Logger::log(EN_L10_LOG_INFO, "[W10] JSON loaded (Pages: %u, Assets: %u), Pages sorted.", s_page_count, s_asset_count);
    return true;
}


// ------------------------------------------------------
// 메뉴 Web API 구현
// ------------------------------------------------------

/**
 * @brief 로드되고 정렬된 JSON 페이지 목록을 기반으로 JSON 형태의 메뉴 데이터를 반환합니다.
 */
static void W10_getMenuJson(AsyncWebServerRequest* r) {
	
    JsonDocument v_doc_out; 
    
    // 로드되고 정렬된 'pages' 배열을 가져옴
    JsonArray v_pages_array = s_pages_doc["pages"].as<JsonArray>();

	// 정렬된 페이지 목록을 순회하며 메뉴 항목 추가
	for (JsonObject v_page : v_pages_array) { 
		// isMain=true 인 항목은 메뉴에서 제외
        if (v_page["isMain"] | false) continue; 
        
        // 메뉴 항목 구성 
        JsonArray v_array_out = v_doc_out.to<JsonArray>();
        JsonObject v_item = v_array_out.add<JsonObject>();
        
        // JSON 필드명 사용: label, path 
        v_item["label"] = v_page["label"];
        // 메뉴 HTML의 상대 경로를 위해 "/html/" 접두사 제거 (예: /html/dashboard.html -> dashboard.html)
        v_item["path"]  = ((String)v_page["path"]).substring(6); 
	}
    
	// JSON 직렬화 및 응답 전송
	String v_json_output;
	// Unicode 공백문자 제거됨
	if (serializeJson(v_doc_out, v_json_output) > 0) {
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
	s_routeCnt_static = 0; // 라우팅 테이블 카운트 초기화
    
    // 1. 페이지 정보 JSON 로드 및 정렬
    if (!W10_loadPagesJson()) {
        CL_D10_Logger::log(EN_L10_LOG_ERROR, "[W10] Failed to load pages JSON. Cannot register static routes.");
        return; // 로드 실패 시 라우팅 등록 중단
    }

	auto& v_web = g_A10_config_root.system->system.web;
    
    // -------------------------------------------------
	// 2. 'pages' (HTML 및 전용 CSS/JS) 목록을 순회하며 라우팅 테이블 구축
    // -------------------------------------------------
    JsonArray v_pages_array = s_pages_doc["pages"].as<JsonArray>();

	for (JsonObject v_page : v_pages_array) { // 정렬된 순서대로 라우팅 등록
		const char* v_uri_key = v_page["uri"];
		const char* v_path_key = v_page["path"]; // HTML 파일 경로
        
        if (!v_uri_key || !v_path_key) continue;

        bool v_is_main = v_page["isMain"] | false;
        
        // 2-1. HTML 파일 라우팅 등록
		const char* v_mime_html = "text/html"; 
		if (v_is_main) {
			// 메인 페이지: 설정값 오버라이드 로직 적용
			const char* v_cfg_html = v_web.html;
			if (strlen(v_cfg_html) > 0)
				W10_pushRoute(v_cfg_html, v_cfg_html, v_mime_html); // 설정 오버라이드 경로 등록
			W10_pushRoute(v_path_key, v_path_key, v_mime_html); // 기본 파일 경로 등록
		} else {
			// 그 외 페이지: 단축 URI와 파일 경로 모두 등록
			W10_pushRoute(v_uri_key, v_path_key, v_mime_html);
			W10_pushRoute(v_path_key, v_path_key, v_mime_html);
		}
        
        // 2-2. 페이지 전용 CSS/JS 파일 라우팅 등록 (파일 경로를 동적으로 구성하여 등록)
        const char* v_css_file = v_page["css"];
        const char* v_js_file  = v_page["js"];
        
        if (v_css_file) {
            String v_css_path = "/html/";
            v_css_path += v_css_file; // 예: /html/SC10_dashboard_001.css
            W10_pushRoute(v_css_file, v_css_path.c_str(), "text/css"); // /SC10_dashboard_001.css -> /html/SC10_dashboard_001.css
            W10_pushRoute(v_css_path.c_str(), v_css_path.c_str(), "text/css"); // /html/... -> /html/...
        }
        if (v_js_file) {
            String v_js_path = "/html/";
            v_js_path += v_js_file; // 예: /html/SC10_dashboard_001.js
            W10_pushRoute(v_js_file, v_js_path.c_str(), "application/javascript"); // /SC10_dashboard_001.js -> /html/SC10_dashboard_001.js
            W10_pushRoute(v_js_path.c_str(), v_js_path.c_str(), "application/javascript"); // /html/... -> /html/...
        }

	} // end for ('pages' array)
    
    // -------------------------------------------------
	// 3. 'assets' (공통 자산) 목록을 순회하며 라우팅 테이블 구축
    // -------------------------------------------------
    JsonArray v_assets_array = s_pages_doc["assets"].as<JsonArray>();
    
    for (JsonObject v_asset : v_assets_array) {
        const char* v_uri_key = v_asset["uri"];
		const char* v_path_key = v_asset["path"];
        
        if (!v_uri_key || !v_path_key) continue;
        
        // MIME 타입 결정
        const char* v_mime = "application/octet-stream";
        if (strstr(v_path_key, ".css")) {
            v_mime = "text/css";
        } else if (strstr(v_path_key, ".js")) {
            v_mime = "application/javascript";
        }
        
        // 자산 파일 라우팅 등록: 단축 URI와 파일 경로 모두 등록 
        W10_pushRoute(v_uri_key, v_path_key, v_mime);
        W10_pushRoute(v_path_key, v_path_key, v_mime);
    } // end for ('assets' array)


	// 4. 루트 경로 '/' 리다이렉트 핸들러 등록
	s_server->on("/", HTTP_GET, [](AsyncWebServerRequest* r) {
		const char* f = g_A10_config_root.system->system.web.html;
        
        // JSON 데이터에서 isMain=true인 페이지의 path를 가져옴
        const char* v_default_html = "/html/SC10_main_019.html"; // 기본값
        
        JsonArray v_pages_array = s_pages_doc["pages"].as<JsonArray>();
        for (JsonObject v_page : v_pages_array) {
            if (v_page["isMain"] | false) {
                v_default_html = v_page["path"] | v_default_html; 
                break;
            }
        }
        
		if (strlen(f) > 0 && LittleFS.exists(f))
			r->redirect(f); // 설정된 경로로 리다이렉트
		else
			r->redirect(v_default_html); // JSON에서 로드한 기본 HTML 경로로 리다이렉트
	});

	// 5. 등록된 정적 파일 경로 마운트 (HTTP GET 핸들러 등록)
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
    
    // 6. 메뉴 API 라우팅 등록
    s_server->on("/api/v1/menu", HTTP_GET, W10_getMenuJson);


	CL_D10_Logger::log(EN_L10_LOG_INFO, "[W10] Web routing initialized (%d routes, Pages: %u, Assets: %u)", 
        s_routeCnt_static, s_page_count, s_asset_count);
}
