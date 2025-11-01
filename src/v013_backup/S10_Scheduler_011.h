#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : S10_Scheduler_010.h
 * 모듈명 : Smart Nature Wind 스케줄러 Manager
 * ------------------------------------------------------
 * 기능 요약:
 *  - cfg_schedule_021.json 기반 시간대별 자동 제어
 *  - 프리셋 적용 / 수동 강제모드 지원
 *  - Simulation 및 PWM 제어 모듈과 연동
 *  - WebAPI 연동 (상태 조회, 강제 실행)
 * ------------------------------------------------------
 * [코드 네이밍 규칙]
 * 		- 현재 파일 모듈약어    : S10
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
#include "A10_Const_010.h"
#include "D10_Logger_010.h"
#include "S10_Simulation_010.h"

class CL_S10_Scheduler {
public:
	bool S10_active = false;
	uint8_t S10_currentIndex = 0;       // 현재 실행중 스케줄 인덱스
	unsigned long S10_lastCheckMs = 0;  // 마지막 체크 시각

private:
	CL_S10_Simulation* _p_sim = nullptr;

public:
	void S10_begin(CL_S10_Simulation& p_sim) {
		_p_sim = &p_sim;
		S10_active = true;
		CL_D10_Logger::log(EN_L10_LOG_INFO, "Scheduler Init OK");
	}

	// ==================================================
	// 주기 체크 및 실행
	// ==================================================
	void S10_tick() {
		if (!S10_active) return;
		if (!g_A10_config_root.schedule) return;
		if (!_p_sim) return;

		unsigned long v_now = millis();
		if (v_now - S10_lastCheckMs < 10000UL) return; // 10초마다
		S10_lastCheckMs = v_now;

		time_t v_t = time(nullptr);
		struct tm* v_tm = localtime(&v_t);
		int v_hour = v_tm->tm_hour;
		int v_min = v_tm->tm_min;
		int v_nowMin = v_hour * 60 + v_min;

		for (size_t i = 0; i < g_A10_config_root.schedule->entries.size(); i++) {
			auto& e = g_A10_config_root.schedule->entries[i];
			int v_start = e.start_hour * 60 + e.start_min;
			int v_end = e.end_hour * 60 + e.end_min;

			if (v_nowMin >= v_start && v_nowMin < v_end) {
				if (S10_currentIndex != i) {
					S10_currentIndex = i;
					CL_D10_Logger::log(EN_L10_LOG_INFO, "Scheduler slot %d active (%s)", i, e.preset.c_str());
					_p_sim->_applyPreset(e.preset.c_str());
				}
				return;
			}
		}
	}

	// ==================================================
	// 상태 JSON
	// ==================================================
	void S10_toJson(JsonDocument& p_doc) {
		JsonObject o = p_doc["schedule"].to<JsonObject>();
		o["active"] = S10_active;
		o["current_index"] = S10_currentIndex;
		if (g_A10_config_root.schedule && S10_currentIndex < g_A10_config_root.schedule->entries.size()) {
			auto& e = g_A10_config_root.schedule->entries[S10_currentIndex];
			o["preset"] = e.preset;
			o["start_hour"] = e.start_hour;
			o["end_hour"] = e.end_hour;
		}
	}
};
