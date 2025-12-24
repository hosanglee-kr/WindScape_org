// ======================================================
// 파일명 : main.cpp
// 프로젝트 : Smart Nature Wind (v011)
// ------------------------------------------------------
// 기능 요약:
//  - Serial 콘솔 초기화 및 Logger 레벨 설정
//  - Smart Nature Wind 메인 컨트롤러(CL_WS10_WindScapeSimulator) 실행
//  - Web API 및 시뮬레이션 Tick 루프 수행
// ------------------------------------------------------

#include <Arduino.h>
#include <LittleFS.h>

#include "v015/A00_Main_040.h"

void setup() {
	Serial.begin(115200);
	delay(1500);


	Serial.println();
	Serial.println("=====================================");
	Serial.println(" Smart Nature Wind - Boot Sequence ");
	Serial.println("=====================================");

	// ------------------------------------------------------
	// 1️⃣ 로그 레벨 설정 (DEBUG / INFO / WARN / ERROR)
	// ------------------------------------------------------
	CL_D10_Logger::setLevel(EN_L10_LOG_INFO);
	CL_D10_Logger::log(EN_L10_LOG_INFO, "[BOOT] Logger ready");

	// ------------------------------------------------------
	// 2️⃣ 시스템 초기화 (FS / Wi-Fi / PWM / WebAPI / Sim)
	// ------------------------------------------------------
	A00_init();

	Serial.println();
	Serial.println("[BOOT] Initialization complete");
	Serial.println("Access endpoints:");
	Serial.println("   → /api/state");
	Serial.println("   → /api/config");
	Serial.println("   → /api/chart");
	Serial.println("   → /api/sim/start");
	Serial.println("   → /api/logs");
	Serial.println();
}

void loop() {
	A00_run();
}
