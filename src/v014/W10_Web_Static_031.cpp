/*
 * ------------------------------------------------------
 * 소스명 : W10_Web_Static_031.cpp
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

#include "W10_Web_029.h"

// ------------------------------------------------------
// 전역 상수 및 변수
// ------------------------------------------------------

// JSON 파일 경로 (v030)
constexpr char G_W10_PAGES_JSON[] = "/json/cfg_pages_031.json";

// 페이지/에셋 JSON Document (전역 1개만 사용)
static JsonDocument s_pages_doc;
static uint16_t     s_page_count  = 0;
static uint16_t     s_asset_count = 0;

// 정렬용 페이지 엔트리 구조체
struct ST_W10_PageEntry_t {
	int    order;
	String uri;
	String path;
	String label;
	bool   isMain;
	bool   enable;
};

// 정적 라우트 테이블
struct ST_W10_Route_t {
	const char* uri;
	const char* file;
	const char* mime;
};

#define G_W10_PAGE_ROUTES_MAX 40
static ST_W10_Route_t s_routes_static[G_W10_PAGE_ROUTES_MAX];
static uint8_t        s_routeCnt_static = 0;

// ------------------------------------------------------
// 헬퍼 함수
// ------------------------------------------------------
static void W10_pushRoute(const char* p_uri, const char* p_file, const char* p_mime) {
	if (!p_uri || !p_file || !p_mime)
		return;

	if (strlen(p_uri) == 0 || strlen(p_file) == 0)
		return;

	if (s_routeCnt_static < G_W10_PAGE_ROUTES_MAX) {
		s_routes_static[s_routeCnt_static++] = {p_uri, p_file, p_mime};
	} else {
		CL_D10_Logger::log(EN_L10_LOG_ERROR, "[W10] Max static routes reached!");
	}
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

// JSON 로딩
static bool W10_loadPagesJson() {
	File v_file = LittleFS.open(G_W10_PAGES_JSON, "r");
	if (!v_file) {
		CL_D10_Logger::log(EN_L10_LOG_ERROR,
						   "[W10] Failed to open pages JSON: %s",
						   G_W10_PAGES_JSON);
		return false;
	}

	DeserializationError v_err = deserializeJson(s_pages_doc, v_file);
	v_file.close();
	if (v_err) {
		CL_D10_Logger::log(EN_L10_LOG_ERROR,
						   "[W10] Pages JSON deserialize failed: %s",
						   v_err.c_str());
		return false;
	}

	if (!s_pages_doc.is<JsonObject>()) {
		CL_D10_Logger::log(EN_L10_LOG_ERROR,
						   "[W10] Pages JSON is not a valid object.");
		return false;
	}

	JsonArray v_pages_array  = s_pages_doc["pages"].as<JsonArray>();
	JsonArray v_assets_array = s_pages_doc["assets"].as<JsonArray>();

	if (v_pages_array.isNull()) {
		CL_D10_Logger::log(EN_L10_LOG_ERROR,
						   "[W10] Pages array missing or invalid in JSON.");
		return false;
	}

	s_page_count  = v_pages_array.size();
	s_asset_count = v_assets_array.isNull() ? 0 : v_assets_array.size();

	CL_D10_Logger::log(EN_L10_LOG_INFO,
					   "[W10] JSON loaded (Pages: %u, Assets: %u)",
					   s_page_count, s_asset_count);
	return true;
}

// pages 배열을 order 기준으로 정렬한 vector 생성
static void W10_buildSortedPages(std::vector<ST_W10_PageEntry_t>& p_out) {
	p_out.clear();

	JsonArray v_pages_array = s_pages_doc["pages"].as<JsonArray>();
	if (v_pages_array.isNull())
		return;

	for (JsonVariantConst v_it : v_pages_array) {
		const JsonObjectConst v_page = v_it.as<JsonObjectConst>();
		ST_W10_PageEntry_t    v_e{};
		v_e.order  = v_page["order"].as<int>();
		v_e.uri    = v_page["uri"].as<String>();
		v_e.path   = v_page["path"].as<String>();
		v_e.label  = v_page["label"].as<String>();
		v_e.isMain = v_page["isMain"] | false;
		// enable 필드 추가 (기본값 true)
		bool v_enable = true;
		if (!v_page["enable"].isNull()) {
			v_enable = v_page["enable"].as<bool>();
		}
		v_e.enable = v_enable;

		p_out.push_back(v_e);
	}

	std::sort(p_out.begin(), p_out.end(),
			  [](const ST_W10_PageEntry_t& a, const ST_W10_PageEntry_t& b) {
				  return a.order < b.order;
			  });
}

// uri로 pages[] 내 JsonObject 찾기
static JsonObject W10_findPageObjectByUri(const char* p_uri) {
	JsonArray v_pages_array = s_pages_doc["pages"].as<JsonArray>();
	if (v_pages_array.isNull() || !p_uri)
		return JsonObject();

	for (JsonObject v_page : v_pages_array) {
		const char* v_uri = v_page["uri"].as<const char*>();
		if (v_uri && strcmp(v_uri, p_uri) == 0) {
			return v_page;
		}
	}
	return JsonObject();
}

// ------------------------------------------------------
// 메뉴 Web API 구현 (/api/v1/menu)
//  - cfg_pages_030.json의 pages를 order순으로 정렬 후
//    그대로 응답 (isMain, enable 필드 포함)
//  - 프론트에서 isMain/enable 조건으로 필터링
// ------------------------------------------------------
static void W10_getMenuJson(AsyncWebServerRequest* r) {
	JsonDocument v_doc_out;
	JsonArray    v_array_out = v_doc_out.to<JsonArray>();

	std::vector<ST_W10_PageEntry_t> v_pages_sorted;
	W10_buildSortedPages(v_pages_sorted);

	for (const auto& v_entry : v_pages_sorted) {
		JsonObject v_item = v_array_out.add<JsonObject>();
		v_item["label"]  = v_entry.label;
		v_item["path"]   = v_entry.path;   // "/html_v2/..." 그대로
		v_item["uri"]    = v_entry.uri;    // "/P040_dashboard_003.html" 같은 short html path
		v_item["order"]  = v_entry.order;
		v_item["isMain"] = v_entry.isMain;
		v_item["enable"] = v_entry.enable; // 새 필드
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
// ------------------------------------------------------
void CL_W10_WebAPI::routeStaticAssets() {
	s_routeCnt_static = 0;

	if (!W10_loadPagesJson()) {
		CL_D10_Logger::log(EN_L10_LOG_ERROR,
						   "[W10] Failed to load pages JSON. Cannot register static routes.");
		return;
	}

	auto& v_web = g_A10_config_root.system->system.web;

	JsonArray v_assets_array   = s_pages_doc["assets"].as<JsonArray>();
	JsonArray v_redirect_array = s_pages_doc["reDirect"].as<JsonArray>();

	// 1) pages 기반 정렬 리스트 생성
	std::vector<ST_W10_PageEntry_t> v_pages_sorted;
	W10_buildSortedPages(v_pages_sorted);

	// 1-1) pages 기반 HTML + 페이지별 전용 자산(pageAssets) 라우트 등록
	for (const auto& v_entry : v_pages_sorted) {
		const char* v_uri_key  = v_entry.uri.c_str();
		const char* v_path_key = v_entry.path.c_str();
		bool        v_is_main  = v_entry.isMain;

		if (!v_uri_key || !v_path_key || strlen(v_path_key) == 0)
			continue;

		const char* v_mime_html = "text/html";

		if (v_is_main) {
			// 메인 페이지:
			// - system.web.html이 설정되어 있으면 해당 경로도 라우팅
			// - path에 정의된 HTML도 직접 접근 가능하게 라우팅
			const char* v_cfg_html = v_web.html; // 예: "/html_v2/P010_main_021.html"
			if (v_cfg_html && strlen(v_cfg_html) > 0) {
				W10_pushRoute(v_cfg_html, v_cfg_html, v_mime_html);
			}
			W10_pushRoute(v_path_key, v_path_key, v_mime_html);

			// 메인 페이지도 uri로 직접 접근 가능하게 (예: "/P010_main_021.html")
			if (strlen(v_uri_key) > 0) {
				W10_pushRoute(v_uri_key, v_path_key, v_mime_html);
			}
		} else {
			// 일반 페이지:
			// - uri("/P040_dashboard_003.html" 등) → path(HTML)
			// - path 자체("/html_v2/..." 등)도 직접 접근 가능
			if (strlen(v_uri_key) > 0) {
				W10_pushRoute(v_uri_key, v_path_key, v_mime_html);
			}
			W10_pushRoute(v_path_key, v_path_key, v_mime_html);
		}

		// 각 페이지의 pageAssets 처리 (CSS/JS 등)
		JsonObject v_page_obj = W10_findPageObjectByUri(v_uri_key);
		if (!v_page_obj.isNull()) {
			JsonArray v_page_assets = v_page_obj["pageAssets"].as<JsonArray>();
			if (!v_page_assets.isNull()) {
				for (JsonObject v_asset : v_page_assets) {
					const char* v_a_uri  = v_asset["uri"].as<const char*>();
					const char* v_a_path = v_asset["path"].as<const char*>();

					if (!v_a_uri || !v_a_path ||
						strlen(v_a_uri) == 0 || strlen(v_a_path) == 0)
						continue;

					const char* v_mime = W10_guessMime(v_a_path);

					// "/P010_main_021.css" → "/html_v2/P010_main_021.css"
					W10_pushRoute(v_a_uri, v_a_path, v_mime);
					// 절대 경로로 직접 접근도 허용
					W10_pushRoute(v_a_path, v_a_path, v_mime);
				}
			}
		}
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

			W10_pushRoute(v_uri_key, v_path_key, v_mime);
			W10_pushRoute(v_path_key, v_path_key, v_mime);
		}
	}

	// 3) 루트 및 단축 URI 리다이렉트 (reDirect 배열)
	bool v_root_redirect_defined = false;
	if (!v_redirect_array.isNull()) {
		for (JsonObject v_redir : v_redirect_array) {
			const char* v_from = v_redir["uriFrom"].as<const char*>();
			const char* v_to   = v_redir["uriTo"].as<const char*>();

			if (!v_from || !v_to || strlen(v_from) == 0 || strlen(v_to) == 0)
				continue;

			if (strcmp(v_from, "/") == 0) {
				v_root_redirect_defined = true;
			}

			// 예: "/chart_t1" → "/P020_chart_t1_008.html"
			s_server->on(v_from, HTTP_GET,
						 [v_to](AsyncWebServerRequest* r) {
							 r->redirect(v_to);
						 });
		}
	}

	// 3-1) reDirect에 "/"가 정의되지 않은 경우, 기존 root fallback 유지
	if (!v_root_redirect_defined) {
		s_server->on("/", HTTP_GET, [](AsyncWebServerRequest* r) {
			const char* v_cfg_html = g_A10_config_root.system->system.web.html;
			const char* v_default_html = "/html_v2/P010_main_021.html";

			JsonArray v_pages_array = s_pages_doc["pages"].as<JsonArray>();
			if (!v_pages_array.isNull()) {
				for (JsonObject v_page : v_pages_array) {
					if (v_page["isMain"] | false) {
						const char* v_p = v_page["path"].as<const char*>();
						if (v_p && v_p[0] != '\0') {
							v_default_html = v_p;
						}
						break;
					}
				}
			}

			if (v_cfg_html && strlen(v_cfg_html) > 0 && LittleFS.exists(v_cfg_html))
				r->redirect(v_cfg_html);
			else
				r->redirect(v_default_html);
		});
	}

	// 4) 정적 파일 GET 핸들러 등록
	for (uint8_t v_i = 0; v_i < s_routeCnt_static; v_i++) {
		s_server->on(s_routes_static[v_i].uri, HTTP_GET,
					 [v_i](AsyncWebServerRequest* r) {
						 const char* v_file = s_routes_static[v_i].file;
						 const char* v_mime = s_routes_static[v_i].mime;

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

	// 5) 메뉴 API
	s_server->on("/api/v1/menu", HTTP_GET, W10_getMenuJson);

	CL_D10_Logger::log(EN_L10_LOG_INFO,
					   "[W10] Web routing initialized (%d routes, Pages: %u, Assets: %u)",
					   s_routeCnt_static, s_page_count, s_asset_count);
}
