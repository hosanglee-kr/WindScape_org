#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : P10_PWM_ctrl_010.h
 * 모듈명 : Smart Nature Wind PWM Controller
 * ------------------------------------------------------
 * 기능 요약:
 *  - PWM 핀, 채널, 주파수, 해상도 초기화 및 동적 변경
 *  - Duty 제어 (0~100%)
 *  - 실시간 상태 조회 (raw/duty%)
 *  - JSON 직렬화 (toJson)
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
 * 		- 클래스 private 멤버   : _ 접두사,
 * 		- 클래스 정적 멤버      : s_접두사
 * 		- 로컬 변수             : v_접두사
 * 		- 함수 인자             : p_접두사
 */

#include <Arduino.h>
#include <ArduinoJson.h>
#include "D10_Logger_010.h"

class CL_P10_PWM {
public:
    // -------------------------------------------
    // 생성자
    // -------------------------------------------
    CL_P10_PWM() = default;

    // =====================================================
    // 초기화
    // =====================================================
    void init(uint8_t p_pin, uint8_t p_ch, uint32_t p_freq, uint8_t p_res) {
        _pwmPin     = p_pin;
        _pwmChannel = p_ch;
        _pwmFreq    = p_freq;
        _pwmRes     = p_res;
        apply();
    }

    // =====================================================
    // LEDC 설정 적용
    // =====================================================
    void apply() {
        ledcSetup(_pwmChannel, _pwmFreq, _pwmRes);
        ledcAttachPin(_pwmPin, _pwmChannel);
        CL_D10_Logger::log(EN_L10_LOG_INFO,
            "PWM configured: pin=%u, ch=%u, freq=%luHz, res=%ubit",
            _pwmPin, _pwmChannel, (unsigned long)_pwmFreq, _pwmRes);
    }

    // =====================================================
    // 설정 변경
    // =====================================================
    void setPin(uint8_t p_newPin) {
        if (p_newPin == _pwmPin) return;
        ledcDetachPin(_pwmPin);
        _pwmPin = p_newPin;
        ledcAttachPin(_pwmPin, _pwmChannel);
        CL_D10_Logger::log(EN_L10_LOG_INFO, "PWM pin changed → %u", _pwmPin);
    }

    void setChannel(uint8_t p_newCh) {
        if (p_newCh == _pwmChannel) return;
        _pwmChannel = p_newCh;
        apply();
        CL_D10_Logger::log(EN_L10_LOG_INFO, "PWM channel changed → %u", _pwmChannel);
    }

    void setFrequency(uint32_t p_freq) {
        if (p_freq == _pwmFreq) return;
        _pwmFreq = p_freq;
        apply();
        CL_D10_Logger::log(EN_L10_LOG_INFO, "PWM frequency changed → %lu Hz", (unsigned long)_pwmFreq);
    }

    void setResolution(uint8_t p_bits) {
        if (p_bits == _pwmRes) return;
        _pwmRes = p_bits;
        apply();
        CL_D10_Logger::log(EN_L10_LOG_INFO, "PWM resolution changed → %u bits", _pwmRes);
    }

    // =====================================================
    // Duty 제어
    // =====================================================
    void setDutyPercent(float p_percent) {
        p_percent = constrain(p_percent, 0.0f, 100.0f);

        uint32_t v_max = ((1UL << _pwmRes) - 1);
        uint32_t v_duty = static_cast<uint32_t>(v_max * (p_percent / 100.0f));

        ledcWrite(_pwmChannel, v_duty);

        _lastDuty = p_percent;
        CL_D10_Logger::log(EN_L10_LOG_DEBUG, "PWM duty: %.2f%% (raw=%lu)", p_percent, (unsigned long)v_duty);
    }

    // =====================================================
    // 상태 조회
    // =====================================================
    uint32_t getDutyRaw() const {
        return ledcRead(_pwmChannel);
    }

    float getDutyPercent() const {
        uint32_t v_raw = ledcRead(_pwmChannel);
        uint32_t v_max = (1UL << _pwmRes) - 1;
        return (v_max > 0) ? (100.0f * v_raw / (float)v_max) : 0.0f;
    }

    uint8_t getPin() const { return _pwmPin; }
    uint8_t getChannel() const { return _pwmChannel; }
    uint32_t getFrequency() const { return _pwmFreq; }
    uint8_t getResolution() const { return _pwmRes; }

    // =====================================================
    // JSON 직렬화
    // =====================================================
    void toJson(JsonDocument& p_doc) {
        JsonObject v_pwm = p_doc["pwm"].to<JsonObject>();
        v_pwm["pin"]          = _pwmPin;
        v_pwm["channel"]      = _pwmChannel;
        v_pwm["frequency"]    = _pwmFreq;
        v_pwm["resolution"]   = _pwmRes;
        v_pwm["duty_percent"] = getDutyPercent();
    }

    // =====================================================
    // 상태 로깅 출력
    // =====================================================
    void printState() const {
        CL_D10_Logger::log(EN_L10_LOG_INFO,
            "PWM state → pin=%u, ch=%u, freq=%luHz, res=%ubit, duty=%.2f%%",
            _pwmPin, _pwmChannel, (unsigned long)_pwmFreq, _pwmRes, getDutyPercent());
    }

private:
    // -----------------------------------------------------
    // 멤버 변수
    // -----------------------------------------------------
    uint8_t  _pwmPin      = 6;
    uint8_t  _pwmChannel  = 0;
    uint32_t _pwmFreq     = 25000;
    uint8_t  _pwmRes      = 10;
    float    _lastDuty    = 0.0f;
};

// ---------------------------------------------------------
// 전역 인스턴스 선언
// ---------------------------------------------------------
extern CL_P10_PWM g_P10_pwm;
