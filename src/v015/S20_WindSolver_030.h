#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : S20_WindSolver_030.h
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
 *  - clamp 및 문자열 복사는 A20_Const_015.h의 공용 유틸 사용
 * ------------------------------------------------------
 * [코드 네이밍 규칙]
 *  - 전역 함수: S20_ 접두사
 *  - 타입: T_S20_ 접두사
 *  - 지역 변수: v_
 *  - 인자: p_
 * ------------------------------------------------------
 */

#include "A20_Const_020.h"

// ------------------------------------------------------
// Wind Profile 해석 함수
// ------------------------------------------------------

inline bool S20_resolveWindParams(const ST_A20_WindProfileDict_t& p_dict, const char* p_presetCode, const char* p_styleCode, const ST_A20_AdjustDelta_t* p_adj, ST_A20_ResolvedWind_t& p_out) {
	// 0) 출력 안전 초기화 (실패해도 안전)
	memset(&p_out, 0, sizeof(p_out));
	p_out.valid	 = false;

	// 1) preset 찾기
	int16_t v_pi = A20_findPresetIndexByCode(p_dict, p_presetCode);
	if (v_pi < 0) {
		// presetCode/styleCode는 호출자가 확인 가능하도록 최소 복사
		strlcpy(p_out.presetCode, p_presetCode ? p_presetCode : "", sizeof(p_out.presetCode));
		strlcpy(p_out.styleCode, p_styleCode ? p_styleCode : "", sizeof(p_out.styleCode));
		return false;
	}

	const ST_A20_PresetEntry_t& v_p	   = p_dict.presets[v_pi];

	// 2) base 로드
	float						v_int  = v_p.base.wind_intensity;
	float						v_var  = v_p.base.wind_variability;
	float						v_gust = v_p.base.gust_frequency;
	float						v_fl   = v_p.base.fan_limit;
	float						v_min  = v_p.base.min_fan;

	float						v_tL   = v_p.base.turbulence_length_scale;
	float						v_tS   = v_p.base.turbulence_intensity_sigma;
	float						v_thB  = v_p.base.thermal_bubble_strength;
	float						v_thR  = v_p.base.thermal_bubble_radius;

	// 3) style factor 적용(스타일 없으면 스킵)
	if (p_styleCode && p_styleCode[0]) {
		int16_t v_si = A20_findStyleIndexByCode(p_dict, p_styleCode);
		if (v_si >= 0) {
			const ST_A20_StyleEntry_t& v_s = p_dict.styles[v_si];
			v_int *= v_s.factors.intensity_factor;
			v_var *= v_s.factors.variability_factor;
			v_gust *= v_s.factors.gust_factor;
			v_thB *= v_s.factors.thermal_factor;
			// (정책 선택) fan_limit/min_fan/turb도 스타일 영향 줄지 여부는 여기서 결정
		}
	}

	// 4) adjust 적용(널이면 0으로)
	const float v_adjInt  = p_adj ? p_adj->wind_intensity : 0.0f;
	const float v_adjVar  = p_adj ? p_adj->wind_variability : 0.0f;
	const float v_adjGust = p_adj ? p_adj->gust_frequency : 0.0f;
	const float v_adjFL	  = p_adj ? p_adj->fan_limit : 0.0f;
	const float v_adjMin  = p_adj ? p_adj->min_fan : 0.0f;

	v_int += v_adjInt;
	v_var += v_adjVar;
	v_gust += v_adjGust;
	v_fl += v_adjFL;
	v_min += v_adjMin;

	// 5) clamp + 관계 보정(min <= limit)
	p_out.wind_intensity   = A20_clampf(v_int, 0.0f, 100.0f);
	p_out.wind_variability = A20_clampf(v_var, 0.0f, 100.0f);
	p_out.gust_frequency   = A20_clampf(v_gust, 0.0f, 100.0f);

	float v_fl_c		   = A20_clampf(v_fl, 0.0f, 100.0f);
	float v_min_c		   = A20_clampf(v_min, 0.0f, 100.0f);
	if (v_min_c > v_fl_c)
		v_min_c = v_fl_c;

	p_out.fan_limit					 = v_fl_c;
	p_out.min_fan					 = v_min_c;

	// 6) 물리 파라미터 최소 보정
	p_out.turbulence_length_scale	 = (v_tL > 1.0f) ? v_tL : 1.0f;
	p_out.turbulence_intensity_sigma = (v_tS > 0.0f) ? v_tS : 0.0f;
	p_out.thermal_bubble_strength	 = (v_thB > 0.1f) ? v_thB : 0.1f;
	p_out.thermal_bubble_radius		 = (v_thR > 1.0f) ? v_thR : 1.0f;

	// 7) 코드 복사
	strlcpy(p_out.presetCode, p_presetCode ? p_presetCode : "", sizeof(p_out.presetCode));
	strlcpy(p_out.styleCode, p_styleCode ? p_styleCode : "", sizeof(p_out.styleCode));

	// 8) 기본 플래그
	p_out.fixedMode	 = false;
	p_out.fixedSpeed = 0.0f;
	p_out.valid		 = true;
	return true;
}

/*
inline bool S20_resolveWindParams(
	const ST_A20_WindProfileDict_t& p_dict,
	const char*						p_presetCode,
	const char*						p_styleCode,
	const ST_A20_AdjustDelta_t*		p_adj,
	ST_A20_ResolvedWind_t&			p_out) {
	int16_t v_pi = A20_findPresetIndexByCode(p_dict, p_presetCode);
	if (v_pi < 0)
		return false;

	const ST_A20_PresetEntry_t& v_p = p_dict.presets[v_pi];

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
		int16_t v_si = A20_findStyleIndexByCode(p_dict, p_styleCode);
		if (v_si >= 0) {
			const ST_A20_StyleEntry_t& v_s = p_dict.styles[v_si];
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
	p_out.wind_intensity			 = A20_clampf(v_int, 0.0f, 100.0f);
	p_out.wind_variability			 = A20_clampf(v_var, 0.0f, 100.0f);
	p_out.gust_frequency			 = A20_clampf(v_gust, 0.0f, 100.0f);

	// min_fan ≤ fan_limit 보정 유지
	v_fl  += p_adj->fan_limit;
	v_min += p_adj->min_fan;

	float v_fl_clamped  = A20_clampf(v_fl,  0.0f, 100.0f);
	float v_min_clamped = A20_clampf(v_min, 0.0f, 100.0f);

	if (v_min_clamped > v_fl_clamped) {
		v_min_clamped = v_fl_clamped;
	}

	p_out.fan_limit = v_fl_clamped;
	p_out.min_fan   = v_min_clamped;

	// p_out.fan_limit					 = A20_clampf(v_fl, 0.0f, 100.0f);
	// p_out.min_fan					 = A20_clampf(v_min, 0.0f, 100.0f);

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
*/
