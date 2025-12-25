/*
 * ------------------------------------------------------
 * 소스명 : CT10_Control_IO_041.cpp
 * 모듈약어 : CT10
 * 모듈명 : Smart Nature Wind 제어 통합 Manager (v026, IO)
 * ------------------------------------------------------
 * 기능 요약:
 * - CT10 제어 상태 / 요약 / 메트릭 / 차트 JSON Export 구현
 * - sim JSON/차트는 S10 책임을 우선하고 CT10은 meta만 병합
 * ------------------------------------------------------
 */

#include "CT10_Control_040.h"

// --------------------------------------------------
// 내부 Helper: safe JsonObject 확보 (타입 꼬임 방지)
// --------------------------------------------------
static JsonObject CT10_ensureObject(JsonVariant p_v) {
	if (p_v.is<JsonObject>()) {
		return p_v.as<JsonObject>();
	}
	return p_v.to<JsonObject>();
}

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
	// control root
	JsonObject v_control = CT10_ensureObject(p_doc["control"]);
	v_control["active"] = active;
	v_control["useProfileMode"] = useProfileMode;
	v_control["runSource"] = (int)runSource;
	v_control["scheduleIdx"] = curScheduleIndex;
	v_control["profileIdx"] = curProfileIndex;

	// override
	JsonObject v_override = CT10_ensureObject(v_control["override"]);
	v_override["active"] = overrideState.active;
	v_override["useFixed"] = overrideState.useFixed;
	v_override["resolved"] = (!overrideState.useFixed && overrideState.active);
	v_override["remainSec"] = calcOverrideRemainSec();

	if (overrideState.active) {
		if (overrideState.useFixed) {
			v_override["fixedPercent"] = overrideState.fixedPercent;
		} else if (overrideState.resolved.valid) {
			// preset/style은 빈 문자열일 수 있으므로 그대로 Export (UI에서 처리 가능)
			v_override["presetCode"] = overrideState.resolved.presetCode;
			v_override["styleCode"] = overrideState.resolved.styleCode;
		}
	}

	// autoOff
	JsonObject v_autoOff = CT10_ensureObject(v_control["autoOff"]);
	v_autoOff["timerArmed"] = autoOffRt.timerArmed;
	v_autoOff["timerMinutes"] = autoOffRt.timerMinutes;
	v_autoOff["offTimeEnabled"] = autoOffRt.offTimeEnabled;
	v_autoOff["offTimeMinutes"] = autoOffRt.offTimeMinutes;
	v_autoOff["offTempEnabled"] = autoOffRt.offTempEnabled;
	v_autoOff["offTemp"] = autoOffRt.offTemp;

	// pwm
	v_control["pwmDuty"] = pwm ? pwm->P10_getDutyPercent() : 0.0f;

	// sim 상태(책임: S10) - S10이 p_doc 내부 sim 구조를 채움
	// CT10은 sim 호출 트리거 역할만 수행(구조/키는 S10에서 유지)
	sim.toJson(p_doc);
}

void CL_CT10_ControlManager::exportChartJson(JsonDocument& p_doc, bool p_diffOnly) {
	// 1) S10 차트 생성 (p_doc["sim"] 구조는 S10이 책임)
	sim.toChartJson(p_doc, p_diffOnly);

	// 2) CT10 메타 병합: p_doc["sim"]["meta"] 안전 확보
	JsonObject v_sim = CT10_ensureObject(p_doc["sim"]);
	JsonObject v_meta = CT10_ensureObject(v_sim["meta"]);

	v_meta["pwmDuty"] = pwm ? pwm->P10_getDutyPercent() : 0.0f;
	v_meta["active"] = active;
	v_meta["runSource"] = (int)runSource;
	v_meta["override"] = overrideState.active ? (overrideState.useFixed ? "fixed" : "resolved") : "none";
}

void CL_CT10_ControlManager::exportSummaryJson(JsonDocument& p_doc) {
	JsonObject v_sum = CT10_ensureObject(p_doc["summary"]);

	// phase index range 방어
	uint8_t v_phase = (uint8_t)sim.phase;
	if (v_phase >= (uint8_t)EN_A20_WEATHER_PHASE_COUNT) v_phase = 0;

	v_sum["phase"] = g_A20_WEATHER_PHASE_NAMES_Arr[v_phase];
	v_sum["wind"] = sim.currentWindSpeed;
	v_sum["target"] = sim.targetWindSpeed;
	v_sum["pwmDuty"] = pwm ? pwm->P10_getDutyPercent() : 0.0f;
	v_sum["override"] = overrideState.active;
	v_sum["useProfile"] = useProfileMode;
	v_sum["scheduleIdx"] = curScheduleIndex;
	v_sum["profileIdx"] = curProfileIndex;
}

void CL_CT10_ControlManager::exportMetricsJson(JsonDocument& p_doc) {
	JsonObject v_m = CT10_ensureObject(p_doc["metrics"]);

	v_m["active"] = active;
	v_m["runSource"] = (int)runSource;
	v_m["useProfileMode"] = useProfileMode;
	v_m["scheduleIdx"] = curScheduleIndex;
	v_m["profileIdx"] = curProfileIndex;

	v_m["overrideActive"] = overrideState.active;
	v_m["overrideFixed"] = overrideState.useFixed;
	v_m["overrideRemain"] = calcOverrideRemainSec();

	v_m["pwmDuty"] = pwm ? pwm->P10_getDutyPercent() : 0.0f;

	// sim metrics (phase 범위 방어)
	uint8_t v_phase = (uint8_t)sim.phase;
	if (v_phase >= (uint8_t)EN_A20_WEATHER_PHASE_COUNT) v_phase = 0;

	v_m["simActive"] = sim.active;
	v_m["simPhase"] = g_A20_WEATHER_PHASE_NAMES_Arr[v_phase];
	v_m["simWind"] = sim.currentWindSpeed;
	v_m["simTarget"] = sim.targetWindSpeed;
	v_m["simGust"] = sim.gustActive;
	v_m["simThermal"] = sim.thermalActive;

	// AutoOff metrics
	v_m["autoOffTimerArmed"] = autoOffRt.timerArmed;
	v_m["autoOffTimerMinutes"] = autoOffRt.timerMinutes;
	v_m["autoOffOffTime"] = autoOffRt.offTimeEnabled ? autoOffRt.offTimeMinutes : 0;
	v_m["autoOffOffTemp"] = autoOffRt.offTempEnabled ? autoOffRt.offTemp : 0.0f;
}
