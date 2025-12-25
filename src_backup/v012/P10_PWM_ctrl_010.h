#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : P10_PWM_ctrl_010.h
 * 모듈명 : Smart Nature Wind PWM Controller
 * ------------------------------------------------------
 * 기능 요약:
 *  - ESP32 LEDC 기반 PWM 제어 래퍼
 *  - 채널/주파수/해상도/핀 동적 변경 및 적용
 *  - 듀티(%) ↔ raw 변환 / 상태 JSON 직렬화
 * ------------------------------------------------------
 * [코드 네이밍 규칙]
 * 		- 현재 파일 모듈약어    : P10
 * 		- 전역 상수,매크로      : G_모듈약어_ 접두사
 * 		- 전역 변수             : g_모듈약어_ 접두사
 * 		- 전역 함수             : 모듈약어_ 접두사
 * 		- type                  : T_모듈약어_ 접두사
 * 		- enum 상수             : EN_모듈약어_ 접두사
 * 		- 구조체                : ST_모듈약어_ 접두사
 * 		- 클래스명              : CL_모듈약어_ 접두사
 * 		- 클래스 private 멤버   : _ 접두사
 * 		- 클래스 정적 멤버      : s_ 접두사
 * 		- 로컬 변수             : v_ 접두사
 * 		- 함수 인자             : p_ 접두사
 */

#include <Arduino.h>
#include <ArduinoJson.h>     // v7: JsonDocument만 사용
#include "D10_Logger_010.h"

class CL_P10_PWM {
public:
	CL_P10_PWM() = default;

	// ==================================================
	// 초기화/적용
	// ==================================================
	/**
	 * @brief PWM 초기화
	 * @param p_pin  GPIO 핀
	 * @param p_ch   LEDC 채널(0~15)
	 * @param p_freq 주파수(Hz)
	 * @param p_res  해상도(bit, 1~20)
	 */
	void P10_init(uint8_t p_pin, uint8_t p_ch, uint32_t p_freq, uint8_t p_res) {
		_pwmPin     = p_pin;
		_pwmChannel = p_ch;
		_pwmFreq    = p_freq;
		_pwmRes     = p_res;
		_apply();
	}

	/**
	 * @brief 현재 설정(채널/주파수/해상도/핀)을 LEDC에 반영
	 */
	void P10_apply() { _apply(); }

	// ==================================================
	// 설정 변경
	// ==================================================
	void P10_setPin(uint8_t p_newPin) {
		if (p_newPin == _pwmPin) return;
		ledcDetachPin(_pwmPin);
		_pwmPin = p_newPin;
		ledcAttachPin(_pwmPin, _pwmChannel);
		CL_D10_Logger::log(EN_L10_LOG_INFO, "PWM pin → %u", (unsigned)_pwmPin);
	}

	void P10_setChannel(uint8_t p_newCh) {
		if (p_newCh == _pwmChannel) return;
		_pwmChannel = p_newCh;
		_apply();
		CL_D10_Logger::log(EN_L10_LOG_INFO, "PWM channel → %u", (unsigned)_pwmChannel);
	}

	void P10_setFrequency(uint32_t p_freq) {
		if (p_freq == _pwmFreq) return;
		_pwmFreq = p_freq;
		_apply();
		CL_D10_Logger::log(EN_L10_LOG_INFO, "PWM freq → %lu Hz", (unsigned long)_pwmFreq);
	}

	void P10_setResolution(uint8_t p_bits) {
		if (p_bits == _pwmRes) return;
		_pwmRes = p_bits;
		_apply();
		CL_D10_Logger::log(EN_L10_LOG_INFO, "PWM res → %u-bit", (unsigned)_pwmRes);
	}

	// ==================================================
	// 출력 제어
	// ==================================================
	/**
	 * @brief 듀티(%) 설정
	 * @param p_percent 0.0 ~ 100.0
	 */
	void P10_setDutyPercent(float p_percent) {
		float v_pct = p_percent;
		if (v_pct < 0.0f)   v_pct = 0.0f;
		if (v_pct > 100.0f) v_pct = 100.0f;

		uint32_t v_max  = ((uint32_t)1 << _pwmRes) - 1U;
		uint32_t v_duty = (uint32_t)llroundf((float)v_max * (v_pct / 100.0f));
		ledcWrite(_pwmChannel, v_duty);

		CL_D10_Logger::log(EN_L10_LOG_DEBUG, "PWM duty %.2f%% (raw=%lu)",
		                   v_pct, (unsigned long)v_duty);
	}

	// ==================================================
	// 상태 조회
	// ==================================================
	/** @brief 현재 raw duty 값 */
	uint32_t P10_getDutyRaw() const {
		return (uint32_t)ledcRead(_pwmChannel);
	}

	/** @brief 현재 듀티(%) */
	float P10_getDutyPercent() const {
		uint32_t v_raw = (uint32_t)ledcRead(_pwmChannel);
		uint32_t v_max = ((uint32_t)1 << _pwmRes) - 1U;
		return (v_max > 0U) ? (100.0f * (float)v_raw / (float)v_max) : 0.0f;
	}

	// ==================================================
	// JSON 직렬화
	// ==================================================
	/**
	 * @brief 상태를 JSON으로 직렬화
	 * @param p_doc JsonDocument (ArduinoJson v7)
	 * @note  p_doc["pwm"] 오브젝트에 채움
	 */
	void P10_toJson(JsonDocument& p_doc) const {
		JsonObject v_pwm = p_doc["pwm"].to<JsonObject>();
		v_pwm["pin"]          = _pwmPin;
		v_pwm["ch"]           = _pwmChannel;
		v_pwm["freq"]         = _pwmFreq;
		v_pwm["res"]          = _pwmRes;
		v_pwm["duty_percent"] = P10_getDutyPercent();
	}

private:
	// --------------------------------------------------
	// 내부 적용 함수
	// --------------------------------------------------
	void _apply() {
		ledcSetup(_pwmChannel, _pwmFreq, _pwmRes);
		ledcAttachPin(_pwmPin, _pwmChannel);
		CL_D10_Logger::log(EN_L10_LOG_INFO,
		                   "PWM setup: pin=%u, ch=%u, freq=%luHz, res=%u-bit",
		                   (unsigned)_pwmPin, (unsigned)_pwmChannel,
		                   (unsigned long)_pwmFreq, (unsigned)_pwmRes);
	}

private:
	// --------------------------------------------------
	// 멤버 (private: _ 접두사)
	// --------------------------------------------------
	uint8_t  _pwmPin     = 6;
	uint8_t  _pwmChannel = 0;
	uint32_t _pwmFreq    = 25000;
	uint8_t  _pwmRes     = 10;
};

// ✅ 전역 인스턴스가 필요하면 외부에서 정의(.cpp)하고 여기서 extern만 노출
extern CL_P10_PWM g_P10_pwm;
