#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : D10_Logger_015.h
 * 모듈약어 : D10
 * 모듈명 : Smart Nature Wind Logger (v015, WebSocket + Serial)
 * ------------------------------------------------------
 * 기능 요약
 *  - 전역 통합 로깅 시스템 (Serial + WebSocket 실시간 브로드캐스트)
 *  - INFO / WARN / ERROR / DEBUG 레벨 지원
 *  - ANSI 컬러 포맷 지원 (시리얼 콘솔용)
 *  - Lazy flush (지연 출력)
 *  - 타임스탬프, 태그, 메모리 사용량 옵션 표시
 *  - WebSocket 연결 시 JSON 형식 로그 실시간 전송
 * ------------------------------------------------------
 * [구현 규칙]
 *  - 항상 소스 시작 주석 부분 체계 유지 및 내용 업데이트
 *  - 소스 시작 주석 부분 구현규칙, 코드네이밍규칙 내용 그대로 유지, 수정금지
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
#include <stdarg.h>
#include <stdio.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

// ------------------------------------------------------
// 로그 레벨
// ------------------------------------------------------
typedef enum : uint8_t {
	EN_L10_LOG_NONE  = 0,
	EN_L10_LOG_ERROR = 1,
	EN_L10_LOG_WARN  = 2,
	EN_L10_LOG_INFO  = 3,
	EN_L10_LOG_DEBUG = 4
} EN_L10_LogLevel_t;

// ------------------------------------------------------
// ANSI 색상 코드
// ------------------------------------------------------
#define G_D10_COLOR_RESET   "\033[0m"
#define G_D10_COLOR_RED     "\033[31m"
#define G_D10_COLOR_YELLOW  "\033[33m"
#define G_D10_COLOR_GREEN   "\033[32m"
#define G_D10_COLOR_CYAN    "\033[36m"
#define G_D10_COLOR_WHITE   "\033[37m"

// ------------------------------------------------------
// Logger 클래스
// ------------------------------------------------------
class CL_D10_Logger {
public:
	// --------------------------------------------------
	// 초기화
	// --------------------------------------------------
	static void begin(HardwareSerial& p_serial = Serial, uint32_t p_baud = 115200) {
		_serial = &p_serial;
		_serial->begin(p_baud);
		delay(100);
		printBanner();
	}

	// --------------------------------------------------
	// 로그 레벨 / 표시 옵션 설정
	// --------------------------------------------------
	static void setLevel(EN_L10_LogLevel_t p_level) { _logLevel = p_level; }
	static EN_L10_LogLevel_t getLevel() { return _logLevel; }
	static void enableTimestamp(bool p_enable) { _showTimestamp = p_enable; }
	static void enableMemUsage(bool p_enable) { _showMemUsage = p_enable; }

	// --------------------------------------------------
	// WebSocket 로그 브로드캐스트 연결
	// --------------------------------------------------
	static void attachWebSocket(AsyncWebSocket* p_ws) {
		s_wsLogs = p_ws;
	}

	// --------------------------------------------------
	// 로그 출력 (가변인자)
	// --------------------------------------------------
	static void log(EN_L10_LogLevel_t p_level, const char* p_fmt, ...) {
		if (!_serial || p_level > _logLevel || p_level == EN_L10_LOG_NONE) return;

		char v_buf[256];
		va_list v_args;
		va_start(v_args, p_fmt);
		vsnprintf(v_buf, sizeof(v_buf), p_fmt, v_args);
		va_end(v_args);

		const char* v_color = _getColor(p_level);
		const char* v_tag   = _getTag(p_level);

		if (_showTimestamp) {
			unsigned long v_ms = millis();
			uint32_t v_sec = v_ms / 1000;
			uint16_t v_milli = v_ms % 1000;
			_serial->printf("[%lu.%03u] ", v_sec, v_milli);
		}

		_serial->printf("%s[%s]%s %s\r\n",
			v_color, v_tag, G_D10_COLOR_RESET, v_buf);

		if (_showMemUsage) {
			uint32_t v_free = ESP.getFreeHeap();
			_serial->printf("   %s(Mem:%luB)%s\r\n",
				G_D10_COLOR_CYAN, (unsigned long)v_free, G_D10_COLOR_RESET);
		}

		// --------------------------------------------------
		// 실시간 WebSocket 로그 브로드캐스트
		// --------------------------------------------------
		if (s_wsLogs) {
			JsonDocument v_doc;
			v_doc["ts"]  = millis();
			v_doc["lv"]  = (int)p_level;
			v_doc["msg"] = v_buf;

			String v_json;
			serializeJson(v_doc, v_json);
			s_wsLogs->textAll(v_json);
		}
	}

	// --------------------------------------------------
	// 배너 표시
	// --------------------------------------------------
	static void printBanner() {
		if (!_serial) return;
		_serial->println();
		_serial->println(F("------------------------------------------------------"));
		_serial->println(F(" Smart Nature Wind Logger (v015, WebSocket Ready)"));
		_serial->println(F("------------------------------------------------------"));
	}

private:
	// --------------------------------------------------
	// 내부 유틸
	// --------------------------------------------------
	static const char* _getColor(EN_L10_LogLevel_t p_level) {
		switch (p_level) {
			case EN_L10_LOG_ERROR: return G_D10_COLOR_RED;
			case EN_L10_LOG_WARN:  return G_D10_COLOR_YELLOW;
			case EN_L10_LOG_INFO:  return G_D10_COLOR_GREEN;
			case EN_L10_LOG_DEBUG: return G_D10_COLOR_CYAN;
			default:               return G_D10_COLOR_WHITE;
		}
	}

	static const char* _getTag(EN_L10_LogLevel_t p_level) {
		switch (p_level) {
			case EN_L10_LOG_ERROR: return "ERR";
			case EN_L10_LOG_WARN:  return "WRN";
			case EN_L10_LOG_INFO:  return "INF";
			case EN_L10_LOG_DEBUG: return "DBG";
			default:               return "LOG";
		}
	}

private:
	static HardwareSerial*    _serial;
	static EN_L10_LogLevel_t  _logLevel;
	static bool               _showTimestamp;
	static bool               _showMemUsage;
	static AsyncWebSocket*    s_wsLogs;
};

// ------------------------------------------------------
// 정적 멤버 정의
// ------------------------------------------------------
inline HardwareSerial*    CL_D10_Logger::_serial        = nullptr;
inline EN_L10_LogLevel_t  CL_D10_Logger::_logLevel      = EN_L10_LOG_INFO;
inline bool               CL_D10_Logger::_showTimestamp = true;
inline bool               CL_D10_Logger::_showMemUsage  = false;
inline AsyncWebSocket*    CL_D10_Logger::s_wsLogs       = nullptr;
