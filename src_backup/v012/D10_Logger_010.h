#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : D10_Logger_010.h
 * 모듈명 : Smart Nature Wind Logger
 * ------------------------------------------------------
 * 기능 요약:
 *  - 공통 로깅 관리 클래스 (Serial + 메모리 보관)
 *  - 로그 레벨 필터링 (core.system.logging.level 연동)
 *  - 최대 보관 개수 제한 (core.system.logging.max_entries 연동)
 *  - 최근 로그 JSON 직렬화 (/api/log 연동용)
 * ------------------------------------------------------
 * [코드 네이밍 규칙]
 * 		- 현재 파일 모듈약어    : L10
 * 		- 전역 상수,매크로      : G_모듈약어_ 접두사
 * 		- 전역 변수             : g_모듈약어_ 접두사
 * 		- 전역 함수             : 모듈약어_ 접두사
 * 		- type                  : T_모듈약어_ 접두사
 * 		- enum 상수             : EN_모듈약어_ 접두사
 * 		- 구조체                : ST_모듈약어_ 접두사
 * 		- 클래스명              : CL_모듈약어_ 접두사
 * 		- 클래스 private 멤버   : _ 접두사
 * 		- 클래스 정적 멤버      : s_ 접두사
 * 		- 로컬 변수             : v_ 접두사
 * 		- 함수 인자             : p_접두사
 */

#include <Arduino.h>
#include <ArduinoJson.h>
#include <deque>
#include <vector>
#include "A10_Const_010.h"

// ======================================================
// ENUM - 로그 레벨
// ======================================================
typedef enum : uint8_t {
	EN_L10_LOG_DEBUG = 0,
	EN_L10_LOG_INFO,
	EN_L10_LOG_WARN,
	EN_L10_LOG_ERROR
} T_L10_LogLevel_t;

// ======================================================
// 클래스 정의
// ======================================================
class CL_D10_Logger {
public:
	// ==================================================
	// 설정
	// ==================================================
	/**
	 * @brief 로그 레벨 설정 (INFO, WARN 등)
	 */
	static void setLevel(T_L10_LogLevel_t p_lvl) {
		s_logLevel = p_lvl;
	}

	/**
	 * @brief 로그 최대 보관 개수 설정
	 */
	static void setMaxEntries(uint16_t p_count) {
		s_maxEntries = (p_count > 10 && p_count <= 2000) ? p_count : 300;
	}

	/**
	 * @brief core.system.logging 값 반영
	 */
	static void applyCoreConfig(const ST_A10_CoreConfig& p_core) {
		// level
		if (strcasecmp(p_core.system.logging.level, "DEBUG") == 0)
			s_logLevel = EN_L10_LOG_DEBUG;
		else if (strcasecmp(p_core.system.logging.level, "WARN") == 0)
			s_logLevel = EN_L10_LOG_WARN;
		else if (strcasecmp(p_core.system.logging.level, "ERROR") == 0)
			s_logLevel = EN_L10_LOG_ERROR;
		else
			s_logLevel = EN_L10_LOG_INFO;

		// max_entries
		setMaxEntries(p_core.system.logging.max_entries);
	}

	// ==================================================
	// 로깅
	// ==================================================
	/**
	 * @brief 포맷 문자열 기반 로그 출력
	 */
	static void log(T_L10_LogLevel_t p_lvl, const char* p_fmt, ...) {
		if (p_lvl < s_logLevel) return;

		char v_buf[256];
		va_list v_ap;
		va_start(v_ap, p_fmt);
		vsnprintf(v_buf, sizeof(v_buf), p_fmt, v_ap);
		va_end(v_ap);

		String v_line = "[" + String(_levelToString(p_lvl)) + "] " + String(v_buf);
		Serial.println(v_line);

		_push(v_line);
	}

	/**
	 * @brief 단순 문자열 로그 (이미 포맷된 메시지)
	 */
	static void logStr(T_L10_LogLevel_t p_lvl, const String& p_msg) {
		if (p_lvl < s_logLevel) return;
		String v_line = "[" + String(_levelToString(p_lvl)) + "] " + p_msg;
		Serial.println(v_line);
		_push(v_line);
	}

	// ==================================================
	// JSON 직렬화
	// ==================================================
	/**
	 * @brief 현재 로그 리스트를 JSON 문자열로 반환
	 * @example
	 * [ "INFO Booting...", "WARN Temp high" ]
	 */
	static String getLogsJson() {
		JsonDocument v_doc;
		JsonArray v_arr = v_doc.to<JsonArray>();

		for (auto& s : s_logs)
			v_arr.add(s);

		String v_out;
		serializeJson(v_doc, v_out);
		return v_out;
	}

	/**
	 * @brief 최근 n개만 JSON으로 직렬화
	 */
	static String getRecentJson(uint16_t p_count) {
		JsonDocument v_doc;
		JsonArray v_arr = v_doc.to<JsonArray>();

		uint16_t v_total = s_logs.size();
		uint16_t v_start = (v_total > p_count) ? (v_total - p_count) : 0;

		for (uint16_t v_i = v_start; v_i < v_total; ++v_i)
			v_arr.add(s_logs[v_i]);

		String v_out;
		serializeJson(v_doc, v_out);
		return v_out;
	}

	// ==================================================
	// 상태 조회
	// ==================================================
	static uint16_t getCount() { return s_logs.size(); }
	static uint16_t getMaxEntries() { return s_maxEntries; }
	static const char* getLevelString() { return _levelToString(s_logLevel); }

private:
	// ==================================================
	// 내부 헬퍼
	// ==================================================
	static const char* _levelToString(T_L10_LogLevel_t p_lvl) {
		switch (p_lvl) {
			case EN_L10_LOG_DEBUG: return "DEBUG";
			case EN_L10_LOG_INFO:  return "INFO";
			case EN_L10_LOG_WARN:  return "WARN";
			case EN_L10_LOG_ERROR: return "ERROR";
			default:               return "UNK";
		}
	}

	static void _push(const String& p_line) {
		if (s_logs.size() >= s_maxEntries)
			s_logs.pop_front();
		s_logs.push_back(p_line);
	}

private:
	// ==================================================
	// 정적 멤버
	// ==================================================
	static inline T_L10_LogLevel_t s_logLevel   = EN_L10_LOG_INFO;
	static inline uint16_t          s_maxEntries = 300;
	static inline std::deque<String> s_logs;
};

// ------------------------------------------------------
// 사용 예시
// ------------------------------------------------------
//
// void setup() {
//     ST_A10_CoreConfig cfg;
//     A10_resetCoreDefault(cfg);
//     CL_D10_Logger::applyCoreConfig(cfg);
//
//     CL_D10_Logger::log(EN_L10_LOG_INFO, "Logger initialized");
// }
//
// void loop() {
//     CL_D10_Logger::log(EN_L10_LOG_DEBUG, "PWM=%.1f%%", 42.3f);
// }
//
