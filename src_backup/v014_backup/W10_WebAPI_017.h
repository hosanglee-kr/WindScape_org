#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : W10_WebAPI_017.h
 * 모듈약어 : W10
 * 모듈명 : Smart Nature Wind Web API Manager (v017)
 * ------------------------------------------------------
 * 기능 요약
 *  - REST API / Web 관리 엔드포인트 제공
 *  - CT10 제어 매니저 연동 (Schedule / User Profile / Override)
 *  - 설정(JSON) 파일 입출력 (System / WiFi / Motion / Schedules / UserProfiles / WindProfile)
 *  - 상태 조회 (/api/diag, /api/control/state 등)
 *  - 인증(API-Key) 기반 보호 옵션
 * ------------------------------------------------------
 * [구현 규칙]
 *  - 항상 소스 시작 주석 체계 유지
 *  - ArduinoJson v7.x.x 사용 (v6 이하 사용 금지)
 *  - JsonDocument 단일 타입만 사용
 *  - createNestedArray/Object/containsKey 사용 금지
 *  - memset + strlcpy 기반 안전 초기화
 *  - 주석/필드명은 JSON 구조와 동일하게 유지
 *  - 모듈별 단일 헤더(h)파일로만 구성 (cpp 없음)
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

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>

#include "A10_Const_014.h"
#include "C10_ConfigManager_014.h"
#include "D10_Logger_011.h"
#include "CT10_ControlManager_015.h"
#include "S10_Simulation_014.h"
#include "M10_MotionLogic_013.h"
#include "P10_PWM_ctrl_014.h"

class CL_W10_WebAPI {
public:
	// ==================================================
	// 초기 설정
	// ==================================================
	static void W10_init(
		AsyncWebServer&			p_server,
		CL_CT10_ControlManager& p_ct,
		CL_S10_Simulation&		p_sim,
		CL_M10_MotionLogic&		p_motion
	) {
		s_srv	= &p_server;
		s_ct	= &p_ct;
		s_sim	= &p_sim;
		s_motion = &p_motion;

		memset(s_apiKey, 0, sizeof(s_apiKey));

		_buildRoutes();

		CL_D10_Logger::log(EN_L10_LOG_INFO, "[W10] WebAPI init complete");
	}

	// 외부에서 보안키 설정 (cfg_system 연동 등)
	static void W10_setAuthApiKey(const char* p_key) {
		if (!p_key) {
			memset(s_apiKey, 0, sizeof(s_apiKey));
			return;
		}
		strlcpy(s_apiKey, p_key, sizeof(s_apiKey));
	}

private:
	// ==================================================
	// 정적 멤버
	// ==================================================
	static AsyncWebServer*			s_srv;
	static CL_CT10_ControlManager*	s_ct;
	static CL_S10_Simulation*		s_sim;
	static CL_M10_MotionLogic*		s_motion;
	static char						s_apiKey[64];

	// ==================================================
	// 라우트 구성
	// ==================================================
	static void _buildRoutes() {
		if (!s_srv) return;

		// 공용/상태
		s_srv->on("/api/version", HTTP_GET,
			[](AsyncWebServerRequest* r) { _handleVersion(r); });

		s_srv->on("/api/diag", HTTP_GET,
			[](AsyncWebServerRequest* r) { _handleDiag(r); });

		// 제어 상태 조회
		s_srv->on("/api/control/state", HTTP_GET,
			[](AsyncWebServerRequest* r) { _handleControlState(r); });

		// Override / Control
		s_srv->on(
			"/api/control/override/fixed",
			HTTP_POST,
			[](AsyncWebServerRequest* r) {},
			nullptr,
			[](AsyncWebServerRequest* r, uint8_t* d, size_t l, size_t i, size_t t) {
				_handleOverrideFixed(r, d, l, i, t);
			}
		);

		s_srv->on(
			"/api/control/override/preset",
			HTTP_POST,
			[](AsyncWebServerRequest* r) {},
			nullptr,
			[](AsyncWebServerRequest* r, uint8_t* d, size_t l, size_t i, size_t t) {
				_handleOverridePreset(r, d, l, i, t);
			}
		);

		s_srv->on("/api/control/override/release", HTTP_POST,
			[](AsyncWebServerRequest* r) { _handleOverrideRelease(r); });

		s_srv->on(
			"/api/control/stop",
			HTTP_POST,
			[](AsyncWebServerRequest* r) { _handleControlStop(r); }
		);

		// Schedules (cfg_schedules_024.json)
		s_srv->on("/api/schedules", HTTP_GET,
			[](AsyncWebServerRequest* r) { _handleSchedulesGet(r); });

		s_srv->on(
			"/api/schedules",
			HTTP_POST,
			[](AsyncWebServerRequest* r) {},
			nullptr,
			[](AsyncWebServerRequest* r, uint8_t* d, size_t l, size_t i, size_t t) {
				_handleSchedulesPost(r, d, l, i, t);
			}
		);

		// User Profiles (cfg_uzOpProfile_025_final.json)
		s_srv->on("/api/userProfiles", HTTP_GET,
			[](AsyncWebServerRequest* r) { _handleUserProfilesGet(r); });

		s_srv->on(
			"/api/userProfiles",
			HTTP_POST,
			[](AsyncWebServerRequest* r) {},
			nullptr,
			[](AsyncWebServerRequest* r, uint8_t* d, size_t l, size_t i, size_t t) {
				_handleUserProfilesPost(r, d, l, i, t);
			}
		);

		// User Profile 실행
		s_srv->on(
			"/api/control/startProfile",
			HTTP_POST,
			[](AsyncWebServerRequest* r) {},
			nullptr,
			[](AsyncWebServerRequest* r, uint8_t* d, size_t l, size_t i, size_t t) {
				_handleStartProfile(r, d, l, i, t);
			}
		);

		// Schedules 기반 자동 운전 시작
		s_srv->on(
			"/api/control/startScheduleAuto",
			HTTP_POST,
			[](AsyncWebServerRequest* r) { _handleStartScheduleAuto(r); }
		);
	}

	// ==================================================
	// 인증 / 응답 헬퍼
	// ==================================================
	static bool _auth(AsyncWebServerRequest* p_r) {
		if (!p_r) return false;
		if (s_apiKey[0] == '\0') return true; // 키 미설정시 무조건 허용

		if (!p_r->hasHeader("X-API-Key")) return false;
		auto* v_h = p_r->getHeader("X-API-Key");
		if (!v_h) return false;
		String v = v_h->value();
		return (v == String(s_apiKey));
	}

	static void _sendJson(AsyncWebServerRequest* p_r, const JsonDocument& p_doc, int p_code = 200) {
		if (!p_r) return;
		String v_out;
		serializeJson(p_doc, v_out);
		p_r->send(p_code, "application/json", v_out);
	}

	static void _sendOk(AsyncWebServerRequest* p_r) {
		if (!p_r) return;
		p_r->send(200, "application/json", "{\"status\":\"OK\"}");
	}

	static void _sendError(AsyncWebServerRequest* p_r, int p_code, const char* p_msg) {
		if (!p_r) return;
		JsonDocument v;
		JsonObject o = v["error"].to<JsonObject>();
		o["code"] = p_code;
		o["msg"]  = (p_msg && p_msg[0]) ? p_msg : "error";
		_sendJson(p_r, v, p_code);
	}

	static bool _checkAuthOrReject(AsyncWebServerRequest* p_r) {
		if (!_auth(p_r)) {
			_sendError(p_r, 401, "unauthorized");
			return false;
		}
		return true;
	}

	// JSON Body는 단일 chunk 가정(소형 REST 용)
	static bool _parseJsonBody(uint8_t* p_data, size_t p_len, size_t p_index, size_t p_total, JsonDocument& p_out) {
		if (p_index != 0) return false;
		if (p_len != p_total) return false;
		DeserializationError v_e = deserializeJson(p_out, p_data, p_len);
		return !v_e;
	}

	// ==================================================
	// /api/version
	// ==================================================
	static void _handleVersion(AsyncWebServerRequest* p_r) {
		if (!_checkAuthOrReject(p_r)) return;

		JsonDocument v;
		JsonObject o = v["version"].to<JsonObject>();
		o["fw"]		  = A10_Const::FW_VERSION;
		o["schedules"] = "024";
		o["userProfiles"] = "025_final";
		_sendJson(p_r, v);
	}

	// ==================================================
	// /api/diag
	// ==================================================
	static void _handleDiag(AsyncWebServerRequest* p_r) {
		if (!_checkAuthOrReject(p_r)) return;

		JsonDocument v;
		JsonObject o = v["diag"].to<JsonObject>();

		o["uptime_ms"] = (uint32_t)millis();

		if (s_ct) {
			JsonObject c = o["control"].to<JsonObject>();
			s_ct->toJson(v); // toJson은 p_doc["control"] 에 쓰는 형식이어야 함
		}

		if (s_sim) {
			JsonObject s = o["sim"].to<JsonObject>();
			s_sim->S10_toJson(v); // 동일 Document 내에 병합
		}

		_sendJson(p_r, v);
	}

	// ==================================================
	// /api/control/state
	// ==================================================
	static void _handleControlState(AsyncWebServerRequest* p_r) {
		if (!_checkAuthOrReject(p_r)) return;

		JsonDocument v;
		if (s_ct) s_ct->toJson(v);
		if (s_sim) s_sim->S10_toJson(v);
		_sendJson(p_r, v);
	}

	// ==================================================
	// /api/control/override/fixed
	// body: { "percent": 60, "seconds": 600 }
	// ==================================================
	static void _handleOverrideFixed(
		AsyncWebServerRequest* p_r,
		uint8_t* p_data, size_t p_len,
		size_t p_index, size_t p_total
	) {
		if (!_checkAuthOrReject(p_r)) return;
		if (!s_ct) { _sendError(p_r, 500, "ct10 null"); return; }

		JsonDocument v;
		if (!_parseJsonBody(p_data, p_len, p_index, p_total, v)) {
			_sendError(p_r, 400, "invalid json");
			return;
		}

		float v_pct = v["percent"] | 0.0f;
		uint32_t v_sec = v["seconds"] | 0UL;

		if (v_sec == 0) {
			_sendError(p_r, 400, "seconds required");
			return;
		}

		s_ct->overrideFixed(v_pct, v_sec);
		_sendOk(p_r);
	}

	// ==================================================
	// /api/control/override/preset
	// body: { "presetCode":"OCEAN", "styleCode":"ACTIVE", "seconds":600,
	//         "adjust":{...} }
	// ==================================================
	static void _handleOverridePreset(
		AsyncWebServerRequest* p_r,
		uint8_t* p_data, size_t p_len,
		size_t p_index, size_t p_total
	) {
		if (!_checkAuthOrReject(p_r)) return;
		if (!s_ct) { _sendError(p_r, 500, "ct10 null"); return; }

		JsonDocument v;
		if (!_parseJsonBody(p_data, p_len, p_index, p_total, v)) {
			_sendError(p_r, 400, "invalid json");
			return;
		}

		const char* v_preset = v["presetCode"] | "";
		const char* v_style  = v["styleCode"]  | "";
		uint32_t    v_sec    = v["seconds"]    | 0UL;

		ST_A10_AdjustDelta_t v_adj;
		memset(&v_adj, 0, sizeof(v_adj));
		if (v["adjust"].is<JsonObjectConst>()) {
			JsonObjectConst j = v["adjust"].as<JsonObjectConst>();
			v_adj.wind_intensity_delta  = j["wind_intensity"]       | 0.0f;
			v_adj.wind_variability_delta= j["wind_variability"]     | 0.0f;
			v_adj.gust_frequency_delta  = j["gust_frequency"]       | 0.0f;
		}

		if (v_sec == 0 || v_preset[0] == '\0') {
			_sendError(p_r, 400, "invalid params");
			return;
		}

		// CT10 내부에서 C10_resolveWindParams 사용하도록 구현되어 있어야 함
		s_ct->overridePreset(v_preset, v_style, v_adj, v_sec);
		_sendOk(p_r);
	}

	// ==================================================
	// /api/control/override/release
	// ==================================================
	static void _handleOverrideRelease(AsyncWebServerRequest* p_r) {
		if (!_checkAuthOrReject(p_r)) return;
		if (!s_ct) { _sendError(p_r, 500, "ct10 null"); return; }

		s_ct->releaseOverride();
		_sendOk(p_r);
	}

	// ==================================================
	// /api/control/stop
	// ==================================================
	static void _handleControlStop(AsyncWebServerRequest* p_r) {
		if (!_checkAuthOrReject(p_r)) return;
		if (!s_ct) { _sendError(p_r, 500, "ct10 null"); return; }

		s_ct->stopAll();
		_sendOk(p_r);
	}

	// ==================================================
	// /api/control/startScheduleAuto
	// ==================================================
	static void _handleStartScheduleAuto(AsyncWebServerRequest* p_r) {
		if (!_checkAuthOrReject(p_r)) return;
		if (!s_ct) { _sendError(p_r, 500, "ct10 null"); return; }

		s_ct->startScheduleAuto();
		_sendOk(p_r);
	}

	// ==================================================
	// /api/control/startProfile
	// body: { "profileNo": 1 }
	// ==================================================
	static void _handleStartProfile(
		AsyncWebServerRequest* p_r,
		uint8_t* p_data, size_t p_len,
		size_t p_index, size_t p_total
	) {
		if (!_checkAuthOrReject(p_r)) return;
		if (!s_ct) { _sendError(p_r, 500, "ct10 null"); return; }

		JsonDocument v;
		if (!_parseJsonBody(p_data, p_len, p_index, p_total, v)) {
			_sendError(p_r, 400, "invalid json");
			return;
		}

		int v_profile = v["profileNo"] | -1;
		if (v_profile <= 0) {
			_sendError(p_r, 400, "profileNo required");
			return;
		}

		if (!s_ct->startUserProfile((uint8_t)v_profile)) {
			_sendError(p_r, 404, "profile not found or disabled");
			return;
		}
		_sendOk(p_r);
	}

	// ==================================================
	// /api/schedules GET
	//  - cfg_schedules_024.json 전체 반환
	// ==================================================
	static void _handleSchedulesGet(AsyncWebServerRequest* p_r) {
		if (!_checkAuthOrReject(p_r)) return;

		JsonDocument v;
		if (!CL_C10_ConfigManager::C10_loadSchedulesJson(v)) {
			_sendError(p_r, 500, "load schedules failed");
			return;
		}
		_sendJson(p_r, v);
	}

	// ==================================================
	// /api/schedules POST
	//  - Body 전체를 cfg_schedules_024.json 로 저장
	// ==================================================
	static void _handleSchedulesPost(
		AsyncWebServerRequest* p_r,
		uint8_t* p_data, size_t p_len,
		size_t p_index, size_t p_total
	) {
		if (!_checkAuthOrReject(p_r)) return;

		JsonDocument v;
		if (!_parseJsonBody(p_data, p_len, p_index, p_total, v)) {
			_sendError(p_r, 400, "invalid json");
			return;
		}

		if (!CL_C10_ConfigManager::C10_saveSchedulesJson(v)) {
			_sendError(p_r, 500, "save schedules failed");
			return;
		}

		// 저장 후, CT10에 새 스케줄 반영(필요시 내부에서 reload)
		if (s_ct) s_ct->reloadSchedulesFromFile();

		_sendOk(p_r);
	}

	// ==================================================
	// /api/userProfiles GET
	// ==================================================
	static void _handleUserProfilesGet(AsyncWebServerRequest* p_r) {
		if (!_checkAuthOrReject(p_r)) return;

		JsonDocument v;
		if (!CL_C10_ConfigManager::C10_loadUserProfilesJson(v)) {
			_sendError(p_r, 500, "load profiles failed");
			return;
		}
		_sendJson(p_r, v);
	}

	// ==================================================
	// /api/userProfiles POST
	// ==================================================
	static void _handleUserProfilesPost(
		AsyncWebServerRequest* p_r,
		uint8_t* p_data, size_t p_len,
		size_t p_index, size_t p_total
	) {
		if (!_checkAuthOrReject(p_r)) return;

		JsonDocument v;
		if (!_parseJsonBody(p_data, p_len, p_index, p_total, v)) {
			_sendError(p_r, 400, "invalid json");
			return;
		}

		if (!CL_C10_ConfigManager::C10_saveUserProfilesJson(v)) {
			_sendError(p_r, 500, "save profiles failed");
			return;
		}

		if (s_ct) s_ct->reloadUserProfilesFromFile();

		_sendOk(p_r);
	}
};


// ======================================================
// 정적 멤버 정의
// ======================================================
AsyncWebServer*			CL_W10_WebAPI::s_srv	= nullptr;
CL_CT10_ControlManager* CL_W10_WebAPI::s_ct		= nullptr;
CL_S10_Simulation*		CL_W10_WebAPI::s_sim	= nullptr;
CL_M10_MotionLogic*		CL_W10_WebAPI::s_motion = nullptr;
char					CL_W10_WebAPI::s_apiKey[64] = {0};
