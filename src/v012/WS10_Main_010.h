#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : WS10_Main_010.h
 * 모듈명 : Smart Nature Wind 통합 실행 엔진 (Main Controller)
 * ------------------------------------------------------
 * 기능 요약:
 *  - LittleFS 파일 시스템 초기화 및 용량 로깅
 *  - 분리된 cfg_*.json 설정 파일 로드 및 검증
 *  - Wi-Fi 모드(AP/STA/AP+STA) 자동 선택 및 재시도
 *  - PWM 컨트롤러 초기화 및 재구성 반영
 *  - Web API 초기화 (정적/동적 파일 경로 포함)
 *  - Simulation 엔진 시작 및 주기적 tick()
 * ------------------------------------------------------
 * [코드 네이밍 규칙]
 * 		- 현재 파일 모듈약어    : WS10
 * 		- 전역 상수,매크로      : G_모듈약어_ 접두사
 * 		- 전역 변수             : g_모듈약어_ 접두사
 * 		- 전역 함수             : 모듈약어_ 접두사
 * 		- type                  : T_모듈약어_ 접두사
 * 		- enum 상수             : EN_모듈약어_ 접두사
 * 		- 구조체                : ST_모듈약어_ 접두사
 * 		- 클래스명              : CL_모듈약어_ 접두사
 * 		- 클래스 private 멤버   : _ 접두사,
 * 		- 클래스 정적 멤버      : s_ 접두사
 * 		- 로컬 변수             : v_ 접두사
 * 		- 함수 인자             : p_접두사
 */

#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WiFiMulti.h>

#include "A10_Const_010.h"
#include "C10_ConfigManager_010.h"
#include "D10_Logger_010.h"
#include "M10_WiFiManager_010.h"
#include "S10_Simulation_010.h"
#include "P10_PWM_ctrl_010.h"
#include "W10_WebAPI_010.h"

// ======================================================
// Smart Nature Wind 메인 시스템 클래스
// ======================================================
class CL_WS10_WindScapeSimulator {
public:
	CL_WS10_WindScapeSimulator() : _webServer(80) {}

	// =====================================================
	// 시스템 초기화
	// =====================================================
	void WS10_init() {
		// ① LittleFS 파일시스템 초기화
		if (!LittleFS.begin(true)) {
			CL_D10_Logger::log(EN_L10_LOG_ERROR, "LittleFS mount failed!");
		} else {
			float v_totalKB = (float)LittleFS.totalBytes() / 1024.0f;
			float v_usedKB  = (float)LittleFS.usedBytes()  / 1024.0f;
			CL_D10_Logger::log(EN_L10_LOG_INFO, "LittleFS mounted (%.1f / %.1f KB)", v_usedKB, v_totalKB);
		}

		// ② 설정파일 로드 (없으면 기본 생성)
		if (!CL_C10_ConfigManager::loadAll(g_A10_config_root)){
			// if (!CL_C10_ConfigManager::C10_loadAll()) {
			CL_D10_Logger::log(EN_L10_LOG_WARN, "Config invalid or missing → defaults created");
		} else {
			CL_D10_Logger::log(EN_L10_LOG_INFO, "All configuration files loaded successfully");
		}

		// ③ Wi-Fi 초기화
		if (g_A10_config_root.wifi && CL_M10_WiFiManager::M10_init(*g_A10_config_root.wifi, _wifiMulti)) {
		//if (CL_M10_WiFiManager::M10_init(g_A10_config_root, _wifiMulti)) {
			CL_D10_Logger::log(EN_L10_LOG_INFO, "Wi-Fi initialized OK");
		} else {
			CL_D10_Logger::log(EN_L10_LOG_WARN, "Wi-Fi fallback to AP-only mode");
		}

		// ④ PWM 초기화
        auto& v_pwmCfg = g_A10_config_root.core.hw.fan_pwm;
		// auto& v_pwmCfg = *g_A10_config_root.pwm;
		_pwmCtrl.P10_init(v_pwmCfg.pin, v_pwmCfg.channel, v_pwmCfg.freq, v_pwmCfg.resolution);
		CL_D10_Logger::log(EN_L10_LOG_INFO, "PWM ready: pin=%d ch=%d freq=%luHz res=%d",
			v_pwmCfg.pin, v_pwmCfg.channel, v_pwmCfg.freq, v_pwmCfg.resolution);

		// ⑤ Web API 서버 초기화
		CL_W10_WebAPI::W10_init(_webServer, _sim, _wifiMulti, _pwmCtrl);
		CL_D10_Logger::log(EN_L10_LOG_INFO, "Web API initialized");

		// ⑥ 시뮬레이션 초기화 및 시작
		_sim.S10_begin(_pwmCtrl);
		CL_D10_Logger::log(EN_L10_LOG_INFO, "Simulation started (preset=%s)", _sim.S10_presetName);

		// ⑦ WebServer 시작
		_webServer.begin();
		CL_D10_Logger::log(EN_L10_LOG_INFO, "AsyncWebServer started on port 80");
	}

	// =====================================================
	// 메인 루프 (tick)
	// =====================================================
	void WS10_run() {
		_sim.S10_tick();
		delay(20); // tick 주기 안정화
	}

	// =====================================================
	// 시스템 상태 출력
	// =====================================================
	void WS10_printStatus() {
		String v_ip = WiFi.getMode() == WIFI_AP ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
		CL_D10_Logger::log(EN_L10_LOG_INFO, "Wi-Fi Mode=%d, IP=%s", WiFi.getMode(), v_ip.c_str());
		CL_D10_Logger::log(EN_L10_LOG_INFO, "Current Wind Speed: %.2f m/s", _sim.S10_currentWindSpeed);
	}

	// =====================================================
	// 내부 구성요소 접근자
	// =====================================================
	CL_S10_Simulation& WS10_sim()  { return _sim; }
	CL_P10_PWM&        WS10_pwm()  { return _pwmCtrl; }

private:
	AsyncWebServer   _webServer;
	WiFiMulti        _wifiMulti;
	CL_S10_Simulation _sim;
	CL_P10_PWM        _pwmCtrl;
};
