// P10_PWMController_004.h

#pragma once
#include <Arduino.h>
#include "D10_Logger_004.h"

class CL_P10_PWMController {
public:
    static void init(int pin, int channel, int freq, int res) {
        ledcSetup(channel, freq, res);
        ledcAttachPin(pin, channel);
        CL_F10_Logger::log(D10_LOG_INFO, "PWM initialized");
    }

    static void setDuty(int channel, float percent, int resolutionBits) {
        percent = constrain(percent, 0.0f, 100.0f);
        uint32_t maxDuty = (1 << resolutionBits) - 1;
        uint32_t duty = (uint32_t)(percent / 100.0f * maxDuty);
        ledcWrite(channel, duty);
    }

    static void stop(int channel) {
        ledcWrite(channel, 0);
    }
};
