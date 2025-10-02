// main.cpp

#include <Arduino.h>
#include <LittleFS.h>

#include "v010/SC10_WindScape_002.h"

WindScapeSimulator g_SC10;

void setup() {
	Serial.begin(115200);
	delay(100);

	// (선택) 로그 레벨
	SC10_Logger::setLevel(SC10_LOG_DEBUG);

	// if (!LittleFS.begin(true)) {
	// 	SC10_Logger::log(SC10_LOG_ERROR,"LittleFS mount failed");
	// } else {
	// 	SC10_Logger::log(SC10_LOG_INFO,"LittleFS mounted");
	// }

	g_SC10.SC10_init();

	Serial.println("[BOOT] Ready. Try /api/state, /api/scan, /api/config, /api/diag, /api/logs...");
}

void loop() {
	g_SC10.SC10_run();
	delay(1);
}
