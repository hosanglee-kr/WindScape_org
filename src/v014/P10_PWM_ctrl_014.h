#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : P10_PWM_ctrl_014.h
 * 모듈약어 : P10
 * 모듈명 : Smart Nature Wind PWM 제어 Manager (v014)
 * ------------------------------------------------------
 * 기능 요약
 *  - cfg_system_xxx.json 의 hw.fan_pwm 설정 기반 PWM 초기화
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

#include "A10_Const_016.h"
#include "D10_Logger_016.h"

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
	//  - cfg.system.hw.fan_pwm 기반
	// ==================================================
	void begin(const ST_A10_SystemConfig& p_cfg) {
		memset(&_state, 0, sizeof(_state));
		_state.pin			  = p_cfg.hw.fan_pwm.pin;
		_state.channel		  = (uint8_t)p_cfg.hw.fan_pwm.channel;
		_state.freq			  = p_cfg.hw.fan_pwm.freq;
		_state.resolutionBits = (uint8_t)p_cfg.hw.fan_pwm.res;
		_state.dutyPercent	  = 0.0f;
		_state.enabled		  = true;
		_state.initialized	  = false;

		if (_state.pin < 0) {
			CL_D10_Logger::log(EN_L10_LOG_ERROR,
							   "[P10] invalid fan pin=%d", (int)_state.pin);
			return;
		}

		// LEDC 설정
		bool v_ok1 = ledcSetup(_state.channel, _state.freq, _state.resolutionBits);
		if (!v_ok1) {
			CL_D10_Logger::log(EN_L10_LOG_ERROR,
							   "[P10] ledcSetup failed ch=%u freq=%lu res=%u",
							   (unsigned)_state.channel,
							   (unsigned long)_state.freq,
							   (unsigned)_state.resolutionBits);
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

		CL_D10_Logger::log(EN_L10_LOG_INFO,
						   "[P10] begin pin=%d ch=%u freq=%lu res=%u",
						   (int)_state.pin,
						   (unsigned)_state.channel,
						   (unsigned long)_state.freq,
						   (unsigned)_state.resolutionBits);
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

    // 논리 duty% → 팬 H/W특성 반영된 실제 PWM % 변환
    float applyFanConfigCurve(const ST_A10_SystemConfig& p_sys,
                                     float p_reqPercent) {
        float v_req = A10_clampf(p_reqPercent, 0.0f, 100.0f);
        const auto& v_fc = p_sys.hw.fanConfig;

        // 완전 OFF
        if (v_req <= 0.1f) {
            return 0.0f;
        }

        // 시동 최소 구간 미만이면 0으로 보냄 (모터 떨림 방지)
        if (v_req < (float)v_fc.startPercentMin) {
            return 0.0f;
        }

        // 절대 상한
        if (v_req > (float)v_fc.hardPercentMax) {
            v_req = (float)v_fc.hardPercentMax;
        }

        // comfort 구간으로 리맵핑 (선택사항)
        float v_span = (float)v_fc.comfortPercentMax - (float)v_fc.comfortPercentMin;
        if (v_span < 1.0f) {
            // 설정 이상 시: 그냥 클램프된 v_req 그대로 사용
            return v_req;
        }

        float v_norm = v_req / 100.0f; // 0~1
        float v_out  = (float)v_fc.comfortPercentMin + v_norm * v_span;

        // hardMax 한 번 더 방어
        v_out = A10_clampf(v_out, 0.0f, (float)v_fc.hardPercentMax);
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
		float v_ratio = p_percent / 100.0f;
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
	void P10_begin(const ST_A10_SystemConfig& p_cfg) {
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
