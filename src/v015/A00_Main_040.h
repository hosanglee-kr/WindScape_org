#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : A00_Main_010.h
 * 모듈 약어 : A00
 * 모듈명 : Smart Nature Wind Main Entrypoint (v002)
 * ------------------------------------------------------
 * 기능 요약:
 * - OTA 제외 모든 보완 사항 적용 버전
 * - 모션센서, Watchdog, FactoryReset, LittleFS WebUI 포함
 * - Wi-Fi LED 상태표시 및 완전 초기화 지원
 * - [Refactored] CT10의 제어 상태 변경 시 브로드캐스트 책임을 위임받음
 * ------------------------------------------------------
 * [구현 규칙]
 * - 주석 구조, 네이밍 규칙, ArduinoJson v7 단일 문서 정책 준수
 * ------------------------------------------------------
 */

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <esp_task_wdt.h>

#include "A20_Const_040.h"
#include "C10_Config_041.h"
#include "CT10_Control_040.h"
#include "D10_Logger_040.h"
#include "M10_MotionLogic_040.h"
#include "N10_NvsManager_040.h"
#include "P10_PWM_ctrl_040.h"
#include "S10_Simul_040.h"
#include "S20_WindSolver_040.h"
#include "W10_Web_050.h"
#include "WF10_WiFiManager_040.h"

#define G_A00_METRICS_DEBUG_LOG

// [main.cpp] 또는 [MotionLogic.cpp] 파일에 추가
CL_M10_MotionLogic* g_M10_motionLogic = nullptr;

// ---- WebAPI Broker Functions (W10 모듈 의존성 분리를 위한 브로커) ----
void A00_broadcastState(JsonDocument& doc, bool diffOnly) {
	CL_W10_WebAPI::broadcastState(doc, diffOnly);
}

void A00_broadcastChart(JsonDocument& doc, bool diffOnly) {
	CL_W10_WebAPI::broadcastChart(doc, diffOnly);
}

void A00_broadcastMetrics(JsonDocument& doc, bool diffOnly) {
	CL_W10_WebAPI::broadcastMetrics(doc, diffOnly);
}

void A00_markDirty(const char* key) {
	CL_CT10_ControlManager::instance().markDirty(key);
}

AsyncWebServer			g_A00_server(80);
static WiFiMulti		g_A00_wifiMulti;

CL_CT10_ControlManager& g_A00_control = CL_CT10_ControlManager::instance();
CL_P10_PWM				g_P10_pwm;

// ------------------------------------------------------
// LED 핀 설정 (Wi-Fi 상태 표시)
// ------------------------------------------------------
constexpr int			G_A00_LED_PIN = 2;	// 내장 LED (ESP32 보드용)

// ------------------------------------------------------
// Factory Reset 유틸 (모든 JSON 삭제 후 기본 복원)
// ------------------------------------------------------
bool A00_factoryReset() {
	CL_D10_Logger::log(EN_L10_LOG_WARN, "[A00] Factory reset initiated...");
	if (!LittleFS.begin(true)) {
		CL_D10_Logger::log(EN_L10_LOG_ERROR, "[A00] LittleFS mount failed");
		return false;
	}

	File root = LittleFS.open("/json");
	if (!root || !root.isDirectory()) {
		CL_D10_Logger::log(EN_L10_LOG_WARN, "[A00] No /json directory");
	} else {
		File file = root.openNextFile();
		while (file) {
			String path = file.name();
			if (path.endsWith(".json")) {
				LittleFS.remove(path);
				CL_D10_Logger::log(EN_L10_LOG_INFO, "[A00] Removed: %s", path.c_str());
			}
			file = root.openNextFile();
		}
	}
	LittleFS.end();

	delay(500);
	CL_C10_ConfigManager::factoryResetFromDefault();
	CL_N10_NvsManager::clearAll();
	ESP.restart();
	return true;
}

// ------------------------------------------------------
// 메인 초기화
// ------------------------------------------------------
void A00_init() {
	// 1. Logger 초기화
	CL_D10_Logger::begin(Serial);
	pinMode(G_A00_LED_PIN, OUTPUT);

	digitalWrite(G_A00_LED_PIN, LOW);
	CL_D10_Logger::log(EN_L10_LOG_INFO, "=== Smart Nature Wind Boot (v002) ===");

	// 2. LittleFS 마운트
	if (!LittleFS.begin(true)) {
		CL_D10_Logger::log(EN_L10_LOG_ERROR, "[FS] LittleFS mount failed");
	} else {
		CL_D10_Logger::log(EN_L10_LOG_INFO, "[FS] LittleFS mounted OK");
	}

	// 3. Config + NVS 초기화
	CL_C10_ConfigManager::loadAll(g_A20_config_root);
	CL_N10_NvsManager::begin();

	if (!g_A20_config_root.system || !g_A20_config_root.wifi) {
		CL_D10_Logger::log(EN_L10_LOG_ERROR, "[A00] Config root invalid (system or wifi is null).");
		// 필요에 따라 FactoryReset 시도 or 안전 모드 진입
		// 예: FactoryReset 후 재부팅:
		// CL_D10_Logger::log(EN_L10_LOG_WARN, "[A00] Trying factory reset due to invalid config.");
		// CL_C10_ConfigManager::factoryResetFromDefault();
		// ESP.restart();
		return;	 // 일단 초기화 중단
	}

	// 4. Wi-Fi 초기화
	const ST_A20_WifiConfig_t&   v_wifi	= *g_A20_config_root.wifi;
	const ST_A20_SystemConfig_t& v_sys	= *g_A20_config_root.system;

	bool					   v_wifiOk = CL_WF10_WiFiManager::init(v_wifi, v_sys, g_A00_wifiMulti);
	digitalWrite(G_A00_LED_PIN, v_wifiOk ? HIGH : LOW);

	// 5. PWM + Control + Simulation
	g_P10_pwm.P10_begin(*g_A20_config_root.system);
	//
	CL_CT10_ControlManager::begin();

	// 6. Motion Logic (PIR/BLE 감지 활성)
	CL_M10_MotionLogic::M10_begin();
	CL_D10_Logger::log(EN_L10_LOG_INFO, "[M10] Motion Logic started");

	// 7. Web API + Web UI
	CL_W10_WebAPI::begin(g_A00_server, g_A00_control, g_A00_wifiMulti);

	g_A00_server.begin();

	// 8. Watchdog 초기화 (10초)
	esp_task_wdt_init(10, true);
	esp_task_wdt_add(NULL);

	CL_D10_Logger::log(EN_L10_LOG_INFO, "[A00] Init complete. Ready.");
}

// ------------------------------------------------------
// 메인 루프
// ------------------------------------------------------
void A00_run() {
	static uint32_t v_lastMetricsMs = 0;  // 메트릭/차트 브로드캐스트 주기 관리
	static uint32_t v_lastLedMs		= 0;
	static uint32_t v_lastFlush		= 0;

	uint32_t		v_now			= millis();

	esp_task_wdt_reset();  // Watchdog feed

	CL_CT10_ControlManager::tick();

	// CT10 Dirty 플래그 기반 브로드캐스트 (책임 위임)
	CL_CT10_ControlManager& v_ctrl = g_A00_control;

#if defined(G_A00_METRICS_DEBUG_LOG)
	static uint32_t v_lastMetricsLogMs = 0;
	static uint32_t v_lastChartLogMs   = 0;
	static uint32_t v_lastStateLogMs   = 0;
#endif

	// 1. 상태 브로드캐스트 (상태 변경 발생 시)
	if (v_ctrl.consumeDirtyState()) {
		JsonDocument v_doc;
		v_ctrl.toJson(v_doc);
		A00_broadcastState(v_doc, true);

#if defined(G_A00_METRICS_DEBUG_LOG)
		uint32_t v_now_ms = millis();
		CL_D10_Logger::log(EN_L10_LOG_DEBUG, "[A00] State broadcast at %u ms (Δ=%u)", v_now_ms, v_now_ms - v_lastStateLogMs);
		v_lastStateLogMs = v_now_ms;
#endif
	}

	// 2. 메트릭/차트 브로드캐스트 (CT10 내부에서 1.5초 주기로 Dirty 설정)
	if (v_ctrl.consumeDirtyMetrics()) {
		JsonDocument v_doc;
		v_ctrl.toMetricsJson(v_doc);
		A00_broadcastMetrics(v_doc, true);

#if defined(G_A00_METRICS_DEBUG_LOG)
		uint32_t v_now_ms = millis();
		CL_D10_Logger::log(EN_L10_LOG_DEBUG, "[A00] Metrics broadcast at %u ms (Δ=%u)", v_now_ms, v_now_ms - v_lastMetricsLogMs);
		v_lastMetricsLogMs = v_now_ms;
#endif
	}

	if (v_ctrl.consumeDirtyChart()) {
		JsonDocument v_doc;
		v_ctrl.toChartJson(v_doc, true);
		A00_broadcastChart(v_doc, true);

#if defined(G_A00_METRICS_DEBUG_LOG)
		uint32_t v_now_ms = millis();
		CL_D10_Logger::log(EN_L10_LOG_DEBUG, "[A00] Chart broadcast at %u ms (Δ=%u)", v_now_ms, v_now_ms - v_lastChartLogMs);
		v_lastChartLogMs = v_now_ms;
#endif
	}

	// 3. 요약 상태 브로드캐스트 (필요 시)
	if (v_ctrl.consumeDirtySummary()) {
		JsonDocument v_doc;
		v_ctrl.toSummaryJson(v_doc);
		// A00_broadcastSummary(v_doc, true); // summary 브로커는 현재 정의되어 있지 않음
	}

	// --------------------------------------------------

	// NVS Dirty Flush (10초마다)
	if (v_now - v_lastFlush >= 10000) {
		v_lastFlush = v_now;
		CL_N10_NvsManager::flushIfNeeded();
	}

	// LED 상태 토글 (Wi-Fi 연결 유지 확인)
	if (v_now - v_lastLedMs >= 1000) {
		v_lastLedMs = v_now;
		digitalWrite(G_A00_LED_PIN, CL_WF10_WiFiManager::isStaConnected() ? HIGH : LOW);
	}

	delay(10);
}
