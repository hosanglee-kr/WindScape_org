/*
 * ------------------------------------------------------
 * 소스명 : W10_Web_Static_029.cpp
 * 모듈 약어 : W10
 * 모듈명 : Smart Nature Wind Web API (v029) - Static Assets & Menu Implementation
 * ------------------------------------------------------
 * 기능 요약:
 * - LittleFS 기반 JSON 파일(`/json/cfg_pages_028.json`)에서 웹 페이지(`pages`)와 정적 자산(`assets`) 정보를 분리 로드.
 * - `pages` 배열은 `order` 필드를 기준으로 정렬하여 메뉴 및 라우팅 등록에 사용.
 * - 로드된 JSON 데이터를 기반으로 모든 정적 자산(HTML, CSS, JS)의 라우팅 초기화 및 등록.
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

// JSON 파일 경로
constexpr char G_W10_PAGES_JSON[] = "/json/cfg_pages_028.json";

// 페이지/에셋 JSON Document
static JsonDocument s_pages_doc;
static uint16_t     s_page_count  = 0;
static uint16_t     s_asset_count = 0;

// 정렬용 구조체
struct ST_W10_PageEntry_t {
	int    order;
	String uri;
	String path;
	String label;
	String css;
	String js;
	bool   isMain;
};

// 정적 라우트 테이블
struct ST_W10_Route_t {
	const char* uri;
	const char* file;
	const char* mime;
};

#define G_W10_PAGE_ROUTES_MAX 20
static ST_W10_Route_t s_routes_static[G_W10_PAGE_ROUTES_MAX];
static uint8_t        s_routeCnt_static = 0;

// ------------------------------------------------------
// 헬퍼 함수
// ------------------------------------------------------
static void W10_pushRoute(const char* p_uri, const char* p_file, const char* p_mime) {
	if (s_routeCnt_static < G_W10_PAGE_ROUTES_MAX) {
		s_routes_static[s_routeCnt_static++] = {p_uri, p_file, p_mime};
	} else {
		CL_D10_Logger::log(EN_L10_LOG_ERROR, "[W10] Max static routes reached!");
	}
}

static bool W10_loadPagesJson() {
	File v_file = LittleFS.open(G_W10_PAGES_JSON, "r");
	if (!v_file) {
		CL_D10_Logger::log(EN_L10_LOG_ERROR, "[W10] Failed to open pages JSON: %s", G_W10_PAGES_JSON);
		return false;
	}

	DeserializationError v_err = deserializeJson(s_pages_doc, v_file);
	v_file.close();
	if (v_err) {
		CL_D10_Logger::log(EN_L10_LOG_ERROR, "[W10] Pages JSON deserialize failed: %s", v_err.c_str());
		return false;
	}

	if (!s_pages_doc.is<JsonObject>()) {
		CL_D10_Logger::log(EN_L10_LOG_ERROR, "[W10] Pages JSON is not a valid object.");
		return false;
	}

	JsonArray v_pages_array  = s_pages_doc["pages"].as<JsonArray>();
	JsonArray v_assets_array = s_pages_doc["assets"].as<JsonArray>();

	if (v_pages_array.isNull()) {
		CL_D10_Logger::log(EN_L10_LOG_ERROR, "[W10] Pages array missing or invalid in JSON.");
		return false;
	}

	// 1) pages → vector 복사
	std::vector<ST_W10_PageEntry_t> v_pages_vector;
	for (JsonVariantConst v : v_pages_array) {
		ST_W10_PageEntry_t v_e{};
		v_e.order  = v["order"].as<int>();
		v_e.uri    = v["uri"].as<String>();
		v_e.path   = v["path"].as<String>();
		v_e.label  = v["label"].as<String>();
		v_e.css    = v["css"].as<String>();
		v_e.js     = v["js"].as<String>();
		v_e.isMain = v["isMain"] | false;
		v_pages_vector.push_back(v_e);
	}

	// 2) order 기준 정렬
	std::sort(v_pages_vector.begin(), v_pages_vector.end(),
			  [](const ST_W10_PageEntry_t& a, const ST_W10_PageEntry_t& b) {
				  return a.order < b.order;
			  });

	// 3) 정렬된 vector로 pages 배열 재구성
	v_pages_array.clear();
	for (const auto& entry : v_pages_vector) {
		JsonObject v_item = v_pages_array.add<JsonObject>();
		v_item["order"]   = entry.order;
		v_item["uri"]	  = entry.uri;
		v_item["path"]	  = entry.path;
		v_item["label"]   = entry.label;
		v_item["css"]	  = entry.css;
		v_item["js"]	  = entry.js;
		v_item["isMain"]  = entry.isMain;
	}

	s_page_count  = v_pages_array.size();
	s_asset_count = v_assets_array.size();

	CL_D10_Logger::log(EN_L10_LOG_INFO,
					   "[W10] JSON loaded (Pages: %u, Assets: %u), Pages sorted.",
					   s_page_count, s_asset_count);
	return true;
}

// ------------------------------------------------------
// 메뉴 Web API 구현 (/api/v1/menu)
// ------------------------------------------------------
static void W10_getMenuJson(AsyncWebServerRequest* r) {
	JsonDocument v_doc_out;
	JsonArray    v_array_out = v_doc_out.to<JsonArray>();

	JsonArray v_pages_array = s_pages_doc["pages"].as<JsonArray>();

	for (JsonObject v_page : v_pages_array) {
		if (v_page["isMain"] | false)
			continue;

		JsonObject v_item = v_array_out.add<JsonObject>();
		v_item["label"]   = v_page["label"].as<JsonVariantConst>();

		String v_path = v_page["path"].as<String>();
		if (v_path.startsWith("/html/")) {
			v_item["path"] = v_path.substring(6); // "/html/" 제거
		} else {
			v_item["path"] = v_path;
		}
	}

	String v_json_output;
	if (serializeJson(v_doc_out, v_json_output) > 0) {
		auto* v_resp = r->beginResponse(200, "application/json", v_json_output);
		CL_W10_WebAPI::_applyHeaders(v_resp, true);
		r->send(v_resp);
	} else {
		CL_D10_Logger::log(EN_L10_LOG_ERROR, "[W10] Menu API serialization failed.");
		r->send(500, "application/json", "{\"error\":\"Serialization Failed\"}");
	}
}

// ------------------------------------------------------
// 정적 자산 라우팅 등록
// ------------------------------------------------------
void CL_W10_WebAPI::routeStaticAssets() {
	s_routeCnt_static = 0;

	if (!W10_loadPagesJson()) {
		CL_D10_Logger::log(EN_L10_LOG_ERROR, "[W10] Failed to load pages JSON. Cannot register static routes.");
		return;
	}

	auto& v_web = g_A10_config_root.system->system.web;

	JsonArray v_pages_array  = s_pages_doc["pages"].as<JsonArray>();
	JsonArray v_assets_array = s_pages_doc["assets"].as<JsonArray>();

	// 1) pages 기반 HTML & 전용 CSS/JS 라우트
	for (JsonObject v_page : v_pages_array) {
		const char* v_uri_key  = v_page["uri"].as<const char*>();
		const char* v_path_key = v_page["path"].as<const char*>();

		if (!v_uri_key || !v_path_key || strlen(v_uri_key) == 0 || strlen(v_path_key) == 0)
			continue;

		bool v_is_main = v_page["isMain"] | false;

		// HTML
		const char* v_mime_html = "text/html";
		if (v_is_main) {
			const char* v_cfg_html = v_web.html;
			if (strlen(v_cfg_html) > 0) {
				W10_pushRoute(v_cfg_html, v_cfg_html, v_mime_html);
			}
			W10_pushRoute(v_path_key, v_path_key, v_mime_html);
		} else {
			W10_pushRoute(v_uri_key, v_path_key, v_mime_html);
			W10_pushRoute(v_path_key, v_path_key, v_mime_html);
		}

		// 페이지 전용 CSS
		const char* v_css_file = v_page["css"].as<const char*>();
		if (v_css_file && strlen(v_css_file) > 0) {
			char v_css_path[64];
			snprintf(v_css_path, sizeof(v_css_path), "/html/%s", v_css_file);
			W10_pushRoute(v_css_file, v_css_path, "text/css");
			W10_pushRoute(v_css_path, v_css_path, "text/css");
		}

		// 페이지 전용 JS
		const char* v_js_file = v_page["js"].as<const char*>();
		if (v_js_file && strlen(v_js_file) > 0) {
			char v_js_path[64];
			snprintf(v_js_path, sizeof(v_js_path), "/html/%s", v_js_file);
			W10_pushRoute(v_js_file, v_js_path, "application/javascript");
			W10_pushRoute(v_js_path, v_js_path, "application/javascript");
		}
	}

	// 2) assets 기반 공통 자산 라우트
	for (JsonObject v_asset : v_assets_array) {
		const char* v_uri_key  = v_asset["uri"].as<const char*>();
		const char* v_path_key = v_asset["path"].as<const char*>();

		if (!v_uri_key || !v_path_key || strlen(v_uri_key) == 0 || strlen(v_path_key) == 0)
			continue;

		const char* v_mime = "application/octet-stream";
		if (strstr(v_path_key, ".css")) {
			v_mime = "text/css";
		} else if (strstr(v_path_key, ".js")) {
			v_mime = "application/javascript";
		}

		W10_pushRoute(v_uri_key, v_path_key, v_mime);
		W10_pushRoute(v_path_key, v_path_key, v_mime);
	}

	// 3) 루트 "/" 리다이렉트
	s_server->on("/", HTTP_GET, [](AsyncWebServerRequest* r) {
		const char* f = g_A10_config_root.system->system.web.html;

		const char* v_default_html = "/html/SC10_main_019.html";
		JsonArray   v_pages_array  = s_pages_doc["pages"].as<JsonArray>();
		for (JsonObject v_page : v_pages_array) {
			if (v_page["isMain"] | false) {
				const char* v_p = v_page["path"].as<const char*>();
				if (v_p && v_p[0] != '\0') {
					v_default_html = v_p;
				}
				break;
			}
		}

		if (strlen(f) > 0 && LittleFS.exists(f))
			r->redirect(f);
		else
			r->redirect(v_default_html);
	});

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

