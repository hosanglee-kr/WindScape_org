// SC10_WindScape_002.h

#pragma once
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>

#include "SC10_ConfigManager_002.h"
#include "SC10_Const_002.h"
#include "SC10_Logger_002.h"
#include "SC10_Simulation_002.h"
#include "SC10_WebAPI_002.h"
#include "SC10_WiFiManager_002.h"

// 전체 오케스트레이션: 초기화/루프 + WebServer 조립
class WindScapeSimulator {
   public:
	WindScapeSimulator() : g_SC10_asyncWeb(80) {
	}

	// 초기화: 설정 로드 → Wi-Fi → PWM → WebServer → 프리셋/시뮬 시작
	void SC10_init(void) {
		if (!LittleFS.begin(true)) {
			SC10_Logger::log(SC10_LOG_ERROR, "LittleFS mount failed");
		} else {
			SC10_Logger::log(SC10_LOG_INFO, "LittleFS mounted");
		}

		// 설정 로드(실패 시 기본값으로 진행)
		ConfigManager::load(g_SC10_config);

		SC10_Logger::log(SC10_LOG_INFO, "SC10_init_010_ConfigManager::loaded");

		// Wi-Fi 초기화
		SC10_WiFiManager::init(g_SC10_config, g_SC10_wifiMulti);

		SC10_Logger::log(SC10_LOG_INFO, "SC10_init_020_SC10_WiFiManager::init");

		// PWM/핀
		ledcSetup(g_SC10_config.pwm_channel, g_SC10_config.pwm_frequency, g_SC10_config.pwm_resolution);
		
		SC10_Logger::log(SC10_LOG_INFO, "SC10_init_031_SC10_ledcSetup");

		ledcAttachPin(g_SC10_config.fan_pwm_pin, g_SC10_config.pwm_channel);
		
		SC10_Logger::log(SC10_LOG_INFO, "SC10_init_032_ledcAttachPin");

		//// // pinMode(g_SC10_config.fan_tach_pin, INPUT_PULLUP);

		SC10_Logger::log(SC10_LOG_INFO, "SC10_init_030_ledcSetup setup");

		// WebServer
		SC10_WebAPI::mountStatic(g_SC10_asyncWeb);
		
		SC10_Logger::log(SC10_LOG_INFO, "SC10_init_040_SC10_WebAPI::mountStatic");

		g_SC10_sim.begin(true);

		SC10_Logger::log(SC10_LOG_INFO, "SC10_init_050_g_SC10_sim.begin");


		SC10_WebAPI::mountApi(g_SC10_asyncWeb, g_SC10_sim, g_SC10_wifiMulti);
		
		SC10_Logger::log(SC10_LOG_INFO, "SC10_init_060_SC10_WebAPI::mountApi");

		g_SC10_asyncWeb.begin();

		SC10_Logger::log(SC10_LOG_INFO, "AsyncWebServer started");
	}

	// 루프: 시뮬레이션 계산
	void SC10_run(void) {
		g_SC10_sim.tick();
	}

	// 현재 시뮬레이터 접근자 (필요 시)
	SC10_Simulation& sim() {
		return g_SC10_sim;
	}

   private:
	AsyncWebServer	g_SC10_asyncWeb;
	WiFiMulti		g_SC10_wifiMulti;
	SC10_Simulation g_SC10_sim;
};
