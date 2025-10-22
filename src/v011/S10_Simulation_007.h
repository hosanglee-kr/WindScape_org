#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : S10_Simulation_007.h
 * 모듈명 : WindScape 시뮬레이션 Manager
 * ------------------------------------------------------
 * 기능 요약:
 *  - 바람 시뮬레이션(Phase/난류/돌풍/열기포/팬 제어)
 *  - Von Kármán 스펙트럼 합성, Phase 전환/히스토리,
 *    목표 재생성 규칙, 지터/yield 포함
 *  - 프리셋 테이블(006의 상세 로직) 완전 복구
 *  - 최신 A10_Const_007.h 구조/단위(%)와 호환
 * ------------------------------------------------------
 * 튜닝 가이드 (필요 시 미세조정)
 *  - 강도 더 세게: location_gust_strength를 0.1~0.3 상향
 *  - 돌풍 더 자주: gust_probability_base를 0.005~0.02 조정
 *  - 열기포 더 자주: thermal_bubble_frequency를 0.005~0.02 조정
 *  - 전체 풍량(체감 세기): base_wind_min/max 폭 조절 또는
 *    사용자 wind_intensity(% 설정)로 조절
 *  - 돌풍 실제 발생확률 ≈ gust_probability_base ×
 *    (user_freq × phase_mul × wind_factor)
 * ------------------------------------------------------
 * [코드 네이밍 규칙]
 * 		- 현재 파일 모듈약어    : S10
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

#include <Arduino.h>
#include <cmath>

#include "A10_Const_007.h"
#include "D10_Logger_004.h"
#include "P10_PWM_ctrl_005.h"

class CL_S10_Simulation {
  public:
	// ===== 외부에서 읽는 상태 =====
	bool  wind_simulation_active = false;
	bool  fan_power_enabled      = true;
	float current_wind_speed     = 3.6f;   // m/s
	float target_wind_speed      = 3.6f;   // m/s
	float wind_change_rate       = 0.1f;
	float wind_momentum          = 0.0f;

	// Phase
	T_A10_WindWeatherPhase_t current_weather_phase = EN_A10_WEATHER_PHASE_NORMAL;
	float                    phase_start_time      = 0.0f; // sec
	float                    phase_duration        = 120.0f; // sec
	float                    phase_wind_min        = 2.0f;  // m/s
	float                    phase_wind_max        = 6.0f;  // m/s

	// 베이스 환경/확률(프리셋 반영)
	float base_wind_min              = 1.8f;
	float base_wind_max              = 5.5f;
	float gust_probability_base      = 0.040f;
	float location_gust_strength     = 2.1f;  // 돌풍 강도 상한(배수)
	float thermal_bubble_frequency   = 0.022f;

	// 난류
	float spectral_energy_buffer     = 0.0f;
	float spectral_phase_accumulator = 0.0f;
	float turbulence_time_scale      = 5.0f;

	// 돌풍
	bool          gust_active     = false;
	float         gust_start_time = 0.0f; // sec
	float         gust_duration   = 3.0f; // sec
	float         gust_intensity  = 1.0f; // ×배
	unsigned long last_gust_check = 0;

	// 열기포
	bool          thermal_bubble_active          = false;
	float         thermal_bubble_start_time      = 0.0f; // sec
	float         thermal_bubble_duration        = 8.0f; // sec
	float         current_thermal_contribution   = 0.0f; // +/-
	unsigned long last_thermal_check             = 0;

	// 타이머
	unsigned long last_wind_sim_update           = 0;

	// ======================================================
	// 초기화 (프리셋 적용 포함)
	// ======================================================
	void begin(CL_P10_PWM &p_pwmCtrl, bool p_applyPreset = true) {
		_pwmCtrl = &p_pwmCtrl; // PWM 제어 객체 저장
		if (p_applyPreset) {
			applyCurrentPreset(true);
		}
	}

	// ======================================================
	// 프리셋 이름 → 인덱스
	// ======================================================
	bool getPresetIndexByName(const char *p_name, int &p_idx) {
		for (int v_i = 0; v_i < EN_A10_PRESET_COUNT; v_i++) {
			if (strcmp(p_name, g_A10_PRESET_MODE_NAMES_Arr[v_i]) == 0) {
				p_idx = v_i;
				return true;
			}
		}
		return false;
	}

	// ======================================================
	// 프리셋 적용 (006 상세 로직 복원)
	//  - 프리셋별 base_wind_min/max, gust_prob, gust_strength, thermal_freq
	//  - OFF면 최소풍 적용 후 시뮬 OFF, 그 외 startWindSimulation()
	// ======================================================
	void applyCurrentPreset(bool p_force_apply) {
		(void)p_force_apply;

		// 공통 초기화
		wind_simulation_active = false;
		gust_active            = false;
		thermal_bubble_active  = false;
		gust_intensity         = 1.0f;

		T_A10_PresetMode_t v_preset = (T_A10_PresetMode_t)g_A10_config.preset_mode_index;

		if (v_preset == EN_A10_PRESET_OFF) {
			fan_power_enabled   = true;
			float v_steady      = (g_A10_config.minimum_fan_speed > 0.0f) ? g_A10_config.minimum_fan_speed : 12.0f;
			applyFanSpeed(v_steady);
			current_wind_speed  = 0.5f;
			target_wind_speed   = 0.5f;
			return;
		}

		// ===== 프리셋별 환경값 스위치 (006 버전 로직 유지/보강) =====
		switch (v_preset) {
			case EN_A10_PRESET_COUNTRY:
				base_wind_min            = 0.7f;
				base_wind_max            = 3.4f;
				gust_probability_base    = 0.006f;
				location_gust_strength   = 1.35f;
				thermal_bubble_frequency = 0.015f;
				break;

			case EN_A10_PRESET_MEDITERRANEAN:
				base_wind_min            = 1.6f;
				base_wind_max            = 3.8f;
				gust_probability_base    = 0.012f;
				location_gust_strength   = 1.55f;
				thermal_bubble_frequency = 0.035f;
				break;

			case EN_A10_PRESET_OCEAN:
				base_wind_min            = 1.8f;
				base_wind_max            = 5.5f;
				gust_probability_base    = 0.040f;
				location_gust_strength   = 2.1f;
				thermal_bubble_frequency = 0.022f;
				break;

			case EN_A10_PRESET_MOUNTAIN:
				base_wind_min            = 2.2f;
				base_wind_max            = 7.5f;
				gust_probability_base    = 0.045f;
				location_gust_strength   = 2.2f;
				thermal_bubble_frequency = 0.028f;
				break;

			case EN_A10_PRESET_PLAINS:
				base_wind_min            = 4.0f;
				base_wind_max            = 8.8f;
				gust_probability_base    = 0.070f;
				location_gust_strength   = 2.4f;
				thermal_bubble_frequency = 0.018f;
				break;

			// ===== 신규 프리셋 5종 (006에서 확장된 테이블 유지) =====
			case EN_A10_PRESET_HARBOR_BREEZE:
				// Harbor Breeze (항구 바람) Base 2.25–5.35 m/s
				base_wind_min            = 2.25f;
				base_wind_max            = 5.35f;
				gust_probability_base    = 0.025f; // Rolling/Gentle
				location_gust_strength   = 1.80f;
				thermal_bubble_frequency = 0.026f; // 해풍 난류 + 약한 대류
				break;

			case EN_A10_PRESET_FOREST_CANOPY:
				// Forest Canopy (숲속 바람) Base 1.35–4.00 m/s
				base_wind_min            = 1.35f;
				base_wind_max            = 4.00f;
				gust_probability_base    = 0.010f; // Rare
				location_gust_strength   = 1.50f;
				thermal_bubble_frequency = 0.012f; // 잔잔, 수면/휴식용
				break;

			case EN_A10_PRESET_URBAN_SUNSET:
				// Urban Sunset (도시 석양) Base 1.8–4.9 m/s
				base_wind_min            = 1.80f;
				base_wind_max            = 4.90f;
				gust_probability_base    = 0.030f; // 골목 난류 mix
				location_gust_strength   = 2.00f;
				thermal_bubble_frequency = 0.020f; // 열섬효과
				break;

			case EN_A10_PRESET_TROPICAL_RAIN:
				// Tropical Rain (열대 소나기) Base 3.15–8.05 m/s
				base_wind_min            = 3.15f;
				base_wind_max            = 8.05f;
				gust_probability_base    = 0.060f; // Sharp/Sustained
				location_gust_strength   = 2.20f;
				thermal_bubble_frequency = 0.038f; // 대류 활발
				break;

			case EN_A10_PRESET_DESERT_NIGHT:
				// Desert Night (사막의 밤) Base 0.9–3.1 m/s
				base_wind_min            = 0.90f;
				base_wind_max            = 3.10f;
				gust_probability_base    = 0.005f; // 매우 드묾
				location_gust_strength   = 1.30f;
				thermal_bubble_frequency = 0.008f; // 복사냉각 → 층류
				break;

			default:
				base_wind_min            = 1.8f;
				base_wind_max            = 5.5f;
				gust_probability_base    = 0.040f;
				location_gust_strength   = 2.1f;
				thermal_bubble_frequency = 0.022f;
				break;
		}

		startWindSimulation();
	}

	// ======================================================
	// 시뮬 시작
	// ======================================================
	void startWindSimulation() {
		float v_mid                 = (base_wind_min + base_wind_max) * 0.5f;
		current_wind_speed          = v_mid;
		target_wind_speed           = v_mid;
		wind_momentum               = 0.0f;
		spectral_energy_buffer      = 0.0f;
		spectral_phase_accumulator  = 0.0f;

		current_weather_phase       = EN_A10_WEATHER_PHASE_NORMAL;
		phase_start_time            = millis() / 1000.0f;
		phase_duration              = 120.0f;

		float v_span                = base_wind_max - base_wind_min;
		phase_wind_min              = base_wind_min + v_span * 0.15f;
		phase_wind_max              = base_wind_min + v_span * 0.85f;

		wind_simulation_active      = true;
		generateWindTarget();
	}

	// ======================================================
	// 팬 속도 반영 (강도/최소/최대 반영) - PWM 컨트롤러 사용
	//  - p_speed_percent: 0~100 [%]
	// ======================================================
	void applyFanSpeed(float p_speed_percent) {
		if (!_PWMReady()) return;

		float v_req       = p_speed_percent / 100.0f; // 0~1.0
		float v_limit     = g_A10_config.fan_speed_limit / 100.0f;
		float v_min       = g_A10_config.minimum_fan_speed / 100.0f;
		float v_intensity = g_A10_config.wind_intensity / 100.0f;

		if (!fan_power_enabled) {
			_pwmCtrl->set_pwmDuty(0.0f);
			return;
		}
		if (v_intensity <= 0.01f) {
			_pwmCtrl->set_pwmDuty(0.0f);
			return;
		}
		if (wind_simulation_active) {
			v_req *= v_intensity;
		}

		v_req = fmaxf(v_min, fminf(v_limit, v_req));
		float v_final_percent = v_req * 100.0f;
		_pwmCtrl->set_pwmDuty(v_final_percent);
	}

	// ======================================================
	// Von Kármán 난류 합성
	// ======================================================
	void calculateVonKarman(float p_dt) {
		if (!wind_simulation_active) return;

		float v_L    = g_A10_config.turbulence_length_scale;
		float v_sigma= g_A10_config.turbulence_intensity_sigma;
		float v_U    = current_wind_speed;
		if (v_U < 0.1f) v_U = 0.1f;

		float v_sum = 0.0f;
		for (int v_i = 1; v_i <= 12; v_i++) {
			float v_n   = v_i * 0.1f;
			float v_f   = v_n * v_U / v_L;
			float v_fL_U= v_f * v_L / v_U;
			float v_term= 70.8f * v_fL_U * v_fL_U;

			float v_numer = 4.0f * v_sigma * v_sigma * (v_L / v_U) * (1.0f + v_term);
			float v_denom = powf(1.0f + v_term, 5.0f / 6.0f);
			float v_S     = v_numer / v_denom;

			float v_phase_rate = 2.0f * M_PI * v_f;
			float v_phase_inc  = v_phase_rate * p_dt;
			float v_phase_jit  = A10_randRange(-0.1f, 0.1f);
			float v_phase      = spectral_phase_accumulator * v_i + v_phase_inc + v_phase_jit;

			float v_amp = sqrtf(2.0f * v_S * 0.083f);
			v_sum += v_amp * sinf(v_phase);
		}

		spectral_phase_accumulator += p_dt * 0.5f;
		if (spectral_phase_accumulator > 2.0f * M_PI)
			spectral_phase_accumulator -= 2.0f * M_PI;

		float v_corr = expf(-p_dt / turbulence_time_scale);
		spectral_energy_buffer = spectral_energy_buffer * v_corr + v_sum * (1.0f - v_corr);
	}

	// ======================================================
	// 열기포 기여 계산
	// ======================================================
	void calculateThermal() {
		if (!wind_simulation_active) return;

		current_thermal_contribution = 0.0f;
		if (!thermal_bubble_active)  return;

		float v_t   = millis() / 1000.0f;
		float v_age = v_t - thermal_bubble_start_time;
		if (v_age >= thermal_bubble_duration) {
			thermal_bubble_active = false;
			return;
		}

		float v_prog = v_age / thermal_bubble_duration;
		float v_env  = 0.0f;
		if (v_prog < 0.2f) {
			v_env = 1.0f - powf(1.0f - v_prog / 0.2f, 2.0f);
		} else if (v_prog < 0.6f) {
			v_env = 1.0f;
			float v_osc = 0.8f + (current_weather_phase * 0.2f);
			v_env += sinf(v_age * v_osc * 2.0f * M_PI) * 0.15f;
		} else {
			float v_d = (v_prog - 0.6f) / 0.4f;
			v_env     = 1.0f - powf(v_d, 1.3f);
		}
		float v_strength = g_A10_config.thermal_bubble_strength * v_env;
		current_thermal_contribution = v_strength - 1.0f; // -1~+?
	}

	// ======================================================
	// 돌풍 상태 갱신
	// ======================================================
	void updateGust() {
		if (!wind_simulation_active) return;

		float v_now = millis() / 1000.0f;
		if (gust_active) {
			float v_age = v_now - gust_start_time;
			if (v_age >= gust_duration) {
				gust_active   = false;
				gust_intensity= 1.0f;
				return;
			}

			float v_prog = v_age / gust_duration;
			float v_env;
			if (v_prog < 0.25f) {
				v_env = 1.0f - powf(1.0f - v_prog / 0.25f, 1.8f);
			} else if (v_prog < 0.65f) {
				v_env = 1.0f;
				float v_osc = 1.5f + (current_weather_phase * 0.5f);
				v_env += sinf(v_age * v_osc) * 0.08f;
			} else {
				float v_d = (v_prog - 0.65f) / 0.35f;
				v_env     = 1.0f - powf(v_d, 1.5f);
			}
			gust_intensity = 1.0f + (location_gust_strength - 1.0f) * v_env;
			return;
		}

		// 새 돌풍 발생 체크
		if ((millis() - last_gust_check) >= (unsigned long)g_A10_config.gust_check_interval_ms) {
			last_gust_check = millis();

			float v_base_prob  = gust_probability_base;
			float v_user_freq  = g_A10_config.gust_frequency / 100.0f; // 0~1
			float v_wind_fac   = 1.0f + (current_wind_speed / 8.9f) * 0.5f;
			float v_phase_mul  = 1.0f;
			if (current_weather_phase == EN_A10_WEATHER_PHASE_CALM)      v_phase_mul = 0.3f * v_wind_fac;
			else if (current_weather_phase == EN_A10_WEATHER_PHASE_STRONG)v_phase_mul = 2.2f * v_wind_fac;
			else                                                          v_phase_mul = 0.9f * v_wind_fac;

			float v_final_p = v_base_prob * v_user_freq * v_phase_mul;
			if (A10_getRandom01() < v_final_p) {
				gust_active     = true;
				gust_start_time = v_now;

				float v_speed_f = current_wind_speed / 6.7f;
				if (current_weather_phase == EN_A10_WEATHER_PHASE_CALM) {
					gust_duration  = A10_randRange(3.0f, 8.0f);
					gust_intensity = A10_randRange(1.08f, 1.33f);
				} else if (current_weather_phase == EN_A10_WEATHER_PHASE_STRONG) {
					gust_duration  = A10_randRange(0.8f, 3.3f);
					gust_intensity = A10_randRange(1.3f, 1.3f + 0.9f * (1.0f + v_speed_f * 0.3f));
				} else {
					gust_duration  = A10_randRange(1.8f, 5.8f);
					gust_intensity = A10_randRange(1.15f, 1.15f + 0.5f * (1.0f + v_speed_f * 0.2f));
				}
				gust_intensity = fminf(gust_intensity, location_gust_strength);
			}
		}
	}

	// ======================================================
	// 열기포 트리거 체크
	// ======================================================
	void updateThermalCheck() {
		if (!wind_simulation_active || thermal_bubble_active) return;
		if ((millis() - last_thermal_check) < (unsigned long)g_A10_config.thermal_check_interval_ms) return;

		last_thermal_check = millis();

		float v_base  = thermal_bubble_frequency;
		float v_wfac  = 1.0f + (current_wind_speed / 8.0f) * 0.3f;
		float v_pmul  = (current_weather_phase == EN_A10_WEATHER_PHASE_CALM)
		              ? 1.2f
		              : (current_weather_phase == EN_A10_WEATHER_PHASE_STRONG ? 0.7f : 1.0f);

		float v_p = v_base * v_wfac * v_pmul;
		if (A10_getRandom01() < v_p) {
			thermal_bubble_active      = true;
			thermal_bubble_start_time  = millis() / 1000.0f;
			float v_dur = A10_randRange(8.0f, 14.0f);
			if (current_weather_phase == EN_A10_WEATHER_PHASE_CALM)      v_dur *= 1.3f;
			else if (current_weather_phase == EN_A10_WEATHER_PHASE_STRONG)v_dur *= 0.8f;
			thermal_bubble_duration = v_dur;
		}
	}

	// ======================================================
	// Phase 전환(히스토리 고려)
	// ======================================================
	void updateWeatherPhase() {
		if (!wind_simulation_active) return;

		float v_now = millis() / 1000.0f;
		if (v_now - phase_start_time < phase_duration) return;

		T_A10_WindWeatherPhase_t v_old = current_weather_phase;
		float v_r = A10_getRandom01();

		if (v_old == EN_A10_WEATHER_PHASE_CALM) {
			current_weather_phase = (v_r < 0.7f) ? EN_A10_WEATHER_PHASE_NORMAL : EN_A10_WEATHER_PHASE_STRONG;
		} else if (v_old == EN_A10_WEATHER_PHASE_STRONG) {
			current_weather_phase = (v_r < 0.7f) ? EN_A10_WEATHER_PHASE_NORMAL : EN_A10_WEATHER_PHASE_CALM;
		} else {
			if (v_r < 0.4f)      current_weather_phase = EN_A10_WEATHER_PHASE_CALM;
			else if (v_r < 0.8f) current_weather_phase = EN_A10_WEATHER_PHASE_NORMAL;
			else                 current_weather_phase = EN_A10_WEATHER_PHASE_STRONG;
		}

		phase_start_time = v_now;
		float v_span = base_wind_max - base_wind_min;

		if (current_weather_phase == EN_A10_WEATHER_PHASE_CALM) {
			phase_duration = A10_randRange(90.0f, 210.0f);
			phase_wind_min = base_wind_min;
			phase_wind_max = base_wind_min + v_span * 0.6f;
		} else if (current_weather_phase == EN_A10_WEATHER_PHASE_NORMAL) {
			phase_duration = A10_randRange(120.0f, 300.0f);
			phase_wind_min = base_wind_min + v_span * 0.15f;
			phase_wind_max = base_wind_min + v_span * 0.85f;
		} else {
			phase_duration = A10_randRange(60.0f, 150.0f);
			phase_wind_min = base_wind_min + v_span * 0.4f;
			phase_wind_max = base_wind_max;
		}

		phase_wind_min = fmaxf(0.2f, phase_wind_min);
		phase_wind_max = fminf(11.0f, phase_wind_max);

		generateWindTarget();
	}

	// ======================================================
	// 새 목표 풍속 생성
	// ======================================================
	void generateWindTarget() {
		if (!wind_simulation_active) return;

		float v_range = phase_wind_max - phase_wind_min;
		float v_new   = phase_wind_min + A10_getRandom01() * v_range;
		float v_mid   = (phase_wind_min + phase_wind_max) * 0.5f;
		float v_bias  = A10_randRange(0.0f, 1.0f); // 중간값 쏠림
		v_new         = (v_new + v_mid * v_bias) / (1.0f + v_bias);
		target_wind_speed = v_new;

		// 변화 속도
		float v_var  = g_A10_config.wind_variability / 100.0f; // 0~1
		float v_base = 0.0f;
		if (current_weather_phase == EN_A10_WEATHER_PHASE_CALM)       v_base = 0.08f + v_var * 0.12f;
		else if (current_weather_phase == EN_A10_WEATHER_PHASE_STRONG) v_base = 0.25f + v_var * 0.35f;
		else                                                           v_base = 0.15f + v_var * 0.25f;

		float v_U = current_wind_speed; if (v_U < 0.1f) v_U = 0.1f;
		float v_tscale = g_A10_config.turbulence_length_scale / v_U;
		v_base *= (1.0f + v_tscale * 0.1f);

		wind_change_rate = v_base * A10_randRange(0.7f, 1.7f);
	}

	// ======================================================
	// 한 틱 계산
	// ======================================================
	void tick() {
		unsigned long v_now_ms = millis();

		// 업데이트 간격 + 지터
		static uint32_t s_jitter = 0;
		if (v_now_ms - last_wind_sim_update < (unsigned long)(g_A10_config.wind_sim_interval_ms + (s_jitter % 300))) {
			yield();
			return;
		}
		s_jitter = esp_random();

		float v_dt_s = (float)(v_now_ms - last_wind_sim_update) / 1000.0f;
		last_wind_sim_update = v_now_ms;

		updateWeatherPhase();
		calculateVonKarman(v_dt_s);
		calculateThermal();

		// 목표로 수렴
		float v_target  = target_wind_speed;
		float v_current = current_wind_speed;
		float v_diff    = v_target - v_current;
		float v_change  = v_diff * wind_change_rate * v_dt_s;

		// 모멘텀
		wind_momentum = wind_momentum * 0.85f + v_change * 0.15f;
		wind_momentum = fmaxf(-0.5f, fminf(0.5f, wind_momentum));
		v_change      = wind_momentum;

		float v_new = v_current + v_change + spectral_energy_buffer; // 난류 합성
		v_new       = fmaxf(0.2f, fminf(11.0f, v_new));
		current_wind_speed = v_new;

		// 목표 재생성 규칙
		float v_change_th = 0.5f + (v_current / 20.0f);
		float v_close_ch  = 30.0f;
		float v_far_ch    = 6.0f;
		if (g_A10_config.wind_variability > 70.0f) {
			v_close_ch *= 1.5f;
			v_far_ch   *= 1.5f;
		}
		if (fabsf(v_diff) < v_change_th) {
			if (A10_randRange(0.0f, 100.0f) < v_close_ch)
				generateWindTarget();
		} else if (A10_randRange(0.0f, 100.0f) < v_far_ch) {
			generateWindTarget();
		}

		// 팬 출력: 돌풍/열기포 반영
		updateGust();
		updateThermalCheck();

		float v_fan_pct = current_wind_speed * 10.0f + 10.0f; // 0.2m/s≈12%, 6m/s≈70% 수준
		v_fan_pct *= gust_intensity;
		v_fan_pct += current_thermal_contribution * 5.0f; // thermal 기여

		applyFanSpeed(v_fan_pct);
		yield();
	}

  private:
	CL_P10_PWM *_PWM() const { return _pwmCtrl; }
	bool        _PWMReady() const { return _pwmCtrl != nullptr; }

  private:
	// PWM 제어기 포인터
	CL_P10_PWM *_pwmCtrl = nullptr;
};

