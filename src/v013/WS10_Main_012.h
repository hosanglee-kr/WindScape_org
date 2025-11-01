#pragma once
/*
 * ------------------------------------------------------
 * 소스명   : WS10_Main_012.h
 * 모듈약어 : WS10
 * 모듈명   : Smart Nature Wind 통합 실행 엔진 (Main Controller v012)
 * ------------------------------------------------------
 * 기능 요약:
 *  - LittleFS 파일 시스템 초기화 및 용량 로깅
 *  - cfg_*.json (022 스키마) 분리파일 로드 및 기본값 생성
 *  - Wi-Fi/AP/STA 멀티모드 자동 초기화
 *  - PWM FAN Controller 초기화
 *  - Motion / BLE / Control Manager 연계 기반 프레임 준비
 *  - Web API 초기화 (정적/동적 라우팅)
 *  - Simulation 엔진 시작 및 주기 tick()
 * ------------------------------------------------------
 * [구현 규칙]
 *  - 항상 소스 시작 주석 체계 유지
 *  - ArduinoJson v7.x.x 사용 (v6 이하 사용 금지)
 *  - JsonDocument 단일 타입만 사용
 *  - createNestedArray/Object/containsKey 사용 금지
 *  - memset + strlcpy 기반 안전 초기화
 *  - 주석/필드명은 JSON 구조와 동일하게 유지
 *  - 모듈별 단일 헤더(h)파일로만 구성 (cpp 없음)
 *  - 소스 앞부분 구현규칙, 코드네이밍규칙 변경 금지
 * ------------------------------------------------------
 * [코드 네이밍 규칙]
 *   - 전역 상수,매크로      : G_모듈약어_ 접두사
 *   - 전역 변수             : g_모듈약어_ 접두사
 *   - 전역 함수             : 모듈약어_ 접두사
 *   - type                  : T_모듈약어_ 접두사
 *   - typedef               : _t  접미사
 *   - enum 상수             : EN_모듈약어_ 접두사
 *   - 구조체                : ST_모듈약어_ 접두사
 *   - 클래스명              : CL_모듈약어_ 접두사
 *   - 클래스 private 멤버   : _ 접두사
 *   - 클래스 멤버(함수/변수) : 모듈약어 접두사 미사용
 *   - 클래스 정적 멤버      : s_ 접두사
 *   - 함수 로컬 변수        : v_ 접두사
 *   - 함수 인자             : p_ 접두사

 * ------------------------------------------------------
 */

#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WiFiMulti.h>

#include "A10_Const_012.h"
#include "C10_ConfigManager_012.h"
#include "D10_Logger_011.h"
#include "M10_WiFiManager_012.h"
#include "S10_Simulation_012.h"
#include "P10_PWM_ctrl_012.h"
#include "M10_MotionLogic_013.h"
#include "CT10_ControlManager_013.h"
#include "B10_BLEScanner_012.h"
#include "W10_WebAPI_012.h"

// ======================================================
// WS10 Main System
// ======================================================
class CL_WS10_WindScapeSystem {
public:
	CL_WS10_WindScapeSystem()
	: _webServer(80) {}

	// ==================================================
	// System Init
	// ==================================================
	void init() {
		_initFS();
		_initConfig();
		_initWiFi();
		_initPWM();
		_initMotionAndBLE();
		_initControl();
		_initWeb();
		_initSim();

		_webServer.begin();
		CL_D10_Logger::log(EN_L10_LOG_INFO, "[WS10] System Ready");
	}

	// ==================================================
	// Main Loop Tick
	// ==================================================
	void run() {
		_bleScanner.tick();
		_motion.tick();
		_control.tick();
		_sim.tick();
		delay(20);
	}

	// ==================================================
	// Status Print (Debug)
	// ==================================================
	void WS10_printStatus() {
		String v_ip =
			(WiFi.getMode() == WIFI_AP)
			? WiFi.softAPIP().toString()
			: WiFi.localIP().toString();

		CL_D10_Logger::log(EN_L10_LOG_INFO, "[WS10] Wi-Fi Mode=%d, IP=%s",
			WiFi.getMode(), v_ip.c_str());

		CL_D10_Logger::log(EN_L10_LOG_INFO, "[WS10] Fan duty=%.2f%%",
			_pwm.P10_getDutyPercent());
	}

	// ==================================================
	// Accessors (외부 의존 구성에 사용)
	// ==================================================
	CL_S10_Simulation& WS10_sim()   { return _sim; }
	CL_P10_PWM&        WS10_pwm()   { return _pwm; }
	CL_M10_MotionLogic& WS10_motion(){ return _motion; }
	CL_CT10_ControlManager& WS10_ctrl(){ return _control; }

private:
	AsyncWebServer        _webServer;
	WiFiMulti             _wifiMulti;
	CL_S10_Simulation     _sim;
	CL_P10_PWM            _pwm;
	CL_M10_MotionLogic    _motion;
	CL_CT10_ControlManager _control;
	CL_B10_BLEScanner     _bleScanner;

	// ==================================================
	// Filesystem
	// ==================================================
	void _initFS() {
		if (!LittleFS.begin(true)) {
			CL_D10_Logger::log(EN_L10_LOG_ERROR,"[WS10] LittleFS FAIL");
			return;
		}
		float v_totalKB=(float)LittleFS.totalBytes()/1024.0f;
		float v_usedKB =(float)LittleFS.usedBytes() /1024.0f;

		CL_D10_Logger::log(EN_L10_LOG_INFO,
			"[WS10] FS OK %.1f/%.1f KB",
			v_usedKB, v_totalKB);
	}

	// ==================================================
	// Config Load (cfg_*022.json)
	// ==================================================
	void _initConfig() {
		CL_D10_Logger::log(EN_L10_LOG_INFO,"[WS10] Load configs...");
		CL_C10_ConfigManager::loadAllConfigs(g_A10_config_root);
		CL_D10_Logger::log(EN_L10_LOG_INFO,"[WS10] Config OK");
	}

	// ==================================================
	// Wi-Fi
	// ==================================================
	void _initWiFi() {
		if (g_A10_config_root.wifi &&
			CL_M10_WiFiManager::init(*g_A10_config_root.wifi, g_A10_config_root.system, _wifiMulti)) {

			CL_D10_Logger::log(EN_L10_LOG_INFO,"[WS10] Wi-Fi OK");
		} else {
			CL_D10_Logger::log(EN_L10_LOG_WARN,"[WS10] Wi-Fi fallback AP-only");
		}
	}

	// ==================================================
	// PWM
	// ==================================================
	void _initPWM() {
		auto &v_cfg=g_A10_config_root.system.hw.fan_pwm;
		_pwm.P10_init(v_cfg.pin, v_cfg.channel, v_cfg.freq, v_cfg.res);

		CL_D10_Logger::log(EN_L10_LOG_INFO,
			"[WS10] PWM pin=%u ch=%u freq=%luHz res=%u",
			v_cfg.pin,v_cfg.channel,v_cfg.freq,v_cfg.res);
	}

	// ==================================================
	// Motion & BLE Logic
	// ==================================================
	void _initMotionAndBLE() {
		_bleScanner.begin();
		_motion.begin();
		_motion.setBLE(&_bleScanner);

		CL_D10_Logger::log(EN_L10_LOG_INFO,"[WS10] Motion + BLE ready");
	}

	// ==================================================
	// Control Manager (CT10)
	// ==================================================
	void _initControl() {
		_control.begin(&_sim, &_pwm, &_motion);
		CL_D10_Logger::log(EN_L10_LOG_INFO,"[WS10] Control manager ready");
	}

	// ==================================================
	// Web API
	// ==================================================
	void _initWeb() {
		CL_W10_WebAPI::W10_init(_webServer, _sim, _wifiMulti, _pwm);
		CL_D10_Logger::log(EN_L10_LOG_INFO,"[WS10] Web API ready");
	}

	// ==================================================
	// Simulation
	// ==================================================
	void _initSim() {
		_sim.begin(_pwm);

		const char* v_preset =
			(g_A10_config_root.sim)
			? g_A10_config_root.sim->preset
			: "DEFAULT";

		CL_D10_Logger::log(EN_L10_LOG_INFO,
			"[WS10] Simulation start preset=%s",
			v_preset);
	}
};
