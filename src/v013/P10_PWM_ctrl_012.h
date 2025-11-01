#pragma once
/*
 * ------------------------------------------------------
 * 소스명   : P10_PWM_ctrl_012.h
 * 파일역할 : Smart Nature Wind PWM Controller
 * 모듈약어 : P10
 * ------------------------------------------------------
 * 기능 요약:
 *  - ESP32 LEDC 기반 PWM 제어 래퍼
 *  - 채널/주파수/해상도/핀 동적 설정 및 재적용
 *  - 듀티(%) <-> RAW 변환, 부드러운 제어(Slew) 옵션
 *  - 현재 상태 JSON 직렬화 지원
 * ------------------------------------------------------
 * [구현 규칙]
 *  - 항상 소스 시작 주석 체계 유지
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
 * ------------------------------------------------------
 */

#include <Arduino.h>
#include <ArduinoJson.h>
#include "D10_Logger_011.h"

class CL_P10_PWM {
public:
	CL_P10_PWM() = default;

	// ==================================================
	// 초기화
	// ==================================================
	/**
	 * @brief PWM 초기화 후 즉시 적용
	 */
	void P10_init(uint8_t p_pin, uint8_t p_ch,
	              uint32_t p_freq, uint8_t p_res) {
		_pwmPin     = p_pin;
		_pwmChannel = p_ch;
		_pwmFreq    = p_freq;
		_pwmRes     = p_res;
		_apply();
	}

	/** @brief 현재 파라미터 LEDC에 반영 */
	void P10_apply() { _apply(); }

	// ==================================================
	// 설정 변경 API
	// ==================================================
	void P10_setPin(uint8_t p_newPin) {
		if (p_newPin == _pwmPin) return;

		ledcDetachPin(_pwmPin);
		_pwmPin = p_newPin;
		ledcAttachPin(_pwmPin, _pwmChannel);

		CL_D10_Logger::log(EN_L10_LOG_INFO, "[P10] Pin => %u", (unsigned)_pwmPin);
	}

	void P10_setChannel(uint8_t p_newCh) {
		if (p_newCh == _pwmChannel) return;
		_pwmChannel = p_newCh;
		_apply();
		CL_D10_Logger::log(EN_L10_LOG_INFO, "[P10] Channel => %u", (unsigned)_pwmChannel);
	}

	void P10_setFrequency(uint32_t p_freq) {
		if (p_freq == _pwmFreq) return;
		_pwmFreq = p_freq;
		_apply();
		CL_D10_Logger::log(EN_L10_LOG_INFO, "[P10] Freq => %lu Hz", (unsigned long)_pwmFreq);
	}

	void P10_setResolution(uint8_t p_bits) {
		if (p_bits == _pwmRes) return;
		_pwmRes = p_bits;
		_apply();
		CL_D10_Logger::log(EN_L10_LOG_INFO, "[P10] Res => %u-bit", (unsigned)_pwmRes);
	}

	// ==================================================
	// 듀티(팬 회전속도) 제어
	// ==================================================
	/**
	 * @brief 즉시 듀티 설정 (%)
	 */
	void P10_setDutyPercent(float p_percent) {
		float v_pct = constrain(p_percent, 0.0f, 100.0f);

		uint32_t v_max  = ((uint32_t)1 << _pwmRes) - 1U;
		uint32_t v_duty = (uint32_t)llroundf(v_max * (v_pct / 100.0f));
		ledcWrite(_pwmChannel, v_duty);

		_lastDutyPct = v_pct;

		CL_D10_Logger::log(EN_L10_LOG_DEBUG,
		                   "[P10] Duty %.2f%% -> raw %lu",
		                   v_pct, (unsigned long)v_duty);
	}

	/**
	 * @brief 슬루 기반 천천히 변경 (자연풍 변동 시 추천)
	 * @param p_target 목표 듀티 (%)
	 * @param p_step   1 iteration step (%)
	 */
	void P10_setDutyPercentSlew(float p_target, float p_step = 0.8f) {
		float v_target = constrain(p_target, 0.0f, 100.0f);

		if (fabs(v_target - _lastDutyPct) <= p_step) {
			P10_setDutyPercent(v_target);
			return;
		}

		float v_next = (v_target > _lastDutyPct)
		              ? (_lastDutyPct + p_step)
		              : (_lastDutyPct - p_step);

		P10_setDutyPercent(v_next);
	}

	// ==================================================
	// 조회
	// ==================================================
	uint32_t P10_getDutyRaw() const {
		return (uint32_t)ledcRead(_pwmChannel);
	}

	float P10_getDutyPercent() const {
		uint32_t v_raw = (uint32_t)ledcRead(_pwmChannel);
		uint32_t v_max = ((uint32_t)1 << _pwmRes) - 1U;
		return (v_max > 0U) ? (100.0f * v_raw / (float)v_max) : 0.0f;
	}

	// ==================================================
	// JSON 직렬화
	// ==================================================
	void P10_toJson(JsonDocument& p_doc) const {
		JsonObject v_pwm = p_doc["pwm"].to<JsonObject>();
		v_pwm["pin"]          = _pwmPin;
		v_pwm["ch"]           = _pwmChannel;
		v_pwm["freq"]         = _pwmFreq;
		v_pwm["res"]          = _pwmRes;
		v_pwm["duty_percent"] = P10_getDutyPercent();
		v_pwm["duty_raw"]     = P10_getDutyRaw();
	}

private:
	// --------------------------------------------------
	// 내부 PWM 적용
	// --------------------------------------------------
	void _apply() {
		ledcSetup(_pwmChannel, _pwmFreq, _pwmRes);
		ledcAttachPin(_pwmPin, _pwmChannel);
		CL_D10_Logger::log(EN_L10_LOG_INFO,
		                   "[P10] setup pin=%u ch=%u freq=%luHz res=%u-bit",
		                   (unsigned)_pwmPin, (unsigned)_pwmChannel,
		                   (unsigned long)_pwmFreq, (unsigned)_pwmRes);
	}

private:
	// --------------------------------------------------
	// 내부 상태
	// --------------------------------------------------
	uint8_t  _pwmPin     = 6;
	uint8_t  _pwmChannel = 0;
	uint32_t _pwmFreq    = 25000;
	uint8_t  _pwmRes     = 10;
	float    _lastDutyPct = 0.0f;   // Slew 기반 제어용 캐시
};

// 전역 인스턴스 extern (정의는 main.cpp 등에서)
extern CL_P10_PWM g_P10_pwm;
