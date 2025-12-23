/*
 * ------------------------------------------------------
 * 소스명 : CT10_Control_IO_030.cpp
 * 모듈약어 : CT10
 * 모듈명 : Smart Nature Wind 제어 통합 Manager (v026, IO)
 * ------------------------------------------------------
 * 기능 요약:
 * - CT10 제어 상태 / 요약 / 메트릭 / 차트 JSON Export 구현
 * - sim JSON/차트는 S10 책임을 우선하고 CT10은 meta만 병합
 * ------------------------------------------------------
 */

#include "CT10_Control_030.h"

// --------------------------------------------------
// 싱글톤
// --------------------------------------------------
CL_CT10_ControlManager& CL_CT10_ControlManager::instance() {
	static CL_CT10_ControlManager v_inst;
	return v_inst;
}

void CL_CT10_ControlManager::toJson(JsonDocument& p_doc) {
	instance().exportStateJson(p_doc);
}

void CL_CT10_ControlManager::toChartJson(JsonDocument& p_doc, bool p_diffOnly) {
	instance().exportChartJson(p_doc, p_diffOnly);
}

void CL_CT10_ControlManager::exportStateJson(JsonDocument& p_doc) {
	JsonObject v_control		= p_doc["control"].to<JsonObject>();
	v_control["active"]			= active;
	v_control["useProfileMode"] = useProfileMode;
	v_control["runSource"]		= (int)runSource;
	v_control["scheduleIdx"]	= curScheduleIndex;
	v_control["profileIdx"]		= curProfileIndex;

	// override
	JsonObject v_override		= v_control["override"].to<JsonObject>();
	v_override["active"]		= overrideState.active;
	v_override["useFixed"]		= overrideState.useFixed;
	v_override["resolved"]		= (!overrideState.useFixed && overrideState.active);
	v_override["remainSec"]		= calcOverrideRemainSec();

	if (overrideState.active) {
		if (overrideState.useFixed) {
			v_override["fixedPercent"] = overrideState.fixedPercent;
		} else if (overrideState.resolved.valid) {
			v_override["presetCode"] = overrideState.resolved.presetCode;
			v_override["styleCode"]	 = overrideState.resolved.styleCode;
		}
	}

	// autoOff
	JsonObject v_autoOff		= v_control["autoOff"].to<JsonObject>();
	v_autoOff["timerArmed"]		= autoOffRt.timerArmed;
	v_autoOff["timerMinutes"]	= autoOffRt.timerMinutes;
	v_autoOff["offTimeEnabled"] = autoOffRt.offTimeEnabled;
	v_autoOff["offTimeMinutes"] = autoOffRt.offTimeMinutes;
	v_autoOff["offTempEnabled"] = autoOffRt.offTempEnabled;
	v_autoOff["offTemp"]		= autoOffRt.offTemp;

	// pwm
	v_control["pwmDuty"]		= pwm ? pwm->P10_getDutyPercent() : 0.0f;

	// sim 상태(책임: S10) - S10이 p_doc["sim"] 내부를 채움
	sim.toJson(p_doc);
}

void CL_CT10_ControlManager::exportChartJson(JsonDocument& p_doc, bool p_diffOnly) {
	// 1) S10 차트 생성
	sim.toChartJson(p_doc, p_diffOnly);

	// 2) CT10 메타 병합
	JsonObject v_meta	= p_doc["sim"]["meta"].to<JsonObject>();
	v_meta["pwmDuty"]	= pwm ? pwm->P10_getDutyPercent() : 0.0f;
	v_meta["active"]	= active;
	v_meta["runSource"] = (int)runSource;
	v_meta["override"]	= overrideState.active ? (overrideState.useFixed ? "fixed" : "resolved") : "none";
}

void CL_CT10_ControlManager::exportSummaryJson(JsonDocument& p_doc) {
	JsonObject v_sum	 = p_doc["summary"].to<JsonObject>();
	v_sum["phase"]		 = g_A20_WEATHER_PHASE_NAMES_Arr[(uint8_t)sim.phase];
	v_sum["wind"]		 = sim.currentWindSpeed;
	v_sum["target"]		 = sim.targetWindSpeed;
	v_sum["pwmDuty"]	 = pwm ? pwm->P10_getDutyPercent() : 0.0f;
	v_sum["override"]	 = overrideState.active;
	v_sum["useProfile"]	 = useProfileMode;
	v_sum["scheduleIdx"] = curScheduleIndex;
	v_sum["profileIdx"]	 = curProfileIndex;
}

void CL_CT10_ControlManager::exportMetricsJson(JsonDocument& p_doc) {
	JsonObject v_m			   = p_doc["metrics"].to<JsonObject>();

	v_m["active"]			   = active;
	v_m["runSource"]		   = (int)runSource;
	v_m["useProfileMode"]	   = useProfileMode;
	v_m["scheduleIdx"]		   = curScheduleIndex;
	v_m["profileIdx"]		   = curProfileIndex;

	v_m["overrideActive"]	   = overrideState.active;
	v_m["overrideFixed"]	   = overrideState.useFixed;
	v_m["overrideRemain"]	   = calcOverrideRemainSec();

	v_m["pwmDuty"]			   = pwm ? pwm->P10_getDutyPercent() : 0.0f;

	// sim metrics
	v_m["simActive"]		   = sim.active;
	v_m["simPhase"]			   = g_A20_WEATHER_PHASE_NAMES_Arr[(uint8_t)sim.phase];
	v_m["simWind"]			   = sim.currentWindSpeed;
	v_m["simTarget"]		   = sim.targetWindSpeed;
	v_m["simGust"]			   = sim.gustActive;
	v_m["simThermal"]		   = sim.thermalActive;

	// AutoOff metrics
	v_m["autoOffTimerArmed"]   = autoOffRt.timerArmed;
	v_m["autoOffTimerMinutes"] = autoOffRt.timerMinutes;
	v_m["autoOffOffTime"]	   = autoOffRt.offTimeEnabled ? autoOffRt.offTimeMinutes : 0;
	v_m["autoOffOffTemp"]	   = autoOffRt.offTempEnabled ? autoOffRt.offTemp : 0.0f;
}
