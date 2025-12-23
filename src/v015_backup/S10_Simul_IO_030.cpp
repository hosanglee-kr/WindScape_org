/*
 * ------------------------------------------------------
 * 소스명 : S10_Simul_IO_030.cpp
 * 모듈약어 : S10
 * 모듈명 : Smart Nature Wind 풍속 시뮬레이션 Manager (v022, Full)
 * ------------------------------------------------------
 * 기능 요약:
 * - CL_S10_Simulation 클래스의 IO/직렬화/패치 구현부
 * - 현재 시뮬레이션 상태(toJson) 및 차트 버퍼(toChartJson) JSON 직렬화
 * - patchFromJson()로 시뮬레이션 파라미터 부분 업데이트(패치) 지원
 * - v022 정책 반영:
 *    - Header(S10_Simul_030.h) 사용
 *    - 차트 전송 간격: G_S10_CHART_FULL_MIN_MS 정책 상수 사용
 *    - 시간 기준: _tickNowMs 우선(0이면 millis() 1회 fallback)
 *    - JSON Key: camelCase 로 통일
 * ------------------------------------------------------
 * [구현 규칙]
 * - 항상 소스 시작 주석 체계 유지 및 내용 업데이트
 * - ArduinoJson v7.x.x 사용 (v6 이하 사용 금지)
 * - JsonDocument 단일 타입만 사용
 * - createNestedArray/Object/containsKey 사용 금지
 * - memset + strlcpy 기반 안전 초기화
 * - 주석/필드명은 JSON 구조와 동일하게 유지
 * ------------------------------------------------------
 * [코드 네이밍 규칙]
 * - 전역 상수,매크로      : G_모듈약어_ prefix
 * - 전역 변수             : g_모듈약어_ prefix
 * - 전역 함수             : 모듈약어_ prefix
 * - Types                 : T_모듈약어_ prefix
 * - Typedefs              : _t suffix
 * - Enum constants        : EN_모듈약어_ prefix
 * - Structs               : ST_모듈약어_ prefix
 * - Classes               : CL_모듈약어_ prefix
 * - Private class members : _ prefix
 * - Class members         : (functions/variables) no module prefix
 * - Static class members  : s_ prefix
 * - Function local vars   : v_ prefix
 * - Function arguments    : p_ prefix
 * ------------------------------------------------------
 */
#include <vector>

#include "S10_Simul_030.h"

// 외부 종속성 헤더 포함 (외부에서 제공되어야 함: 시스템 상수, 설정, 로그, PWM 제어)
#include "A20_Const_020.h"
#include "C10_Config_030.h"
#include "D10_Logger_020.h"
#include "P10_PWM_ctrl_020.h"

// ------------------------------------------------------
// 정적 멤버 정의 (클래스 인스턴스와 무관하게 유지되는 공유 데이터)
// ------------------------------------------------------
std::deque<CL_S10_Simulation::ST_ChartEntry> CL_S10_Simulation::s_chartBuffer;
unsigned long								 CL_S10_Simulation::s_lastChartLogMs	= 0;
unsigned long								 CL_S10_Simulation::s_lastChartSampleMs = 0;

// ==================================================
// JSON Export (현재 시뮬레이션 상태)
// ==================================================
/**
 * @brief 현재 시뮬레이션 상태 변수들을 JSON Object에 직렬화합니다.
 * JSON Key는 camelCase로 통일합니다.
 *
 * 출력 예:
 * {
 *   "sim": {
 *     "active": true,
 *     "fanPowerEnabled": true,
 *     "phase": "NORMAL",
 *     "windSpeed": 3.2,
 *     "targetWind": 3.6,
 *     "gustActive": false,
 *     "thermalActive": false,
 *     "pwmDuty": 42.0,
 *     "presetCode": "OCEAN",
 *     "styleCode": "BALANCE",
 *     "intensity": 70,
 *     "variability": 50,
 *     "gustFreq": 45,
 *     "fanLimit": 90,
 *     "minFan": 10,
 *     "turbSigma": 0.5,
 *     "turbLenScale": 40,
 *     "thermalStrength": 2.0,
 *     "thermalRadius": 18
 *   }
 * }
 */
void CL_S10_Simulation::toJson(JsonDocument& p_doc) {
	// 상태 읽기 중 변수 변경 방지
	portENTER_CRITICAL(&_simMutex);

	JsonObject v_objSim			= p_doc["sim"].to<JsonObject>();

	v_objSim["active"]			= active;
	v_objSim["fanPowerEnabled"] = fanPowerEnabled;

	v_objSim["phase"]			= g_A20_WEATHER_PHASE_NAMES_Arr[(uint8_t)phase];
	v_objSim["windSpeed"]		= currentWindSpeed;
	v_objSim["targetWind"]		= targetWindSpeed;

	v_objSim["gustActive"]		= gustActive;
	v_objSim["thermalActive"]	= thermalActive;

	v_objSim["pwmDuty"]			= _pwm ? _pwm->P10_getDutyPercent() : 0.0f;

	v_objSim["presetCode"]		= presetCode;
	v_objSim["styleCode"]		= styleCode;

	v_objSim["intensity"]		= userIntensity;
	v_objSim["variability"]		= userVariability;
	v_objSim["gustFreq"]		= userGustFreq;

	v_objSim["fanLimit"]		= fanLimitPct;
	v_objSim["minFan"]			= minFanPct;

	v_objSim["turbSigma"]		= turbSigma;
	v_objSim["turbLenScale"]	= turbLenScale;

	v_objSim["thermalStrength"] = thermalStrength;
	v_objSim["thermalRadius"]	= thermalRadius;

	portEXIT_CRITICAL(&_simMutex);
}

// ==================================================
// 차트 데이터 JSON Export (/api/sim/chart)
// ==================================================
/**
 * @brief 차트 버퍼(s_chartBuffer)의 내용을 JSON Array로 직렬화합니다.
 * @param p_doc JSON 문서
 * @param p_diffOnly true인 경우, 마지막 1개 샘플만 전송 (WebSocket/REST diff 전송)
 *
 * JSON Key는 camelCase로 통일합니다.
 * {
 *   "sim": {
 *     "meta": {
 *        "phase": "NORMAL",
 *        "avgWind": 3.1,
 *        "gustActive": false,
 *        "thermalActive": false,
 *        "samples": 60
 *     },
 *     "chart": [
 *        {"ts":123, "wind":3.2, "pwm":42.0, "gustActive":false, "thermalActive":false},
 *        ...
 *     ],
 *     "chartCount": 120
 *   }
 * }
 */
void CL_S10_Simulation::toChartJson(JsonDocument& p_doc, bool p_diffOnly) {
	// 시간 기준: _tickNowMs 우선, 0이면 millis() 1회 fallback
	unsigned long v_nowMs = _tickNowMs;
	if (v_nowMs == 0UL) {
		v_nowMs = millis();
	}

	// ---- (A) 메타/샘플 스냅샷(락 안에서 값만 캡처) ----
	T_A20_WindPhase_t		   v_phase = EN_A20_WEATHER_PHASE_NORMAL;
	float					   v_avg   = 0.0f;
	bool					   v_gust  = false;
	bool					   v_therm = false;
	uint8_t					   v_samp  = 0;

	// chart entries 스냅샷
	std::vector<ST_ChartEntry> v_entries;
	v_entries.clear();

	portENTER_CRITICAL(&_simMutex);

	v_phase = phase;
	v_avg	= _getAvgWindFast();
	v_gust	= gustActive;
	v_therm = thermalActive;
	v_samp	= historyCount;

	// Full Dump 전송 간격 제한: diffOnly는 제한하지 않음
	if (!p_diffOnly) {
		const unsigned long v_elapsedMs = (v_nowMs >= s_lastChartSampleMs) ? (v_nowMs - s_lastChartSampleMs) : 0UL;
		if (v_elapsedMs < G_S10_CHART_FULL_MIN_MS) {
			portEXIT_CRITICAL(&_simMutex);
			return;
		}
		s_lastChartSampleMs = v_nowMs;
	}

	if (s_chartBuffer.empty()) {
		portEXIT_CRITICAL(&_simMutex);
		return;
	}

	if (p_diffOnly) {
		v_entries.reserve(1);
		v_entries.push_back(s_chartBuffer.back());
	} else {
		v_entries.reserve((size_t)s_chartBuffer.size());
		for (const auto& v_e : s_chartBuffer) {
			v_entries.push_back(v_e);
		}
	}

	portEXIT_CRITICAL(&_simMutex);

	// ---- (B) 락 밖에서 JSON 생성 ----
	JsonObject v_objSim		= p_doc["sim"].to<JsonObject>();

	JsonObject v_meta		= v_objSim["meta"].to<JsonObject>();
	v_meta["phase"]			= g_A20_WEATHER_PHASE_NAMES_Arr[(uint8_t)v_phase];
	v_meta["avgWind"]		= v_avg;
	v_meta["gustActive"]	= v_gust;
	v_meta["thermalActive"] = v_therm;
	v_meta["samples"]		= v_samp;

	JsonArray v_arr			= v_objSim["chart"].to<JsonArray>();

	// diffOnly: 마지막 1개만 전송
	if (p_diffOnly) {
		const ST_ChartEntry& v_e  = v_entries[0];
		JsonObject			 v_jo = v_arr.add<JsonObject>();
		v_jo["ts"]				  = (uint32_t)(v_e.timestamp / 1000UL);
		v_jo["wind"]			  = v_e.wind_speed;
		v_jo["pwm"]				  = v_e.pwm_duty;
		v_jo["gustActive"]		  = v_e.gust_active;
		v_jo["thermalActive"]	  = v_e.thermal_active;

		v_objSim["chartCount"]	  = 1;
		return;
	}

	// Full Dump: 전체 전송
	for (size_t v_i = 0; v_i < v_entries.size(); v_i++) {
		const ST_ChartEntry& v_e  = v_entries[v_i];
		JsonObject			 v_jo = v_arr.add<JsonObject>();
		v_jo["ts"]				  = (uint32_t)(v_e.timestamp / 1000UL);
		v_jo["wind"]			  = v_e.wind_speed;
		v_jo["pwm"]				  = v_e.pwm_duty;
		v_jo["gustActive"]		  = v_e.gust_active;
		v_jo["thermalActive"]	  = v_e.thermal_active;
	}

	v_objSim["chartCount"] = (int)v_entries.size();
}

// ==================================================
// Patch From JSON (부분 업데이트)
// ==================================================
/**
 * @brief 주어진 JSON 문서로부터 시뮬레이션 설정(사용자 파라미터/물리 파라미터)을 패치합니다.
 * @param p_doc 시뮬레이션 설정 업데이트 데이터가 포함된 JsonDocument (ArduinoJson v7.x)
 * @return 설정이 변경되었으면 true, 아니면 false
 *
 * JSON Key는 camelCase로 통일합니다.
 * 입력 예:
 * {
 *   "sim": {
 *     "presetCode":"OCEAN",
 *     "styleCode":"BALANCE",
 *     "fanPowerEnabled":true,
 *     "intensity":70,
 *     "variability":50,
 *     "gustFreq":45,
 *     "fanLimit":90,
 *     "minFan":10,
 *     "turbLenScale":40,
 *     "turbSigma":0.5,
 *     "thermalStrength":2.0,
 *     "thermalRadius":18
 *   }
 * }
 */
bool CL_S10_Simulation::patchFromJson(const JsonDocument& p_doc) {
	bool v_changed			 = false;
	bool v_needPresetReapply = false;
	bool v_needPhaseReset	 = false;

	portENTER_CRITICAL(&_simMutex);

	JsonObjectConst v_sim = p_doc["sim"].as<JsonObjectConst>();
	if (v_sim.isNull()) {
		CL_D10_Logger::log(EN_L10_LOG_ERROR, "[S10] patchFromJson failed: 'sim' object not found.");
		portEXIT_CRITICAL(&_simMutex);
		return false;
	}

	// presetCode
	if (!v_sim["presetCode"].isNull() && v_sim["presetCode"].is<const char*>()) {
		const char* v_new = v_sim["presetCode"].as<const char*>();
		if (v_new && v_new[0] && strcasecmp(v_new, presetCode) != 0) {
			memset(presetCode, 0, sizeof(presetCode));
			strlcpy(presetCode, v_new, sizeof(presetCode));
			v_changed			= true;
			v_needPresetReapply = true;
			v_needPhaseReset	= true;
		}
	}

	// styleCode
	if (!v_sim["styleCode"].isNull() && v_sim["styleCode"].is<const char*>()) {
		const char* v_new = v_sim["styleCode"].as<const char*>();
		if (v_new && v_new[0] && strcasecmp(v_new, styleCode) != 0) {
			memset(styleCode, 0, sizeof(styleCode));
			strlcpy(styleCode, v_new, sizeof(styleCode));
			v_changed = true;
		}
	}

	// fanPowerEnabled
	if (!v_sim["fanPowerEnabled"].isNull()) {
		const bool v_new = v_sim["fanPowerEnabled"].as<bool>();
		if (v_new != fanPowerEnabled) {
			fanPowerEnabled = v_new;
			v_changed		= true;
		}
	}

	// intensity
	if (!v_sim["intensity"].isNull()) {
		const float v_new = constrain(v_sim["intensity"].as<float>(), 0.0f, 100.0f);
		if (v_new != userIntensity) {
			userIntensity = v_new;
			v_changed	  = true;
		}
	}

	// variability
	if (!v_sim["variability"].isNull()) {
		const float v_new = constrain(v_sim["variability"].as<float>(), 0.0f, 100.0f);
		if (v_new != userVariability) {
			userVariability		  = v_new;
			// variability 변경 시 windChangeRate 즉시 갱신(정책은 Physic/generateTarget에서도 갱신)
			const float v_varNorm = userVariability / 100.0f;
			windChangeRate		  = constrain(0.10f + v_varNorm * 0.20f, 0.06f, 0.34f);
			v_changed			  = true;
		}
	}

	// gustFreq
	if (!v_sim["gustFreq"].isNull()) {
		const float v_new = constrain(v_sim["gustFreq"].as<float>(), 0.0f, 100.0f);
		if (v_new != userGustFreq) {
			userGustFreq = v_new;
			v_changed	 = true;
		}
	}

	// fanLimit / minFan (관계 보정 포함)
	bool  v_minOrLimitChanged = false;
	float v_limit			  = fanLimitPct;
	float v_min				  = minFanPct;

	if (!v_sim["fanLimit"].isNull()) {
		const float v_new = constrain(v_sim["fanLimit"].as<float>(), 0.0f, 100.0f);
		if (v_new != v_limit) {
			v_limit				= v_new;
			v_minOrLimitChanged = true;
		}
	}

	if (!v_sim["minFan"].isNull()) {
		const float v_new = constrain(v_sim["minFan"].as<float>(), 0.0f, 100.0f);
		if (v_new != v_min) {
			v_min				= v_new;
			v_minOrLimitChanged = true;
		}
	}

	if (v_minOrLimitChanged) {
		if (v_min > v_limit) {
			v_min = v_limit;  // 정책: min이 limit를 넘으면 min을 limit에 맞춘다
		}
		if (v_limit != fanLimitPct || v_min != minFanPct) {
			fanLimitPct = v_limit;
			minFanPct	= v_min;
			v_changed	= true;
		}
	}

	// turbLenScale
	if (!v_sim["turbLenScale"].isNull()) {
		const float v_new = max(1.0f, v_sim["turbLenScale"].as<float>());
		if (v_new != turbLenScale) {
			turbLenScale = v_new;
			v_changed	 = true;
		}
	}

	// turbSigma
	if (!v_sim["turbSigma"].isNull()) {
		const float v_new = max(0.0f, v_sim["turbSigma"].as<float>());
		if (v_new != turbSigma) {
			turbSigma = v_new;
			v_changed = true;
		}
	}

	// thermalStrength
	if (!v_sim["thermalStrength"].isNull()) {
		const float v_new = max(1.0f, v_sim["thermalStrength"].as<float>());
		if (v_new != thermalStrength) {
			thermalStrength = v_new;
			v_changed		= true;
		}
	}

	// thermalRadius
	if (!v_sim["thermalRadius"].isNull()) {
		const float v_new = max(0.0f, v_sim["thermalRadius"].as<float>());
		if (v_new != thermalRadius) {
			thermalRadius = v_new;
			v_changed	  = true;
		}
	}

	// preset 변경 시 core 재적용 + phase 재설정
	if (v_needPresetReapply) {
		applyPresetCore(presetCode);
	}
	if (v_needPhaseReset) {
		// tick 스냅샷 시간 확보 (patch가 tick 외부에서 호출될 수 있음)
		if (_tickNowMs == 0UL) {
			_tickNowMs	= millis();
			_tickNowSec = (float)_tickNowMs / 1000.0f;
		}
		initPhaseFromBase();
	}

	// 변경 시 로그
	if (v_changed) {
		CL_D10_Logger::log(EN_L10_LOG_INFO, "[S10] patchFromJson applied. preset=%s style=%s intensity=%.1f var=%.1f gust=%.1f fanLimit=%.1f minFan=%.1f turbSigma=%.2f", presetCode, styleCode, userIntensity, userVariability, userGustFreq, fanLimitPct, minFanPct, turbSigma);
	}

	portEXIT_CRITICAL(&_simMutex);
	return v_changed;
}

// --------------------------------------------------
// 최근 풍속 이력 관리 (순환 버퍼 기반)
// --------------------------------------------------
/**
 * @brief 현재 풍속을 순환 버퍼(history)에 저장하고 평균 풍속 캐시를 갱신합니다.
 */
void CL_S10_Simulation::_updateWindHistory(float p_speed) {
	history[historyIndex] = p_speed;
	historyIndex		  = (uint8_t)((historyIndex + 1u) % HISTORY_SIZE);
	if (historyCount < HISTORY_SIZE) {
		historyCount++;
	}

	float v_sum = 0.0f;
	for (uint8_t v_i = 0; v_i < historyCount; v_i++) {
		v_sum += history[v_i];
	}
	avgWindCached = (historyCount > 0) ? (v_sum / (float)historyCount) : p_speed;
}

// --------------------------------------------------
// 캐시된 평균 풍속 반환 (O(1))
// --------------------------------------------------
/**
 * @brief 캐시된 평균 풍속을 반환합니다. (O(1) 접근)
 */
float CL_S10_Simulation::_getAvgWindFast() const {
	return (historyCount > 0) ? avgWindCached : currentWindSpeed;
}
