#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : S10_Simulation_017.h
 * 모듈약어 : S10
 * 모듈명 : Smart Nature Wind Simulation Engine (v017)
 * ------------------------------------------------------
 * 기능 요약
 *  - WindProfileDict 기반 자연풍 파라미터 해석 결과(ResolvedWind) 수신
 *  - 난류(turbulence) + 거스트(gust) + 열기포(thermal bubble) + 저주파 변동 구현
 *  - 실제 PWM 제어(P10)에 전달될 순간 풍속값 계산
 *  - millis() 기반 시간적 변동 → update() 주기적 호출로 반영
 *  - random(), sin(), noise 기반 미세변동
 * ------------------------------------------------------
 * [구현 규칙]
 *  - ArduinoJson v7.x.x 사용 시 JsonDocument 단일 타입만 사용
 *  - createNestedArray/Object/containsKey 사용 금지
 *  - memset + strlcpy 기반 초기화
 *  - cpp 분리 없음 (단일 헤더)
 *  - P10_PWM과 직접 연동 (S10_applyResolvedWind, S10_update)
 * ------------------------------------------------------
 * [코드 네이밍 규칙]
 *   - 전역 상수,매크로      : G_S10_ 접두사
 *   - 전역 변수             : g_S10_ 접두사
 *   - 클래스명              : CL_S10_Simulation
 *   - 클래스 private 멤버   : _ 접두사
 *   - 함수 인자             : p_ 접두사
 *   - 로컬 변수             : v_ 접두사
 * ------------------------------------------------------
 */

#include <Arduino.h>
#include <math.h>
#include "A10_Const_014.h"
#include "D10_Logger_011.h"
#include "P10_PWM_ctrl_016.h"

#define G_S10_DEFAULT_UPDATE_MS 200   // 바람 계산 주기 (5Hz)
#define G_S10_GUST_FREQ_BASE    0.25f // 초당 최대 거스트 발생 빈도
#define G_S10_TURB_NOISE_SCALE  0.02f // 기본 노이즈 변동폭

// ------------------------------------------------------
// 상태 구조체
// ------------------------------------------------------
typedef struct {
	bool		active			= false;
	float		curDutyPercent	= 0.0f;

	unsigned long lastUpdateMs	= 0;
	unsigned long startMs		= 0;

	// 바람 파라미터
	ST_A10_ResolvedWind_t wind;

	// 내부 난류 요소
	float gustPhase			= 0.0f;
	float gustFreqHz			= 0.0f;
	float turbulenceValue		= 0.0f;
	float thermalStrength		= 0.0f;

	// 캐싱
	float baseIntensity			= 0.0f;
	float variability			= 0.0f;
	float gustAmp				= 0.0f;
} ST_S10_state_t;

// ------------------------------------------------------
// S10 시뮬레이터 클래스
// ------------------------------------------------------
class CL_S10_Simulation {
public:
	bool active = false;

	// --------------------------------------------------
	// 초기화
	// --------------------------------------------------
	void begin(CL_P10_PWM& p_pwm) {
		_pwm = &p_pwm;
		memset(&_state, 0, sizeof(_state));
		active = true;
		CL_D10_Logger::log(EN_L10_LOG_INFO, "[S10] Simulation begin");
	}

	// --------------------------------------------------
	// 시뮬레이션 시작 (ResolvedWind 적용)
	// --------------------------------------------------
	void S10_applyResolvedWind(const ST_A10_ResolvedWind_t& p_wind,
							   CL_P10_PWM& p_pwm) {
		_pwm = &p_pwm;
		_state.active = true;
		_state.wind = p_wind;
		_state.startMs = millis();
		_state.lastUpdateMs = 0;

		// 캐시 파라미터 계산
		_state.baseIntensity = constrain(p_wind.wind_intensity, 0.0f, 100.0f);
		_state.variability   = constrain(p_wind.wind_variability, 0.0f, 100.0f);
		_state.gustAmp       = p_wind.gust_frequency / 100.0f;

		_state.gustFreqHz = G_S10_GUST_FREQ_BASE * (_state.gustAmp + 0.3f);
		_state.turbulenceValue = random(-500, 500) / 500.0f;
		_state.thermalStrength = p_wind.thermal_bubble_strength;

		CL_D10_Logger::log(EN_L10_LOG_INFO,
			"[S10] applyResolvedWind I=%.1f V=%.1f G=%.1f F=%.1f",
			(double)p_wind.wind_intensity, (double)p_wind.wind_variability,
			(double)p_wind.gust_frequency, (double)p_wind.fan_limit);
	}

	// --------------------------------------------------
	// 시뮬레이션 중지
	// --------------------------------------------------
	void S10_stop() {
		if (!_pwm) return;
		_state.active = false;
		_pwm->P10_setDutyPercent(0.0f);
	}

	// --------------------------------------------------
	// 주기적 업데이트 (loop에서 호출)
	// --------------------------------------------------
	void S10_update() {
		if (!_state.active || !_pwm) return;

		unsigned long v_now = millis();
		if (v_now - _state.lastUpdateMs < G_S10_DEFAULT_UPDATE_MS) return;
		_state.lastUpdateMs = v_now;

		float v_elapsed = (v_now - _state.startMs) / 1000.0f;
		float v_wave = _calcWave(v_elapsed);
		float v_turb = _calcTurbulence(v_elapsed);
		float v_bubble = _calcThermalBubble(v_elapsed);

		// 기본 세기
		float v_base = _state.baseIntensity / 100.0f;

		// 복합 변동 조합
		float v_factor = 1.0f
			+ (v_wave * _state.variability / 100.0f)
			+ (v_turb * 0.5f)
			+ (v_bubble * 0.3f);

		float v_duty = v_base * v_factor * (_state.wind.fan_limit / 100.0f);
		v_duty = constrain(v_duty, _state.wind.min_fan / 100.0f, 1.0f);

		_state.curDutyPercent = v_duty * 100.0f;
		_pwm->P10_setDutyPercent(_state.curDutyPercent);
	}

	// --------------------------------------------------
	// 현재 풍속(%) 조회
	// --------------------------------------------------
	float S10_getCurrentDutyPercent() const {
		return _state.curDutyPercent;
	}

	// --------------------------------------------------
	// 현재 wind 정보 조회
	// --------------------------------------------------
	ST_A10_ResolvedWind_t S10_getCurrentWind() const {
		return _state.wind;
	}

	// --------------------------------------------------
	// 활성 상태
	// --------------------------------------------------
	bool S10_isActive() const { return _state.active; }

private:
	CL_P10_PWM* _pwm = nullptr;
	ST_S10_state_t _state;

	// --------------------------------------------------
	// 내부: 저주파 파동 (gust)
	// --------------------------------------------------
	float _calcWave(float p_timeSec) {
		// 기본 사인파 기반 거스트
		_state.gustPhase += _state.gustFreqHz * G_S10_DEFAULT_UPDATE_MS / 1000.0f;
		if (_state.gustPhase > TWO_PI) _state.gustPhase -= TWO_PI;
		float v_wave = sinf(_state.gustPhase);
		return v_wave * _state.gustAmp;
	}

	// --------------------------------------------------
	// 내부: 난류 (pseudo-random flicker)
	// --------------------------------------------------
	float _calcTurbulence(float p_timeSec) {
		float v_n = random(-1000, 1000) / 1000.0f;
		v_n *= G_S10_TURB_NOISE_SCALE * (_state.wind.turbulence_intensity_sigma);
		return v_n;
	}

	// --------------------------------------------------
	// 내부: 열기포 (thermal bubble)
	// --------------------------------------------------
	float _calcThermalBubble(float p_timeSec) {
		float v_period = max(5.0f, _state.wind.thermal_bubble_radius);
		float v_strength = _state.wind.thermal_bubble_strength;
		float v_val = sinf((p_timeSec / v_period) * TWO_PI) * v_strength * 0.1f;
		return v_val;
	}
};

// ------------------------------------------------------
// 전역 인스턴스 (옵션)
// ------------------------------------------------------
inline CL_S10_Simulation g_S10_simulator;
