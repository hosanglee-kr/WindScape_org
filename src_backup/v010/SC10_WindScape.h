// SC10_WindScape.h

#pragma once
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include "SC10_Const.h"
#include "SC10_Logger.h"
#include "SC10_ConfigManager.h"
#include "SC10_WiFiManager.h"
#include "SC10_Simulation.h"
#include "SC10_WebAPI.h"

// 전체 오케스트레이션: 초기화/루프 + WebServer 조립
class WindScapeSimulator {
public:
  WindScapeSimulator(): g_SC10_asyncWeb(80) {}

  // 초기화: 설정 로드 → Wi-Fi → PWM → WebServer → 프리셋/시뮬 시작
  void SC10_init(void) {
    
    if (!LittleFS.begin(true)) {
      SC10_Logger::log(SC10_LOG_ERROR,"LittleFS mount failed");
    } else {
      SC10_Logger::log(SC10_LOG_INFO,"LittleFS mounted");
    }

    // 설정 로드(실패 시 기본값으로 진행)
    ConfigManager::load(g_SC10_config);

    // Wi-Fi 초기화
    SC10_WiFiManager::init(g_SC10_config, g_SC10_wifiMulti);

    // PWM/핀
    ledcSetup(g_SC10_config.pwm_channel, g_SC10_config.pwm_frequency, g_SC10_config.pwm_resolution);
    ledcAttachPin(g_SC10_config.fan_pwm_pin, g_SC10_config.pwm_channel);
    pinMode(g_SC10_config.fan_tach_pin, INPUT_PULLUP);

    // WebServer
    SC10_WebAPI::mountStatic(g_SC10_asyncWeb);
    g_SC10_sim.begin(true);
    SC10_WebAPI::mountApi(g_SC10_asyncWeb, g_SC10_sim, g_SC10_wifiMulti);
    g_SC10_asyncWeb.begin();
    SC10_Logger::log(SC10_LOG_INFO,"AsyncWebServer started");
  }

  // 루프: 시뮬레이션 계산
  void SC10_run(void) {
    g_SC10_sim.tick();
  }

  // 현재 시뮬레이터 접근자 (필요 시)
  SC10_Simulation& sim(){ return g_SC10_sim; }

private:
  AsyncWebServer g_SC10_asyncWeb;
  WiFiMulti      g_SC10_wifiMulti;
  SC10_Simulation g_SC10_sim;
};


