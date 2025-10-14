// D10_Logger_004.h

#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

#include <deque>
#include <vector>

typedef enum {
	SC10_LOG_DEBUG,
	SC10_LOG_INFO,
	SC10_LOG_WARN,
	SC10_LOG_ERROR
} SC10_LogLevel;

class SC10_Logger {
   public:
	static void setLevel(SC10_LogLevel p_lvl) {
		g_logLevel = p_lvl;
	}

	static void log(SC10_LogLevel p_lvl, const char* p_fmt, ...) {
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
	static inline SC10_LogLevel		 g_logLevel = SC10_LOG_INFO;
	static inline std::deque<String> g_logs;

	static const char* levelStr(SC10_LogLevel l) {
		switch (l) {
			case SC10_LOG_DEBUG:
				return "DEBUG";
			case SC10_LOG_INFO:
				return "INFO";
			case SC10_LOG_WARN:
				return "WARN";
			case SC10_LOG_ERROR:
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
