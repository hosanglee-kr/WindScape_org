#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : WS10_Main_007.h
 * 모듈명 : WindScape 통합 실행 엔진 (Main Controller)
 * ------------------------------------------------------
 * 기능 요약:
 *  - 설정 파일 로드 및 검증 (없으면 기본 생성)
 *  - LittleFS 파일 시스템 초기화
 *  - Wi-Fi 모드(AP/STA/AP+STA) 자동 선택 및 초기화
 *  - PWM 컨트롤러 초기화
 *  - Web API/서버 초기화 (HTML/JS/CSS 동적 경로 포함)
 *  - Simulation 엔진 시작 및 tick() 루프 실행
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
 * 		- 함수 인자             : p_ 접두사
 */

#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WiFiMulti.h>

#include "A10_Const_007.h"
#include "C10_ConfigManager_007.h"
#include "D10_Logger_004.h"
#include "S10_Simulation_007.h"
#include "M10_WiFiManager_007.h"
#include "W10_WebAPI_007.h"
#include "P10_PWM_ctrl_005.h"

// ======================================================
// WindScape 메인 시스템 클래스
// ======================================================
class CL_WS10_WindScapeSimulator {
public:
	CL_WS10_WindScapeSimulator() : _webServer(80) {}

	// =====================================================
	// 초기화 순서:
	//   ① 파일시스템
	//   ② 설정 로드
	//   ③ Wi-Fi 초기화
	//   ④ PWM 초기화
	//   ⑤ Web API 초기화
	//   ⑥ Simulation 시작
	//   ⑦ WebServer 시작
	// =====================================================
	void init() {
		// ① LittleFS
		if (!LittleFS.begin(true)) {
			CL_D10_Logger::log(EN_L10_LOG_ERROR, "LittleFS mount failed!");
		} else {
			size_t v_total = LittleFS.totalBytes();
			size_t v_used  = LittleFS.usedBytes();
			CL_D10_Logger::log(EN_L10_LOG_INFO,
				"LittleFS OK (%.2f KB / %.2f KB)",
				(float)v_used / 1024.0f, (float)v_total / 1024.0f);
		}

		// ② 설정 로드 (없으면 기본 생성)
		bool v_loaded = CL_C10_ConfigManager::loadOrCreate(g_A10_config);
		if (!v_loaded) {
			CL_D10_Logger::log(EN_L10_LOG_WARN, "Using default configuration - not found or invalid.");
		} else {
			CL_D10_Logger::log(EN_L10_LOG_INFO, "Config loaded from %s", A10_Const::CONFIG_JSON_FILE);
		}

		// ③ Wi-Fi 초기화 (자동 분기)
		bool v_wifiOk = CL_M10_WiFiManager::init(g_A10_config, _wifiMulti);
		if (v_wifiOk) {
			CL_D10_Logger::log(EN_L10_LOG_INFO, "Wi-Fi connected successfully (mode=%d)", g_A10_config.wifi_mode);
		} else {
			CL_D10_Logger::log(EN_L10_LOG_WARN, "Wi-Fi connection failed → running in AP mode only");
		}

		// ④ PWM 초기화
		_pwmCtrl.init(
			g_A10_config.fan_pwm_pin,
			g_A10_config.pwm_channel,
			g_A10_config.pwm_frequency,
			g_A10_config.pwm_resolution
		);
		CL_D10_Logger::log(EN_L10_LOG_INFO,
			"PWM initialized → pin=%d, ch=%d, freq=%lu Hz, res=%d bit",
			g_A10_config.fan_pwm_pin,
			g_A10_config.pwm_channel,
			g_A10_config.pwm_frequency,
			g_A10_config.pwm_resolution
		);

		// ⑤ Web API 초기화 (HTML/JS/CSS 설정 기반)
		CL_W10_WebAPI::init(_webServer, _sim, _wifiMulti, _pwmCtrl);
		CL_D10_Logger::log(EN_L10_LOG_INFO, "Web API routes mounted");

		// ⑥ Simulation 시작 (프리셋 적용)
		_sim.begin(_pwmCtrl, true);
		CL_D10_Logger::log(EN_L10_LOG_INFO, "Wind simulation engine started (preset index=%d)", g_A10_config.preset_mode_index);

		// ⑦ Web 서버 시작
		_webServer.begin();
		CL_D10_Logger::log(EN_L10_LOG_INFO, "AsyncWebServer started on port 80");
	}

	// =====================================================
	// 메인 루프 (tick)
	// =====================================================
	void run() {
		_sim.tick();
	}

	// =====================================================
	// Simulation 객체 접근자
	// =====================================================
	CL_S10_Simulation& sim() { return _sim; }

	// Wi-Fi 상태 점검
	void printStatus() {
		CL_D10_Logger::log(EN_L10_LOG_INFO, "Wi-Fi: %s", CL_M10_WiFiManager::getStaStatusString());
	}

private:
	// --------------------------------------------------
	// 구성 요소
	// --------------------------------------------------
	AsyncWebServer   _webServer;
	WiFiMulti        _wifiMulti;
	CL_S10_Simulation _sim;
	CL_P10_PWM        _pwmCtrl;
};

