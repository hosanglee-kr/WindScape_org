/*
 * ------------------------------------------------------
 * 소스명 : S10_Simul_Core_030.cpp
 * 모듈약어 : S10
 * 모듈명 : Smart Nature Wind 풍속 시뮬레이션 Manager (v019, Full)
 * ------------------------------------------------------
 * 기능 요약:
 * - CL_S10_Simulation Core 구현부
 *   - begin/stop/resetDefaults/tick/applyResolvedWind/applyFan
 * - tick 시간 캡처 일관성 유지(_tickNowMs/_tickNowSec)
 * - 주기 정책 상수는 S10_Simul_022.h로 승격되어 공용 사용
 * - fanConfig 포인터 스냅샷(락 내 1회 캡처) 기반 PWM 커브 적용
 * - 브로드캐스트는 락 밖에서 수행(JsonDocument 생성/A00_* 호출 금지 준수)
 * ------------------------------------------------------
 */

#include "S10_Simul_030.h"	// (변경 반영) 헤더 포함

// 외부 종속성 헤더 포함
#include "A20_Const_020.h"
#include "C10_Config_030.h"
#include "D10_Logger_020.h"
#include "P10_PWM_ctrl_020.h"

// ==================================================
// 초기화 / 정지 / 리셋
// ==================================================
/**
 * @brief 시뮬레이션 객체를 초기화하고 PWM 객체 레퍼런스를 설정합니다.
 * @param p_pwm PWM 제어 모듈 인스턴스
 */
void CL_S10_Simulation::begin(CL_P10_PWM& p_pwm) {
	_pwm		= &p_pwm;

	// fanConfig 스냅샷 초기화
	_fanCfgSnap = nullptr;

	resetDefaults();

	// 풍속 이력 버퍼(history) 초기화 및 인덱스 리셋 (평균 풍속 계산용)
	memset(history, 0, sizeof(history));
	historyIndex  = 0;
	historyCount  = 0;
	avgWindCached = 0.0f;

	// ESP32 HW RNG 호출(시드/지터 유도)
	(void)esp_random();

	CL_D10_Logger::log(EN_L10_LOG_INFO, "[S10] begin()");
}

/**
 * @brief 시뮬레이션을 정지하고 팬을 끕니다.
 */
void CL_S10_Simulation::stop() {
	active				= false;
	_fanCfgSnap			= nullptr;

	phase				= EN_A20_WEATHER_PHASE_CALM;
	targetWindSpeed		= 0.0f;
	currentWindSpeed	= 0.0f;

	gustActive			= false;
	gustIntensity		= 1.0f;
	thermalActive		= false;
	thermalContribution = 0.0f;

	if (_pwm) {
		_pwm->P10_setDutyPercent(0.0f);
	}
}

/**
 * @brief 모든 시뮬레이션 파라미터를 기본 Preset("OCEAN") 값으로 리셋합니다.
 */
void CL_S10_Simulation::resetDefaults() {
	active			= false;
	_fanCfgSnap		= nullptr;

	fanPowerEnabled = true;

	// 기본 Preset/Style
	memset(presetCode, 0, sizeof(presetCode));
	memset(styleCode, 0, sizeof(styleCode));
	strlcpy(presetCode, "OCEAN", sizeof(presetCode));
	strlcpy(styleCode, "BALANCE", sizeof(styleCode));

	// 사용자 설정
	userIntensity		= 70.0f;
	userVariability		= 50.0f;
	userGustFreq		= 45.0f;
	minFanPct			= 10.0f;
	fanLimitPct			= 90.0f;

	// 물리 파라미터
	turbLenScale		= 40.0f;
	turbSigma			= 0.5f;
	thermalStrength		= 2.0f;
	thermalRadius		= 18.0f;

	// Preset 기반(코어 적용 전에 기본값)
	baseMinWind			= 1.8f;
	baseMaxWind			= 5.5f;

	// [중요] 확률 의미 정규화
	// - gustProbBase / thermalFreqBase는 "초당 발생률(rate, 1/sec)"로 정의됨.
	// - Physic 구현부에서 dtSec 기반 확률(P=1-exp(-rate*dt))로 자동 보정해야 함.
	gustProbBase		= 0.040f;  // [1/sec]
	gustStrengthMax		= 2.10f;   // [-]
	thermalFreqBase		= 0.022f;  // [1/sec]

	// 현재 상태
	currentWindSpeed	= 3.6f;
	targetWindSpeed		= 3.6f;
	windMomentum		= 0.0f;

	// 난류/위상
	spectralEnergyBuf	= 0.0f;
	spectralPhaseAcc	= 0.0f;
	turbTimeScale		= 5.0f;

	// 이벤트
	gustActive			= false;
	gustIntensity		= 1.0f;
	thermalActive		= false;
	thermalContribution = 0.0f;

	// 시간 기준 초기화(캡처)
	_tickNowMs			= millis();
	_tickNowSec			= (float)_tickNowMs / 1000.0f;
	lastUpdateMs		= _tickNowMs;

	// 체크 주기 타임스탬프
	lastGustCheckMs		= 0;
	lastThermalCheckMs	= 0;

	// Preset 코어/Phase 초기화
	applyPresetCore(presetCode);
	initPhaseFromBase();
}

// ==================================================
// 메인 tick (CT10에서 주기 호출)
// ==================================================
/**
 * @brief 시뮬레이션의 모든 물리 및 상태를 업데이트하는 메인 함수.
 * 주기적으로 호출되어 풍속을 계산하고 PWM에 반영합니다.
 */
void CL_S10_Simulation::tick() {
	// ---- (A) 락 밖에서 브로드캐스트 수행을 위한 로컬 스냅샷 ----
	bool			  v_needBroadcast = false;
	float			  v_bc_avgWind	  = 0.0f;
	float			  v_bc_target	  = 0.0f;
	uint8_t			  v_bc_samples	  = 0;
	float			  v_bc_delta	  = 0.0f;
	T_A20_WindPhase_t v_bc_phase	  = EN_A20_WEATHER_PHASE_NORMAL;

	portENTER_CRITICAL(&_simMutex);

	// 1) 비활성 상태면 종료
	if (!active) {
		portEXIT_CRITICAL(&_simMutex);
		return;
	}

	// 2) fanConfig 스냅샷(락 내 1회 캡처)
	_fanCfgSnap = nullptr;
	if (g_A20_config_root.system != nullptr) {
		_fanCfgSnap = &g_A20_config_root.system->hw.fanConfig;
	}

	// 3) tick 기준 시간(딱 1회 캡처)
	_tickNowMs					   = millis();
	_tickNowSec					   = (float)_tickNowMs / 1000.0f;

	// 4) tick 업데이트 최소 간격(지터 포함)
	// - BASE + [0..RANGE-1]
	const uint32_t v_jitterMs	   = (G_S10_TICK_JITTER_RANGE_MS > 0u) ? (esp_random() % G_S10_TICK_JITTER_RANGE_MS) : 0u;
	const uint32_t v_minIntervalMs = G_S10_TICK_MIN_BASE_MS + v_jitterMs;

	if (_tickNowMs - lastUpdateMs < (unsigned long)v_minIntervalMs) {
		portEXIT_CRITICAL(&_simMutex);
		return;
	}

	// 5) dt 계산
	float v_dt							= (_tickNowMs - lastUpdateMs) / 1000.0f;
	v_dt								= A20_clampf(v_dt, 0.001f, 0.5f);
	lastUpdateMs						= _tickNowMs;

	// 6) 이전 상태 기록(변화 감지)
	const float				v_prevWind	= currentWindSpeed;
	const T_A20_WindPhase_t v_prevPhase = phase;

	// 7) Phase 업데이트
	updatePhase();

	// 8) 내부 물리 계산
	calcTurb(v_dt);
	calcThermalEnvelope();
	updateGust();
	updateThermal();

	// 9) 목표 풍속으로 수렴 + 관성
	const float v_diff	 = targetWindSpeed - currentWindSpeed;
	const float v_change = v_diff * windChangeRate * v_dt;

	windMomentum		 = windMomentum * 0.85f + v_change * 0.15f;
	windMomentum		 = constrain(windMomentum, -0.5f, 0.5f);

	// 10) 최종 풍속 업데이트(관성 + 난류)
	const float v_new	 = currentWindSpeed + windMomentum + spectralEnergyBuf;
	currentWindSpeed	 = constrain(v_new, 0.2f, 11.0f);

	// 11) 목표 재생성(확률)
	const float v_th	 = 0.5f + (currentWindSpeed / 20.0f);
	if (fabsf(v_diff) < v_th) {
		if (A20_randRange(0.0f, 100.0f) < 30.0f) {
			generateTarget();
		}
	} else {
		if (A20_randRange(0.0f, 100.0f) < 6.0f) {
			generateTarget();
		}
	}

	// 12) 풍속 -> PWM duty 변환 + 이벤트 영향
	float v_pwmPct = currentWindSpeed * 10.0f + 10.0f;
	v_pwmPct *= gustIntensity;
	v_pwmPct += thermalContribution * 5.0f;
	applyFan(v_pwmPct);

	// 13) history/avgWindCached 갱신
	_updateWindHistory(currentWindSpeed);

	// 14) 브로드캐스트 조건 판단(락 내에서는 값만 복사)
	const float v_delta = fabsf(currentWindSpeed - v_prevWind);
	if (phase != v_prevPhase || v_delta > 2.0f) {
		v_needBroadcast = true;
		v_bc_avgWind	= _getAvgWindFast();
		v_bc_target		= targetWindSpeed;
		v_bc_samples	= historyCount;
		v_bc_delta		= v_delta;
		v_bc_phase		= phase;
	}

	// 15) 차트 샘플링(1Hz / 이벤트 중 2Hz)
	const uint32_t v_chartIntervalMs = (gustActive || thermalActive) ? G_S10_CHART_HZ2_MS : G_S10_CHART_HZ1_MS;
	if (_tickNowMs - s_lastChartLogMs > (unsigned long)v_chartIntervalMs) {
		if (s_chartBuffer.size() >= 120) {
			s_chartBuffer.pop_front();
		}

		ST_ChartEntry v_e{};
		v_e.timestamp		 = _tickNowMs;
		v_e.wind_speed		 = currentWindSpeed;
		v_e.pwm_duty		 = _pwm ? _pwm->P10_getDutyPercent() : 0.0f;
		v_e.intensity		 = userIntensity;
		v_e.variability		 = userVariability;
		v_e.turbulence_sigma = turbSigma;
		v_e.preset_index	 = (uint8_t)A20_getPresetIndexByCode(presetCode);
		v_e.gust_active		 = gustActive;
		v_e.thermal_active	 = thermalActive;

		s_chartBuffer.push_back(v_e);
		s_lastChartLogMs = _tickNowMs;
	}

	portEXIT_CRITICAL(&_simMutex);

	// ---- (B) 락 밖에서 브로드캐스트 수행 ----
	if (v_needBroadcast) {
		JsonDocument v_doc;	 // JsonDocument 단일 타입 사용
		JsonObject	 v_sim = v_doc["sim"].to<JsonObject>();

		v_sim["phase"]	   = g_A20_WEATHER_PHASE_NAMES_Arr[(uint8_t)v_bc_phase];
		v_sim["avgWind"]   = v_bc_avgWind;
		v_sim["target"]	   = v_bc_target;
		v_sim["samples"]   = v_bc_samples;
		v_sim["delta"]	   = v_bc_delta;

		A00_broadcastChart(v_doc, true);
		A00_markDirty("chart");
	}
}

// ==================================================
// 해석 결과 적용: resolveWindParams → 여기 호출
// ==================================================
/**
 * @brief WindParam 해석 결과(ST_A20_ResolvedWind_t)를 시뮬레이션 파라미터에 적용합니다.
 */
void CL_S10_Simulation::applyResolvedWind(const ST_A20_ResolvedWind_t& p_resolved) {
	portENTER_CRITICAL(&_simMutex);

	// 시간 캡처(Phase 초기화 시 사용하는 _tickNowSec 일관성 보장)
	_tickNowMs	= millis();
	_tickNowSec = (float)_tickNowMs / 1000.0f;

	// fanConfig 스냅샷(락 내 1회 캡처)
	_fanCfgSnap = nullptr;
	if (g_A20_config_root.system != nullptr) {
		_fanCfgSnap = &g_A20_config_root.system->hw.fanConfig;
	}

	// preset/style 코드
	memset(presetCode, 0, sizeof(presetCode));
	memset(styleCode, 0, sizeof(styleCode));
	strlcpy(presetCode, p_resolved.presetCode, sizeof(presetCode));
	strlcpy(styleCode, p_resolved.styleCode, sizeof(styleCode));

	// 사용자 파라미터
	userIntensity	= constrain(p_resolved.wind_intensity, 0.0f, 100.0f);
	userVariability = constrain(p_resolved.wind_variability, 0.0f, 100.0f);
	userGustFreq	= constrain(p_resolved.gust_frequency, 0.0f, 100.0f);

	// min/limit 관계 보정(min <= limit)
	float v_limit	= constrain(p_resolved.fan_limit, 0.0f, 100.0f);
	float v_min		= constrain(p_resolved.min_fan, 0.0f, 100.0f);
	if (v_min > v_limit) {
		v_min = v_limit;
	}
	fanLimitPct		= v_limit;
	minFanPct		= v_min;

	// 물리 파라미터 최소값 보정
	turbLenScale	= max(1.0f, p_resolved.turbulence_length_scale);
	turbSigma		= max(0.0f, p_resolved.turbulence_intensity_sigma);
	thermalStrength = max(1.0f, p_resolved.thermal_bubble_strength);
	thermalRadius	= max(0.0f, p_resolved.thermal_bubble_radius);

	// Preset 코어 파라미터 재적용
	applyPresetCore(presetCode);

	// variability -> windChangeRate
	const float v_varNorm = userVariability / 100.0f;
	windChangeRate		  = constrain(0.10f + v_varNorm * 0.20f, 0.06f, 0.34f);

	// Phase/상태 초기화
	initPhaseFromBase();

	// 이벤트 리셋
	active				= true;
	gustActive			= false;
	thermalActive		= false;
	gustIntensity		= 1.0f;
	thermalContribution = 0.0f;

	// 새 목표 생성
	generateTarget();

	portEXIT_CRITICAL(&_simMutex);
}

/**
 * @brief 최종 계산된 풍속 기반 Duty Percent를 PWM 모듈에 적용합니다.
 * 사용자 Intensity, Min/Limit 값을 반영합니다.
 */
void CL_S10_Simulation::applyFan(float p_pct) {
	if (!_pwm) {
		return;
	}

	// 1) 요청 duty(%)를 0~1로 정규화
	float		v_req01 = A20_clampf(p_pct, 0.0f, 100.0f) / 100.0f;

	// 2) 사용자 intensity(0~1)
	const float v_int01 = A20_clampf(userIntensity, 0.0f, 100.0f) / 100.0f;

	// 3) 팬 전원 OFF 또는 intensity가 거의 0이면 정지
	if (!fanPowerEnabled || v_int01 <= 0.01f) {
		_pwm->P10_setDutyPercent(0.0f);
		return;
	}

	// 4) 시뮬레이션 active일 때만 intensity 스케일 적용
	if (active) {
		v_req01 *= v_int01;
	}

	// 5) min/limit(%) -> 0~1 변환 + 관계 보정(min <= limit)
	float v_min01 = A20_clampf(minFanPct, 0.0f, 100.0f) / 100.0f;
	float v_max01 = A20_clampf(fanLimitPct, 0.0f, 100.0f) / 100.0f;

	if (v_min01 > v_max01) {
		v_min01 = v_max01;
	}

	// 6) 커브 적용: 논리 duty(0~1) -> 실제 PWM duty(0~1)
	const ST_A20_FanConfig_t* v_fc	  = _fanCfgSnap;
	const float				  v_phy01 = _pwm->applyFanConfigCurve(v_fc, v_req01, v_min01, v_max01);

	// 7) 최종 %로 전달
	_pwm->P10_setDutyPercent(v_phy01 * 100.0f);
}
