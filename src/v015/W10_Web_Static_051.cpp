/*  
 * ------------------------------------------------------  
 * 소스명 : W10_Web_Static_051.cpp  
 * 모듈 약어 : W10  
 * 모듈명 : Smart Nature Wind Web API (v029) - Static Assets & Menu Implementation  
 * ------------------------------------------------------  
 * 기능 요약:  
 * - LittleFS 기반 JSON 파일(`/json/cfg_pages_032.json`)에서  
 *   웹 페이지(`pages`), 정적 자산(`assets`), 리다이렉트(`reDirect`) 정보 로드.  
 * - `pages` 배열은 `order` 필드를 기준으로 정렬한 뷰를 생성하여  
 *   메뉴(`/api/v1/menu`) 및 라우팅 등록에 사용.  
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
  
#include <ArduinoJson.h>  
#include <LittleFS.h>  
#include <string.h>  
  
#include <algorithm>  
  
#include "W10_Web_050.h"  
  
// ------------------------------------------------------  
// CL_W10_WebAPI 정적 멤버 정의 (Static Assets)  
// ------------------------------------------------------  
  
// JSON 파일 경로 (v032)  
// CL_W10_WebAPI::G_W10_PAGES_JSON 정의  
// const char* CL_W10_WebAPI::G_W10_PAGES_JSON = "/json/cfg_pages_032.json";  
  
// 메뉴 상태  
CL_W10_WebAPI::ST_W10_MenuState_t CL_W10_WebAPI::s_menu_state;  
  
// ------------------------------------------------------  
// 헬퍼 함수 구현  
// ------------------------------------------------------  
  
// 안전한 C 문자열 복사 (new[] + memset + strlcpy)  
char* CL_W10_WebAPI::W10_allocCString(const char* p_src) {  
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
  
const char* CL_W10_WebAPI::W10_guessMime(const char* p_path) {  
	if (!p_path)  
		return "application/octet-stream";  
  
	if (strstr(p_path, ".html"))  
		return "text/html; charset=utf-8";  
	if (strstr(p_path, ".css"))  
		return "text/css; charset=utf-8";  
	if (strstr(p_path, ".js"))  
		return "application/javascript; charset=utf-8";  
  
	return "application/octet-stream";  
}  
  
// JSON 로딩 (로컬 JsonDocument 사용)  
// ✅ B 루트형 확정: 루트에 pages/reDirect/assets 가 존재 (wrapper webPage 미지원)  
bool CL_W10_WebAPI::W10_loadPagesJson(JsonDocument& p_doc, uint16_t& p_page_count, uint16_t& p_asset_count) {  
	p_page_count  = 0;  
	p_asset_count = 0;  
  
	const auto& v_cfgJsonPath = CL_C10_ConfigManager::getCfgJsonFileMap();  
  
	File v_file = LittleFS.open(v_cfgJsonPath.webPage, "r");  
	if (!v_file) {  
		CL_D10_Logger::log(EN_L10_LOG_ERROR, "[W10] Failed to open pages JSON: %s", v_cfgJsonPath.webPage);  
		return false;  
	}  
  
	DeserializationError v_err = deserializeJson(p_doc, v_file);  
	v_file.close();  
	if (v_err) {  
		CL_D10_Logger::log(EN_L10_LOG_ERROR, "[W10] Pages JSON deserialize failed: %s", v_err.c_str());  
		return false;  
	}  
  
	if (!p_doc.is<JsonObject>()) {  
		CL_D10_Logger::log(EN_L10_LOG_ERROR, "[W10] Pages JSON root is not a valid object.");  
		return false;  
	}  
  
	JsonObjectConst j_root = p_doc.as<JsonObjectConst>();  
  
	// ✅ camelCase only  
	JsonArrayConst v_pages_array  = j_root["pages"].as<JsonArrayConst>();  
	JsonArrayConst v_assets_array = j_root["assets"].as<JsonArrayConst>();  
  
	if (v_pages_array.isNull()) {  
		CL_D10_Logger::log(EN_L10_LOG_ERROR, "[W10] 'pages' array missing or invalid in JSON root.");  
		return false;  
	}  
  
	p_page_count  = (uint16_t)v_pages_array.size();  
	p_asset_count = v_assets_array.isNull() ? 0 : (uint16_t)v_assets_array.size();  
  
	CL_D10_Logger::log(EN_L10_LOG_INFO, "[W10] JSON loaded (Pages: %u, Assets: %u)", p_page_count, p_asset_count);  
	return true;  
}  
  
// 정적 파일 라우트 등록 (등록 시점에 URI/FILE/MIME 문자열을 복사해서 평생 유지)  
// - "/html_v2/..." 실제 경로는 serveStatic("/html_v2", ...)에 위임  
// - 여기서는 short URI("/P0xx_...") 등 별칭만 등록하는 것을 기본으로 함  
void CL_W10_WebAPI::W10_registerStaticRoute(const char* p_uri, const char* p_file, const char* p_mime) {  
	if (!p_uri || !p_file || !p_mime)  
		return;  
  
	if (strlen(p_uri) == 0 || strlen(p_file) == 0)  
		return;  
  
	char* v_uri	 = W10_allocCString(p_uri);  
	char* v_file = W10_allocCString(p_file);  
	char* v_mime = W10_allocCString(p_mime);  
  
	if (!v_uri || !v_file || !v_mime) {  
		if (v_uri)  
			delete[] v_uri;  
		if (v_file)  
			delete[] v_file;  
		if (v_mime)  
			delete[] v_mime;  
  
		CL_D10_Logger::log(EN_L10_LOG_ERROR, "[W10] Route alloc failed (uri:%s, file:%s)", p_uri, p_file);  
		return;  
	}  
  
	s_server->on(v_uri, HTTP_GET, [v_file, v_mime](AsyncWebServerRequest* r) {  
		if (LittleFS.exists(v_file)) {  
			auto* v_resp = r->beginResponse(LittleFS, v_file, v_mime);  
			CL_W10_WebAPI::_applyHeaders(v_resp, false);  
			r->send(v_resp);  
			return;  
		}  
  
		String v_msg  = "/* missing:" + String(v_file) + " */";  
		auto*  v_resp = r->beginResponse(200, v_mime, v_msg);  
		CL_W10_WebAPI::_applyHeaders(v_resp, true);  
		r->send(v_resp);  
	});  
}  
  
// ------------------------------------------------------  
// 메뉴 Web API 구현 (/api/v1/menu)  
//  - s_menu_state.pages_sorted 기반으로 응답  
//  - pages는 routeStaticAssets() 실행 시 1회 로드/정렬됨  
// ------------------------------------------------------  
void CL_W10_WebAPI::W10_getMenuJson(AsyncWebServerRequest* r) {  
	JsonDocument v_doc_out;  
	JsonArray	 v_array_out = v_doc_out.to<JsonArray>();  
  
	for (const auto& v_entry : s_menu_state.pages_sorted) {  
		JsonObject v_item = v_array_out.add<JsonObject>();  
		v_item["label"]	  = v_entry.label;  
		v_item["path"]	  = v_entry.path;  // "/html_v2/..." 그대로  
		v_item["uri"]	  = v_entry.uri;   // "/P040_dashboard_003.html" 같은 short html path  
		v_item["order"]	  = v_entry.order;  
		v_item["isMain"]  = v_entry.isMain;  
		v_item["enable"]  = v_entry.enable;	 // enable 필드  
	}  
  
	sendJson(r, v_doc_out);  
}  
  
// ------------------------------------------------------  
// 정적 자산 라우팅 등록  
//  - pages[].path        → HTML 라우트 (short URI 기준으로 등록)  
//  - pages[].uri         → HTML 라우트 (short html path)  
//  - pages[].pageAssets[]→ 페이지 전용 CSS/JS 라우트  
//  - assets[]            → 공통 CSS/JS 라우트  
//  - reDirect[]          → 단축 URI 리다이렉트  
//  - 메뉴 데이터는 s_menu_state에 정렬/보관  
//  - JsonDocument는 이 함수 내부 로컬 변수로만 사용  
// ------------------------------------------------------  
void CL_W10_WebAPI::routeStaticAssets() {  
	// 메뉴 상태 초기화  
	s_menu_state.pages_sorted.clear();  
	s_menu_state.main_path = "";  
  
	JsonDocument v_doc;  
	uint16_t	 v_page_count  = 0;  
	uint16_t	 v_asset_count = 0;  
  
	if (!W10_loadPagesJson(v_doc, v_page_count, v_asset_count)) {  
		CL_D10_Logger::log(EN_L10_LOG_ERROR, "[W10] Failed to load pages JSON. Cannot register static routes.");  
		return;  
	}  
  
	// ✅ B 루트형 확정 (wrapper 미지원)  
	JsonObjectConst j_root = v_doc.as<JsonObjectConst>();  
	if (j_root.isNull()) {  
		CL_D10_Logger::log(EN_L10_LOG_ERROR, "[W10] Invalid root object after load.");  
		return;  
	}  
  
	JsonArrayConst v_pages_array	   = j_root["pages"].as<JsonArrayConst>();  
	JsonArrayConst v_assets_array	   = j_root["assets"].as<JsonArrayConst>();  
	JsonArrayConst v_redirect_array   = j_root["reDirect"].as<JsonArrayConst>();  // camelCase only  
  
	// 1) pages 기반 HTML + 페이지별 전용 자산(pageAssets) 라우트 등록  
	if (!v_pages_array.isNull()) {  
		for (JsonObjectConst v_page : v_pages_array) {  
			ST_W10_PageEntry_t v_entry{};  
  
			v_entry.order  = v_page["order"].as<int>();  
			v_entry.uri	   = v_page["uri"].as<String>();  
			v_entry.path   = v_page["path"].as<String>();  
			v_entry.label  = v_page["label"].as<String>();  
			v_entry.isMain = v_page["isMain"] | false;  
  
			// enable 필드 (기본값 true)  
			bool v_enable  = true;  
			if (!v_page["enable"].isNull()) {  
				v_enable = v_page["enable"].as<bool>();  
			}  
			v_entry.enable = v_enable;  
  
			// 메뉴용 엔트리 누적  
			s_menu_state.pages_sorted.push_back(v_entry);  
  
			// isMain == true 인 첫 번째 페이지 path를 main_path로 보관 (root fallback용)  
			if (v_entry.isMain && s_menu_state.main_path.isEmpty()) {  
				if (!v_entry.path.isEmpty()) {  
					s_menu_state.main_path = v_entry.path;  
				}  
			}  
  
			// 라우팅 등록  
			const char* v_uri_key  = v_entry.uri.c_str();	// "/P010_main_021.html"  
			const char* v_path_key = v_entry.path.c_str();	// "/html_v2/P010_main_021.html"  
  
			if (!v_uri_key || !v_path_key || strlen(v_path_key) == 0)  
				continue;  
  
			// ✅ charset 포함 (기존 동작 영향 최소)  
			const char* v_mime_html = "text/html; charset=utf-8";  
  
			// HTML 페이지는 short URI만 등록, 실제 경로("/html_v2/...")는 serveStatic에 위임  
			if (strlen(v_uri_key) > 0) {  
				W10_registerStaticRoute(v_uri_key, v_path_key, v_mime_html);  
			}  
  
			// 각 페이지의 pageAssets 처리 (CSS/JS 등)  
			JsonArrayConst v_page_assets = v_page["pageAssets"].as<JsonArrayConst>();  // camelCase only  
			if (!v_page_assets.isNull()) {  
				for (JsonObjectConst v_asset : v_page_assets) {  
					const char* v_a_uri	 = v_asset["uri"].as<const char*>();  
					const char* v_a_path = v_asset["path"].as<const char*>();  
  
					if (!v_a_uri || !v_a_path || strlen(v_a_uri) == 0 || strlen(v_a_path) == 0)  
						continue;  
  
					const char* v_mime = W10_guessMime(v_a_path);  
  
					// short URI만 route에 등록, 실제 경로("/html_v2/...")는 serveStatic 처리  
					if (strcmp(v_a_uri, v_a_path) != 0) {  
						W10_registerStaticRoute(v_a_uri, v_a_path, v_mime);  
					}  
				}  
			}  
		}  
  
		// 메뉴 표시용 pages_sorted를 order 기준으로 정렬  
		std::sort(s_menu_state.pages_sorted.begin(), s_menu_state.pages_sorted.end(),  
		          [](const ST_W10_PageEntry_t& a, const ST_W10_PageEntry_t& b) { return a.order < b.order; });  
	}  
  
	// 2) assets 기반 공통 자산 라우트 (공통 CSS/JS 등)  
	if (!v_assets_array.isNull()) {  
		for (JsonObjectConst v_asset : v_assets_array) {  
			const char* v_uri_key  = v_asset["uri"].as<const char*>();  
			const char* v_path_key = v_asset["path"].as<const char*>();  
  
			if (!v_uri_key || !v_path_key || strlen(v_uri_key) == 0 || strlen(v_path_key) == 0)  
				continue;  
  
			const char* v_mime = W10_guessMime(v_path_key);  
  
			// 공통 자산도 short URI만 등록 (예: "/P000_common_001.css")  
			if (strcmp(v_uri_key, v_path_key) != 0) {  
				W10_registerStaticRoute(v_uri_key, v_path_key, v_mime);  
			}  
		}  
	}  
  
	// 3) 루트 및 단축 URI 리다이렉트 (reDirect 배열, camelCase only)  
	bool v_root_redirect_defined = false;  
	if (!v_redirect_array.isNull()) {  
		for (JsonObjectConst v_redir : v_redirect_array) {  
			const char* v_from = v_redir["uriFrom"].as<const char*>();  
			const char* v_to   = v_redir["uriTo"].as<const char*>();  
  
			if (!v_from || !v_to || strlen(v_from) == 0 || strlen(v_to) == 0)  
				continue;  
  
			if (strcmp(v_from, "/") == 0) {  
				v_root_redirect_defined = true;  
			}  
  
			// 복사본 생성 (서버 생애 동안 유지)  
			char* v_from_copy = W10_allocCString(v_from);  
			char* v_to_copy	  = W10_allocCString(v_to);  
  
			if (!v_from_copy || !v_to_copy) {  
				CL_D10_Logger::log(EN_L10_LOG_ERROR, "[W10] Redirect alloc failed (from:%s, to:%s)", v_from, v_to);  
				continue;  
			}  
  
			// 예: "/chart_t1" → "/P020_chart_t1_008.html"  
			s_server->on(v_from_copy, HTTP_GET, [v_to_copy](AsyncWebServerRequest* r) { r->redirect(v_to_copy); });  
		}  
	}  
  
	// 3-1) reDirect에 "/"가 정의되지 않은 경우, isMain 기반 root fallback  
	if (!v_root_redirect_defined) {  
		// main_path를 캡처해서 사용 (String 복사본)  
		String v_root_path = s_menu_state.main_path;  
  
		s_server->on("/", HTTP_GET, [v_root_path](AsyncWebServerRequest* r) {  
			// 기본값  
			const char* v_default_html = "/html_v2/P010_main_021.html";  
  
			if (!v_root_path.isEmpty()) {  
				v_default_html = v_root_path.c_str();  
			}  
  
			// 파일 존재 여부 확인 후 리다이렉트  
			if (LittleFS.exists(v_default_html))  
				r->redirect(v_default_html);  
			else  
				r->redirect("/");  // 최악의 경우 루프지만, JSON/FS 구성이 잘못된 상황  
		});  
	}  
  
	// 4) 정적 폴더 매핑  
	// - "/html_v2/..." 경로는 모두 LittleFS의 "/html_v2" 폴더에서 정적 서빙  
	s_server->serveStatic(W10_Const::PATH_STATIC_HTML, LittleFS, W10_Const::PATH_STATIC_HTML);  
  
	// 5) 메뉴 API  
	s_server->on(W10_Const::HTTP_API_MENU, HTTP_GET, W10_getMenuJson);  
  
	CL_D10_Logger::log(EN_L10_LOG_INFO, "[W10] Web routing initialized (Pages: %u, Assets: %u)", v_page_count, v_asset_count);  
}
