// main.cpp

#include <Arduino.h>
#include <LittleFS.h>

#include "v011/WS10_Main_006.h"

CL_WS10_WindScapeSimulator g_WS10;

void setup() {
	Serial.begin(115200);
	
	delay(3000);

	Serial.println("setup...");

	// (선택) 로그 레벨
	CL_D10_Logger::setLevel(EN_L10_LOG_DEBUG);

	// if (!LittleFS.begin(true)) {
	// 	CL_D10_Logger::log(SC10_LOG_ERROR,"LittleFS mount failed");
	// } else {
	// 	CL_D10_Logger::log(SC10_LOG_INFO,"LittleFS mounted");
	// }

	g_WS10.init();

	Serial.println("[BOOT] Ready. Try /api/state, /api/scan, /api/config, /api/diag, /api/logs...");
}

void loop() {
	g_WS10.run();
	delay(1);
}
