#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : D10_Logger_011.h
 * 모듈명 : Smart Nature Wind Logger
 * ------------------------------------------------------
 * 기능 요약:
 *  - 공통 로깅 관리 클래스 (Serial + 메모리 보관)
 *  - core.system.logging.level / max_entries 연동
 *  - 최근 로그 JSON 직렬화 (/api/logs 연동용)
 * ------------------------------------------------------
 * [구현 규칙]
 *  - ArduinoJson v7.x.x / JsonDocument only
 *  - createNestedArray/Object/containsKey 사용 금지
 *  - memset + strlcpy 기반 안전 초기화
 *  - .h 단일 파일 구성 (cpp 없음)
 * ------------------------------------------------------
 * [코드 네이밍 규칙]
 *  - 현재 파일 모듈약어 : L10
 *  - 전역 상수,매크로   : G_L10_
 *  - 전역 변수          : g_L10_
 *  - 함수/멤버          : 접두사 없음
 *  - private 멤버       : _ 접두사
 *  - 정적 멤버          : s_ 접두사
 * ------------------------------------------------------
 */

#include <Arduino.h>
#include <ArduinoJson.h>
#include <deque>
#include "A10_Const_012.h"

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
// Logger 클래스
// ======================================================
class CL_D10_Logger {
public:
	// ==================================================
	// Core Config 연동
	// ==================================================
	static void applyCoreConfig(const ST_A10_SystemConfig& p_sys)
	{
		// level
		if      (strcasecmp(p_sys.system.logging.level, "DEBUG") == 0) s_logLevel = EN_L10_LOG_DEBUG;
		else if (strcasecmp(p_sys.system.logging.level, "WARN")  == 0) s_logLevel = EN_L10_LOG_WARN;
		else if (strcasecmp(p_sys.system.logging.level, "ERROR") == 0) s_logLevel = EN_L10_LOG_ERROR;
		else                                                           s_logLevel = EN_L10_LOG_INFO;

		// max entries
		setMaxEntries(p_sys.system.logging.max_entries);
	}

	static void setMaxEntries(uint16_t p_count)
	{
		// safety clamp
		s_maxEntries = (p_count >= 50 && p_count <= 2000) ? p_count : 300;
		_trimIfNeeded();
	}

	// ==================================================
	// Logging
	// ==================================================
	static void log(T_L10_LogLevel_t p_lvl, const char* p_fmt, ...)
	{
		if (p_lvl < s_logLevel) return;

		char v_buf[256];
		va_list v;
		va_start(v, p_fmt);
		vsnprintf(v_buf, sizeof(v_buf), p_fmt, v);
		va_end(v);

		String v_line = "[" + String(_lvlStr(p_lvl)) + "] " + String(v_buf);

		// Serial 출력 (안전 가드)
		if (Serial) Serial.println(v_line);

		_push(v_line);
	}

	static void logStr(T_L10_LogLevel_t p_lvl, const String& p_msg)
	{
		if (p_lvl < s_logLevel) return;

		String v_line = "[" + String(_lvlStr(p_lvl)) + "] " + p_msg;

		if (Serial) Serial.println(v_line);

		_push(v_line);
	}

	// ==================================================
	// JSON 직렬화
	// ==================================================
	static String toJson()
	{
		JsonDocument v;
		JsonArray a = v.to<JsonArray>();

		for (auto &s : s_logs)
			a.add(s);

		String out; serializeJson(v, out);
		return out;
	}

	static String recentToJson(uint16_t p_count)
	{
		JsonDocument v;
		JsonArray a = v.to<JsonArray>();

		uint16_t tot   = s_logs.size();
		uint16_t start = (tot > p_count) ? (tot - p_count) : 0;

		for (uint16_t i = start; i < tot; i++)
			a.add(s_logs[i]);

		String out; serializeJson(v, out);
		return out;
	}

	// ==================================================
	// Utility
	// ==================================================
	static void clear()
	{
		s_logs.clear();
	}

	static uint16_t count() { return s_logs.size(); }
	static const char* level() { return _lvlStr(s_logLevel); }


private:
	// ==================================================
	// 내부 도우미
	// ==================================================
	static const char* _lvlStr(T_L10_LogLevel_t v)
	{
		switch(v){
			case EN_L10_LOG_DEBUG: return "DEBUG";
			case EN_L10_LOG_INFO:  return "INFO";
			case EN_L10_LOG_WARN:  return "WARN";
			case EN_L10_LOG_ERROR: return "ERROR";
		}
		return "UNK";
	}

	static void _push(const String& p_line)
	{
		if (s_logs.size() >= s_maxEntries)
			s_logs.pop_front();
		s_logs.push_back(p_line);
	}

	static void _trimIfNeeded()
	{
		while (s_logs.size() > s_maxEntries)
			s_logs.pop_front();
	}

private:
	// ==================================================
	// 정적 멤버
	// ==================================================
	static inline T_L10_LogLevel_t s_logLevel = EN_L10_LOG_INFO;
	static inline uint16_t         s_maxEntries = 300;
	static inline std::deque<String> s_logs;
};
