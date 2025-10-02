// SC10_Logger_002.h

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

	static void log(SC10_LogLevel p_lvl, const char* fmt, ...) {
		if (p_lvl < g_logLevel)
			return;
		char	buf[256];
		va_list ap;
		va_start(ap, fmt);
		vsnprintf(buf, sizeof(buf), fmt, ap);
		va_end(ap);
		String line = "[" + String(levelStr(p_lvl)) + "] " + String(buf);
		Serial.println(line);
		push(line);
	}

	static String getLogsJson() {
		JsonDocument doc;
		JsonArray	 arr = doc.to<JsonArray>();
		for (auto& s : g_logs) arr.add(s);
		String out;
		serializeJson(doc, out);
		return out;
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
	static void push(const String& s) {
		if (g_logs.size() > 100)
			g_logs.pop_front();
		g_logs.push_back(s);
	}
};
