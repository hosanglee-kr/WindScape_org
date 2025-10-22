요청하신 대로 두 가지 온도 기반 제어 방식인 **풍속 강도 조절 (체감 반영)**과 **열기포 발생 빈도 조절 (대류 반영)**을 통합하여 적용하고, 각 기능별 활성화/비활성화 설정을 포함한 최종 정리 내용을 제공해 드립니다.
1. 통합 설정 항목 정의 (Configuration Items)
이 기능들을 제어하기 위해 A10_Const_007.h의 설정 구조체(ST_A10_Config_t 등)에 다음과 같은 6가지 항목이 추가되어야 합니다.
| 설정 항목 | 타입 | 단위 | 기본값 | 설명 |
|---|---|---|---|---|
| enable_thermal_fan_boost | bool | N/A | true | [체감] 온도 기반 팬 듀티(\%) 부스트 활성화 여부. |
| thermal_fan_boost_base_temp | float | ^\circ C | 25.0f | 체감 Fan Boost가 시작되는 기준 온도. |
| thermal_fan_boost_per_degree | float | \% / ^\circ C | 0.5f | 기준 온도 초과 시 1^\circ C당 추가되는 PWM 듀티 퍼센트($%$p). |
| enable_thermal_freq_boost | bool | N/A | true | [대류] 온도 기반 열기포 발생 빈도 부스트 활성화 여부. |
| thermal_freq_boost_base_temp | float | ^\circ C | 25.0f | 빈도 부스트가 시작되는 기준 온도. |
| thermal_freq_boost_per_degree | float | \% / ^\circ C | 4.0f | 기준 온도 초과 시 1^\circ C당 열기포 빈도가 증가하는 비율(\%). |
2. 제어 로직 반영 (Control Logic Implementation)
새로운 설정 항목(g_A10_config)과 외부 온도 변수(g_A10_current_temperature)를 사용하여 해당 함수들을 수정합니다.
A. 풍속 강도 조절 (체감 반영) 로직: applyFanSpeed()
이 로직은 enable_thermal_fan_boost 플래그가 참일 때만 실행되어, 최종 요구 PWM 듀티에 추가적인 보정값(Boost)을 더합니다.
void applyFanSpeed(float p_speed_percent) {
    // ... (기존 로직: v_req, v_limit, v_min, v_intensity 계산) ...

    if (wind_simulation_active) {
        v_req *= v_intensity;
    }

    // ======================================================
    // 🔥 [로직 1: 온도 기반 체감 Fan Boost]
    // ======================================================
    if (g_A10_config.enable_thermal_fan_boost) { // <--- 활성화 플래그 체크
        float v_base_temp = g_A10_config.thermal_fan_boost_base_temp;
        
        if (g_A10_current_temperature > v_base_temp) {
            float v_temp_diff = g_A10_current_temperature - v_base_temp;
            
            // 1도당 추가되는 %를 0~1.0 범위로 변환하여 v_req에 합산
            float v_boost_adj = v_temp_diff * (g_A10_config.thermal_fan_boost_per_degree / 100.0f);
            v_req += v_boost_adj;
        }
    }
    // ======================================================

    v_req = fmaxf(v_min, fminf(v_limit, v_req));
    float v_final_percent = v_req * 100.0f;
    _pwmCtrl->set_pwmDuty(v_final_percent);
}

B. 열기포 발생 빈도 조절 (대류 반영) 로직: updateThermalCheck()
이 로직은 enable_thermal_freq_boost 플래그가 참일 때만 실행되어, 열기포 발생 확률(v\_p)에 추가적인 승수(Factor)를 곱합니다.
void updateThermalCheck() {
    // ... (기존 로직: last_thermal_check, v_base, v_wfac, v_pmul 계산) ...

    // ======================================================
    // 🔥 [로직 2: 온도 기반 열기포 빈도 부스트]
    // ======================================================
    float v_temp_boost_factor = 1.0f; // 기본 계수 1.0

    if (g_A10_config.enable_thermal_freq_boost) { // <--- 활성화 플래그 체크
        float v_base_temp = g_A10_config.thermal_freq_boost_base_temp;
        
        if (g_A10_current_temperature > v_base_temp) {
            float v_temp_diff = g_A10_current_temperature - v_base_temp;
            
            // 1.0 + (1도당 증가 비율)로 승수 계산
            v_temp_boost_factor += v_temp_diff * (g_A10_config.thermal_freq_boost_per_degree / 100.0f);
        }
    }
    // ======================================================
    
    // 최종 발생 확률: v_base * v_wfac * v_pmul * v_temp_boost_factor
    float v_p = v_base * v_wfac * v_pmul * v_temp_boost_factor; 

    if (A10_getRandom01() < v_p) {
        // ... (열기포 발생 로직) ...
    }
}

3. 튜닝 가이드
온도 관련 튜닝 항목들은 각각 독립적으로 작동하므로, 원하는 체감 효과와 기상 현실성을 분리하여 조정할 수 있습니다.
| 목표 | 조정 항목 | 조정 방향 | 효과 (예시) |
|---|---|---|---|
| 체감 강도 민감도 | thermal_fan_boost_per_degree | 상향/하향 | \uparrow : 1^\circ C만 올라도 팬이 더 세게 돌게 됨. |
| 체감 강도 시작점 | thermal_fan_boost_base_temp | 상향/하향 | \uparrow : 25^\circ C 대신 28^\circ C부터 부스트가 시작되어 느슨하게 제어됨. |
| 대류 빈도 민감도 | thermal_freq_boost_per_degree | 상향/하향 | \uparrow : 1^\circ C당 열기포 발생 확률이 급격히 증가 (더 불규칙해짐). |
| 대류 빈도 시작점 | thermal_freq_boost_base_temp | 상향/하향 | \downarrow : 22^\circ C 등 낮은 온도에서도 대류 불안정성이 시작됨. |
| 기능 전체 활성화 | enable_thermal_fan_boost enable_thermal_freq_boost | true/false | 각 기능을 완전히 켜거나 끌 수 있어 시뮬레이션 테스트 및 사용자 선택에 유용함. |
튜닝 팁:
 * 체감 강도 부스트(fan_boost)는 PWM 듀티(\%)에 덧셈으로 작용하므로, 1.0f를 초과하지 않도록 매우 작은 값(0.1 \sim 1.0)으로 튜닝해야 합니다.
 * 열기포 빈도 부스트(freq_boost)는 기존 확률에 곱셈으로 작용하므로, 100\% 초과 시 빈도 자체가 너무 높아질 수 있어 1.0 \sim 5.0 사이의 작은 비율로 튜닝하는 것이 좋습니다.
