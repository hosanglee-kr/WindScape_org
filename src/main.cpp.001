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

#include "v013/WS10_Main_012.h"
// #include "v012/WS10_Main_010.h"

// 메인 컨트롤러 인스턴스
CL_WS10_WindScapeSystem g_WS10;
//CL_WS10_WindScapeSimulator g_WS10;

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
	g_WS10.init();

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
	// ------------------------------------------------------
	// 3️⃣ Simulation Tick Loop
	// ------------------------------------------------------
	g_WS10.run();

	// 로그나 Wi-Fi 상태를 주기적으로 출력하고 싶다면:
	// static unsigned long lastPrint = 0;
	// if (millis() - lastPrint > 5000) {
	//     g_WS10.WS10_printStatus();
	//     lastPrint = millis();
	// }

	delay(5); // CPU 점유율 완화
}
