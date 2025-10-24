#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : S10_PresetTable_010.h
 * 모듈명 : Smart Nature Wind 프리셋 테이블
 * ------------------------------------------------------
 * 기능 요약:
 *  - 프리셋별 풍환경 파라미터 테이블 분리/제공
 *  - base_wind_min/max, gust/thermal 빈도, 강도 상한
 *  - Phase( CALM / NORMAL / STRONG )별
 *      - 풍속 범위 factor(기본폭 대비 비율)
 *      - 지속시간 범위(sec)
 *  - S10_Simulation에서 참조하여 applyCurrentPreset() 작성 단순화
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
 * 		- 클래스 private 멤버   : _ 접두사
 * 		- 클래스 정적 멤버      : s_ 접두사
 * 		- 로컬 변수             : v_ 접두사
 * 		- 함수 인자             : p_ 접두사
 */

#include <Arduino.h>
#include "A10_Const_010.h"

typedef struct {
	float min_factor;   // phase의 (base_min + span*min_factor)
	float max_factor;   // phase의 (base_min + span*max_factor)
	float dur_min_sec;  // phase 지속 최소
	float dur_max_sec;  // phase 지속 최대
} ST_S10_PhaseProfile;

typedef struct {
	const char*                  name;                     // g_A10_PRESET_MODE_NAMES_Arr[*]와 동일 문자열
	float                        base_wind_min;           // m/s
	float                        base_wind_max;           // m/s
	float                        gust_probability_base;   // 0.0~ (프리셋 기반 베이스 확률)
	float                        gust_strength_max;       // 돌풍 강도 상한 배수
	float                        thermal_frequency;       // 열기포 발생 기본 확률(틱 기반)
	ST_S10_PhaseProfile          phase[EN_A10_WEATHER_PHASE_COUNT]; // CALM/NORMAL/STRONG 프로필
} ST_S10_PresetRow;

class CL_S10_PresetTable {
public:
	// --------------------------------------------------
	// 프리셋 테이블 (OFF 포함)
	//  - OFF는 base_wind를 사용하지 않지만 호환 목적 포함
	// --------------------------------------------------
	static const ST_S10_PresetRow* S10_getTable(uint8_t& p_count) {
		p_count = (uint8_t)(sizeof(s_table)/sizeof(s_table[0]));
		return s_table;
	}

	static const ST_S10_PresetRow* S10_findByName(const char* p_name) {
		if (!p_name) return nullptr;
		uint8_t v_cnt=0;
		const ST_S10_PresetRow* v_tab = S10_getTable(v_cnt);
		for (uint8_t v_i=0; v_i<v_cnt; ++v_i) {
			if (strcasecmp(p_name, v_tab[v_i].name)==0) return &v_tab[v_i];
		}
		return nullptr;
	}
	static const ST_S10_PresetRow* S10_findByIndex(uint8_t p_index) {
		uint8_t v_cnt=0;
		const ST_S10_PresetRow* v_tab = S10_getTable(v_cnt);
		if (p_index>=v_cnt) return nullptr;
		return &v_tab[p_index];
	}

private:
	static constexpr ST_S10_PresetRow s_table[] = {
		// OFF (고정/최소풍만 사용, phase/dur은 무시)
		{
			"OFF", 1.0f, 1.0f, 0.0f, 1.0f, 0.0f,
			{
				{0.00f, 0.10f, 120.0f, 240.0f}, // CALM
				{0.10f, 0.20f, 120.0f, 240.0f}, // NORMAL
				{0.20f, 0.30f,  60.0f, 120.0f}  // STRONG
			}
		},
		// COUNTRY
		{
			"COUNTRY", 0.7f, 3.4f, 0.006f, 1.35f, 0.015f,
			{
				{0.00f, 0.60f,  90.0f, 210.0f}, // CALM
				{0.15f, 0.85f, 120.0f, 300.0f}, // NORMAL
				{0.40f, 1.00f,  60.0f, 150.0f}  // STRONG
			}
		},
		// MEDITERRANEAN
		{
			"MEDITERRANEAN", 1.6f, 3.8f, 0.012f, 1.55f, 0.035f,
			{
				{0.00f, 0.60f,  90.0f, 210.0f},
				{0.15f, 0.85f, 120.0f, 300.0f},
				{0.40f, 1.00f,  60.0f, 150.0f}
			}
		},
		// OCEAN
		{
			"OCEAN", 1.8f, 5.5f, 0.040f, 2.10f, 0.022f,
			{
				{0.00f, 0.60f,  90.0f, 210.0f},
				{0.15f, 0.85f, 120.0f, 300.0f},
				{0.40f, 1.00f,  60.0f, 150.0f}
			}
		},
		// MOUNTAIN
		{
			"MOUNTAIN", 2.2f, 7.5f, 0.045f, 2.20f, 0.028f,
			{
				{0.00f, 0.60f,  90.0f, 210.0f},
				{0.15f, 0.85f, 120.0f, 300.0f},
				{0.40f, 1.00f,  60.0f, 150.0f}
			}
		},
		// PLAINS
		{
			"PLAINS", 4.0f, 8.8f, 0.070f, 2.40f, 0.018f,
			{
				{0.00f, 0.60f,  90.0f, 210.0f},
				{0.15f, 0.85f, 120.0f, 300.0f},
				{0.40f, 1.00f,  60.0f, 150.0f}
			}
		},
		// HARBOR_BREEZE
		{
			"HARBOR_BREEZE", 2.25f, 5.35f, 0.025f, 1.80f, 0.026f,
			{
				{0.00f, 0.60f,  90.0f, 210.0f},
				{0.15f, 0.85f, 120.0f, 300.0f},
				{0.40f, 1.00f,  60.0f, 150.0f}
			}
		},
		// FOREST_CANOPY
		{
			"FOREST_CANOPY", 1.35f, 4.00f, 0.010f, 1.50f, 0.012f,
			{
				{0.00f, 0.60f,  90.0f, 210.0f},
				{0.15f, 0.85f, 120.0f, 300.0f},
				{0.40f, 1.00f,  60.0f, 150.0f}
			}
		},
		// URBAN_SUNSET
		{
			"URBAN_SUNSET", 1.80f, 4.90f, 0.030f, 2.00f, 0.020f,
			{
				{0.00f, 0.60f,  90.0f, 210.0f},
				{0.15f, 0.85f, 120.0f, 300.0f},
				{0.40f, 1.00f,  60.0f, 150.0f}
			}
		},
		// TROPICAL_RAIN
		{
			"TROPICAL_RAIN", 3.15f, 8.05f, 0.060f, 2.20f, 0.038f,
			{
				{0.00f, 0.60f,  90.0f, 210.0f},
				{0.15f, 0.85f, 120.0f, 300.0f},
				{0.40f, 1.00f,  60.0f, 150.0f}
			}
		},
		// DESERT_NIGHT
		{
			"DESERT_NIGHT", 0.90f, 3.10f, 0.005f, 1.30f, 0.008f,
			{
				{0.00f, 0.60f,  90.0f, 210.0f},
				{0.15f, 0.85f, 120.0f, 300.0f},
				{0.40f, 1.00f,  60.0f, 150.0f}
			}
		},
	};
};
