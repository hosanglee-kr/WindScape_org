#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : S10_Simulation_007.h
 * 모듈명 : WindScape Simulation Manager
 * ------------------------------------------------------
 * 기능 요약:
 *  - 자연풍 시뮬레이션(Phase/난류/돌풍/열기포/팬 제어)
 *  - Von Kármán 스펙트럼 합성, Phase 전환, 목표 재생성, PWM 제어 통합
 *  - config_014.json 구조 완전 반영 (sim/timing/hw 통합)
 * ------------------------------------------------------
 * 튜닝 가이드:
 *  - 강도 세게 : wind_intensity ↑
 *  - 돌풍 자주 : gust_frequency ↑
 *  - 열기포 자주 : thermal_bubble_strength ↑
 *  - 난류 강하게 : turbulence_intensity_sigma ↑
 *  - PWM 세기 제한 : fan_speed_limit 조정
 * ------------------------------------------------------
 * 코드 규칙:
 *   - 모듈약어: S10
 *   - 전역: g_S10_, 상수: G_S10_, 구조체: ST_S10_, 클래스: CL_S10_
 * ------------------------------------------------------
 */

#include <Arduino.h>
#include <cmath>

#include "A10_Const_007.h"
#include "D10_Logger_004.h"
#include "P10_PWM_ctrl_005.h"

class CL_S10_Simulation {
public:
    // --- 시뮬 상태 ---
    bool  wind_simulation_active = false;
    bool  fan_power_enabled      = true;
    float current_wind_speed     = 3.6f;
    float target_wind_speed      = 3.6f;
    float wind_change_rate       = 0.1f;
    float wind_momentum          = 0.0f;

    // --- Phase ---
    T_A10_WindWeatherPhase_t current_weather_phase = EN_A10_WEATHER_PHASE_NORMAL;
    float phase_start_time = 0.0f;
    float phase_duration   = 120.0f;
    float phase_wind_min   = 2.0f;
    float phase_wind_max   = 6.0f;

    // --- 난류/돌풍/열기포 ---
    float spectral_energy_buffer     = 0.0f;
    float spectral_phase_accumulator = 0.0f;
    float turbulence_time_scale      = 5.0f;

    bool  gust_active        = false;
    float gust_start_time    = 0.0f;
    float gust_duration      = 3.0f;
    float gust_intensity     = 1.0f;
    unsigned long last_gust_check = 0;

    bool  thermal_bubble_active         = false;
    float thermal_bubble_start_time     = 0.0f;
    float thermal_bubble_duration       = 8.0f;
    float current_thermal_contribution  = 0.0f;
    unsigned long last_thermal_check    = 0;

    unsigned long last_wind_sim_update = 0;

    // PWM 컨트롤러
    CL_P10_PWM* pwmCtrl_ = nullptr;

    // --------------------------------------------------
    // 초기화
    // --------------------------------------------------
    void begin(CL_P10_PWM& p_pwmCtrl, bool applyPreset = true) {
        pwmCtrl_ = &p_pwmCtrl;
        if (applyPreset) startWindSimulation();
    }

    // --------------------------------------------------
    // 시뮬레이션 시작
    // --------------------------------------------------
    void startWindSimulation() {
        float v_min = 1.0f;
        float v_max = 6.0f;
        current_wind_speed = target_wind_speed = (v_min + v_max) * 0.5f;
        wind_momentum = spectral_energy_buffer = spectral_phase_accumulator = 0.0f;
        current_weather_phase = EN_A10_WEATHER_PHASE_NORMAL;
        phase_start_time = millis() / 1000.0f;
        phase_duration   = 120.0f;
        wind_simulation_active = true;
        generateWindTarget();
    }

    // --------------------------------------------------
    // 팬 속도 반영
    // --------------------------------------------------
    void applyFanSpeed(float p_speed_percent) {
        if (!pwmCtrl_) return;

        float v_req   = p_speed_percent / 100.0f;
        float v_limit = g_A10_config.fan_speed_limit / 100.0f;
        float v_min   = g_A10_config.minimum_fan_speed / 100.0f;
        float v_int   = g_A10_config.wind_intensity / 100.0f;

        if (!fan_power_enabled || v_int < 0.01f) {
            pwmCtrl_->set_pwmDuty(0.0f);
            return;
        }

        v_req *= v_int;
        v_req = fmax(v_min, fmin(v_limit, v_req));
        pwmCtrl_->set_pwmDuty(v_req * 100.0f);
    }

    // --------------------------------------------------
    // Von Kármán 난류 합성
    // --------------------------------------------------
    void calculateVonKarman(float dt) {
        if (!wind_simulation_active) return;

        float L = g_A10_config.turbulence_length_scale;
        float sigma = g_A10_config.turbulence_intensity_sigma;
        float U = fmax(current_wind_speed, 0.1f);

        float v_sum = 0.0f;
        for (int i = 1; i <= 12; i++) {
            float f = i * 0.1f * U / L;
            float term = 70.8f * powf(f * L / U, 2.0f);
            float S = (4.0f * sigma * sigma * (L / U) * (1.0f + term)) / powf(1.0f + term, 5.0f/6.0f);
            float phase_rate = 2.0f * M_PI * f;
            float cur_phase = spectral_phase_accumulator * i + phase_rate * dt + A10_randRange(-0.1f, 0.1f);
            v_sum += sqrtf(2.0f * S * 0.083f) * sinf(cur_phase);
        }

        spectral_phase_accumulator = fmodf(spectral_phase_accumulator + dt * 0.5f, 2.0f * M_PI);
        float corr = expf(-dt / turbulence_time_scale);
        spectral_energy_buffer = spectral_energy_buffer * corr + v_sum * (1.0f - corr);
    }

    // --------------------------------------------------
    // 돌풍 업데이트
    // --------------------------------------------------
    void updateGust() {
        if (!wind_simulation_active) return;

        float now = millis() / 1000.0f;
        if (gust_active) {
            float age = now - gust_start_time;
            if (age >= gust_duration) { gust_active = false; gust_intensity = 1.0f; return; }
            float env = 1.0f - fabsf(age / gust_duration - 0.5f) * 2.0f;
            gust_intensity = 1.0f + (1.5f - 1.0f) * env;
            return;
        }

        if ((millis() - last_gust_check) >= g_A10_config.gust_check_interval_ms) {
            last_gust_check = millis();
            float base_p = (g_A10_config.gust_frequency / 100.0f) * 0.03f;
            if (A10_getRandom01() < base_p) {
                gust_active = true;
                gust_start_time = now;
                gust_duration = A10_randRange(1.5f, 4.0f);
                gust_intensity = A10_randRange(1.05f, 1.35f);
            }
        }
    }

    // --------------------------------------------------
    // 열기포 업데이트
    // --------------------------------------------------
    void updateThermal() {
        if (!wind_simulation_active) return;
        if (thermal_bubble_active) {
            float now = millis() / 1000.0f;
            float age = now - thermal_bubble_start_time;
            if (age >= thermal_bubble_duration) {
                thermal_bubble_active = false;
                current_thermal_contribution = 0.0f;
                return;
            }
            float prog = age / thermal_bubble_duration;
            float env = (prog < 0.3f) ? prog / 0.3f : (prog < 0.7f ? 1.0f : 1.0f - (prog - 0.7f) / 0.3f);
            current_thermal_contribution = (g_A10_config.thermal_bubble_strength - 1.0f) * env;
        } else {
            if ((millis() - last_thermal_check) >= g_A10_config.thermal_check_interval_ms) {
                last_thermal_check = millis();
                float p = 0.02f * (g_A10_config.thermal_bubble_strength);
                if (A10_getRandom01() < p) {
                    thermal_bubble_active = true;
                    thermal_bubble_start_time = millis() / 1000.0f;
                    thermal_bubble_duration = A10_randRange(6.0f, 12.0f);
                }
            }
        }
    }

    // --------------------------------------------------
    // 목표 풍속 생성
    // --------------------------------------------------
    void generateWindTarget() {
        float base_min = 1.0f, base_max = 6.0f;
        float range = base_max - base_min;
        target_wind_speed = base_min + A10_getRandom01() * range;
        float var = g_A10_config.wind_variability / 100.0f;
        wind_change_rate = (0.15f + var * 0.25f) * A10_randRange(0.8f, 1.5f);
    }

    // --------------------------------------------------
    // 메인 Tick
    // --------------------------------------------------
    void tick() {
        unsigned long now = millis();
        static uint32_t s_jitter = 0;
        if (now - last_wind_sim_update < (g_A10_config.wind_sim_interval_ms + (s_jitter % 200))) return;
        s_jitter = esp_random();

        float dt = (now - last_wind_sim_update) / 1000.0f;
        last_wind_sim_update = now;

        calculateVonKarman(dt);
        updateGust();
        updateThermal();

        float diff = target_wind_speed - current_wind_speed;
        current_wind_speed += diff * wind_change_rate * dt + spectral_energy_buffer;
        current_wind_speed = fmaxf(0.2f, fminf(11.0f, current_wind_speed));

        float fan_pct = current_wind_speed * 10.0f + 10.0f;
        fan_pct *= gust_intensity;
        fan_pct += current_thermal_contribution * 5.0f;

        applyFanSpeed(fan_pct);
    }
};
