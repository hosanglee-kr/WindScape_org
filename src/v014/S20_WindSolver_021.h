#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : S20_WindSolver_021.h
 * 모듈 약어 : S20
 * 모듈명 : Smart Nature Wind - Wind Parameter Solver
 * ------------------------------------------------------
 * 기능 요약:
 *  - WindProfileDict 기반 프리셋/스타일/보정값을 조합하여
 *    최종 제어용 바람 파라미터(ResolvedWind) 계산
 *  - ControlManager(CT10), Simulation(S10) 등에서 공용 사용
 * ------------------------------------------------------
 * [구현 규칙]
 *  - ArduinoJson 의존 없음 (ConfigManager에서 역직렬화 완료된 구조체만 사용)
 *  - 순수 데이터 연산만 수행, 외부 상태 접근 금지
 *  - JsonDocument 생성/조작 금지
 *  - clamp 및 문자열 복사는 A10_Const_015.h의 공용 유틸 사용
 * ------------------------------------------------------
 * [코드 네이밍 규칙]
 *  - 전역 함수: S20_ 접두사
 *  - 타입: T_S20_ 접두사
 *  - 지역 변수: v_
 *  - 인자: p_
 * ------------------------------------------------------
 */

#include "A10_Const_016.h"

// ------------------------------------------------------
// Wind Profile 해석 함수
// ------------------------------------------------------
inline bool S20_resolveWindParams(
	const ST_A10_WindProfileDict_t& p_dict,
	const char*						p_presetCode,
	const char*						p_styleCode,
	const ST_A10_AdjustDelta_t*		p_adj,
	ST_A10_ResolvedWind_t&			p_out) {
	int16_t v_pi = A10_findPresetIndexByCode(p_dict, p_presetCode);
	if (v_pi < 0)
		return false;

	const ST_A10_PresetEntry_t& v_p = p_dict.presets[v_pi];

	float v_int	 = v_p.base.wind_intensity;
	float v_var	 = v_p.base.wind_variability;
	float v_gust = v_p.base.gust_frequency;
	float v_fl	 = v_p.base.fan_limit;
	float v_min	 = v_p.base.min_fan;
	float v_tL	 = v_p.base.turbulence_length_scale;
	float v_tS	 = v_p.base.turbulence_intensity_sigma;
	float v_thB	 = v_p.base.thermal_bubble_strength;
	float v_thR	 = v_p.base.thermal_bubble_radius;

	// 스타일 적용
	if (p_styleCode && p_styleCode[0]) {
		int16_t v_si = A10_findStyleIndexByCode(p_dict, p_styleCode);
		if (v_si >= 0) {
			const ST_A10_StyleEntry_t& v_s = p_dict.styles[v_si];
			v_int *= v_s.factors.intensity_factor;
			v_var *= v_s.factors.variability_factor;
			v_gust *= v_s.factors.gust_factor;
			v_thB *= v_s.factors.thermal_factor;
		}
	}

	// 사용자 보정값 적용
	if (p_adj) {
		v_int += p_adj->wind_intensity;
		v_var += p_adj->wind_variability;
		v_gust += p_adj->gust_frequency;
		v_fl += p_adj->fan_limit;
		v_min += p_adj->min_fan;
	}

	// 결과 클램프
	p_out.wind_intensity			 = A10_clampf(v_int, 0.0f, 100.0f);
	p_out.wind_variability			 = A10_clampf(v_var, 0.0f, 100.0f);
	p_out.gust_frequency			 = A10_clampf(v_gust, 0.0f, 100.0f);

	// min_fan ≤ fan_limit 보정 유지
	v_fl  += p_adj->fan_limit;
	v_min += p_adj->min_fan;

	float v_fl_clamped  = A10_clampf(v_fl,  0.0f, 100.0f);
	float v_min_clamped = A10_clampf(v_min, 0.0f, 100.0f);

	if (v_min_clamped > v_fl_clamped) {
		v_min_clamped = v_fl_clamped;
	}

	p_out.fan_limit = v_fl_clamped;
	p_out.min_fan   = v_min_clamped;

	// p_out.fan_limit					 = A10_clampf(v_fl, 0.0f, 100.0f);
	// p_out.min_fan					 = A10_clampf(v_min, 0.0f, 100.0f);

	p_out.turbulence_length_scale	 = (v_tL > 1.0f) ? v_tL : 1.0f;
	p_out.turbulence_intensity_sigma = (v_tS > 0.0f) ? v_tS : 0.0f;
	p_out.thermal_bubble_strength	 = (v_thB > 0.1f) ? v_thB : 0.1f;
	p_out.thermal_bubble_radius		 = (v_thR > 1.0f) ? v_thR : 1.0f;

	strlcpy(p_out.presetCode, p_presetCode ? p_presetCode : "", sizeof(p_out.presetCode));
	strlcpy(p_out.styleCode, p_styleCode ? p_styleCode : "", sizeof(p_out.styleCode));

	p_out.valid		 = true;
	p_out.fixedMode	 = false;
	p_out.fixedSpeed = 0.0f;

	return true;
}
