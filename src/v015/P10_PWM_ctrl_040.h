#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : P10_PWM_ctrl_040.h
 * 모듈약어 : P10
 * 모듈명 : Smart Nature Wind PWM 제어 Manager (v014)
 * ------------------------------------------------------
 * 기능 요약
 *  - cfg_system_xxx.json 의 hw.fanPwm 설정 기반 PWM 초기화
 *  - ESP32 LEDC 하드웨어 PWM 래핑 (pin/channel/freq/resolution)
 *  - 0~100% 듀티 제어 (실제 레졸루션 스케일링)
 *  - 현재 듀티 조회 및 enable/disable 관리
 *  - 헤더 단일(h) 구성, 외부 모듈(S10/CT10)에서 사용
 * ------------------------------------------------------
 * [구현 규칙]
 *  - 항상 소스 시작 주석 부분 체계 유지 및 내용 업데이트
 *  - 소스 시작 주석 부분 구현규칙, 코드네이밍규칙 내용 그대로 유지, 수정금지
 *  - ArduinoJson v7.x.x 사용 (v6 이하 사용 금지)
 *  - JsonDocument 단일 타입만 사용
 *  - createNestedArray/Object/containsKey 사용 금지
 *  - memset + strlcpy 기반 안전 초기화
 *  - 주석/필드명은 JSON 구조와 동일하게 유지
 *  - 모듈별 단일 헤더(h)파일로만 구성 (cpp 없음)
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
 * ------------------
 */

#include <Arduino.h>
#include <string.h>

#include "A20_Const_040.h"
#include "D10_Logger_040.h"

// ------------------------------------------------------
// 내부 상태 구조체
// ------------------------------------------------------
typedef struct {
	bool	 initialized;
	bool	 enabled;
	int16_t	 pin;
	uint8_t	 channel;
	uint32_t freq;
	uint8_t	 resolutionBits;
	uint32_t maxDuty;	   // (1<<resolutionBits)-1
	float	 dutyPercent;  // 0.0 ~ 100.0
} ST_P10_PWMState_t;

// ------------------------------------------------------
// CL_P10_PWM
// ------------------------------------------------------
class CL_P10_PWM {
  public:
	CL_P10_PWM() {
		memset(&_state, 0, sizeof(_state));
		_state.pin			  = -1;
		_state.channel		  = 0;
		_state.freq			  = 0;
		_state.resolutionBits = 0;
		_state.maxDuty		  = 0;
		_state.dutyPercent	  = 0.0f;
		_state.initialized	  = false;
		_state.enabled		  = false;
	}

	// ==================================================
	// 초기화
	//  - cfg.system.hw.fanPwm 기반
	// ==================================================
	void begin(const ST_A20_SystemConfig_t& p_cfg) {
		memset(&_state, 0, sizeof(_state));
		_state.pin			  = p_cfg.hw.fanPwm.pin;
		_state.channel		  = (uint8_t)p_cfg.hw.fanPwm.channel;
		_state.freq			  = p_cfg.hw.fanPwm.freq;
		_state.resolutionBits = (uint8_t)p_cfg.hw.fanPwm.res;
		_state.dutyPercent	  = 0.0f;
		_state.enabled		  = true;
		_state.initialized	  = false;

		if (_state.pin < 0) {
			CL_D10_Logger::log(EN_L10_LOG_ERROR, "[P10] invalid fan pin=%d", (int)_state.pin);
			return;
		}

		// LEDC 설정
		bool v_ok1 = ledcSetup(_state.channel, _state.freq, _state.resolutionBits);
		if (!v_ok1) {
			CL_D10_Logger::log(EN_L10_LOG_ERROR, "[P10] ledcSetup failed ch=%u freq=%lu res=%u", (unsigned)_state.channel, (unsigned long)_state.freq, (unsigned)_state.resolutionBits);
			return;
		}
		ledcAttachPin(_state.pin, _state.channel);

		if (_state.resolutionBits >= 1 && _state.resolutionBits <= 20) {
			_state.maxDuty = (1UL << _state.resolutionBits) - 1UL;
		} else {
			_state.maxDuty = 1023;	// fallback
		}

		_setRawDuty(0);
		_state.initialized = true;

		CL_D10_Logger::log(EN_L10_LOG_INFO, "[P10] begin pin=%d ch=%u freq=%lu res=%u", (int)_state.pin, (unsigned)_state.channel, (unsigned long)_state.freq, (unsigned)_state.resolutionBits);
	}

	// ==================================================
	// Enable / Disable
	// ==================================================
	void setEnabled(bool p_enabled) {
		_state.enabled = p_enabled;
		if (!_state.initialized)
			return;

		if (!p_enabled) {
			_state.dutyPercent = 0.0f;
			_setRawDuty(0);
		}
	}

	bool isEnabled() const {
		return _state.enabled;
	}

	bool isInitialized() const {
		return _state.initialized;
	}

	float applyFanConfigCurve(const ST_A20_FanConfig_t* p_cfg, float p_req01, float p_minFan01, float p_maxFan01) {
		// 0~1 범위 방어
		float v_req = A20_clampf(p_req01, 0.0f, 1.0f);

		// 완전 정지 요청인 경우는 그냥 0으로 내보냄
		if (v_req <= 0.0f) {
			return 0.0f;
		}

		// ResolvedWind min/max 먼저 정리
		float v_min = A20_clampf(p_minFan01, 0.0f, 1.0f);
		float v_max = A20_clampf(p_maxFan01, 0.0f, 1.0f);
		if (v_max < v_min) {
			v_max = v_min;
		}

		// fanConfig 없으면 그냥 min/max만 적용해서 반환
		if (!p_cfg) {
			float v_out = v_req;
			if (v_out < v_min)
				v_out = v_min;
			if (v_out > v_max)
				v_out = v_max;
			return v_out;
		}

		// fanConfig 값을 0~1로 정규화
		float s	 = A20_clampf(p_cfg->startPercentMin / 100.0f, 0.0f, 1.0f);
		float c1 = A20_clampf(p_cfg->comfortPercentMin / 100.0f, 0.0f, 1.0f);
		float c2 = A20_clampf(p_cfg->comfortPercentMax / 100.0f, 0.0f, 1.0f);
		float h	 = A20_clampf(p_cfg->hardPercentMax / 100.0f, 0.0f, 1.0f);

		// 순서 보정: s ≤ c1 ≤ c2 ≤ h 보장
		if (c1 < s)
			c1 = s;
		if (c2 < c1)
			c2 = c1;
		if (h < c2)
			h = c2;

		// ResolvedWind 의 minFan / fanLimit 과 merge
		if (s < v_min)
			s = v_min;
		if (h > v_max)
			h = v_max;
		if (c1 < s)
			c1 = s;
		if (c2 > h)
			c2 = h;

		// ---------------------------
		// 3구간 커브:
		//  - 0 ~ 0.33  : 0 → s 로 부드럽게
		//  - 0.33~0.66 : s → c2 (컴포트 범위)
		//  - 0.66~1.0  : c2 → h (하드 상한)
		// ---------------------------
		float v_out = 0.0f;

		if (v_req <= 0.0001f) {
			v_out = 0.0f;
		} else if (v_req < 0.33f) {
			float v_t = v_req / 0.33f;			  // 0~1
			v_out	  = s * v_t;				  // 0 -> s
		} else if (v_req < 0.66f) {
			float v_t = (v_req - 0.33f) / 0.33f;  // 0~1
			v_out	  = s + (c2 - s) * v_t;		  // s -> c2
		} else {
			float v_t = (v_req - 0.66f) / 0.34f;  // 0~1
			v_out	  = c2 + (h - c2) * v_t;	  // c2 -> h
		}

		// 최종 min/max 한 번 더 방어
		if (v_out < v_min)
			v_out = v_min;
		if (v_out > v_max)
			v_out = v_max;

		return v_out;
	}

	// ==================================================
	// 듀티 설정 (0.0 ~ 100.0)
	// ==================================================
	void setDutyPercent(float p_percent) {
		if (!_state.initialized)
			return;

		// enable=false 인 경우 항상 0%
		if (!_state.enabled) {
			_state.dutyPercent = 0.0f;
			_setRawDuty(0);
			return;
		}

		if (p_percent < 0.0f)
			p_percent = 0.0f;
		if (p_percent > 100.0f)
			p_percent = 100.0f;
		_state.dutyPercent = p_percent;

		// 실제 레졸루션 스케일링
		float v_ratio	   = p_percent / 100.0f;
		if (v_ratio < 0.0f)
			v_ratio = 0.0f;
		if (v_ratio > 1.0f)
			v_ratio = 1.0f;

		uint32_t v_raw = (uint32_t)(v_ratio * (float)_state.maxDuty + 0.5f);
		if (v_raw > _state.maxDuty)
			v_raw = _state.maxDuty;

		_setRawDuty(v_raw);
	}

	// 현재 듀티 조회
	float getDutyPercent() const {
		return _state.dutyPercent;
	}

	// 현재 하드웨어 RAW duty 조회용 (필요 시 사용)
	uint32_t getRawDuty() const {
		return (_state.initialized) ? ledcRead(_state.channel) : 0;
	}

	// --------------------------------------------------
	// 호환용 래퍼 (기존 P10_ 접두사 멤버 이름 유지)
	//  - CT10 / S10 등 기존 코드와의 연동용
	// --------------------------------------------------
	void P10_begin(const ST_A20_SystemConfig_t& p_cfg) {
		begin(p_cfg);
	}
	void P10_setEnabled(bool p_enabled) {
		setEnabled(p_enabled);
	}
	bool P10_isEnabled() const {
		return isEnabled();
	}
	bool P10_isInitialized() const {
		return isInitialized();
	}
	void P10_setDutyPercent(float p_percent) {
		setDutyPercent(p_percent);
	}
	float P10_getDutyPercent() const {
		return getDutyPercent();
	}
	uint32_t P10_getRawDuty() const {
		return getRawDuty();
	}

  private:
	ST_P10_PWMState_t _state;

	void _setRawDuty(uint32_t p_raw) {
		if (!_state.initialized)
			return;
		if (p_raw > _state.maxDuty)
			p_raw = _state.maxDuty;
		ledcWrite(_state.channel, p_raw);
	}
};

extern CL_P10_PWM g_P10_pwm;
