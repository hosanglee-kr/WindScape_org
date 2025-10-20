#pragma once
/*
 * WS10_Main_006.h
 * ------------------------------------------------------
 * WindScape 통합 실행 엔진
 * ------------------------------------------------------
 * 기능:
 *  - 설정 로드 (없으면 자동 생성)
 *  - Wi-Fi 초기화
 *  - PWM 초기화
 *  - Web API 초기화
 *  - 시뮬레이션 시작 및 주기적 tick()
 * ------------------------------------------------------
 */

#include <ESPAsyncWebServer.h>
#include <LittleFS.h>

#include "A10_Const_007.h"
#include "C10_ConfigManager_007.h"
#include "D10_Logger_004.h"
#include "S10_Simulation_007.h"
#include "M10_WiFiManager_007.h"
#include "W10_WebAPI_007.h"
#include "P10_PWM_ctrl_005.h"

// WS2812 or Fan PWM on configurable pin (default: 6)

// ======================================================
// WindScape 전체 시스템 클래스
// ======================================================
class CL_WS10_WindScapeSimulator {
public:
	CL_WS10_WindScapeSimulator() : g_WS10_asyncWeb(80) {}

	// --------------------------------------------------
	// 초기화 순서: FS → Config → WiFi → PWM → Web → Sim
	// --------------------------------------------------
	void init() {
		// 1️⃣ 파일 시스템
		if (!LittleFS.begin(true)) {
			CL_D10_Logger::log(EN_L10_LOG_ERROR, "LittleFS mount failed!");
		} else {
			CL_D10_Logger::log(EN_L10_LOG_INFO, "LittleFS mounted OK");
		}

		// 2️⃣ 설정 로드 (없으면 기본 생성)
		if (!CL_C10_ConfigManager::loadOrCreate(g_A10_config)) {
			CL_D10_Logger::log(EN_L10_LOG_WARN, "Using default configuration");
		}
		CL_D10_Logger::log(EN_L10_LOG_INFO, "Config loaded: %s", A10_Const::CONFIG_JSON_FILE);

		// 3️⃣ Wi-Fi 초기화
		CL_M10_WiFiManager::init(g_A10_config, g_WS10_wifiMulti);
		CL_D10_Logger::log(EN_L10_LOG_INFO, "Wi-Fi initialized");

		// 4️⃣ PWM 초기화
		g_P10_pwm.init(
			g_A10_config.fan_pwm_pin,
			g_A10_config.pwm_channel,
			g_A10_config.pwm_frequency,
			g_A10_config.pwm_resolution
		);
		CL_D10_Logger::log(EN_L10_LOG_INFO,
			"PWM init: pin=%d ch=%d freq=%lu res=%d",
			g_A10_config.fan_pwm_pin,
			g_A10_config.pwm_channel,
			g_A10_config.pwm_frequency,
			g_A10_config.pwm_resolution
		);

		// 5️⃣ Web API 등록
		CL_W10_WebAPI::init(g_WS10_asyncWeb, g_WS10_sim, g_WS10_wifiMulti, g_P10_pwm);
		CL_D10_Logger::log(EN_L10_LOG_INFO, "Web API mounted successfully");

		// 6️⃣ 시뮬레이션 시작
		g_WS10_sim.begin(g_P10_pwm, true);
		CL_D10_Logger::log(EN_L10_LOG_INFO, "Simulation engine started");

		// 7️⃣ WebServer 시작
		g_WS10_asyncWeb.begin();
		CL_D10_Logger::log(EN_L10_LOG_INFO, "AsyncWebServer started on port 80");
	}

	// --------------------------------------------------
	// 메인 루프: 바람 시뮬레이션 tick()
	// --------------------------------------------------
	void run() {
		g_WS10_sim.tick();
	}

	// --------------------------------------------------
	// 시뮬 객체 접근자
	// --------------------------------------------------
	CL_S10_Simulation& sim() { return g_WS10_sim; }

private:
	AsyncWebServer   g_WS10_asyncWeb;
	WiFiMulti        g_WS10_wifiMulti;
	CL_S10_Simulation g_WS10_sim;
	CL_P10_PWM        g_P10_pwm;
};
