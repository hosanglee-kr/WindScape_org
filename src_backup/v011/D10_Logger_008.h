
/*
 * ------------------------------------------------------
 * 소스명 : D10_Logger_008.h
 * 모듈명 :  
 * ------------------------------------------------------
 * 기능 요약:
 *  - 
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
 * 		- 클래스 private 멤버   : _ 접두사,
 * 		- 클래스 정적 멤버      : s_ 접두사
 * 		- 로컬 변수             : v_ 접두사
 * 		- 함수 인자             : p_ 접두사
 */

#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

#include <deque>
#include <vector>

typedef enum {
	EN_L10_LOG_DEBUG,
	EN_L10_LOG_INFO,
	EN_L10_LOG_WARN,
	EN_L10_LOG_ERROR
} T_D10_LogLevel;

class CL_D10_Logger {
   public:
	static void setLevel(T_D10_LogLevel p_lvl) {
		g_logLevel = p_lvl;
	}

	static void log(T_D10_LogLevel p_lvl, const char* p_fmt, ...) {
		if (p_lvl < g_logLevel){
			return;
		}
		
		char	v_buf[256];
		
		va_list v_ap;
		va_start(v_ap, p_fmt);
		vsnprintf(v_buf, sizeof(v_buf), p_fmt, v_ap);
		va_end(v_ap);
		String v_line = "[" + String(levelStr(p_lvl)) + "] " + String(v_buf);
		Serial.println(v_line);
		push(v_line);
	}

	static String getLogsJson() {
		JsonDocument v_doc;
		JsonArray	 v_jsonArr = v_doc.to<JsonArray>();
		for (auto& s : g_logs) {
			v_jsonArr.add(s);
		}
		
		String v_out;
		serializeJson(v_doc, v_out);
		return v_out;
	}

   private:
	static inline T_D10_LogLevel		 g_logLevel = EN_L10_LOG_INFO;
	static inline std::deque<String> g_logs;

	static const char* levelStr(T_D10_LogLevel l) {
		switch (l) {
			case EN_L10_LOG_DEBUG:
				return "DEBUG";
			case EN_L10_LOG_INFO:
				return "INFO";
			case EN_L10_LOG_WARN:
				return "WARN";
			case EN_L10_LOG_ERROR:
				return "ERROR";
		}
		return "";
	}
	static void push(const String& p_s) {
		if (g_logs.size() > 100){
			g_logs.pop_front();
		}
		g_logs.push_back(p_s);
	}
};
