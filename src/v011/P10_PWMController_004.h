// P10_PWMController_004.h

#pragma once
#include <Arduino.h>
#include "D10_Logger_004.h"

#include <ArduinoJson.h>

class CL_P10_PWM {
public:
    CL_P10_PWM() = default;

    void init(uint8_t pin, uint8_t ch, uint32_t freq, uint8_t res) {
        pwmPin = pin;
        pwmChannel = ch;
        pwmFreq = freq;
        pwmRes = res;
        setup();
    }

    void init() {
        ledcSetup(pwmChannel, pwmFreq, pwmRes);
        ledcAttachPin(pwmPin, pwmChannel);
    }

    void set_pwmPin(uint8_t newPin) {
        if (newPin == pwmPin) return;
        ledcDetachPin(pwmPin);
        pwmPin = newPin;
        ledcAttachPin(pwmPin, pwmChannel);
    }

    void set_pwmChannel(uint8_t newCh) {
        pwmChannel = newCh;
        setup();
    }

    void set_pwmFrequency(uint32_t freq) {
        pwmFreq = freq;
        setup();
    }

    void set_pwmResolution(uint8_t bits) {
        pwmRes = bits;
        setup();
    }

    void set_pwmDuty(float percent) {
        percent = constrain(percent, 0.0f, 100.0f);
        uint32_t duty = (uint32_t)((pow(2, pwmRes) - 1) * (percent / 100.0f));
        ledcWrite(pwmChannel, duty);
    }

    void toJson(JsonDocument& doc) {
        JsonObject pwm = doc["pwm"].to<JsonObject>();
        pwm["pin"] = pwmPin;
        pwm["ch"] = pwmChannel;
        pwm["freq"] = pwmFreq;
        pwm["res"] = pwmRes;
    }

private:
    uint8_t pwmPin = 6;
    uint8_t pwmChannel = 0;
    uint32_t pwmFreq = 25000;
    uint8_t pwmRes = 10;
};
