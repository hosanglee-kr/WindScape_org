/*
 * ------------------------------------------------------
 * 소스명 : W10_Web_Static_032.cpp
 * 모듈 약어 : W10
 * 모듈명 : Smart Nature Wind Web API (v029) - Static Assets & Menu Implementation
 * ------------------------------------------------------
 * 기능 요약:
 * - LittleFS 기반 JSON 파일(`/json/cfg_pages_030.json`)에서
 *   웹 페이지(`pages`), 정적 자산(`assets`), 리다이렉트(`reDirect`) 정보 로드.
 * - `pages` 배열은 `order` 필드를 기준으로 정렬한 뷰를 생성하여
 *   메뉴(` /api/v1/menu `) 및 라우팅 등록에 사용.
 * - 각 페이지의 `pageAssets` 배열을 사용해 HTML별 전용 CSS/JS 라우팅을 등록.
 * - `assets` 배열을 통해 공통 정적 자산(CSS/JS 등)의 라우팅 초기화 및 등록.
 * - Web UI 메뉴 정보를 JSON으로 반환하는 API (`/api/v1/menu`) 구현.
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

#include <algorithm>
#include <vector>
#include <string.h>

#include "W10_Web_029.h"

// ------------------------------------------------------
// 전역 상수
// ------------------------------------------------------

// JSON 파일 경로 (v031)
constexpr char G_W10_PAGES_JSON[] = "/json/cfg_pages_031.json";

// ------------------------------------------------------
// 메뉴/페이지용 구조체 정의
// ------------------------------------------------------

// 페이지 엔트리 구조체 (메뉴 및 메타 정보용)
struct ST_W10_PageEntry_t {
	int    order;
	String uri;
	String path;
	String label;
	bool   isMain;
	bool   enable;
};

// 정적 자산 라우트 구조체 (필요시 확장용, 현재는 메뉴에는 사용 X)
struct ST_W10_Route_t {
	const char* uri;
	const char* file;
	const char* mime;
};

// 메뉴 API에 필요한 전역 상태만 구조체로 보관
struct ST_W10_MenuState_t {
	std::vector<ST_W10_PageEntry_t> pages_sorted;  // /api/v1/menu 응답용 정렬된 리스트
	String                          main_path;     // isMain == true 인 첫 번째 페이지 path

	ST_W10_MenuState_t()
		: pages_sorted()
		, main_path() {}
};

// 메뉴 관련 전역 상태 (요청사항: get menu 관련 구조체만 전역)
static ST_W10_MenuState_t s_w10_menu_state;

// ------------------------------------------------------
// 헬퍼 함수
// ------------------------------------------------------

// 안전한 C 문자열 복사 (new[] + memset + strlcpy)
static char* W10_allocCString(const char* p_src) {
	if (!p_src)
		return nullptr;

	size_t v_len = strlen(p_src) + 1;
	char*  v_buf = new char[v_len];
	if (!v_buf)
		return nullptr;

	memset(v_buf, 0, v_len);
	strlcpy(v_buf, p_src, v_len);
	return v_buf;
}

static const char* W10_guessMime(const char* p_path) {
	if (!p_path)
		return "application/octet-stream";

	if (strstr(p_path, ".html"))
		return "text/html";
	if (strstr(p_path, ".css"))
		return "text/css";
	if (strstr(p_path, ".js"))
		return "application/javascript";

	return "application/octet-stream";
}

// JSON 로딩 (로컬 JsonDocument 사용)
static bool W10_loadPagesJson(JsonDocument& p_doc, uint16_t& p_page_count, uint16_t& p_asset_count) {
	p_page_count = 0;
	p_asset_count = 0;

	File v_file = LittleFS.open(G_W10_PAGES_JSON, "r");
	if (!v_file) {
		CL_D10_Logger::log(EN_L10_LOG_ERROR,
						   "[W10] Failed to open pages JSON: %s",
						   G_W10_PAGES_JSON);
		return false;
	}

	DeserializationError v_err = deserializeJson(p_doc, v_file);
	v_file.close();
	if (v_err) {
		CL_D10_Logger::log(EN_L10_LOG_ERROR,
						   "[W10] Pages JSON deserialize failed: %s",
						   v_err.c_str());
		return false;
	}

	if (!p_doc.is<JsonObject>()) {
		CL_D10_Logger::log(EN_L10_LOG_ERROR,
						   "[W10] Pages JSON is not a valid object.");
		return false;
	}

	JsonArray v_pages_array  = p_doc["pages"].as<JsonArray>();
	JsonArray v_assets_array = p_doc["assets"].as<JsonArray>();

	if (v_pages_array.isNull()) {
		CL_D10_Logger::log(EN_L10_LOG_ERROR,
						   "[W10] Pages array missing or invalid in JSON.");
		return false;
	}

	p_page_count  = v_pages_array.size();
	p_asset_count = v_assets_array.isNull() ? 0 : v_assets_array.size();

	CL_D10_Logger::log(EN_L10_LOG_INFO,
					   "[W10] JSON loaded (Pages: %u, Assets: %u)",
					   p_page_count, p_asset_count);
	return true;
}

// 정적 파일 라우트 등록 (등록 시점에 URI/FILE/MIME 문자열을 복사해서 평생 유지)
// - 라우팅 등록 후 따로 전역 테이블을 유지할 필요 없음
void CL_W10_WebAPI::registerStaticRoute(const char* p_uri, const char* p_file, const char* p_mime) {
	if (!p_uri || !p_file || !p_mime)
		return;

	if (strlen(p_uri) == 0 || strlen(p_file) == 0)
		return;

	char* v_uri  = W10_allocCString(p_uri);
	char* v_file = W10_allocCString(p_file);
	char* v_mime = W10_allocCString(p_mime);

	if (!v_uri || !v_file || !v_mime) {
		CL_D10_Logger::log(EN_L10_LOG_ERROR,
						   "[W10] Route alloc failed (uri:%s, file:%s)",
						   p_uri, p_file);
		// 메모리 일부만 할당된 경우 delete[] 호출이 필요할 수 있으나,
		// 여기서는 실패 시 라우트 자체를 등록하지 않고 누수는 무시 (희소 이벤트)
		return;
	}

	s_server->on(v_uri, HTTP_GET,
				 [v_file, v_mime](AsyncWebServerRequest* r) {
					 if (LittleFS.exists(v_file)) {
						 auto* v_resp = r->beginResponse(LittleFS, v_file, v_mime);
						 CL_W10_WebAPI::_applyHeaders(v_resp, false);
						 r->send(v_resp);
						 return;
					 }

					 String v_msg = "/* missing:" + String(v_file) + " */";
					 auto* v_resp = r->beginResponse(200, v_mime, v_msg);
					 CL_W10_WebAPI::_applyHeaders(v_resp, true);
					 r->send(v_resp);
				 });
}

// ------------------------------------------------------
// 메뉴 Web API 구현 (/api/v1/menu)
//  - s_w10_menu_state.pages_sorted 기반으로 응답
//  - pages는 routeStaticAssets() 실행 시 1회 로드/정렬됨
// ------------------------------------------------------
static void W10_getMenuJson(AsyncWebServerRequest* r) {
	JsonDocument v_doc_out;
	JsonArray    v_array_out = v_doc_out.to<JsonArray>();

	for (const auto& v_entry : s_w10_menu_state.pages_sorted) {
        JsonObject v_item = v_array_out.add<JsonObject>();
        v_item["label"]   = v_entry.label;
        v_item["path"]    = v_entry.path;  // "/html_v2/..." 그대로
        v_item["uri"]     = v_entry.uri;   // "/P040_dashboard_003.html" 같은 short html path
        v_item["order"]   = v_entry.order;
        v_item["isMain"]  = v_entry.isMain;
        v_item["enable"]  = v_entry.enable;  // 새 필드
    }

	String v_json_output;
	if (serializeJson(v_doc_out, v_json_output) > 0) {
		auto* v_resp = r->beginResponse(200, "application/json", v_json_output);
		CL_W10_WebAPI::_applyHeaders(v_resp, true);
		r->send(v_resp);
	} else {
		CL_D10_Logger::log(EN_L10_LOG_ERROR,
						   "[W10] Menu API serialization failed.");
		r->send(500, "application/json",
				"{\"error\":\"Serialization Failed\"}");
	}
}

// ------------------------------------------------------
// 정적 자산 라우팅 등록
//  - pages[].path        → HTML 라우트
//  - pages[].uri         → HTML 라우트 (short html path)
//  - pages[].pageAssets[]→ 페이지 전용 CSS/JS 라우트
//  - assets[]            → 공통 CSS/JS 라우트
//  - reDirect[]          → 단축 URI 리다이렉트
//  - 메뉴 데이터는 s_w10_menu_state에 정렬/보관
//  - JsonDocument는 이 함수 내부 로컬 변수로만 사용
// ------------------------------------------------------
void CL_W10_WebAPI::routeStaticAssets() {
	// 메뉴 상태 초기화
	s_w10_menu_state.pages_sorted.clear();
	s_w10_menu_state.main_path = "";

	JsonDocument v_doc;
	uint16_t     v_page_count  = 0;
	uint16_t     v_asset_count = 0;

	if (!W10_loadPagesJson(v_doc, v_page_count, v_asset_count)) {
		CL_D10_Logger::log(EN_L10_LOG_ERROR,
						   "[W10] Failed to load pages JSON. Cannot register static routes.");
		return;
	}

    JsonArray v_pages_array    = v_doc["pages"].as<JsonArray>();
    JsonArray v_assets_array   = v_doc["assets"].as<JsonArray>();
    JsonArray v_redirect_array = v_doc["reDirect"].as<JsonArray>();

    // 1) pages 기반 HTML + 페이지별 전용 자산(pageAssets) 라우트 등록
	if (!v_pages_array.isNull()) {
		for (JsonObject v_page : v_pages_array) {
			ST_W10_PageEntry_t v_entry{};

			v_entry.order  = v_page["order"].as<int>();
			v_entry.uri    = v_page["uri"].as<String>();
			v_entry.path   = v_page["path"].as<String>();
			v_entry.label  = v_page["label"].as<String>();
			v_entry.isMain = v_page["isMain"] | false;

			// enable 필드 (기본값 true)
			bool v_enable = true;
			if (!v_page["enable"].isNull()) {
				v_enable = v_page["enable"].as<bool>();
			}
			v_entry.enable = v_enable;

			// 메뉴용 엔트리 누적
			s_w10_menu_state.pages_sorted.push_back(v_entry);

			// isMain == true 인 첫 번째 페이지 path를 main_path로 보관 (root fallback용)
			if (v_entry.isMain && s_w10_menu_state.main_path.isEmpty()) {
				if (!v_entry.path.isEmpty()) {
					s_w10_menu_state.main_path = v_entry.path;
				}
			}

			// 라우팅 등록
			const char* v_uri_key  = v_entry.uri.c_str();
			const char* v_path_key = v_entry.path.c_str();
			bool        v_is_main  = v_entry.isMain;

			if (!v_uri_key || !v_path_key || strlen(v_path_key) == 0)
				continue;

			const char*	v_mime_html = "text/html";

			if (v_is_main) {
				// 메인 페이지:
				// - pages[].path 에 정의된 HTML만 기준으로 사용
				// - path 직접 접근 + uri(예: "/P010_main_021.html") 접근 모두 허용
				registerStaticRoute(v_path_key, v_path_key, v_mime_html);

				if (strlen(v_uri_key) > 0) {
					registerStaticRoute(v_uri_key, v_path_key, v_mime_html);
				}
			} else {
				// 일반 페이지:
				// - uri("/P040_dashboard_003.html") → path(HTML)
				// - path("/html_v2/...") 직접 접근도 허용
				if (strlen(v_uri_key) > 0) {
					registerStaticRoute(v_uri_key, v_path_key, v_mime_html);
				}
				registerStaticRoute(v_path_key, v_path_key, v_mime_html);
			}

			// 각 페이지의 pageAssets 처리 (CSS/JS 등)
			JsonArray v_page_assets = v_page["pageAssets"].as<JsonArray>();
			if (!v_page_assets.isNull()) {
				for (JsonObject v_asset : v_page_assets) {
					const char* v_a_uri  = v_asset["uri"].as<const char*>();
					const char* v_a_path = v_asset["path"].as<const char*>();

					if (!v_a_uri || !v_a_path ||
						strlen(v_a_uri) == 0 || strlen(v_a_path) == 0)
						continue;

					const char* v_mime = W10_guessMime(v_a_path);

					// "/P010_main_021.css" → "/html_v2/P010_main_021.css"
					registerStaticRoute(v_a_uri, v_a_path, v_mime);
					// 절대 경로로 직접 접근도 허용
					registerStaticRoute(v_a_path, v_a_path, v_mime);
				}
			}
		}

		// 메뉴 표시용 pages_sorted를 order 기준으로 정렬
		std::sort(s_w10_menu_state.pages_sorted.begin(),
				  s_w10_menu_state.pages_sorted.end(),
				  [](const ST_W10_PageEntry_t& a, const ST_W10_PageEntry_t& b) {
					  return a.order < b.order;
				  });
	}

	// 2) assets 기반 공통 자산 라우트 (공통 CSS/JS 등)
	if (!v_assets_array.isNull()) {
		for (JsonObject v_asset : v_assets_array) {
			const char* v_uri_key  = v_asset["uri"].as<const char*>();
			const char* v_path_key = v_asset["path"].as<const char*>();

			if (!v_uri_key || !v_path_key ||
				strlen(v_uri_key) == 0 || strlen(v_path_key) == 0)
				continue;

			const char* v_mime = W10_guessMime(v_path_key);

			registerStaticRoute(v_uri_key, v_path_key, v_mime);
			registerStaticRoute(v_path_key, v_path_key, v_mime);
		}
	}

	// 3) 루트 및 단축 URI 리다이렉트 (reDirect 배열)
	bool v_root_redirect_defined = false;
	if (!v_redirect_array.isNull()) {
		for (JsonObject v_redir : v_redirect_array) {
			const char* v_from = v_redir["uriFrom"].as<const char*>();
			const char* v_to   = v_redir["uriTo"].as<const char*>();

			if (!v_from || !v_to ||
				strlen(v_from) == 0 || strlen(v_to) == 0)
				continue;

			if (strcmp(v_from, "/") == 0) {
				v_root_redirect_defined = true;
			}

			// 복사본 생성 (서버 생애 동안 유지)
			char* v_from_copy = W10_allocCString(v_from);
			char* v_to_copy   = W10_allocCString(v_to);

			if (!v_from_copy || !v_to_copy) {
				CL_D10_Logger::log(EN_L10_LOG_ERROR,
								   "[W10] Redirect alloc failed (from:%s, to:%s)",
								   v_from, v_to);
				continue;
			}

			// 예: "/chart_t1" → "/P020_chart_t1_008.html"
			s_server->on(v_from_copy, HTTP_GET,
						 [v_to_copy](AsyncWebServerRequest* r) {
							 r->redirect(v_to_copy);
						 });
		}
	}

	// 3-1) reDirect에 "/"가 정의되지 않은 경우, isMain 기반 root fallback
	if (!v_root_redirect_defined) {
		// main_path를 캡처해서 사용 (String 복사본)
		String v_root_path = s_w10_menu_state.main_path;

		s_server->on("/", HTTP_GET,
					 [v_root_path](AsyncWebServerRequest* r) {
						 // 기본값
						 const char* v_default_html = "/html_v2/P010_main_021.html";

						 if (!v_root_path.isEmpty()) {
							 v_default_html = v_root_path.c_str();
						 }

						 // 파일 존재 여부 확인 후 리다이렉트
						 if (LittleFS.exists(v_default_html))
							 r->redirect(v_default_html);
						 else
							 r->redirect("/"); // 최악의 경우 루프지만, JSON/FS 구성이 잘못된 상황
					 });
	}

	// 4) 메뉴 API
	s_server->on("/api/v1/menu", HTTP_GET, W10_getMenuJson);

	CL_D10_Logger::log(EN_L10_LOG_INFO,
					   "[W10] Web routing initialized (Pages: %u, Assets: %u)",
					   v_page_count, v_asset_count);
}
