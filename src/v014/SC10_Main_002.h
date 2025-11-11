#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : SC10_Main_002.h
 * 모듈 약어 : SC10
 * 모듈명 : Smart Nature Wind Main Entrypoint (v002)
 * ------------------------------------------------------
 * 기능 요약:
 *  - OTA 제외 모든 보완 사항 적용 버전
 *  - 모션센서, Watchdog, FactoryReset, LittleFS WebUI 포함
 *  - Wi-Fi LED 상태표시 및 완전 초기화 지원
 * ------------------------------------------------------
 * [구현 규칙]
 *  - 주석 구조, 네이밍 규칙, ArduinoJson v7 단일 문서 정책 준수
 * ------------------------------------------------------
 */

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <esp_task_wdt.h>

#include "A10_Const_015.h"
#include "C10_ConfigManager_023.h"
#include "CT10_ControlManager_021.h"
#include "D10_Logger_016.h"
#include "M10_MotionLogic_016.h"
#include "N10_NvsManager_017.h"
#include "P10_PWM_ctrl_014.h"
#include "S10_Simulation_018.h"
#include "S20_WindSolver_021.h"
#include "W10_WebAPI_022.h"
#include "WF10_WiFiManager_023.h"


// ---- WebAPI Broker Functions ----
void SC10_broadcastState(JsonDocument& doc, bool diffOnly) {
    CL_W10_WebAPI::broadcastState(doc, diffOnly);
}

void SC10_broadcastChart(JsonDocument& doc, bool diffOnly) {
    CL_W10_WebAPI::broadcastChart(doc, diffOnly);
}

void SC10_broadcastMetrics(JsonDocument& doc, bool diffOnly) {
    CL_W10_WebAPI::broadcastMetrics(doc, diffOnly);
}

AsyncWebServer		   g_SC10_server(80);
WiFiMulti			   g_SC10_wifiMulti;
CL_CT10_ControlManager g_SC10_control;

// ------------------------------------------------------
// LED 핀 설정 (Wi-Fi 상태 표시)
// ------------------------------------------------------
constexpr int G_SC10_LED_PIN = 2;  // 내장 LED (ESP32 보드용)

// ------------------------------------------------------
// Factory Reset 유틸 (모든 JSON 삭제 후 기본 복원)
// ------------------------------------------------------
bool SC10_factoryReset() {
	CL_D10_Logger::log(EN_L10_LOG_WARN, "[SC10] Factory reset initiated...");
	if (!LittleFS.begin(true)) {
		CL_D10_Logger::log(EN_L10_LOG_ERROR, "[SC10] LittleFS mount failed");
		return false;
	}

	File root = LittleFS.open("/json");
	if (!root || !root.isDirectory()) {
		CL_D10_Logger::log(EN_L10_LOG_WARN, "[SC10] No /json directory");
	} else {
		File file = root.openNextFile();
		while (file) {
			String path = file.name();
			if (path.endsWith(".json")) {
				LittleFS.remove(path);
				CL_D10_Logger::log(EN_L10_LOG_INFO, "[SC10] Removed: %s", path.c_str());
			}
			file = root.openNextFile();
		}
	}
	LittleFS.end();

	delay(500);
	CL_C10_ConfigManager::factoryResetFromDefault();
	CL_N10_NvsManager::N10_clearAll();
	ESP.restart();
	return true;
}

// ------------------------------------------------------
// 메인 초기화
// ------------------------------------------------------
void SC10_init() {


	// 1. Logger 초기화
	CL_D10_Logger::begin(Serial);
	pinMode(G_SC10_LED_PIN, OUTPUT);

	digitalWrite(G_SC10_LED_PIN, LOW);
	CL_D10_Logger::log(EN_L10_LOG_INFO, "=== Smart Nature Wind Boot (v002) ===");

	// 2. LittleFS 마운트
	if (!LittleFS.begin(true)) {
		CL_D10_Logger::log(EN_L10_LOG_ERROR, "[FS] LittleFS mount failed");
	} else {
		CL_D10_Logger::log(EN_L10_LOG_INFO, "[FS] LittleFS mounted OK");
	}

	// 3. Config + NVS 초기화
	CL_C10_ConfigManager::loadAll(g_A10_config_root);
	// CL_C10_ConfigManager::init();
	CL_N10_NvsManager::N10_begin();

	// 4. Wi-Fi 초기화
	const ST_A10_WifiConfig&   v_wifi	= *g_A10_config_root.wifi;
	const ST_A10_SystemConfig& v_sys 	= *g_A10_config_root.system;
	// const ST_A10_SystemConfig& v_sys	= g_A10_config_root.system;

	bool					   v_wifiOk = CL_WF10_WiFiManager::init(v_wifi, v_sys, g_SC10_wifiMulti);
	digitalWrite(G_SC10_LED_PIN, v_wifiOk ? HIGH : LOW);

	// 5. PWM + Control + Simulation
	g_P10_pwm.P10_begin(*g_A10_config_root.system);
	g_SC10_control.begin();
	g_SC10_control.sim.begin(g_P10_pwm);
	g_SC10_control.sim.setActive(true);

	// 6. Motion Logic (PIR/BLE 감지 활성)
	CL_M10_MotionLogic::M10_begin();
	CL_D10_Logger::log(EN_L10_LOG_INFO, "[M10] Motion Logic started");

	// 7. Web API + Web UI
	CL_W10_WebAPI::begin(g_SC10_server, g_SC10_control);
	g_SC10_server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
	g_SC10_server.begin();

	// 8. Watchdog 초기화 (10초)
	esp_task_wdt_init(10, true);
	esp_task_wdt_add(NULL);

	CL_D10_Logger::log(EN_L10_LOG_INFO, "[SC10] Init complete. Ready.");
}

// ------------------------------------------------------
// 메인 루프
// ------------------------------------------------------
void SC10_run() {
	static uint32_t v_lastSimMs = 0;
	static uint32_t v_lastLedMs = 0;
	static uint32_t v_lastFlush = 0;

	uint32_t v_now = millis();

	esp_task_wdt_reset();  // Watchdog feed

	// Simulation 2Hz
	if (v_now - v_lastSimMs >= 500) {
		v_lastSimMs = v_now;
		g_SC10_control.sim.tick();
	}

	// ControlManager 루프
	g_SC10_control.tick();

	// NVS Dirty Flush (10초마다)
	if (v_now - v_lastFlush >= 10000) {
		v_lastFlush = v_now;
		CL_N10_NvsManager::flushIfNeeded();
		// CL_N10_NvsManager::N10_flushIfDirty();
	}

	// LED 상태 토글 (Wi-Fi 연결 유지 확인)
	if (v_now - v_lastLedMs >= 1000) {
		v_lastLedMs = v_now;
		digitalWrite(G_SC10_LED_PIN,
					 CL_WF10_WiFiManager::isStaConnected() ? HIGH : LOW);
	}

	delay(10);
}
