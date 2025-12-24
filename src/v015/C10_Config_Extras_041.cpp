/*
 * ------------------------------------------------------
 * 소스명 : C10_Config_Extras_041.cpp
 * 모듈 약어 : C10
 * 모듈명 : Smart Nature Wind Configuration Manager - Extras (NvsSpec/WebPages/etc)
 * ------------------------------------------------------
 * 기능 요약:
 *  - NVS Spec JSON Export (nvsspec)
 *  - WebPages 설정 Load/Save/Patch/Export
 *  - (필요 시) 기타 목적물별 확장 엔트리 추가용
 * ------------------------------------------------------
 * [구현 규칙]
 *  - 항상 소스 시작 주석 체계 유지
 *  - ArduinoJson v7.x.x 사용 (v6 이하 사용 금지)
 *  - JsonDocument 단일 타입만 사용
 *  - createNestedArray/Object/containsKey 사용 금지
 *  - memset + strlcpy 기반 안전 초기화
 *  - 주석/필드명은 JSON 구조와 동일하게 유지
 *  - 모듈별 헤더(h) + 목적물별 cpp 분리 구성 (Core/System/Schedule/Extras)
 * ------------------------------------------------------
 * [코드 네이밍 규칙]
 *   - 전역 상수,매크로      : G_모듈약어_ 접두사
 *   - 전역 변수             : g_모듈약어_ 접두사
 *   - 전역 함수             : 모듈약어_ 접두사
 *   - type                  : T_모듈약어_ 접두사
 *   - typedef               : _t  접미사
 *   - enum 상수             : EN_모듈약어_ 접두사
 *   - 구조체                : ST_모듈약어_ 접두사
 *   - 클래스명              : CL_모듈약어_ 접두사
 *   - 클래스 private 멤버   : _ 접두사
 *   - 클래스 멤버(함수/변수) : 모듈약어 접두사 미사용
 *   - 클래스 정적 멤버      : s_ 접두사
 *   - 함수 로컬 변수        : v_ 접두사
 *   - 함수 인자             : p_ 접두사
 * ------------------------------------------------------
 */

#include "C10_Config_041.h"

#include <cstring>

// =====================================================
// 내부 Helper: key 호환 (camelCase 우선, snake_case fallback)
//  - System cpp에도 동일 helper가 있으나, cpp 분리 파일이므로 로컬 재정의.
// =====================================================
static const char* C10_getStr2_ex(JsonObjectConst p_obj, const char* p_k1, const char* p_k2, const char* p_def) {
	if (p_obj.isNull()) return p_def;
	if (p_obj[p_k1].is<const char*>()) {
		const char* v = p_obj[p_k1].as<const char*>();
		return v ? v : p_def;
	}
	if (p_obj[p_k2].is<const char*>()) {
		const char* v = p_obj[p_k2].as<const char*>();
		return v ? v : p_def;
	}
	return p_def;
}

template <typename T>
static T C10_getNum2_ex(JsonObjectConst p_obj, const char* p_k1, const char* p_k2, T p_def) {
	if (p_obj.isNull()) return p_def;
	if (p_obj[p_k1].is<T>()) return p_obj[p_k1].as<T>();
	if (p_obj[p_k2].is<T>()) return p_obj[p_k2].as<T>();
	return p_def;
}

static bool C10_getBool2_ex(JsonObjectConst p_obj, const char* p_k1, const char* p_k2, bool p_def) {
	if (p_obj.isNull()) return p_def;
	if (p_obj[p_k1].is<bool>()) return p_obj[p_k1].as<bool>();
	if (p_obj[p_k2].is<bool>()) return p_obj[p_k2].as<bool>();
	return p_def;
}

// =====================================================
// 5. NVS Spec (nvsspec) - Export
//  - 실제 ST 타입/요구 키 구조는 헤더/스펙에 맞춰 조정 필요
//  - 여기서는 "NVS에 어떤 키를 쓰는지" 를 JSON으로 뽑는 목적의 일반적인 구현.
// =====================================================

// [주의] 아래 함수명/시그니처는 "예상" 입니다.
// C10_Config_041.h 선언과 다르면, 헤더에 맞게 이름/인자만 치환하세요.
void CL_C10_ConfigManager::toJson_NvsSpec(JsonDocument& p_doc) {
	// 예시 스펙 (필요 키만)
	// 실제 프로젝트에서는 "namespace", "key", "type", "desc", "default" 등을 담는 형태가 많음.
	//
	// createNestedArray/Object 금지 → 인덱스 기반으로 배열을 만들어 채움.

	p_doc.clear();

	p_doc["nvsSpec"]["version"] = "041";

	// items 배열
	JsonArray v_items = p_doc["nvsSpec"]["items"].to<JsonArray>();

	// item0
	{
		JsonObject v0 = v_items.add<JsonObject>();
		v0["ns"] = "system";
		v0["key"] = "apiKey";
		v0["type"] = "string";
		v0["desc"] = "Security API key";
		v0["default"] = "";
	}

	// item1
	{
		JsonObject v1 = v_items.add<JsonObject>();
		v1["ns"] = "wifi";
		v1["key"] = "wifiMode";
		v1["type"] = "uint8";
		v1["desc"] = "WiFi mode (0=AP,1=STA,2=AP+STA)";
		v1["default"] = (uint8_t)EN_A20_WIFI_MODE_AP_STA;
	}

	// item2
	{
		JsonObject v2 = v_items.add<JsonObject>();
		v2["ns"] = "motion";
		v2["key"] = "bleRssiOn";
		v2["type"] = "int8";
		v2["desc"] = "BLE RSSI on threshold";
		v2["default"] = (int8_t)-65;
	}

	// 필요 시 계속 추가…
}

// =====================================================
// 6. WebPages Config (webpages) - Load/Save/Patch/Export
//  - 실제 ST_A20_WebPagesConfig_t 같은 타입이 존재한다는 가정.
//  - 헤더 타입명 다르면 교체 필요.
// =====================================================

// [주의] 아래 ST 타입은 "예상" 입니다. 프로젝트 실제 타입에 맞게 치환하세요.
// - 만약 아직 ST가 없다면: A20쪽 struct 정의 먼저 필요.
#ifndef C10_WEBPAGES_CFG_GUARD
#define C10_WEBPAGES_CFG_GUARD
// 빌드가 깨지지 않도록 "임시" 가드.
// 실제 프로젝트에 ST_A20_WebPagesConfig_t가 이미 있으면 이 블록은 제거하세요.
typedef struct ST_A20_WebPageItem_tmp {
	char path[64];
	char title[64];
	bool enabled;
} ST_A20_WebPageItem_tmp_t;

typedef struct ST_A20_WebPagesConfig_tmp {
	uint8_t count;
	ST_A20_WebPageItem_tmp_t pages[16];
} ST_A20_WebPagesConfig_t;
#endif

// [주의] 아래 함수명/시그니처는 "예상" 입니다.
// C10_Config_041.h 선언과 다르면, 헤더에 맞게 이름/인자만 치환하세요.
bool CL_C10_ConfigManager::loadWebPagesConfig(ST_A20_WebPagesConfig_t& p_cfg) {
	JsonDocument v_doc;

	const char* v_cfgJsonPath = nullptr;
	if (s_cfgJsonFileMap.webpages[0] != '\0') {
		v_cfgJsonPath = s_cfgJsonFileMap.webpages;
	} else {
		CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] loadWebPagesConfig: s_cfgJsonFileMap.webpages is empty");
		return false;
	}

	if (!ioLoadJson(v_cfgJsonPath, v_doc)) {
		CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] loadWebPagesConfig: ioLoadJson failed (%s)", v_cfgJsonPath);
		return false;
	}

	JsonObjectConst j_root = v_doc.as<JsonObjectConst>();
	JsonObjectConst j_wp = j_root["webPages"].as<JsonObjectConst>();
	if (j_wp.isNull()) j_wp = j_root["web_pages"].as<JsonObjectConst>();

	if (j_wp.isNull()) {
		CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] loadWebPagesConfig: missing 'webPages'");
		return false;
	}

	p_cfg.count = 0;

	JsonArrayConst j_arr = j_wp["pages"].as<JsonArrayConst>();
	if (j_arr.isNull()) j_arr = j_wp["items"].as<JsonArrayConst>(); // 호환용

	if (!j_arr.isNull()) {
		for (JsonObjectConst j_it : j_arr) {
			if (p_cfg.count >= (uint8_t)(sizeof(p_cfg.pages) / sizeof(p_cfg.pages[0]))) break;

			ST_A20_WebPageItem_tmp_t& v_item = p_cfg.pages[p_cfg.count];

			// path/title/enabled (camelCase 우선)
			strlcpy(v_item.path,
			        C10_getStr2_ex(j_it, "path", "path", ""),
			        sizeof(v_item.path));
			strlcpy(v_item.title,
			        C10_getStr2_ex(j_it, "title", "title", ""),
			        sizeof(v_item.title));
			v_item.enabled = C10_getBool2_ex(j_it, "enabled", "enabled", true);

			p_cfg.count++;
		}
	}

	return true;
}

bool CL_C10_ConfigManager::saveWebPagesConfig(const ST_A20_WebPagesConfig_t& p_cfg) {
	JsonDocument v_doc;

	// camelCase로 저장
	v_doc["webPages"]["count"] = p_cfg.count;

	JsonArray v_arr = v_doc["webPages"]["pages"].to<JsonArray>();
	for (uint8_t v_i = 0; v_i < p_cfg.count; v_i++) {
		JsonObject v_it = v_arr.add<JsonObject>();
		v_it["path"] = p_cfg.pages[v_i].path;
		v_it["title"] = p_cfg.pages[v_i].title;
		v_it["enabled"] = p_cfg.pages[v_i].enabled;
	}

	return ioSaveJson(s_cfgJsonFileMap.webpages, v_doc);
}

bool CL_C10_ConfigManager::patchWebPagesFromJson(ST_A20_WebPagesConfig_t& p_config, const JsonDocument& p_patch) {
	bool v_changed = false;

	C10_MUTEX_ACQUIRE_BOOL();

	JsonObjectConst j_wp = p_patch["webPages"].as<JsonObjectConst>();
	if (j_wp.isNull()) j_wp = p_patch["web_pages"].as<JsonObjectConst>();
	if (j_wp.isNull()) {
		C10_MUTEX_RELEASE();
		return false;
	}

	// pages 배열이 오면 "전체 replace" 정책 (안전/명확)
	JsonArrayConst j_arr = j_wp["pages"].as<JsonArrayConst>();
	if (j_arr.isNull()) j_arr = j_wp["items"].as<JsonArrayConst>();

	if (!j_arr.isNull()) {
		p_config.count = 0;

		for (JsonObjectConst j_it : j_arr) {
			if (p_config.count >= (uint8_t)(sizeof(p_config.pages) / sizeof(p_config.pages[0]))) break;

			ST_A20_WebPageItem_tmp_t& v_item = p_config.pages[p_config.count];

			strlcpy(v_item.path,
			        C10_getStr2_ex(j_it, "path", "path", ""),
			        sizeof(v_item.path));
			strlcpy(v_item.title,
			        C10_getStr2_ex(j_it, "title", "title", ""),
			        sizeof(v_item.title));
			v_item.enabled = C10_getBool2_ex(j_it, "enabled", "enabled", true);

			p_config.count++;
		}

		v_changed = true;
		CL_D10_Logger::log(EN_L10_LOG_DEBUG, "[C10] WebPages pages array fully replaced.");
	}

	if (v_changed) {
		// 헤더에 _dirty_webpages 같은 플래그가 있으면 그것으로 교체하세요.
		// 없으면 최소 로그만 남김.
		CL_D10_Logger::log(EN_L10_LOG_INFO, "[C10] WebPages config patched (Memory Only, camelCase).");
	}

	C10_MUTEX_RELEASE();
	return v_changed;
}

void CL_C10_ConfigManager::toJson_WebPages(const ST_A20_WebPagesConfig_t& p_cfg, JsonDocument& p_doc) {
	p_doc.clear();

	p_doc["webPages"]["count"] = p_cfg.count;

	JsonArray v_arr = p_doc["webPages"]["pages"].to<JsonArray>();
	for (uint8_t v_i = 0; v_i < p_cfg.count; v_i++) {
		JsonObject v_it = v_arr.add<JsonObject>();
		v_it["path"] = p_cfg.pages[v_i].path;
		v_it["title"] = p_cfg.pages[v_i].title;
		v_it["enabled"] = p_cfg.pages[v_i].enabled;
	}
}
