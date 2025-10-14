// S10_Simulation_004.h

#pragma once
#include <Arduino.h>

#include <cmath>

#include "A10_Const_004.h"
#include "D10_Logger_004.h"

// 바람 시뮬레이션(Phase/난류/돌풍/열기포/팬 제어)
// - 원본의 Von Kármán 스펙트럼 합성, Phase 전환/히스토리, 목표재생성, 지터/yield 포함

// //튜닝 가이드 (필요 시 미세조정)
// 강도 더 세게: location_gust_strength를 0.1~0.3 범위에서 상향
// 돌풍 더 자주: gust_probability_base를 0.005~0.02 단위로 조정
// 열기포 더 자주: thermal_bubble_frequency를 0.005~0.02 단위로 조정
// 전체 풍량(체감 세기): 프리셋별 base_wind_min/max 폭을 넓히거나, 사용자 wind_intensity(설정)로 조절

// > 참고: 실제 돌풍 빈도는 gust_probability_base × (user_freq × phase_mul × wind_factor)로 계산되어
// 유저 설정과 날씨 Phase에 의해 가중됩니다. 체감이 너무 드물거나 잦으면 base만 소폭 조절해도 충분합니다.

class S10_Simulation {
   public:
	// 외부에서 읽는 상태
	bool  wind_simulation_active = false;
	bool  fan_power_enabled		 = true;
	float current_wind_speed	 = 3.6f;
	float target_wind_speed		 = 3.6f;
	float wind_change_rate		 = 0.1f;
	float wind_momentum			 = 0.0f;

	// Phase
	SC10_WindWeatherPhase_t current_weather_phase = SC10_WEATHER_PHASE_NORMAL;
	float					phase_start_time	  = 0.0f;
	float					phase_duration		  = 120.0f;
	float					phase_wind_min		  = 2.0f;
	float					phase_wind_max		  = 6.0f;

	// 베이스 환경/확률(프리셋 반영)
	float base_wind_min			   = 1.8f;
	float base_wind_max			   = 5.5f;
	float gust_probability_base	   = 0.040f;
	float location_gust_strength   = 2.1f;
	float thermal_bubble_frequency = 0.022f;

	// 난류
	float spectral_energy_buffer	 = 0.0f;
	float spectral_phase_accumulator = 0.0f;
	float turbulence_time_scale		 = 5.0f;

	// 돌풍
	bool		  gust_active	  = false;
	float		  gust_start_time = 0.0f;
	float		  gust_duration	  = 3.0f;
	float		  gust_intensity  = 1.0f;
	unsigned long last_gust_check = 0;

	// 열기포
	bool		  thermal_bubble_active		   = false;
	float		  thermal_bubble_start_time	   = 0.0f;
	float		  thermal_bubble_duration	   = 8.0f;
	float		  current_thermal_contribution = 0.0f;
	unsigned long last_thermal_check		   = 0;

	// 타이머
	unsigned long last_wind_sim_update = 0;

	// 초기화 (프리셋 적용 포함)
	void begin(bool p_applyPreset = true) {
		// srand(esp_random());
		if (p_applyPreset) {
			applyCurrentPreset(true);
		}
	}

	// 프리셋 이름 → 인덱스
	bool getPresetIndexByName(const char* p_name, int& p_idx) {
		for (int i = 0; i < SC10_PRESET_COUNT; i++) {
			if (strcmp(p_name, G_SC10_PRESET_MODE_NAMES[i]) == 0) {
				p_idx = i;
				return true;
			}
		}
		return false;
	}

	// 프리셋 적용
	void applyCurrentPreset(bool p_force_apply) {
		wind_simulation_active = false;
		gust_active			   = false;
		thermal_bubble_active  = false;
		gust_intensity		   = 1.0f;

		SC10_PresetMode_t v_preset = (SC10_PresetMode_t)g_SC10_config.preset_mode_index;
		if (v_preset == SC10_PRESET_OFF) {
			fan_power_enabled = true;
			float v_steady	  = (g_SC10_config.minimum_fan_speed > 0.0f) ? g_SC10_config.minimum_fan_speed : 12.0f;
			applyFanSpeed(v_steady);
			current_wind_speed = 0.5f;
			target_wind_speed  = 0.5f;
		} else {
			// 환경값 스위치
			switch (v_preset) {
				case SC10_PRESET_COUNTRY:
					base_wind_min			 = 0.7f;
					base_wind_max			 = 3.4f;
					gust_probability_base	 = 0.006f;
					location_gust_strength	 = 1.35f;
					thermal_bubble_frequency = 0.015f;
					break;
				case SC10_PRESET_MEDITERRANEAN:
					base_wind_min			 = 1.6f;
					base_wind_max			 = 3.8f;
					gust_probability_base	 = 0.012f;
					location_gust_strength	 = 1.55f;
					thermal_bubble_frequency = 0.035f;
					break;
				case SC10_PRESET_OCEAN:
					base_wind_min			 = 1.8f;
					base_wind_max			 = 5.5f;
					gust_probability_base	 = 0.040f;
					location_gust_strength	 = 2.1f;
					thermal_bubble_frequency = 0.022f;
					break;
				case SC10_PRESET_MOUNTAIN:
					base_wind_min			 = 2.2f;
					base_wind_max			 = 7.5f;
					gust_probability_base	 = 0.045f;
					location_gust_strength	 = 2.2f;
					thermal_bubble_frequency = 0.028f;
					break;
				case SC10_PRESET_PLAINS:
					base_wind_min			 	= 4.0f;
					base_wind_max			 	= 8.8f;
					gust_probability_base	 	= 0.070f;
					location_gust_strength	 	= 2.4f;
					thermal_bubble_frequency 	= 0.018f;
					break;

				// ===== 신규 프리셋 5종 =====
				case SC10_PRESET_HARBOR_BREEZE: {
					// Harbor Breeze (항구 바람) 		// Base Wind: 5–12 mph ≈ 2.24–5.36 m/s
					base_wind_min 				= 2.25f;
					base_wind_max 				= 5.35f;
					gust_probability_base  		= 0.025f;	// Rolling/Gentle, 2.5% @ 1.8×
					location_gust_strength 		= 1.80f;
	
					thermal_bubble_frequency 	= 0.026f;	// 해풍/수평 난류 + 약한 대류
					break;
				}
				case SC10_PRESET_FOREST_CANOPY: {
					// Forest Canopy (숲 그늘 바람) 			// Base Wind: 3–9 mph ≈ 1.34–4.02 m/s
					
					base_wind_min 				= 1.35f;
					base_wind_max 				= 4.00f;
					gust_probability_base  		= 0.010f;		// Rare, 1% @ 1.5×
					location_gust_strength 		= 1.50f;
					thermal_bubble_frequency 	= 0.012f;	  // 수면/휴식용: 대류는 드물고 잔잔
					break;
				}
				case SC10_PRESET_URBAN_SUNSET: {
					// Urban Sunset (도시 석양 바람)			// Base Wind: 4–11 mph ≈ 1.79–4.92 m/s
					
					base_wind_min 				= 1.80f;
					base_wind_max 				= 4.90f;
					
					gust_probability_base  		= 0.030f;		// Gentle–Sharp mix, 3% @ 2.0× (골목 난류)
					location_gust_strength 		= 2.00f;
					
					thermal_bubble_frequency 	= 0.020f;		// 열섬효과로 완만한 대류
					break;
				}
				case SC10_PRESET_TROPICAL_RAIN: {
					// Tropical Rain (열대 소나기 바람)		// Base Wind: 7–18 mph ≈ 3.13–8.05 m/s
					
					base_wind_min 				= 3.15f;
					base_wind_max 				= 8.05f;
					
					gust_probability_base  		= 0.060f;		// Sharp/Sustained, 6% @ 2.2×
					location_gust_strength 		= 2.20f;
					
					thermal_bubble_frequency 	= 0.038f;		// 대류 활발 (소나기 전후)
					break;
				}
				case SC10_PRESET_DESERT_NIGHT: {
					// Desert Night (사막의 밤 바람)		// Base Wind: 2–7 mph ≈ 0.89–3.13 m/s
					
					base_wind_min 				= 0.90f;
					base_wind_max 				= 3.10f;
					
					gust_probability_base  		= 0.005f;	// Very rare & soft, 0.5% @ 1.3×
					location_gust_strength 		= 1.30f;
					
					thermal_bubble_frequency 	= 0.008f;	// 복사냉각 → 약한 하강/완만한 층류
					break;
				}
				// ==========================
				default:
					base_wind_min			 	= 1.8f;
					base_wind_max			 	= 5.5f;
					gust_probability_base	 	= 0.040f;
					location_gust_strength	 	= 2.1f;
					thermal_bubble_frequency 	= 0.022f;
					break;
			}
			startWindSimulation();
		}
	}

	// 시뮬 시작
	void startWindSimulation() {
		float v_mid				   = (base_wind_min + base_wind_max) * 0.5f;
		current_wind_speed		   = v_mid;
		target_wind_speed		   = v_mid;
		wind_momentum			   = 0.0f;
		spectral_energy_buffer	   = 0.0f;
		spectral_phase_accumulator = 0.0f;

		current_weather_phase = SC10_WEATHER_PHASE_NORMAL;
		phase_start_time	  = millis() / 1000.0f;
		phase_duration		  = 120.0f;

		float v_span   = base_wind_max - base_wind_min;
		phase_wind_min = base_wind_min + v_span * 0.15f;
		phase_wind_max = base_wind_min + v_span * 0.85f;

		wind_simulation_active = true;
		generateWindTarget();
	}

	// 팬 속도 반영 (강도/최소/최대 반영)
	void applyFanSpeed(float p_speed_percent) {
		if (!fan_power_enabled) {
			ledcWrite(g_SC10_config.pwm_channel, 0);
			return;
		}
		float v_req		  = p_speed_percent / 100.0f;
		float v_limit	  = g_SC10_config.fan_speed_limit / 100.0f;
		float v_min		  = g_SC10_config.minimum_fan_speed / 100.0f;
		float v_intensity = g_SC10_config.wind_intensity / 100.0f;

		if (v_intensity <= 0.01f) {
			ledcWrite(g_SC10_config.pwm_channel, 0);
			return;
		}
		if (wind_simulation_active) {
			v_req *= v_intensity;
		}
		v_req = fmax(v_min, fmin(v_limit, v_req));

		int v_levels = (1 << g_SC10_config.pwm_resolution) - 1;
		int v_pwm	 = (int)(v_req * v_levels);
		if (v_req <= 0.01f) {
			v_pwm = 0;
		}
		ledcWrite(g_SC10_config.pwm_channel, v_pwm);
	}

	// Von Kármán 난류 합성
	void calculateVonKarman(float p_dt) {
		if (!wind_simulation_active) {
			return;
		}

		float v_L	  = g_SC10_config.turbulence_length_scale;
		float v_sigma = g_SC10_config.turbulence_intensity_sigma;
		float v_U	  = current_wind_speed;

		if (v_U < 0.1f) {
			v_U = 0.1f;
		}

		float v_sum = 0.0f;
		for (int v_i = 1; v_i <= 12; v_i++) {
			float v_n	 = v_i * 0.1f;
			float v_f	 = v_n * v_U / v_L;
			float v_fL_U = v_f * v_L / v_U;
			float term	 = 70.8f * v_fL_U * v_fL_U;
			float numer	 = 4.0f * v_sigma * v_sigma * (v_L / v_U) * (1.0f + term);
			float denom	 = powf(1.0f + term, 5.0f / 6.0f);
			float S		 = numer / denom;

			float phase_rate  = 2.0f * M_PI * v_f;
			float phase_inc	  = phase_rate * p_dt;
			float phase_noise = A10_randRange(-0.1f, 0.1f);
			float cur_phase	  = spectral_phase_accumulator * v_i + phase_inc + phase_noise;

			float amp  = sqrtf(2.0f * S * 0.083f);
			float comp = amp * sinf(cur_phase);
			v_sum += comp;
		}
		spectral_phase_accumulator += p_dt * 0.5f;
		if (spectral_phase_accumulator > 2.0f * M_PI)
			spectral_phase_accumulator -= 2.0f * M_PI;

		float corr			   = expf(-p_dt / turbulence_time_scale);
		spectral_energy_buffer = spectral_energy_buffer * corr + v_sum * (1.0f - corr);
	}

	// 열기포 기여 계산
	void calculateThermal() {
		if (!wind_simulation_active)
			return;
		current_thermal_contribution = 0.0f;
		if (!thermal_bubble_active)
			return;

		float t	  = millis() / 1000.0f;
		float age = t - thermal_bubble_start_time;
		if (age >= thermal_bubble_duration) {
			thermal_bubble_active = false;
			return;
		}

		float prog = age / thermal_bubble_duration;
		float env  = 0.0f;
		if (prog < 0.2f) {
			env = 1.0f - powf(1.0f - prog / 0.2f, 2.0f);
		} else if (prog < 0.6f) {
			env		  = 1.0f;
			float osc = 0.8f + (current_weather_phase * 0.2f);
			env += sinf(age * osc * 2.0f * M_PI) * 0.15f;
		} else {
			float d = (prog - 0.6f) / 0.4f;
			env		= 1.0f - powf(d, 1.3f);
		}
		float strength				 = g_SC10_config.thermal_bubble_strength * env;
		current_thermal_contribution = strength - 1.0f;
	}

	// 돌풍 상태 갱신
	void updateGust() {
		if (!wind_simulation_active)
			return;
		float now = millis() / 1000.0f;
		if (gust_active) {
			float age = now - gust_start_time;
			if (age >= gust_duration) {
				gust_active	   = false;
				gust_intensity = 1.0f;
			} else {
				float prog = age / gust_duration;
				float env;
				if (prog < 0.25f)
					env = 1.0f - powf(1.0f - prog / 0.25f, 1.8f);
				else if (prog < 0.65f) {
					env		  = 1.0f;
					float osc = 1.5f + (current_weather_phase * 0.5f);
					env += sinf(age * osc) * 0.08f;
				} else {
					float d = (prog - 0.65f) / 0.35f;
					env		= 1.0f - powf(d, 1.5f);
				}
				gust_intensity = 1.0f + (location_gust_strength - 1.0f) * env;
			}
			return;
		}
		// 새 돌풍 발생 확률
		if ((millis() - last_gust_check) >= (unsigned long)g_SC10_config.gust_check_interval_ms) {
			last_gust_check	  = millis();
			float base_prob	  = gust_probability_base;
			float user_freq	  = g_SC10_config.gust_frequency / 100.0f;
			float wind_factor = 1.0f + (current_wind_speed / 8.9f) * 0.5f;
			float phase_mul	  = 1.0f;
			if (current_weather_phase == SC10_WEATHER_PHASE_CALM)
				phase_mul = 0.3f * wind_factor;
			else if (current_weather_phase == SC10_WEATHER_PHASE_STRONG)
				phase_mul = 2.2f * wind_factor;
			else
				phase_mul = 0.9f * wind_factor;

			float final_p = base_prob * user_freq * phase_mul;
			if (A10_randRange() < final_p) {
				gust_active		   = true;
				gust_start_time	   = now;
				float speed_factor = current_wind_speed / 6.7f;
				if (current_weather_phase == SC10_WEATHER_PHASE_CALM) {
					gust_duration  = A10_randRange(3.0f, 8.0f);
					gust_intensity = A10_randRange(1.08f, 1.33f);
				} else if (current_weather_phase == SC10_WEATHER_PHASE_STRONG) {
					gust_duration  = A10_randRange(0.8f, 3.3f);
					gust_intensity = A10_randRange(1.3f, 1.3f + 0.9f * (1.0f + speed_factor * 0.3f));
				} else {
					gust_duration  = A10_randRange(1.8f, 5.8f);
					gust_intensity = A10_randRange(1.15f, 1.15f + 0.5f * (1.0f + speed_factor * 0.2f));
				}
				gust_intensity = fminf(gust_intensity, location_gust_strength);
			}
		}
	}

	// 열기포 트리거 체크
	void updateThermalCheck() {
		if (!wind_simulation_active || thermal_bubble_active)
			return;
		if ((millis() - last_thermal_check) < (unsigned long)g_SC10_config.thermal_check_interval_ms)
			return;
		last_thermal_check = millis();
		float base		   = thermal_bubble_frequency;
		float wind_factor  = 1.0f + (current_wind_speed / 8.0f) * 0.3f;
		float phase_mul	   = (current_weather_phase == SC10_WEATHER_PHASE_CALM) ? 1.2f : (current_weather_phase == SC10_WEATHER_PHASE_STRONG) ? 0.7f
																																			  : 1.0f;
		float p			   = base * wind_factor * phase_mul;
		if (A10_randRange() < p) {
			thermal_bubble_active	  = true;
			thermal_bubble_start_time = millis() / 1000.0f;
			float dur				  = A10_randRange(8.0f, 14.0f);
			if (current_weather_phase == SC10_WEATHER_PHASE_CALM)
				dur *= 1.3f;
			else if (current_weather_phase == SC10_WEATHER_PHASE_STRONG)
				dur *= 0.8f;
			thermal_bubble_duration = dur;
		}
	}

	// Phase 전환(히스토리 고려)
	void updateWeatherPhase() {
		if (!wind_simulation_active)
			return;
		float now = millis() / 1000.0f;
		if (now - phase_start_time < phase_duration)
			return;

		SC10_WindWeatherPhase_t old = current_weather_phase;
		float					r	= A10_randRange();
		if (old == SC10_WEATHER_PHASE_CALM) {
			current_weather_phase = (r < 0.7f) ? SC10_WEATHER_PHASE_NORMAL : SC10_WEATHER_PHASE_STRONG;
		} else if (old == SC10_WEATHER_PHASE_STRONG) {
			current_weather_phase = (r < 0.7f) ? SC10_WEATHER_PHASE_NORMAL : SC10_WEATHER_PHASE_CALM;
		} else {
			if (r < 0.4f)
				current_weather_phase = SC10_WEATHER_PHASE_CALM;
			else if (r < 0.8f)
				current_weather_phase = SC10_WEATHER_PHASE_NORMAL;
			else
				current_weather_phase = SC10_WEATHER_PHASE_STRONG;
		}

		phase_start_time = now;
		float span		 = base_wind_max - base_wind_min;
		if (current_weather_phase == SC10_WEATHER_PHASE_CALM) {
			phase_duration = A10_randRange(90.0f, 210.0f);
			phase_wind_min = base_wind_min;
			phase_wind_max = base_wind_min + span * 0.6f;
		} else if (current_weather_phase == SC10_WEATHER_PHASE_NORMAL) {
			phase_duration = A10_randRange(120.0f, 300.0f);
			phase_wind_min = base_wind_min + span * 0.15f;
			phase_wind_max = base_wind_min + span * 0.85f;
		} else {
			phase_duration = A10_randRange(60.0f, 150.0f);
			phase_wind_min = base_wind_min + span * 0.4f;
			phase_wind_max = base_wind_max;
		}
		phase_wind_min = fmaxf(0.2f, phase_wind_min);
		phase_wind_max = fminf(11.0f, phase_wind_max);
		generateWindTarget();
	}

	// 새 목표 풍속 생성
	void generateWindTarget() {
		if (!wind_simulation_active)
			return;
		float range		  = phase_wind_max - phase_wind_min;
		float new_t		  = phase_wind_min + SC10_getRandom01() * range;
		float mid		  = (phase_wind_min + phase_wind_max) * 0.5f;
		float bias		  = A10_randRange(0.0f, 1.0f);
		new_t			  = (new_t + mid * bias) / (1.0f + bias);
		target_wind_speed = new_t;

		float var		= g_SC10_config.wind_variability / 100.0f;
		float base_rate = 0.0f;
		if (current_weather_phase == SC10_WEATHER_PHASE_CALM)
			base_rate = 0.08f + var * 0.12f;
		else if (current_weather_phase == SC10_WEATHER_PHASE_STRONG)
			base_rate = 0.25f + var * 0.35f;
		else
			base_rate = 0.15f + var * 0.25f;

		float U = current_wind_speed;
		if (U < 0.1f)
			U = 0.1f;
		float time_scale = g_SC10_config.turbulence_length_scale / U;
		base_rate *= (1.0f + time_scale * 0.1f);
		wind_change_rate = base_rate * A10_randRange(0.7f, 1.7f);
	}

	// 한 틱 계산
	void tick() {
		unsigned long v_now = millis();
		// 지터 포함
		static uint32_t s_jitter = 0;
		if (v_now - last_wind_sim_update < (unsigned long)(g_SC10_config.wind_sim_interval_ms + (s_jitter % 300))) {
			yield();
			return;
		}
		s_jitter			 = esp_random();
		float v_dt			 = (float)(v_now - last_wind_sim_update) / 1000.0f;
		last_wind_sim_update = v_now;

		updateWeatherPhase();
		calculateVonKarman(v_dt);
		calculateThermal();

		float v_target	= target_wind_speed;
		float v_current = current_wind_speed;
		float v_diff	= v_target - v_current;
		float v_change	= v_diff * wind_change_rate * v_dt;

		// 모멘텀
		wind_momentum = wind_momentum * 0.85f + v_change * 0.15f;
		wind_momentum = fmaxf(-0.5f, fminf(0.5f, wind_momentum));
		v_change	  = wind_momentum;

		float v_new		   = v_current + v_change + spectral_energy_buffer;	 // 난류 합성
		v_new			   = fmaxf(0.2f, fminf(11.0f, v_new));
		current_wind_speed = v_new;

		// 목표 재생성 규칙
		float change_th = 0.5f + (v_current / 20.0f);
		float close_ch	= 30.0f;
		float far_ch	= 6.0f;
		if (g_SC10_config.wind_variability > 70.0f) {
			close_ch *= 1.5f;
			far_ch *= 1.5f;
		}
		if (fabsf(v_diff) < change_th) {
			if (A10_randRange(0.0f, 100.0f) < close_ch)
				generateWindTarget();
		} else if (A10_randRange(0.0f, 100.0f) < far_ch) {
			generateWindTarget();
		}

		// 팬 출력: 돌풍/열기포 반영
		updateGust();
		updateThermalCheck();
		float fan_pct = current_wind_speed * 10.0f + 10.0f;
		fan_pct *= gust_intensity;
		fan_pct += current_thermal_contribution * 5.0f;
		applyFanSpeed(fan_pct);

		yield();
	}
};
