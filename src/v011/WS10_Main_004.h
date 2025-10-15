// WS10_Main_004.h

#pragma once
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>

#include "C10_ConfigManager_004.h"
#include "A10_Const_004.h"
#include "D10_Logger_004.h"
#include "S10_Simulation_004.h"
#include "W10_WebAPI_004.h"
#include "M10_WiFiManager_004.h"

//WS2812 PIN 21

// 전체 오케스트레이션: 초기화/루프 + WebServer 조립
class CL_WS10_WindScapeSimulator {
   public:
	CL_WS10_WindScapeSimulator() : g_WS10_asyncWeb(80) {
	}

	// 초기화: 설정 로드 → Wi-Fi → PWM → WebServer → 프리셋/시뮬 시작
	void init(void) {
		if (!LittleFS.begin(true)) {
			CL_D10_Logger::log(EN_L10_LOG_ERROR, "LittleFS mount failed");
		} else {
			CL_D10_Logger::log(EN_L10_LOG_INFO, "LittleFS mounted");
		}

		// 설정 로드(실패 시 기본값으로 진행)
		CL_C10_ConfigManager::load(g_A10_config);

		CL_D10_Logger::log(EN_L10_LOG_INFO, "WS10_init_010_C10_ConfigManager::loaded");

		// Wi-Fi 초기화
		CL_M10_WiFiManager::init(g_A10_config, g_WS10_wifiMulti);

		CL_D10_Logger::log(EN_L10_LOG_INFO, "WS10_init_020_M10_WiFiManager::init");

		// PWM/핀
		ledcSetup(g_A10_config.pwm_channel, g_A10_config.pwm_frequency, g_A10_config.pwm_resolution);

		CL_D10_Logger::log(EN_L10_LOG_INFO, "WS10_init_031_SC10_ledcSetup");

		ledcAttachPin(g_A10_config.fan_pwm_pin, g_A10_config.pwm_channel);

		CL_D10_Logger::log(EN_L10_LOG_INFO, "WS10_init_032_ledcAttachPin");

		//// // pinMode(g_A10_config.fan_tach_pin, INPUT_PULLUP);

		CL_D10_Logger::log(EN_L10_LOG_INFO, "WS10_init_030_ledcSetup setup");

		// WebServer

		CL_W10_WebAPI::init(g_WS10_asyncWeb, g_WS10_sim, g_WS10_wifiMulti);
		CL_D10_Logger::log(EN_L10_LOG_INFO, "W10_Webapi_init_040_");

		
		// CL_W10_WebAPI::mountApi(g_WS10_asyncWeb, g_WS10_sim, g_WS10_wifiMulti);
		// CL_D10_Logger::log(EN_L10_LOG_INFO, "W10_init_060_SC10_WebAPI::mountApi");

		// CL_W10_WebAPI::mountStatic(g_WS10_asyncWeb);
		// CL_D10_Logger::log(EN_L10_LOG_INFO, "W10_init_040_SC10_WebAPI::mountStatic");

		g_WS10_sim.begin(true);
		CL_D10_Logger::log(EN_L10_LOG_INFO, "WS10_init_050_g_WS10_sim.begin");

		g_WS10_asyncWeb.begin();
		CL_D10_Logger::log(EN_L10_LOG_INFO, "AsyncWebServer started");
	}

	// 루프: 시뮬레이션 계산
	void run(void) {
		g_WS10_sim.tick();
	}

	// 현재 시뮬레이터 접근자 (필요 시)
	CL_S10_Simulation& sim() {
		return g_WS10_sim;
	}

   private:
	AsyncWebServer	g_WS10_asyncWeb;
	WiFiMulti		g_WS10_wifiMulti;
	CL_S10_Simulation  g_WS10_sim;
};
