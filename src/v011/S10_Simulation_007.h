#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : S10_Simulation_007.h
 * 모듈명 : WindScape Simulation Manager
 * ------------------------------------------------------
 * 기능 요약:
 *  - 바람 시뮬레이션(Phase/난류/돌풍/열기포/팬 제어)
 *  - Von Kármán 스펙트럼 합성, Phase 전환/히스토리, 목표재생성, 지터 포함
 *  - config_014.json, A10_Const_007.h 구조 완전 반영
 * ------------------------------------------------------
 * 튜닝 가이드:
 *  - 강도 세게: wind_intensity ↑
 *  - 돌풍 자주: gust_frequency ↑
 *  - 열기포 자주: thermal_bubble_strength ↑
 *  - 전체 풍량: fan_speed_limit ↑
 *  - 난류 길이 조정: turbulence_length_scale / intensity_sigma
 * ------------------------------------------------------
 * 코드 네이밍 규칙:
 *   - 모듈약어 : S10
 *   - 전역 상수/매크로: G_S10_
 *   - 전역 변수: g_S10_
 *   - 로컬 변수: v_
 *   - 함수 인자: p_
 *   - type: T_S10_
 *   - enum: EN_S10_
 *   - 구조체: ST_S10_
 *   - 클래스: CL_S10_
 *   - 클래스 private: _
 *   - 전역함수: S10_
 * ------------------------------------------------------
 */

#include <Arduino.h>
#include <cmath>
#include "A10_Const_007.h"
#include "D10_Logger_004.h"
#include "P10_PWM_ctrl_005.h"

class CL_S10_Simulation {
public:
    // ------------------- 상태 -------------------
    bool  wind_simulation_active = false;
    bool  fan_power_enabled      = true;
    float current_wind_speed     = 3.6f;
    float target_wind_speed      = 3.6f;
    float wind_change_rate       = 0.1f;
    float wind_momentum          = 0.0f;

    // Phase
    T_A10_WindWeatherPhase_t current_weather_phase = EN_A10_WEATHER_PHASE_NORMAL;
    float phase_start_time = 0.0f;
    float phase_duration   = 120.0f;
    float phase_wind_min   = 2.0f;
    float phase_wind_max   = 6.0f;

    // 환경/확률
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
    bool          thermal_bubble_active         = false;
    float         thermal_bubble_start_time     = 0.0f;
    float         thermal_bubble_duration       = 8.0f;
    float         current_thermal_contribution  = 0.0f;
    unsigned long last_thermal_check            = 0;

    // 시간
    unsigned long last_wind_sim_update = 0;

    // PWM 컨트롤러 포인터
    CL_P10_PWM* pwmCtrl_ = nullptr;

    // --------------------------------------------------
    // 초기화
    // --------------------------------------------------
    void begin(CL_P10_PWM& p_pwmCtrl, bool applyPreset = true) {
        pwmCtrl_ = &p_pwmCtrl;
        if (applyPreset) applyCurrentPreset();
    }

    // --------------------------------------------------
    // 프리셋 적용 (현재 config 기반)
    // --------------------------------------------------
    void applyCurrentPreset() {
        wind_simulation_active = gust_active = thermal_bubble_active = false;
        gust_intensity = 1.0f;

        // config 값 직접 반영 (프리셋 구조 통합)
        base_wind_min  = 1.0f;
        base_wind_max  = 6.0f;
        gust_probability_base    = 0.02f * (g_A10_config.gust_frequency / 100.0f);
        location_gust_strength   = 1.0f + (g_A10_config.wind_intensity / 100.0f) * 1.2f;
        thermal_bubble_frequency = 0.01f + (g_A10_config.thermal_bubble_strength * 0.02f);

        startWindSimulation();
    }

    // --------------------------------------------------
    // 시뮬 시작
    // --------------------------------------------------
    void startWindSimulation() {
        float mid = (base_wind_min + base_wind_max) * 0.5f;
        current_wind_speed = target_wind_speed = mid;
        wind_momentum = spectral_energy_buffer = spectral_phase_accumulator = 0.0f;
        current_weather_phase = EN_A10_WEATHER_PHASE_NORMAL;
        phase_start_time = millis()/1000.0f;
        phase_duration   = 120.0f;
        wind_simulation_active = true;
        generateWindTarget();
    }

    // --------------------------------------------------
    // 팬 속도 반영
    // --------------------------------------------------
    void applyFanSpeed(float p_speed_percent) {
        if (!pwmCtrl_) return;
        float v_req = p_speed_percent / 100.0f;
        float v_limit = g_A10_config.fan_speed_limit / 100.0f;
        float v_min   = g_A10_config.minimum_fan_speed / 100.0f;
        float v_int   = g_A10_config.wind_intensity / 100.0f;
        if (!fan_power_enabled || v_int < 0.01f) { pwmCtrl_->set_pwmDuty(0.0f); return; }

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

        float sum = 0.0f;
        for (int i = 1; i <= 12; i++) {
            float f = i * 0.1f * U / L;
            float term = 70.8f * powf(f * L / U, 2.0f);
            float S = (4.0f * sigma * sigma * (L / U) * (1.0f + term)) / powf(1.0f + term, 5.0f/6.0f);
            float phase_rate = 2.0f * M_PI * f;
            float phase_inc  = phase_rate * dt + A10_randRange(-0.1f, 0.1f);
            float cur_phase  = spectral_phase_accumulator * i + phase_inc;
            float amp = sqrtf(2.0f * S * 0.083f);
            sum += amp * sinf(cur_phase);
        }

        spectral_phase_accumulator = fmodf(spectral_phase_accumulator + dt * 0.5f, 2.0f * M_PI);
        float corr = expf(-dt / turbulence_time_scale);
        spectral_energy_buffer = spectral_energy_buffer * corr + sum * (1.0f - corr);
    }

    // --------------------------------------------------
    // 열기포 계산
    // --------------------------------------------------
    void calculateThermal() {
        if (!wind_simulation_active || !thermal_bubble_active) return;

        float t = millis()/1000.0f;
        float age = t - thermal_bubble_start_time;
        if (age >= thermal_bubble_duration) { thermal_bubble_active = false; current_thermal_contribution = 0.0f; return; }

        float prog = age / thermal_bubble_duration;
        float env  = (prog < 0.3f)? prog/0.3f : (prog < 0.7f? 1.0f : 1.0f - (prog-0.7f)/0.3f);
        current_thermal_contribution = (g_A10_config.thermal_bubble_strength - 1.0f) * env;
    }

    // --------------------------------------------------
    // 돌풍 상태 갱신
    // --------------------------------------------------
    void updateGust() {
        if (!wind_simulation_active) return;
        float now = millis()/1000.0f;

        if (gust_active) {
            float age = now - gust_start_time;
            if (age >= gust_duration) { gust_active = false; gust_intensity = 1.0f; return; }
            float env = 1.0f - fabsf(age / gust_duration - 0.5f) * 2.0f;
            gust_intensity = 1.0f + (location_gust_strength - 1.0f) * env;
            return;
        }

        if ((millis() - last_gust_check) >= g_A10_config.gust_check_interval_ms) {
            last_gust_check = millis();
            float p = gust_probability_base * (g_A10_config.gust_frequency / 100.0f);
            if (A10_getRandom01() < p) {
                gust_active = true; gust_start_time = now;
                gust_duration = A10_randRange(1.5f, 4.0f);
                gust_intensity = A10_randRange(1.05f, location_gust_strength);
            }
        }
    }

    // --------------------------------------------------
    // 열기포 트리거
    // --------------------------------------------------
    void updateThermalCheck() {
        if (!wind_simulation_active || thermal_bubble_active) return;
        if ((millis() - last_thermal_check) < g_A10_config.thermal_check_interval_ms) return;
        last_thermal_check = millis();

        if (A10_getRandom01() < thermal_bubble_frequency) {
            thermal_bubble_active = true;
            thermal_bubble_start_time = millis()/1000.0f;
            thermal_bubble_duration = A10_randRange(6.0f, 12.0f);
        }
    }

    // --------------------------------------------------
    // Phase 전환
    // --------------------------------------------------
    void updateWeatherPhase() {
        if (!wind_simulation_active) return;
        float now = millis()/1000.0f;
        if (now - phase_start_time < phase_duration) return;

        T_A10_WindWeatherPhase_t old = current_weather_phase;
        float r = A10_getRandom01();
        if (old == EN_A10_WEATHER_PHASE_CALM)
            current_weather_phase = (r < 0.7f)? EN_A10_WEATHER_PHASE_NORMAL : EN_A10_WEATHER_PHASE_STRONG;
        else if (old == EN_A10_WEATHER_PHASE_STRONG)
            current_weather_phase = (r < 0.7f)? EN_A10_WEATHER_PHASE_NORMAL : EN_A10_WEATHER_PHASE_CALM;
        else
            current_weather_phase = (r < 0.4f)? EN_A10_WEATHER_PHASE_CALM : (r < 0.8f? EN_A10_WEATHER_PHASE_NORMAL : EN_A10_WEATHER_PHASE_STRONG);

        phase_start_time = now;
        float span = base_wind_max - base_wind_min;
        if (current_weather_phase == EN_A10_WEATHER_PHASE_CALM) {
            phase_duration = A10_randRange(90.0f, 210.0f);
            phase_wind_min = base_wind_min;
            phase_wind_max = base_wind_min + span*0.6f;
        } else if (current_weather_phase == EN_A10_WEATHER_PHASE_NORMAL) {
            phase_duration = A10_randRange(120.0f, 300.0f);
            phase_wind_min = base_wind_min + span*0.15f;
            phase_wind_max = base_wind_min + span*0.85f;
        } else {
            phase_duration = A10_randRange(60.0f, 150.0f);
            phase_wind_min = base_wind_min + span*0.4f;
            phase_wind_max = base_wind_max;
        }
        generateWindTarget();
    }

    // --------------------------------------------------
    // 목표 풍속 생성
    // --------------------------------------------------
    void generateWindTarget() {
        float range = phase_wind_max - phase_wind_min;
        target_wind_speed = phase_wind_min + A10_getRandom01()*range;
        float var = g_A10_config.wind_variability / 100.0f;
        float base_rate = (current_weather_phase == EN_A10_WEATHER_PHASE_CALM)? 0.08f + var*0.12f :
                          (current_weather_phase == EN_A10_WEATHER_PHASE_STRONG)? 0.25f + var*0.35f :
                          0.15f + var*0.25f;
        float U = fmax(current_wind_speed,0.1f);
        float time_scale = g_A10_config.turbulence_length_scale / U;
        base_rate *= (1.0f + time_scale * 0.1f);
        wind_change_rate = base_rate * A10_randRange(0.7f, 1.7f);
    }

    // --------------------------------------------------
    // Tick (메인 루프)
    // --------------------------------------------------
    void tick() {
        unsigned long now = millis();
        static uint32_t s_jitter = 0;
        if (now - last_wind_sim_update < (g_A10_config.wind_sim_interval_ms + (s_jitter % 300))) return;
        s_jitter = esp_random();

        float dt = (now - last_wind_sim_update)/1000.0f;
        last_wind_sim_update = now;

        updateWeatherPhase();
        calculateVonKarman(dt);
        calculateThermal();
        updateGust();
        updateThermalCheck();

        float diff = target_wind_speed - current_wind_speed;
        float change = diff * wind_change_rate * dt;
        wind_momentum = wind_momentum*0.85f + change*0.15f;
        wind_momentum = fmaxf(-0.5f, fminf(0.5f, wind_momentum));
        float v_new = current_wind_speed + wind_momentum + spectral_energy_buffer;
        current_wind_speed = fmaxf(0.2f, fminf(11.0f, v_new));

        float fan_pct = current_wind_speed*10.0f + 10.0f;
        fan_pct *= gust_intensity;
        fan_pct += current_thermal_contribution * 5.0f;
        applyFanSpeed(fan_pct);
    }
};

