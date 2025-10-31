#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : S10_ChartAPI_010.h
 * 모듈약어 : S10
 * 모듈명 : Smart Nature Wind 차트 API
 * ------------------------------------------------------
 * 기능 요약:
 *  - S10_Simulation의 s_chartBuffer → JSON 직렬화
 *  - 버퍼 관리(최대 개수, 초기화, 샘플 주입)
 *  - WebAPI 핸들러에서 바로 사용 가능한 toJson()
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
#include <deque>
#include "A10_Const_011.h"
#include "S10_Simulation_011.h" // s_chartBuffer / ST_S10_ChartEntry 사용

class CL_S10_ChartAPI {
public:
	// 차트 JSON 생성
	static void S10_toJsonDoc(JsonDocument& p_doc, uint16_t p_limit = 120) {
		JsonArray v_arr = p_doc["chart"].to<JsonArray>();
		const auto& v_buf = CL_S10_Simulation::s_chartBuffer;

		// 최근 p_limit개만 역순으로 슬라이스 후 정방향 출력
		uint16_t v_total = (uint16_t)v_buf.size();
		uint16_t v_count = (p_limit < v_total) ? p_limit : v_total;
		uint16_t v_start = v_total - v_count;

		for (uint16_t v_i = v_start; v_i < v_total; ++v_i) {
			const auto& e = v_buf[v_i];
			JsonObject o  = v_arr.add<JsonObject>();
			o["t"]        = (uint32_t)e.timestamp;     // ms
			o["wind"]     = e.wind_speed;              // m/s
			o["duty"]     = e.pwm_duty;                // %
			o["int"]      = e.intensity;               // %
			o["var"]      = e.variability;             // %
			o["turb"]     = e.turbulence;              // sigma
			o["preset_id"]= e.preset_id;               // enum idx
			o["gust"]     = e.gust_active;             // bool
			o["thermal"]  = e.thermal_active;          // bool
		}
	}

	static String S10_toJson(uint16_t p_limit = 120) {
		JsonDocument v_doc;
		S10_toJsonDoc(v_doc, p_limit);
		String v_out;
		serializeJson(v_doc, v_out);
		return v_out;
	}

	// 버퍼 관리
	static void S10_clear() {
		CL_S10_Simulation::s_chartBuffer.clear();
		CL_S10_Simulation::s_lastChartLogMs = 0;
	}

	static void S10_pushSample(
		uint32_t p_ts_ms,
		float p_wind, float p_duty, float p_intensity,
		float p_variability, float p_turbulence,
		uint8_t p_preset_id, bool p_gust, bool p_thermal,
		uint16_t p_keepMax = 120
	) {
		CL_S10_Simulation::ST_S10_ChartEntry v_e;
		v_e.timestamp      = p_ts_ms;
		v_e.wind_speed     = p_wind;
		v_e.pwm_duty       = p_duty;
		v_e.intensity      = p_intensity;
		v_e.variability    = p_variability;
		v_e.turbulence     = p_turbulence;
		v_e.preset_id      = p_preset_id;
		v_e.gust_active    = p_gust;
		v_e.thermal_active = p_thermal;

		auto& v_buf = CL_S10_Simulation::s_chartBuffer;
		v_buf.push_back(v_e);
		while (v_buf.size() > p_keepMax) v_buf.pop_front();
	}

	static void S10_setMaxKeep(uint16_t p_keepMax) {
		auto& v_buf = CL_S10_Simulation::s_chartBuffer;
		while (v_buf.size() > p_keepMax) v_buf.pop_front();
		s_keepMax = p_keepMax;
	}

	static uint16_t S10_getMaxKeep() { return s_keepMax; }

private:
	static inline uint16_t s_keepMax = 120;
};
