#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : D10_Logger_020.h
 * 모듈약어 : D10
 * 모듈명 : Smart Nature Wind Logger (v016, WebSocket + RingBuffer)
 * ------------------------------------------------------
 * 기능 요약
 *  - 전역 통합 로깅 시스템 (Serial + WebSocket 실시간 브로드캐스트)
 *  - INFO / WARN / ERROR / DEBUG 레벨 지원
 *  - RingBuffer(256개) 순환 로그 저장 및 JSON 조회(getLogsAsJson)
 *  - ANSI 컬러 포맷 지원 (시리얼 콘솔용)
 *  - WebSocket 연결 시 실시간 JSON 로그 전송
 *  - 로그 파일 저장(saveToFile) 및 메모리 기반 진단
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
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <Stream.h>	 // Stream 헤더 파일 포함 필요
#include <stdarg.h>
#include <stdio.h>

// ------------------------------------------------------
// 로그 레벨
// ------------------------------------------------------
typedef enum : uint8_t {
	EN_L10_LOG_NONE	 = 0,
	EN_L10_LOG_ERROR = 1,
	EN_L10_LOG_WARN	 = 2,
	EN_L10_LOG_INFO	 = 3,
	EN_L10_LOG_DEBUG = 4
} EN_L10_LogLevel_t;

// ------------------------------------------------------
// ANSI 색상 코드
// ------------------------------------------------------
#define G_D10_COLOR_RESET  "\033[0m"
#define G_D10_COLOR_RED	   "\033[31m"
#define G_D10_COLOR_YELLOW "\033[33m"
#define G_D10_COLOR_GREEN  "\033[32m"
#define G_D10_COLOR_CYAN   "\033[36m"
#define G_D10_COLOR_WHITE  "\033[37m"

// ------------------------------------------------------
// 로그 엔트리 구조체
// ------------------------------------------------------
typedef struct {
	uint32_t		  timestamp;
	EN_L10_LogLevel_t level;
	char			  message[128];
} ST_D10_LogEntry;

// ------------------------------------------------------
// Logger 클래스
// ------------------------------------------------------
class CL_D10_Logger {
  public:
	static const uint16_t BUFFER_SIZE = 256;

	// --------------------------------------------------
	// 초기화
	// --------------------------------------------------

	static void begin(Stream& p_serial = Serial, uint32_t p_baud = 115200) {
		// static void begin(HardwareSerial& p_serial = Serial, uint32_t p_baud = 115200) {
		_serial = &p_serial;
		// _serial->begin(p_baud);
		delay(100);
		printBanner();
	}

	// --------------------------------------------------
	// 로그 레벨 설정 / 조회
	// --------------------------------------------------
	static void setLevel(EN_L10_LogLevel_t p_level) {
		_logLevel = p_level;
	}
	static EN_L10_LogLevel_t getLevel() {
		return _logLevel;
	}

	static void enableTimestamp(bool p_enable) {
		_showTimestamp = p_enable;
	}
	static void enableMemUsage(bool p_enable) {
		_showMemUsage = p_enable;
	}

	// --------------------------------------------------
	// WebSocket 로그 브로드캐스트 연결
	// --------------------------------------------------
	static void attachWebSocket(AsyncWebSocket* p_ws) {
		s_wsLogs = p_ws;
	}

	// --------------------------------------------------
	// 로그 출력 (버퍼 저장 + Serial + WS 브로드캐스트)
	// --------------------------------------------------
	static void log(EN_L10_LogLevel_t p_level, const char* p_fmt, ...) {
		if (!_serial || p_level > _logLevel || p_level == EN_L10_LOG_NONE)
			return;

		char	v_buf[256];
		va_list v_args;
		va_start(v_args, p_fmt);
		vsnprintf(v_buf, sizeof(v_buf), p_fmt, v_args);
		va_end(v_args);

		// 순환 버퍼 저장
		uint16_t v_idx			  = (s_head + s_count) % BUFFER_SIZE;
		s_buffer[v_idx].timestamp = millis();
		s_buffer[v_idx].level	  = p_level;
		strlcpy(s_buffer[v_idx].message, v_buf, sizeof(s_buffer[v_idx].message));

		if (s_count < BUFFER_SIZE)
			s_count++;
		else
			s_head = (s_head + 1) % BUFFER_SIZE;

		// 시리얼 출력
		const char* v_color = _getColor(p_level);
		const char* v_tag	= _getTag(p_level);

		if (_showTimestamp) {
			unsigned long v_ms	  = millis();
			uint32_t	  v_sec	  = v_ms / 1000;
			uint16_t	  v_milli = v_ms % 1000;
			_serial->printf("[%lu.%03u] ", v_sec, v_milli);
		}

		_serial->printf("%s[%s]%s %s\r\n", v_color, v_tag, G_D10_COLOR_RESET, v_buf);

		if (_showMemUsage) {
			uint32_t v_free = ESP.getFreeHeap();
			_serial->printf("   %s(Mem:%luB)%s\r\n", G_D10_COLOR_CYAN, (unsigned long)v_free, G_D10_COLOR_RESET);
		}

		// WebSocket 송출
		broadcastLog(p_level, v_buf);
	}

	// --------------------------------------------------
	// WebSocket 브로드캐스트 (내부/외부 호출용)
	// --------------------------------------------------
	static void broadcastLog(EN_L10_LogLevel_t p_level, const char* p_msg) {
		if (!s_wsLogs)
			return;
		JsonDocument v_doc;
		v_doc["ts"]	 = millis();
		v_doc["lv"]	 = (int)p_level;
		v_doc["msg"] = p_msg;
		String v_json;
		serializeJson(v_doc, v_json);
		s_wsLogs->textAll(v_json);
	}

	// --------------------------------------------------
	// 로그 전체를 JSON 형태로 내보내기
	// --------------------------------------------------
	static void getLogsAsJson(JsonDocument& p_doc) {
		JsonArray v_arr = p_doc["logs"].to<JsonArray>();
		for (uint16_t v_i = 0; v_i < s_count; v_i++) {
			uint16_t   v_idx = (s_head + v_i) % BUFFER_SIZE;
			JsonObject v_j	 = v_arr.add<JsonObject>();
			v_j["ts"]		 = s_buffer[v_idx].timestamp;
			v_j["lv"]		 = (int)s_buffer[v_idx].level;
			v_j["msg"]		 = s_buffer[v_idx].message;
		}
	}

	// --------------------------------------------------
	// 로그 파일로 저장 (/json/debug.json)
	// --------------------------------------------------
	static bool saveToFile(const char* p_path = "/json/debug.json") {
		File v_f = LittleFS.open(p_path, "w");
		if (!v_f)
			return false;

		JsonDocument v_doc;
		getLogsAsJson(v_doc);
		serializeJsonPretty(v_doc, v_f);
		v_f.close();
		return true;
	}

	// --------------------------------------------------
	// 배너 출력
	// --------------------------------------------------
	static void printBanner() {
		if (!_serial)
			return;
		_serial->println();
		_serial->println(F("------------------------------------------------------"));
		_serial->println(F(" Smart Nature Wind Logger (v016, WebSocket + RingBuffer)"));
		_serial->println(F("------------------------------------------------------"));
	}

  private:
	// --------------------------------------------------
	// 내부 유틸 (색상 및 태그)
	// --------------------------------------------------
	static const char* _getColor(EN_L10_LogLevel_t p_level) {
		switch (p_level) {
			case EN_L10_LOG_ERROR:
				return G_D10_COLOR_RED;
			case EN_L10_LOG_WARN:
				return G_D10_COLOR_YELLOW;
			case EN_L10_LOG_INFO:
				return G_D10_COLOR_GREEN;
			case EN_L10_LOG_DEBUG:
				return G_D10_COLOR_CYAN;
			default:
				return G_D10_COLOR_WHITE;
		}
	}

	static const char* _getTag(EN_L10_LogLevel_t p_level) {
		switch (p_level) {
			case EN_L10_LOG_ERROR:
				return "ERR";
			case EN_L10_LOG_WARN:
				return "WRN";
			case EN_L10_LOG_INFO:
				return "INF";
			case EN_L10_LOG_DEBUG:
				return "DBG";
			default:
				return "LOG";
		}
	}

  private:
	// --------------------------------------------------
	// 정적 멤버 변수
	// --------------------------------------------------
	static Stream*			 _serial;  // Stream*로 변경
	// static HardwareSerial*	 _serial;

	static EN_L10_LogLevel_t _logLevel;
	static bool				 _showTimestamp;
	static bool				 _showMemUsage;

	static AsyncWebSocket*	 s_wsLogs;
	static ST_D10_LogEntry	 s_buffer[BUFFER_SIZE];
	static uint16_t			 s_head;
	static uint16_t			 s_count;
};

// ------------------------------------------------------
// 정적 멤버 정의
// ------------------------------------------------------
inline Stream*			 CL_D10_Logger::_serial		   = nullptr;
// inline HardwareSerial*	 CL_D10_Logger::_serial		   = nullptr;
inline EN_L10_LogLevel_t CL_D10_Logger::_logLevel	   = EN_L10_LOG_INFO;
inline bool				 CL_D10_Logger::_showTimestamp = true;
inline bool				 CL_D10_Logger::_showMemUsage  = false;

inline AsyncWebSocket*	 CL_D10_Logger::s_wsLogs	   = nullptr;
inline ST_D10_LogEntry	 CL_D10_Logger::s_buffer[BUFFER_SIZE];
inline uint16_t			 CL_D10_Logger::s_head	= 0;
inline uint16_t			 CL_D10_Logger::s_count = 0;
