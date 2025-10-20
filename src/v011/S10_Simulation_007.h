#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : S10_Simulation_007.h
 * 모듈명 : WindScape 시뮬레이션 Manager
 * ------------------------------------------------------
 * 기능 요약:
 *  - 바람 시뮬레이션(Phase/난류/돌풍/열기포/팬 제어)
 *  - Von Kármán 스펙트럼 합성, Phase 전환/히스토리, 목표재생성, 지터 및 yield 포함
 *  - config_013.json 구조( sim / timing / hardware ) 완전 반영
 *
 * 튜닝 가이드:
 *   - 강도 더 세게 → sim.intensity 상향 (예: 100 → 150)
 *   - 돌풍 더 자주 → sim.gust_freq 0.005~0.02 단위 조정
 *   - 열기포 더 자주 → sim.thermal_freq 0.005~0.02 단위 조정
 *   - 전체 풍량 세기 → sim.wind_min/max 폭 확장
 *   - PWM 세기 제한 → hardware.pwm_limit 변경
 * ------------------------------------------------------
 * 네이밍 규칙:
 *   - 모듈약어 S10
 *   - 전역 상수/변수: G_/g_S10_
 *   - 구조체: ST_S10_, 클래스: CL_S10_
 * ------------------------------------------------------
 */

#include <Arduino.h>
#include <cmath>

#include "A10_Const_007.h"
#include "D10_Logger_004.h"
#include "P10_PWM_ctrl_005.h"

class CL_S10_Simulation {
public:
    // 외부 읽기 상태
    bool  wind_simulation_active = false;
    bool  fan_power_enabled      = true;
    float current_wind_speed     = 3.6f;
    float target_wind_speed      = 3.6f;
    float wind_change_rate       = 0.1f;
    float wind_momentum          = 0.0f;

    // Phase
    SC10_WindWeatherPhase_t current_weather_phase = EN_A10_WEATHER_PHASE_NORMAL;
    float phase_start_time = 0.0f;
    float phase_duration   = 120.0f;
    float phase_wind_min   = 2.0f;
    float phase_wind_max   = 6.0f;

    // 베이스 환경/확률(프리셋 반영)
    float base_wind_min            = 1.8f;
    float base_wind_max            = 5.5f;
    float gust_probability_base    = 0.04f;
    float location_gust_strength   = 2.1f;
    float thermal_bubble_frequency = 0.022f;

    // 난류
    float spectral_energy_buffer     = 0.0f;
    float spectral_phase_accumulator = 0.0f;
    float turbulence_time_scale      = 5.0f;

    // 돌풍
    bool          gust_active      = false;
    float         gust_start_time  = 0.0f;
    float         gust_duration    = 3.0f;
    float         gust_intensity   = 1.0f;
    unsigned long last_gust_check  = 0;

    // 열기포
    bool          thermal_bubble_active          = false;
    float         thermal_bubble_start_time      = 0.0f;
    float         thermal_bubble_duration        = 8.0f;
    float         current_thermal_contribution   = 0.0f;
    unsigned long last_thermal_check             = 0;

    // 타이머
    unsigned long last_wind_sim_update = 0;

    // 초기화 (프리셋 반영)
    void begin(CL_P10_PWM& p_pwmCtrl, bool p_applyPreset = true) {
        pwmCtrl_ = &p_pwmCtrl;
        if (p_applyPreset) applyCurrentPreset(true);
    }

    // 프리셋 적용
    void applyCurrentPreset(bool p_force = false) {
        wind_simulation_active = false;
        gust_active = thermal_bubble_active = false;
        gust_intensity = 1.0f;

        T_A10_PresetMode_t v_preset = (T_A10_PresetMode_t)g_A10_config.sim.preset;
        switch (v_preset) {
            case EN_A10_PRESET_FOREST_CANOPY:
                base_wind_min = 1.2f; base_wind_max = 3.8f;
                gust_probability_base = 0.012f; location_gust_strength = 1.6f;
                thermal_bubble_frequency = 0.015f; break;
            case EN_A10_PRESET_HARBOR_BREEZE:
                base_wind_min = 2.2f; base_wind_max = 5.3f;
                gust_probability_base = 0.025f; location_gust_strength = 1.8f;
                thermal_bubble_frequency = 0.026f; break;
            case EN_A10_PRESET_TROPICAL_RAIN:
                base_wind_min = 3.0f; base_wind_max = 8.0f;
                gust_probability_base = 0.060f; location_gust_strength = 2.2f;
                thermal_bubble_frequency = 0.038f; break;
            case EN_A10_PRESET_DESERT_NIGHT:
                base_wind_min = 0.9f; base_wind_max = 3.1f;
                gust_probability_base = 0.005f; location_gust_strength = 1.3f;
                thermal_bubble_frequency = 0.008f; break;
            default:
                base_wind_min = 1.8f; base_wind_max = 5.5f;
                gust_probability_base = 0.040f; location_gust_strength = 2.1f;
                thermal_bubble_frequency = 0.022f; break;
        }
        startWindSimulation();
    }

    // 시뮬 시작
    void startWindSimulation() {
        float v_mid = (base_wind_min + base_wind_max) * 0.5f;
        current_wind_speed = target_wind_speed = v_mid;
        wind_momentum = spectral_energy_buffer = spectral_phase_accumulator = 0.0f;

        current_weather_phase = EN_A10_WEATHER_PHASE_NORMAL;
        phase_start_time = millis() / 1000.0f;
        phase_duration = 120.0f;
        float v_span = base_wind_max - base_wind_min;
        phase_wind_min = base_wind_min + v_span * 0.15f;
        phase_wind_max = base_wind_min + v_span * 0.85f;

        wind_simulation_active = true;
        generateWindTarget();
    }

    // 팬 속도 적용
    void applyFanSpeed(float p_speed_percent) {
        if (!pwmCtrl_) return;
        float v_req = p_speed_percent / 100.0f;

        float v_limit = g_A10_config.hardware.pwm_limit / 100.0f;
        float v_min   = g_A10_config.hardware.pwm_min / 100.0f;
        float v_int   = g_A10_config.sim.intensity / 100.0f;

        if (!fan_power_enabled || v_int <= 0.01f) {
            pwmCtrl_->set_pwmDuty(0.0f);
            return;
        }

        v_req *= v_int;
        v_req = fmax(v_min, fmin(v_limit, v_req));
        pwmCtrl_->set_pwmDuty(v_req * 100.0f);
    }

    // Von Kármán 난류 합성
    void calculateVonKarman(float p_dt) {
        if (!wind_simulation_active) return;

        float v_L = g_A10_config.sim.turb_len;
        float v_sigma = g_A10_config.sim.turb_sig;
        float v_U = fmax(current_wind_speed, 0.1f);

        float v_sum = 0.0f;
        for (int i = 1; i <= 12; ++i) {
            float f = i * 0.1f * v_U / v_L;
            float term = 70.8f * powf(f * v_L / v_U, 2.0f);
            float S = (4.0f * v_sigma * v_sigma * (v_L / v_U) * (1.0f + term)) / powf(1.0f + term, 5.0f/6.0f);

            float phase_rate = 2.0f * M_PI * f;
            float phase_inc  = phase_rate * p_dt + A10_randRange(-0.1f, 0.1f);
            float cur_phase  = spectral_phase_accumulator * i + phase_inc;

            float amp = sqrtf(2.0f * S * 0.083f);
            v_sum += amp * sinf(cur_phase);
        }
        spectral_phase_accumulator = fmodf(spectral_phase_accumulator + p_dt * 0.5f, 2.0f * M_PI);
        spectral_energy_buffer = spectral_energy_buffer * expf(-p_dt / turbulence_time_scale) + v_sum * (1.0f - expf(-p_dt / turbulence_time_scale));
    }

    // 열기포 계산
    void calculateThermal() {
        if (!thermal_bubble_active) { current_thermal_contribution = 0.0f; return; }

        float t = millis() / 1000.0f;
        float age = t - thermal_bubble_start_time;
        if (age >= thermal_bubble_duration) { thermal_bubble_active = false; current_thermal_contribution = 0.0f; return; }

        float prog = age / thermal_bubble_duration;
        float env = (prog < 0.3f) ? prog / 0.3f : (prog < 0.7f ? 1.0f : 1.0f - (prog - 0.7f)/0.3f);
        current_thermal_contribution = (g_A10_config.sim.therm_str - 1.0f) * env;
    }

    // 돌풍 상태 갱신
    void updateGust() {
        if (!wind_simulation_active) return;
        float now = millis() / 1000.0f;

        if (gust_active) {
            float age = now - gust_start_time;
            if (age >= gust_duration) { gust_active = false; gust_intensity = 1.0f; return; }
            float env = 1.0f - fabsf(age / gust_duration - 0.5f) * 2.0f;
            gust_intensity = 1.0f + (location_gust_strength - 1.0f) * env;
            return;
        }

        if ((millis() - last_gust_check) >= g_A10_config.timing.gust_check_ms) {
            last_gust_check = millis();
            float base_p = gust_probability_base * (g_A10_config.sim.gust_freq / 100.0f);
            if (A10_getRandom01() < base_p) {
                gust_active = true; gust_start_time = now;
                gust_duration = A10_randRange(1.5f, 4.0f);
                gust_intensity = A10_randRange(1.05f, location_gust_strength);
            }
        }
    }

    // 열기포 트리거
    void updateThermalCheck() {
        if (!wind_simulation_active || thermal_bubble_active) return;
        if ((millis() - last_thermal_check) < g_A10_config.timing.thermal_check_ms) return;
        last_thermal_check = millis();

        if (A10_getRandom01() < thermal_bubble_frequency) {
            thermal_bubble_active = true;
            thermal_bubble_start_time = millis()/1000.0f;
            thermal_bubble_duration = A10_randRange(6.0f, 12.0f);
        }
    }

    // 새 목표 풍속 생성
    void generateWindTarget() {
        float range = phase_wind_max - phase_wind_min;
        target_wind_speed = phase_wind_min + A10_getRandom01() * range;
        float var = g_A10_config.sim.variability / 100.0f;
        wind_change_rate = (0.15f + var * 0.25f) * A10_randRange(0.8f, 1.5f);
    }

    // 메인 tick
    void tick() {
        unsigned long now = millis();
        static uint32_t s_jitter = 0;
        if (now - last_wind_sim_update < (g_A10_config.timing.sim_interval_ms + (s_jitter % 200))) return;
        s_jitter = esp_random();
        float dt = (now - last_wind_sim_update) / 1000.0f;
        last_wind_sim_update = now;

        calculateVonKarman(dt);
        calculateThermal();
        updateGust();
        updateThermalCheck();

        float diff = target_wind_speed - current_wind_speed;
        current_wind_speed += diff * wind_change_rate * dt + spectral_energy_buffer;
        current_wind_speed = fmaxf(0.2f, fminf(11.0f, current_wind_speed));

        float fan_pct = current_wind_speed * 10.0f + 10.0f;
        fan_pct *= gust_intensity;
        fan_pct += current_thermal_contribution * 5.0f;

        applyFanSpeed(fan_pct);
    }

private:
    CL_P10_PWM* pwmCtrl_ = nullptr;
};
