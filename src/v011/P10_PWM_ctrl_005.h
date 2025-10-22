// 

/*
 * ------------------------------------------------------
 * 소스명 : P10_PWM_ctrl_005.h
 * 모듈명 : 
 * ------------------------------------------------------
 * 기능 요약:
 *  - 
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
 * 		- 클래스 정적 멤버      : s_ 접두사
 * 		- 로컬 변수             : v_ 접두사
 * 		- 함수 인자             : p_ 접두사
 */

#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "D10_Logger_004.h"

class CL_P10_PWM {
public:
    CL_P10_PWM() = default;

    // ==============================
    // 초기화
    // ==============================
    void init(uint8_t pin, uint8_t ch, uint32_t freq, uint8_t res) {
        pwmPin = pin;
        pwmChannel = ch;
        pwmFreq = freq;
        pwmRes = res;
        apply();  // 내부 세팅 적용
    }

    // 내부용: LEDC 설정 재적용
    void apply() {
        ledcSetup(pwmChannel, pwmFreq, pwmRes);
        ledcAttachPin(pwmPin, pwmChannel);
        CL_D10_Logger::log(EN_L10_LOG_INFO,
            "PWM setup: pin=%d, ch=%d, freq=%uHz, res=%dbit",
            pwmPin, pwmChannel, pwmFreq, pwmRes);
    }

    // ==============================
    // 설정 변경
    // ==============================
    void set_pwmPin(uint8_t newPin) {
        if (newPin == pwmPin) return;
        ledcDetachPin(pwmPin);
        pwmPin = newPin;
        ledcAttachPin(pwmPin, pwmChannel);
        CL_D10_Logger::log(EN_L10_LOG_INFO, "PWM pin changed to %d", pwmPin);
    }

    void set_pwmChannel(uint8_t newCh) {
        if (newCh == pwmChannel) return;
        pwmChannel = newCh;
        apply();
        CL_D10_Logger::log(EN_L10_LOG_INFO, "PWM channel changed to %d", pwmChannel);
    }

    void set_pwmFrequency(uint32_t freq) {
        if (freq == pwmFreq) return;
        pwmFreq = freq;
        apply();
        CL_D10_Logger::log(EN_L10_LOG_INFO, "PWM frequency changed to %u Hz", pwmFreq);
    }

    void set_pwmResolution(uint8_t bits) {
        if (bits == pwmRes) return;
        pwmRes = bits;
        apply();
        CL_D10_Logger::log(EN_L10_LOG_INFO, "PWM resolution changed to %d bits", pwmRes);
    }

    // ==============================
    // 출력 제어
    // ==============================
    void set_pwmDuty(float p_percent) {
        p_percent = constrain(p_percent, 0.0f, 100.0f);
        uint32_t v_duty = (uint32_t)((pow(2, pwmRes) - 1) * (p_percent / 100.0f));
        ledcWrite(pwmChannel, v_duty);

        CL_D10_Logger::log(EN_L10_LOG_DEBUG, "PWM duty %.2f%% (raw=%u)", p_percent, v_duty);

        Serial.print(">PWM Duty:");   Serial.println(p_percent);

    }

    // ==============================
    // 상태 조회
    // ==============================
    int getDutyRaw() const {
        return ledcRead(pwmChannel);
    }

    float getDutyPercent() const {
        int raw = ledcRead(pwmChannel);
        int max = (1 << pwmRes) - 1;
        return (max > 0) ? (100.0f * raw / (float)max) : 0.0f;
    }

    // ==============================
    // JSON 직렬화
    // ==============================
    void toJson(JsonDocument& doc) {
        JsonObject pwm = doc["pwm"].to<JsonObject>();
        pwm["pin"]          = pwmPin;
        pwm["ch"]           = pwmChannel;
        pwm["freq"]         = pwmFreq;
        pwm["res"]          = pwmRes;
        pwm["duty_percent"] = getDutyPercent();
    }

private:
    uint8_t pwmPin      = 6;
    uint8_t pwmChannel  = 0;
    uint32_t pwmFreq    = 25000;
    uint8_t pwmRes      = 10;
};

// ✅ WebAPI 등에서 접근 가능하도록 extern 선언
extern CL_P10_PWM g_P10_pwm;
