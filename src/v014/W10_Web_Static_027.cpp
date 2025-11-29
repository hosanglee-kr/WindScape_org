
/*
 * ------------------------------------------------------
 * 소스명 : W10_Web_Static_027.cpp
 * 모듈 약어 : W10
 * 모듈명 : Smart Nature Wind Web API (v025) - Static Assets & Menu API Implementation
 * ------------------------------------------------------
 * 기능 요약:
 * - **LittleFS 기반 JSON 파일** (`G_W10_PAGES_JSON`)에서 **웹 페이지 및 공통 자산 목록 정보**를 로드 및 관리.
 * - 로드된 JSON 데이터를 기반으로 **모든 정적 자산(HTML, CSS, JS)**의 라우팅 초기화 및 등록.
 * - **Web UI 메뉴 정보를 JSON으로 반환**하는 API (`/api/v1/menu`) 구현.
 * - **루트 경로 (`/`) 요청** 시 설정된 경로 또는 JSON에 정의된 기본 홈 페이지로 **리다이렉트**.
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

#include "W10_Web_027.h"


// ------------------------------------------------------
// 전역 상수 및 변수
// ------------------------------------------------------

// [JSON 파일 경로]
constexpr char G_W10_PAGES_JSON[] = "/config/pages.json";

// [페이지 데이터 저장]
// JSON 파일에서 로드한 페이지 목록 (전역 JsonDocument 사용)
static JsonDocument s_pages_doc;
static uint16_t s_page_count = 0;


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
#define G_W10_PAGE_ROUTES_MAX 20 // 충분히 큰 크기로 설정
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
    
    // 로드된 데이터가 JsonArray인지 확인하고 카운트 업데이트
    if (s_pages_doc.is<JsonArray>()) {
        s_page_count = s_pages_doc.as<JsonArray>().size();
        CL_D10_Logger::log(EN_L10_LOG_INFO, "[W10] Pages JSON loaded successfully (%u items)", s_page_count);
        return true;
    }

	CL_D10_Logger::log(EN_L10_LOG_ERROR, "[W10] Pages JSON is not a valid array.");
    return false;
}


// ------------------------------------------------------
// 메뉴 Web API 구현
// ------------------------------------------------------

/**
 * @brief 로드된 JSON 페이지 목록을 기반으로 JSON 형태의 메뉴 데이터를 반환합니다.
 */
static void W10_getMenuJson(AsyncWebServerRequest* r) {
	
    JsonDocument v_doc_out; 
    
    // 로드된 페이지 JSON Document를 JsonArray로 가져옴
    JsonArray v_pages_array = s_pages_doc.as<JsonArray>();

	// 페이지 목록을 순회하며 메뉴 항목 추가
	for (JsonObject v_page : v_pages_array) {
		// isMain=true 또는 isAsset=true 인 항목은 메뉴에서 제외
        if (v_page["isMain"] | false) continue; 
        if (v_page["isAsset"] | false) continue;
        
        // 메뉴 항목 구성
        // JsonDocument 단일 타입 사용 및 createNestedObject 사용 금지 규칙 준수
        JsonArray v_array_out = v_doc_out.to<JsonArray>();
        JsonObject v_item = v_array_out.add<JsonObject>();
        
        // JSON 필드명 사용: label, path (path는 HTML 파일 경로)
        v_item["label"] = v_page["label"];
        // 메뉴 HTML의 상대 경로를 위해 "/html/" 접두사 제거 (예: /html/dashboard.html -> dashboard.html)
        // path 필드는 String 타입으로 가져와서 substring(6) 적용
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
    
    // 1. 페이지 정보 JSON 로드
    if (!W10_loadPagesJson()) {
        CL_D10_Logger::log(EN_L10_LOG_FATAL, "[W10] Failed to load pages JSON. Cannot register static routes.");
        return; // 로드 실패 시 라우팅 등록 중단
    }

	auto& v_web = g_A10_config_root.system->system.web;

	// 2. 로드된 JSON 페이지 목록을 순회하며 라우팅 테이블 구축
    JsonArray v_pages_array = s_pages_doc.as<JsonArray>();

	for (JsonObject v_page : v_pages_array) {
		// 필수 필드 확인
		const char* v_uri_key = v_page["uri"];
		const char* v_path_key = v_page["path"];
        
        if (!v_uri_key || !v_path_key) {
            CL_D10_Logger::log(EN_L10_LOG_WARN, "[W10] Skip page: Missing uri or path field in JSON.");
            continue;
        }

        bool v_is_main = v_page["isMain"] | false;
        
		// 파일 경로에서 확장자 추출 (MIME 타입 결정을 위해 필요)
        String v_path_str = v_path_key;
        int v_ext_idx = v_path_str.lastIndexOf('.');
        int v_type_idx = v_path_str.lastIndexOf('/');
        
        String v_file_ext = (v_ext_idx != -1 && v_ext_idx > v_type_idx) ? v_path_str.substring(v_ext_idx + 1) : "";
        
        const char* v_mime = "application/octet-stream";
        if (v_file_ext == "html") {
            v_mime = "text/html";
        } else if (v_file_ext == "css") {
            v_mime = "text/css";
        } else if (v_file_ext == "js") {
            v_mime = "application/javascript";
        }
        
		// 2-1. HTML/Asset 파일 라우팅 등록
		if (v_is_main) {
			// 메인 페이지: 설정값 오버라이드 로직 적용
			const char* v_cfg_html = v_web.html;
			if (strlen(v_cfg_html) > 0)
				W10_pushRoute(v_cfg_html, v_cfg_html, v_mime); // 설정 오버라이드 경로 등록
			W10_pushRoute(v_path_key, v_path_key, v_mime); // 기본 파일 경로 등록
		} else {
			// 그 외 페이지 및 일반 자산: 단축 URI와 파일 경로 모두 등록 (예: /style.css -> /html/style.css)
			W10_pushRoute(v_uri_key, v_path_key, v_mime);
			W10_pushRoute(v_path_key, v_path_key, v_mime);
		}
	} // end for

	// 3. 루트 경로 '/' 리다이렉트 핸들러 등록
	s_server->on("/", HTTP_GET, [](AsyncWebServerRequest* r) {
		const char* f = g_A10_config_root.system->system.web.html;
        
        // JSON 데이터에서 isMain=true인 페이지의 path를 가져옴
        const char* v_default_html = "/html/SC10_main_019.html"; // 기본값
        
        JsonArray v_pages_array = s_pages_doc.as<JsonArray>();
        for (JsonObject v_page : v_pages_array) {
            if (v_page["isMain"] | false) {
                // v_page["path"]가 없는 경우를 대비해 기본값 사용
                v_default_html = v_page["path"] | v_default_html; 
                break;
            }
        }
        
		if (strlen(f) > 0 && LittleFS.exists(f))
			r->redirect(f); // 설정된 경로로 리다이렉트
		else
			r->redirect(v_default_html); // JSON에서 로드한 기본 HTML 경로로 리다이렉트
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
    
    // 5. 메뉴 API 라우팅 등록
    s_server->on("/api/v1/menu", HTTP_GET, W10_getMenuJson);


	CL_D10_Logger::log(EN_L10_LOG_INFO, "[W10] Web routing initialized (%d routes, %u pages)", s_routeCnt_static, s_page_count);
}

📄 pages.json 파일 예시 (소스 기준)
위에 제공된 소스 코드(W10_Web_Static_027.cpp)에 정의된 페이지 목록을 JSON 파일 구조에 맞게 변환한 예시입니다.
이 파일은 /config/pages.json 경로에 저장되어야 하며, uri와 path 필드를 모두 포함해야 합니다.
[
  {
    "uri": "/",
    "path": "/html/SC10_main_019.html",
    "label": "Home",
    "isMain": true,
    "isAsset": false
  },
  {
    "uri": "/chart_t1",
    "path": "/html/SC10_chart_t1_006.html",
    "label": "차트 모니터링 T1",
    "isMain": false,
    "isAsset": false
  },
  {
    "uri": "/schedules_t1",
    "path": "/html/SC30_schedules_t1_005.html",
    "label": "스케줄 관리 T1",
    "isMain": false,
    "isAsset": false
  },
  {
    "uri": "/dashboard",
    "path": "/html/SC10_dashboard_001.html",
    "label": "대시보드",
    "isMain": false,
    "isAsset": false
  },
  {
    "uri": "/chart_t2",
    "path": "/html/SC10_chart_t2_007.html",
    "label": "차트 모니터링 T2",
    "isMain": false,
    "isAsset": false
  },
  {
    "uri": "/sim_details",
    "path": "/html/SC10_sim_details_001.html",
    "label": "시뮬 설정",
    "isMain": false,
    "isAsset": false
  },
  {
    "uri": "/profiles",
    "path": "/html/SC10_profile_001.html",
    "label": "프로파일 관리",
    "isMain": false,
    "isAsset": false
  },
  {
    "uri": "/schedules_t2",
    "path": "/html/SC10_schedules_t2_001.html",
    "label": "스케줄 관리 T2",
    "isMain": false,
    "isAsset": false
  },
  {
    "uri": "/user_profiles",
    "path": "/html/SC10_user_001.html",
    "label": "사용자 프로필",
    "isMain": false,
    "isAsset": false
  },
  {
    "uri": "/settings",
    "path": "/html/SC10_settings_001.html",
    "label": "시스템 설정",
    "isMain": false,
    "isAsset": false
  },
  {
    "uri": "/config",
    "path": "/html/SC10_config_025.html",
    "label": "Config 설정",
    "isMain": false,
    "isAsset": false
  },
  {
    "uri": "/diag",
    "path": "/html/SC10_diag_025.html",
    "label": "시스템 진단",
    "isMain": false,
    "isAsset": false
  },
  {
    "uri": "/SC10_common_001.css",
    "path": "/html/SC10_common_001.css",
    "label": "Common CSS",
    "isMain": false,
    "isAsset": true
  },
  {
    "uri": "/SC10_common_001.js",
    "path": "/html/SC10_common_001.js",
    "label": "Common JS",
    "isMain": false,
    "isAsset": true
  }
]

