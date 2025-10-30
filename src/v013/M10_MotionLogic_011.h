#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : M10_MotionLogic_010.h
 * 모듈명 : Smart Nature Wind 모션 감지 Manager
 * ------------------------------------------------------
 * 기능 요약:
 *  - cfg_motion_021.json 기반 PIR / BLE 모션 감지 제어
 *  - 감지 시 일정시간 유지 (hold_sec)
 *  - BLE RSSI 기반 근접 판정
 *  - Scheduler 및 Simulation과 연동
 * ------------------------------------------------------
 * [코드 네이밍 규칙]
 * 		- 현재 파일 모듈약어    : M10
 * 		- 전역 상수,매크로      : G_모듈약어_ 접두사
 * 		- 전역 변수             : g_모듈약어_ 접두사
 * 		- 전역 함수             : 모듈약어_ 접두사
 * 		- type                  : T_모듈약어_ 접두사
 * 		- enum 상수             : EN_모듈약어_ 접두사
 * 		- 구조체                : ST_모듈약어_ 접두사
 * 		- 클래스명              : CL_모듈약어_ 접두사
 * 		- 클래스 private 멤버   : _ 접두사
 * 		- 클래스 정적 멤버      : s_ 접두사
 */

#include <Arduino.h>
#include <ArduinoJson.h>
#include "A10_Const_011.h"
#include "D10_Logger_011.h"
#include "S10_Simulation_011.h"

class CL_M10_MotionLogic {
public:
	bool M10_active = false;
	bool M10_motionDetected = false;
	unsigned long M10_lastMotionMs = 0;

private:
	CL_S10_Simulation* _p_sim = nullptr;

public:
	void M10_begin(CL_S10_Simulation& p_sim) {
		_p_sim = &p_sim;
		M10_active = true;
		CL_D10_Logger::log(EN_L10_LOG_INFO, "Motion Logic Init OK");
	}

	// ==================================================
	// 주기 실행
	// ==================================================
	void M10_tick() {
		if (!M10_active) return;
		if (!g_A10_config_root.motion) return;
		ST_A10_MotionConfig& v_motion = *g_A10_config_root.motion;

		// PIR 감지
		if (v_motion.pir.enabled) {
			int v_pir = digitalRead(v_motion.pir.pin);
			if (v_pir == HIGH) {
				M10_motionDetected = true;
				M10_lastMotionMs = millis();
			}
		}

		// BLE 모의 감지 (추후 실제 RSSI 스캔 연동)
		if (v_motion.ble.enabled) {
			for (auto& dev : v_motion.ble.devices) {
				if (dev.enabled && dev.last_rssi > v_motion.ble.rssi_threshold) {
					M10_motionDetected = true;
					M10_lastMotionMs = millis();
				}
			}
		}

		// 유지시간 체크
		unsigned long v_hold = (v_motion.pir.hold_sec > 0) ? v_motion.pir.hold_sec : 120;
		if (M10_motionDetected && millis() - M10_lastMotionMs > v_hold * 1000UL) {
			M10_motionDetected = false;
			CL_D10_Logger::log(EN_L10_LOG_INFO, "Motion hold expired");
		}

		// 팬 제어 (boost 모드)
		if (_p_sim && M10_motionDetected) {
			_p_sim->S10_applyBoost(1.25f); // 강도 25% 증가
		}
	}

	// ==================================================
	// 상태 JSON
	// ==================================================
	void M10_toJson(JsonDocument& p_doc) {
		JsonObject o = p_doc["motion"].to<JsonObject>();
		o["active"] = M10_active;
		o["detected"] = M10_motionDetected;
		o["last_ms"] = M10_lastMotionMs;
	}
};
