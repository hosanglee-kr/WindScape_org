/*
 * ------------------------------------------------------
 * 소스명 : C10_Config_Extras_041.cpp
 * 모듈 약어 : C10
 * 모듈명 : Smart Nature Wind Configuration Manager - Extras (NvsSpec/WebPage)
 * ------------------------------------------------------
 * 기능 요약:
 *  - NvsSpec / WebPage 설정 Load/Save
 *  - NvsSpec / WebPage 설정 JSON Patch 적용
 *  - NvsSpec / WebPage 설정 JSON Export
 *  - camelCase only (snake_case fallback 제거)
 *  - WebPage는 루트형(JSON root: pages/reDirect/assets) 저장/로드/패치/익스포트 (옵션 B 확정)
 *  - NvsSpec는 랩핑(nvsSpec) 또는 루트형 모두 로드/패치 허용 (저장은 nvsSpec 랩핑 유지)
 * ------------------------------------------------------
 * [구현 규칙]
 *  - 항상 소스 시작 주석 부분 체계 유지 및 내용 업데이트
 *  - 소스 시작 주석 부분 구현규칙, 코드네이밍규칙 내용 그대로 유지, 수정금지
 *  - ArduinoJson v7.x.x 사용 (v6 이하 사용 금지)
 *  - JsonDocument 단일 타입만 사용
 *  - createNestedArray/Object/containsKey 사용 금지
 *  - memset + strlcpy 기반 안전 초기화
 *  - 주석/필드명은 JSON 구조와 동일하게 유지
 *  - 변수명은 가능한 해석 가능하게
 * ------------------------------------------------------
 * [코드 네이밍 규칙]
 *   - namespace 명        : 모듈약어_ 접두사
 *   - namespace 내 상수    : 모둘약어 접두시 미사용
 *   - 전역 상수,매크로      : G_모듈약어_ 접두사
 *   - 전역 변수             : g_모듈약어_ 접두사
 *   - 전역 함수             : 모듈약어_ 접두사
 *   - type                  : T_모듈약어_ 접두사
 *   - typedef               : _t  접미사
 *   - enum 상수             : EN_모듈약어_ 접두사
 *   - 구조체                : ST_모듈약어_ 접두사
 *   - 클래스명              : CL_모듈약어_ 접두사 , 버전 제거
 *   - 클래스 private 멤버   : _ 접두사
 *   - 클래스 멤버(함수/변수) : 모듈약어 접두사 미사용
 *   - 클래스 정적 멤버      : s_ 접두사
 *   - 함수 로컬 변수        : v_ 접두사
 *   - 함수 인자             : p_ 접두사
 * ------------------------------------------------------
 */

#include "C10_Config_041.h"

// =====================================================
// 내부 Helper: camelCase only
//  - WebPage: 루트형 확정 -> pages/reDirect/assets는 문서 루트에서 직접 사용
//  - NvsSpec: {"nvsSpec":{...}} 또는 루트형 {...} 둘 다 허용(로드/패치)
// =====================================================
static const char* C10_getStr(JsonObjectConst p_obj, const char* p_key, const char* p_def) {
	if (p_obj.isNull()) return p_def;
	if (p_obj[p_key].is<const char*>()) {
		const char* v = p_obj[p_key].as<const char*>();
		return v ? v : p_def;
	}
	return p_def;
}

template <typename T>
static T C10_getNum(JsonObjectConst p_obj, const char* p_key, T p_def) {
	if (p_obj.isNull()) return p_def;
	if (p_obj[p_key].is<T>()) return p_obj[p_key].as<T>();
	return p_def;
}

static bool C10_getBool(JsonObjectConst p_obj, const char* p_key, bool p_def) {
	if (p_obj.isNull()) return p_def;
	if (p_obj[p_key].is<bool>()) return p_obj[p_key].as<bool>();
	return p_def;
}

static JsonArrayConst C10_getArr(JsonObjectConst p_obj, const char* p_key) {
	if (p_obj.isNull()) return JsonArrayConst();
	return p_obj[p_key].as<JsonArrayConst>();
}

// 랩핑 키가 있으면 우선 사용, 없으면 루트 자체를 사용
static JsonObjectConst C10_pickRootObject(JsonDocument& p_doc, const char* p_wrapKey) {
	JsonObjectConst v_wrapped = p_doc[p_wrapKey].as<JsonObjectConst>();
	if (!v_wrapped.isNull()) return v_wrapped;
	return p_doc.as<JsonObjectConst>();
}

// =====================================================
// 2-x. 목적물별 Load 구현 (NvsSpec / WebPage)
// =====================================================
bool CL_C10_ConfigManager::loadNvsSpecConfig(ST_A20_NvsSpecConfig_t& p_cfg) {
	JsonDocument v_doc;

	const char* v_cfgJsonPath = nullptr;
	if (s_cfgJsonFileMap.nvsSpec[0] != '\0') {
		v_cfgJsonPath = s_cfgJsonFileMap.nvsSpec;
	} else {
		CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] loadNvsSpecConfig: s_cfgJsonFileMap.nvsSpec is empty");
		return false;
	}

	if (!ioLoadJson(v_cfgJsonPath, v_doc)) {
		CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] loadNvsSpecConfig: ioLoadJson failed (%s)", v_cfgJsonPath);
		return false;
	}

	// {"nvsSpec":{...}} 또는 루트 자체 {...} 지원
	JsonObjectConst j_root = C10_pickRootObject(v_doc, "nvsSpec");
	if (j_root.isNull()) {
		CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] loadNvsSpecConfig: root object invalid");
		return false;
	}

	// 초기화(안전)
	memset(&p_cfg, 0, sizeof(p_cfg));

	// namespaceName: JSON key "namespace"
	const char* v_ns = C10_getStr(j_root, "namespace", "SNW");
	strlcpy(p_cfg.namespaceName, v_ns, sizeof(p_cfg.namespaceName));

	// entries[]
	p_cfg.entryCount = 0;
	JsonArrayConst j_entries = C10_getArr(j_root, "entries");
	if (j_entries.isNull()) {
		// 스펙 파일이 비었거나 키가 없으면 기본값 유지(유연)
		CL_D10_Logger::log(EN_L10_LOG_WARN, "[C10] loadNvsSpecConfig: missing 'entries' (empty spec)");
		return true;
	}

	for (JsonObjectConst j_e : j_entries) {
		if (p_cfg.entryCount >= A20_Const::MAX_NVS_ENTRIES) break;

		ST_A20_NvsEntry_t& v_ent = p_cfg.entries[p_cfg.entryCount];
		memset(&v_ent, 0, sizeof(v_ent));

		const char* v_key  = C10_getStr(j_e, "key", "");
		const char* v_type = C10_getStr(j_e, "type", "");
		const char* v_def  = C10_getStr(j_e, "defaultValue", "");

		// key는 필수
		if (!v_key || v_key[0] == '\0') continue;

		strlcpy(v_ent.key, v_key, sizeof(v_ent.key));
		strlcpy(v_ent.type, v_type ? v_type : "", sizeof(v_ent.type));
		strlcpy(v_ent.defaultValue, v_def ? v_def : "", sizeof(v_ent.defaultValue));

		p_cfg.entryCount++;
	}

	return true;
}

bool CL_C10_ConfigManager::loadWebPageConfig(ST_A20_WebPageConfig_t& p_cfg) {
	JsonDocument v_doc;

	const char* v_cfgJsonPath = nullptr;
	if (s_cfgJsonFileMap.webPage[0] != '\0') {
		v_cfgJsonPath = s_cfgJsonFileMap.webPage;
	} else {
		CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] loadWebPageConfig: s_cfgJsonFileMap.webPage is empty");
		return false;
	}

	if (!ioLoadJson(v_cfgJsonPath, v_doc)) {
		CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] loadWebPageConfig: ioLoadJson failed (%s)", v_cfgJsonPath);
		return false;
	}

	// ✅ WebPage는 루트형 확정: pages/reDirect/assets는 문서 루트에서 읽음
	JsonObjectConst j_root = v_doc.as<JsonObjectConst>();
	if (j_root.isNull()) {
		CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] loadWebPageConfig: root object invalid");
		return false;
	}

	memset(&p_cfg, 0, sizeof(p_cfg));

	// pages[]
	p_cfg.pageCount = 0;
	JsonArrayConst j_pages = C10_getArr(j_root, "pages");
	if (!j_pages.isNull()) {
		for (JsonObjectConst j_p : j_pages) {
			if (p_cfg.pageCount >= A20_Const::MAX_PAGES) break;

			ST_A20_PageItem_t& v_p = p_cfg.pages[p_cfg.pageCount];
			memset(&v_p, 0, sizeof(v_p));

			strlcpy(v_p.uri,   C10_getStr(j_p, "uri", ""), sizeof(v_p.uri));
			strlcpy(v_p.path,  C10_getStr(j_p, "path", ""), sizeof(v_p.path));
			strlcpy(v_p.label, C10_getStr(j_p, "label", ""), sizeof(v_p.label));

			v_p.enable = C10_getBool(j_p, "enable", true);
			v_p.isMain = C10_getBool(j_p, "isMain", false);
			v_p.order  = C10_getNum<uint16_t>(j_p, "order", 0);

			// pageAssets[]
			v_p.pageAssetCount = 0;
			JsonArrayConst j_assets = C10_getArr(j_p, "pageAssets");
			if (!j_assets.isNull()) {
				for (JsonObjectConst j_a : j_assets) {
					if (v_p.pageAssetCount >= A20_Const::MAX_PAGE_ASSETS) break;

					ST_A20_PageAsset_t& v_a = v_p.pageAssets[v_p.pageAssetCount];
					memset(&v_a, 0, sizeof(v_a));

					strlcpy(v_a.uri,  C10_getStr(j_a, "uri", ""), sizeof(v_a.uri));
					strlcpy(v_a.path, C10_getStr(j_a, "path", ""), sizeof(v_a.path));

					// 불량 방지
					if (v_a.uri[0] == '\0' || v_a.path[0] == '\0') continue;

					v_p.pageAssetCount++;
				}
			}

			// uri/path 둘 중 하나라도 없으면 스킵(불량 방지)
			if (v_p.uri[0] == '\0' || v_p.path[0] == '\0') {
				continue;
			}

			p_cfg.pageCount++;
		}
	} else {
		CL_D10_Logger::log(EN_L10_LOG_WARN, "[C10] loadWebPageConfig: missing 'pages' (empty config)");
	}

	// reDirect[]
	p_cfg.reDirectCount = 0;
	JsonArrayConst j_red = C10_getArr(j_root, "reDirect");
	if (!j_red.isNull()) {
		for (JsonObjectConst j_r : j_red) {
			if (p_cfg.reDirectCount >= A20_Const::MAX_REDIRECTS) break;

			ST_A20_ReDirectItem_t& v_r = p_cfg.reDirect[p_cfg.reDirectCount];
			memset(&v_r, 0, sizeof(v_r));

			strlcpy(v_r.uriFrom, C10_getStr(j_r, "uriFrom", ""), sizeof(v_r.uriFrom));
			strlcpy(v_r.uriTo,   C10_getStr(j_r, "uriTo", ""), sizeof(v_r.uriTo));

			if (v_r.uriFrom[0] == '\0' || v_r.uriTo[0] == '\0') continue;

			p_cfg.reDirectCount++;
		}
	}

	// assets[] (common assets)
	p_cfg.assetCount = 0;
	JsonArrayConst j_cas = C10_getArr(j_root, "assets");
	if (!j_cas.isNull()) {
		for (JsonObjectConst j_c : j_cas) {
			if (p_cfg.assetCount >= A20_Const::MAX_COMMON_ASSETS) break;

			ST_A20_CommonAsset_t& v_c = p_cfg.assets[p_cfg.assetCount];
			memset(&v_c, 0, sizeof(v_c));

			strlcpy(v_c.uri,  C10_getStr(j_c, "uri", ""), sizeof(v_c.uri));
			strlcpy(v_c.path, C10_getStr(j_c, "path", ""), sizeof(v_c.path));
			v_c.isCommon = C10_getBool(j_c, "isCommon", false);

			if (v_c.uri[0] == '\0' || v_c.path[0] == '\0') continue;

			p_cfg.assetCount++;
		}
	}

	return true;
}

// =====================================================
// 2-x. 목적물별 Save 구현 (NvsSpec / WebPage)
//  - NvsSpec: 기존대로 {"nvsSpec":{...}} 랩핑 저장(스펙 파일 성격)
//  - WebPage: ✅ 옵션 B 확정 -> 루트형 저장 {pages,reDirect,assets}
// =====================================================
bool CL_C10_ConfigManager::saveNvsSpecConfig(const ST_A20_NvsSpecConfig_t& p_cfg) {
	JsonDocument v_doc;

	// 루트 래핑: nvsSpec
	JsonObject v_root = v_doc["nvsSpec"].to<JsonObject>();

	v_root["namespace"] = p_cfg.namespaceName;

	JsonArray v_arr = v_root["entries"].to<JsonArray>();
	for (uint8_t v_i = 0; v_i < p_cfg.entryCount && v_i < A20_Const::MAX_NVS_ENTRIES; v_i++) {
		const ST_A20_NvsEntry_t& v_e = p_cfg.entries[v_i];
		if (v_e.key[0] == '\0') continue;

		JsonObject v_o = v_arr.add<JsonObject>();
		v_o["key"]          = v_e.key;
		v_o["type"]         = v_e.type;
		v_o["defaultValue"] = v_e.defaultValue;
	}

	return ioSaveJson(s_cfgJsonFileMap.nvsSpec, v_doc);
}

bool CL_C10_ConfigManager::saveWebPageConfig(const ST_A20_WebPageConfig_t& p_cfg) {
	JsonDocument v_doc;

	// ✅ 루트형 저장: pages / reDirect / assets

	// pages[]
	JsonArray v_pages = v_doc["pages"].to<JsonArray>();
	for (uint8_t v_i = 0; v_i < p_cfg.pageCount && v_i < A20_Const::MAX_PAGES; v_i++) {
		const ST_A20_PageItem_t& v_p = p_cfg.pages[v_i];
		if (v_p.uri[0] == '\0' || v_p.path[0] == '\0') continue;

		JsonObject v_po = v_pages.add<JsonObject>();
		v_po["uri"]    = v_p.uri;
		v_po["path"]   = v_p.path;
		v_po["label"]  = v_p.label;
		v_po["enable"] = v_p.enable;
		v_po["isMain"] = v_p.isMain;
		v_po["order"]  = v_p.order;

		JsonArray v_pa = v_po["pageAssets"].to<JsonArray>();
		for (uint8_t v_j = 0; v_j < v_p.pageAssetCount && v_j < A20_Const::MAX_PAGE_ASSETS; v_j++) {
			const ST_A20_PageAsset_t& v_a = v_p.pageAssets[v_j];
			if (v_a.uri[0] == '\0' || v_a.path[0] == '\0') continue;

			JsonObject v_ao = v_pa.add<JsonObject>();
			v_ao["uri"]  = v_a.uri;
			v_ao["path"] = v_a.path;
		}
	}

	// reDirect[]
	JsonArray v_red = v_doc["reDirect"].to<JsonArray>();
	for (uint8_t v_i = 0; v_i < p_cfg.reDirectCount && v_i < A20_Const::MAX_REDIRECTS; v_i++) {
		const ST_A20_ReDirectItem_t& v_r = p_cfg.reDirect[v_i];
		if (v_r.uriFrom[0] == '\0' || v_r.uriTo[0] == '\0') continue;

		JsonObject v_ro = v_red.add<JsonObject>();
		v_ro["uriFrom"] = v_r.uriFrom;
		v_ro["uriTo"]   = v_r.uriTo;
	}

	// assets[]
	JsonArray v_as = v_doc["assets"].to<JsonArray>();
	for (uint8_t v_i = 0; v_i < p_cfg.assetCount && v_i < A20_Const::MAX_COMMON_ASSETS; v_i++) {
		const ST_A20_CommonAsset_t& v_c = p_cfg.assets[v_i];
		if (v_c.uri[0] == '\0' || v_c.path[0] == '\0') continue;

		JsonObject v_co = v_as.add<JsonObject>();
		v_co["uri"]      = v_c.uri;
		v_co["path"]     = v_c.path;
		v_co["isCommon"] = v_c.isCommon;
	}

	return ioSaveJson(s_cfgJsonFileMap.webPage, v_doc);
}

// =====================================================
// 4-x. JSON Patch (NvsSpec / WebPage) - camelCase only
//  - NvsSpec: {"nvsSpec":{...}} 또는 루트형 patch 허용
//  - WebPage: ✅ 루트형 patch 확정 (pages/reDirect/assets가 루트에 위치)
// =====================================================
bool CL_C10_ConfigManager::patchNvsSpecFromJson(ST_A20_NvsSpecConfig_t& p_cfg, const JsonDocument& p_patch) {
	bool v_changed = false;

	C10_MUTEX_ACQUIRE_BOOL();

	// {"nvsSpec":{...}} 또는 루트 자체 {...}
	JsonObjectConst j_root = p_patch["nvsSpec"].as<JsonObjectConst>();
	if (j_root.isNull()) j_root = p_patch.as<JsonObjectConst>();
	if (j_root.isNull()) {
		C10_MUTEX_RELEASE();
		return false;
	}

	// namespace
	if (j_root["namespace"].is<const char*>()) {
		const char* v_ns = j_root["namespace"].as<const char*>();
		if (v_ns && v_ns[0] && strcmp(v_ns, p_cfg.namespaceName) != 0) {
			strlcpy(p_cfg.namespaceName, v_ns, sizeof(p_cfg.namespaceName));
			v_changed = true;
		}
	}

	// entries: 전체 교체 정책(단순/명확)
	JsonArrayConst j_entries = j_root["entries"].as<JsonArrayConst>();
	if (!j_entries.isNull()) {
		memset(p_cfg.entries, 0, sizeof(p_cfg.entries));
		p_cfg.entryCount = 0;

		for (JsonObjectConst j_e : j_entries) {
			if (p_cfg.entryCount >= A20_Const::MAX_NVS_ENTRIES) break;

			const char* v_key  = C10_getStr(j_e, "key", "");
			const char* v_type = C10_getStr(j_e, "type", "");
			const char* v_def  = C10_getStr(j_e, "defaultValue", "");

			if (!v_key || v_key[0] == '\0') continue;

			ST_A20_NvsEntry_t& v_ent = p_cfg.entries[p_cfg.entryCount];
			memset(&v_ent, 0, sizeof(v_ent));

			strlcpy(v_ent.key, v_key, sizeof(v_ent.key));
			strlcpy(v_ent.type, v_type ? v_type : "", sizeof(v_ent.type));
			strlcpy(v_ent.defaultValue, v_def ? v_def : "", sizeof(v_ent.defaultValue));

			p_cfg.entryCount++;
		}

		v_changed = true;
		CL_D10_Logger::log(EN_L10_LOG_DEBUG, "[C10] NvsSpec entries fully replaced.");
	}

	if (v_changed) {
		_dirty_nvsSpec = true;
		CL_D10_Logger::log(EN_L10_LOG_INFO, "[C10] NvsSpec config patched (Memory Only, camelCase). Dirty=true");
	}

	C10_MUTEX_RELEASE();
	return v_changed;
}

bool CL_C10_ConfigManager::patchWebPageFromJson(ST_A20_WebPageConfig_t& p_cfg, const JsonDocument& p_patch) {
	bool v_changed = false;

	C10_MUTEX_ACQUIRE_BOOL();

	// ✅ WebPage는 루트형 patch 확정
	JsonObjectConst j_root = p_patch.as<JsonObjectConst>();
	if (j_root.isNull()) {
		C10_MUTEX_RELEASE();
		return false;
	}

	// pages: 전체 교체
	JsonArrayConst j_pages = j_root["pages"].as<JsonArrayConst>();
	if (!j_pages.isNull()) {
		memset(&p_cfg.pages, 0, sizeof(p_cfg.pages));
		p_cfg.pageCount = 0;

		for (JsonObjectConst j_p : j_pages) {
			if (p_cfg.pageCount >= A20_Const::MAX_PAGES) break;

			ST_A20_PageItem_t& v_p = p_cfg.pages[p_cfg.pageCount];
			memset(&v_p, 0, sizeof(v_p));

			strlcpy(v_p.uri,   C10_getStr(j_p, "uri", ""), sizeof(v_p.uri));
			strlcpy(v_p.path,  C10_getStr(j_p, "path", ""), sizeof(v_p.path));
			strlcpy(v_p.label, C10_getStr(j_p, "label", ""), sizeof(v_p.label));

			v_p.enable = C10_getBool(j_p, "enable", true);
			v_p.isMain = C10_getBool(j_p, "isMain", false);
			v_p.order  = C10_getNum<uint16_t>(j_p, "order", 0);

			v_p.pageAssetCount = 0;
			JsonArrayConst j_pa = C10_getArr(j_p, "pageAssets");
			if (!j_pa.isNull()) {
				for (JsonObjectConst j_a : j_pa) {
					if (v_p.pageAssetCount >= A20_Const::MAX_PAGE_ASSETS) break;

					ST_A20_PageAsset_t& v_a = v_p.pageAssets[v_p.pageAssetCount];
					memset(&v_a, 0, sizeof(v_a));

					strlcpy(v_a.uri,  C10_getStr(j_a, "uri", ""), sizeof(v_a.uri));
					strlcpy(v_a.path, C10_getStr(j_a, "path", ""), sizeof(v_a.path));

					if (v_a.uri[0] == '\0' || v_a.path[0] == '\0') continue;

					v_p.pageAssetCount++;
				}
			}

			if (v_p.uri[0] == '\0' || v_p.path[0] == '\0') continue;

			p_cfg.pageCount++;
		}

		v_changed = true;
		CL_D10_Logger::log(EN_L10_LOG_DEBUG, "[C10] WebPage pages fully replaced.");
	}

	// reDirect: 전체 교체
	JsonArrayConst j_red = j_root["reDirect"].as<JsonArrayConst>();
	if (!j_red.isNull()) {
		memset(&p_cfg.reDirect, 0, sizeof(p_cfg.reDirect));
		p_cfg.reDirectCount = 0;

		for (JsonObjectConst j_r : j_red) {
			if (p_cfg.reDirectCount >= A20_Const::MAX_REDIRECTS) break;

			ST_A20_ReDirectItem_t& v_r = p_cfg.reDirect[p_cfg.reDirectCount];
			memset(&v_r, 0, sizeof(v_r));

			strlcpy(v_r.uriFrom, C10_getStr(j_r, "uriFrom", ""), sizeof(v_r.uriFrom));
			strlcpy(v_r.uriTo,   C10_getStr(j_r, "uriTo", ""), sizeof(v_r.uriTo));

			if (v_r.uriFrom[0] == '\0' || v_r.uriTo[0] == '\0') continue;

			p_cfg.reDirectCount++;
		}

		v_changed = true;
		CL_D10_Logger::log(EN_L10_LOG_DEBUG, "[C10] WebPage reDirect fully replaced.");
	}

	// assets: 전체 교체
	JsonArrayConst j_assets = j_root["assets"].as<JsonArrayConst>();
	if (!j_assets.isNull()) {
		memset(&p_cfg.assets, 0, sizeof(p_cfg.assets));
		p_cfg.assetCount = 0;

		for (JsonObjectConst j_c : j_assets) {
			if (p_cfg.assetCount >= A20_Const::MAX_COMMON_ASSETS) break;

			ST_A20_CommonAsset_t& v_c = p_cfg.assets[p_cfg.assetCount];
			memset(&v_c, 0, sizeof(v_c));

			strlcpy(v_c.uri,  C10_getStr(j_c, "uri", ""), sizeof(v_c.uri));
			strlcpy(v_c.path, C10_getStr(j_c, "path", ""), sizeof(v_c.path));
			v_c.isCommon = C10_getBool(j_c, "isCommon", false);

			if (v_c.uri[0] == '\0' || v_c.path[0] == '\0') continue;

			p_cfg.assetCount++;
		}

		v_changed = true;
		CL_D10_Logger::log(EN_L10_LOG_DEBUG, "[C10] WebPage assets fully replaced.");
	}

	if (v_changed) {
		_dirty_webPage = true;
		CL_D10_Logger::log(EN_L10_LOG_INFO, "[C10] WebPage config patched (Memory Only, camelCase, root-mode). Dirty=true");
	}

	C10_MUTEX_RELEASE();
	return v_changed;
}

// =====================================================
// 3-x. JSON Export (NvsSpec / WebPage)
//  - NvsSpec: {"nvsSpec":{...}} 랩핑 Export 유지
//  - WebPage: ✅ 루트형 Export 확정
// =====================================================
void CL_C10_ConfigManager::toJson_NvsSpec(const ST_A20_NvsSpecConfig_t& p_cfg, JsonDocument& p_doc) {
	// 루트 래핑: nvsSpec
	JsonObject v_root = p_doc["nvsSpec"].to<JsonObject>();

	v_root["namespace"] = p_cfg.namespaceName;

	JsonArray v_arr = v_root["entries"].to<JsonArray>();
	for (uint8_t v_i = 0; v_i < p_cfg.entryCount && v_i < A20_Const::MAX_NVS_ENTRIES; v_i++) {
		const ST_A20_NvsEntry_t& v_e = p_cfg.entries[v_i];
		if (v_e.key[0] == '\0') continue;

		JsonObject v_o = v_arr.add<JsonObject>();
		v_o["key"]          = v_e.key;
		v_o["type"]         = v_e.type;
		v_o["defaultValue"] = v_e.defaultValue;
	}
}

void CL_C10_ConfigManager::toJson_WebPage(const ST_A20_WebPageConfig_t& p_cfg, JsonDocument& p_doc) {
	// ✅ 루트형 Export: pages / reDirect / assets

	JsonArray v_pages = p_doc["pages"].to<JsonArray>();
	for (uint8_t v_i = 0; v_i < p_cfg.pageCount && v_i < A20_Const::MAX_PAGES; v_i++) {
		const ST_A20_PageItem_t& v_p = p_cfg.pages[v_i];
		if (v_p.uri[0] == '\0' || v_p.path[0] == '\0') continue;

		JsonObject v_po = v_pages.add<JsonObject>();
		v_po["uri"]    = v_p.uri;
		v_po["path"]   = v_p.path;
		v_po["label"]  = v_p.label;
		v_po["enable"] = v_p.enable;
		v_po["isMain"] = v_p.isMain;
		v_po["order"]  = v_p.order;

		JsonArray v_pa = v_po["pageAssets"].to<JsonArray>();
		for (uint8_t v_j = 0; v_j < v_p.pageAssetCount && v_j < A20_Const::MAX_PAGE_ASSETS; v_j++) {
			const ST_A20_PageAsset_t& v_a = v_p.pageAssets[v_j];
			if (v_a.uri[0] == '\0' || v_a.path[0] == '\0') continue;

			JsonObject v_ao = v_pa.add<JsonObject>();
			v_ao["uri"]  = v_a.uri;
			v_ao["path"] = v_a.path;
		}
	}

	JsonArray v_red = p_doc["reDirect"].to<JsonArray>();
	for (uint8_t v_i = 0; v_i < p_cfg.reDirectCount && v_i < A20_Const::MAX_REDIRECTS; v_i++) {
		const ST_A20_ReDirectItem_t& v_r = p_cfg.reDirect[v_i];
		if (v_r.uriFrom[0] == '\0' || v_r.uriTo[0] == '\0') continue;

		JsonObject v_ro = v_red.add<JsonObject>();
		v_ro["uriFrom"] = v_r.uriFrom;
		v_ro["uriTo"]   = v_r.uriTo;
	}

	JsonArray v_as = p_doc["assets"].to<JsonArray>();
	for (uint8_t v_i = 0; v_i < p_cfg.assetCount && v_i < A20_Const::MAX_COMMON_ASSETS; v_i++) {
		const ST_A20_CommonAsset_t& v_c = p_cfg.assets[v_i];
		if (v_c.uri[0] == '\0' || v_c.path[0] == '\0') continue;

		JsonObject v_co = v_as.add<JsonObject>();
		v_co["uri"]      = v_c.uri;
		v_co["path"]     = v_c.path;
		v_co["isCommon"] = v_c.isCommon;
	}
}
