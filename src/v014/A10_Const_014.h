#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : A10_Const_014.h
 * 모듈약어 : A10
 * 모듈명 : Smart Nature Wind 공용 상수/타입/구조체 선언 (v014)
 * ------------------------------------------------------
 * 기능 요약
 *  - 파일 경로/버퍼/개수 제한 상수
 *  - 프리셋/스타일 코드 상수 및 인덱싱 유틸
 *  - Schedule / UserProfile / WindProfile JSON 구조체
 *  - 공용 헬퍼 (clamp, safe strlcpy 등)
 *  - 전역 구성 루트 객체 선언(g_A10_config_root)
 * ------------------------------------------------------
 * [구현 규칙]
 *  - 항상 소스 시작 주석 부분 체계 유지 및 내용 업데이트
 *  - 소스 시작 주석 부분 구현규칙, 코드네이밍규칙 내용 그대로 유지, 수정금지
 *  - ArduinoJson v7.x.x 사용 (v6 이하 사용 금지)
 *  - JsonDocument 단일 타입만 사용
 *  - createNestedArray/Object/containsKey 사용 금지
 *  - memset + strlcpy 기반 안전 초기화
 *  - 주석/필드명은 JSON 구조와 동일하게 유지
 *  - 모듈별 단일 헤더(h)파일로만 구성 (cpp 없음)
 * ------------------------------------------------------
 * [코드 네이밍 규칙]
 *   - 전역 상수,매크로      : G_모듈약어_ 접두사
 *   - 전역 변수             : g_모듈약어_ 접두사
 *   - 전역 함수             : 모듈약어_ 접두사
 *   - type                  : T_모듈약어_ 접두사
 *   - typedef               : _t  접미사
 *   - enum 상수             : EN_모듈약어_ 접두사
 *   - 구조체                : ST_모듈약어_ 접두사
 *   - 클래스명              : CL_모듈약어_ 접두사
 *   - 클래스 private 멤버   : _ 접두사
 *   - 클래스 멤버(함수/변수) : 모듈약어 접두사 미사용
 *   - 클래스 정적 멤버      : s_ 접두사
 *   - 함수 로컬 변수        : v_ 접두사
 *   - 함수 인자             : p_ 접두사
 * ------------------------------------------------------
 */

#include <Arduino.h>
#include <ArduinoJson.h>
#include <string.h>

/* ======================================================
 * 경로/파일 이름 (최신 스펙)
 * ====================================================== */
namespace A10_Const {
	// JSON config files (최신 합의안)
	constexpr char 		SCHEDULES_FILE[]			= "/json/cfg_schedules_024.json";
	constexpr char 		USER_PROFILES_FILE[] 		= "/json/cfg_uzOpProfile_025_final.json";
	// Wind Profile 사전 (preset/style 기본값 + 계수)
	constexpr char 		WIND_PROFILE_FILE[] 		= "/json/cfg_dft_windProfile_024.json";

	// 백업 파일
	constexpr char 		SCHEDULES_FILE_BAK[]		= "/json/cfg_schedules_024.bak";
	constexpr char 		USER_PROFILES_FILE_BAK[] 	= "/json/cfg_uzOpProfile_025_final.bak";
	constexpr char 		WIND_PROFILE_FILE_BAK[]		= "/json/cfg_dft_windProfile_024.bak";

	// 배열 개수 제한
	constexpr uint8_t 	MAX_SCHEDULES				= 8;
	constexpr uint8_t 	MAX_SEGMENTS_PER_SCHEDULE 	= 8;
	constexpr uint8_t 	MAX_USER_PROFILES			= 6;  // 최신 스펙: 6개
	constexpr uint8_t 	MAX_SEGMENTS_PER_PROFILE	= 8;

	// 문자열 길이 제한
	constexpr size_t 	MAX_NAME_LEN 				= 32;
	constexpr size_t 	MAX_CODE_LEN 				= 24;
}  // namespace A10_Const

/* ======================================================
 * Enum/Code: Segment Mode
 * ====================================================== */
typedef enum : uint8_t {
	EN_A10_SEG_MODE_PRESET = 0,	 // 프리셋/스타일 기반
	EN_A10_SEG_MODE_FIXED  = 1,	 // 고정속도
} EN_A10_segment_mode_t;

/* ======================================================
 * Wind Profile 테이블(사전)
 *  - presets[] : code/name + base 파라미터
 *  - styles[]  : code/name + factor 계수
 * ====================================================== */
typedef struct {
	// base: 절대값 파라미터
	float wind_intensity;			   // 0~100
	float gust_frequency;			   // 0~100
	float wind_variability;			   // 0~100
	float fan_limit;				   // 0~100
	float min_fan;					   // 0~100
	float turbulence_length_scale;	   // L
	float turbulence_intensity_sigma;  // sigma
	float thermal_bubble_strength;	   // 1.0 ~ 3.0
	float thermal_bubble_radius;	   // unit
} ST_A10_WindBase_t;

typedef struct {
	char			  code[A10_Const::MAX_CODE_LEN];  // 예: "OCEAN"
	char			  name[A10_Const::MAX_NAME_LEN];  // 예: "Ocean"
	ST_A10_WindBase_t base;
} ST_A10_PresetEntry_t;

typedef struct {
	// 가중치 계수
	float intensity_factor;	 // intensity 곱
	float variability_factor;
	float gust_factor;
	float thermal_factor;
} ST_A10_StyleFactors_t;

typedef struct {
	char				  code[A10_Const::MAX_CODE_LEN];  // 예: "RELAX"
	char				  name[A10_Const::MAX_NAME_LEN];
	ST_A10_StyleFactors_t factors;
} ST_A10_StyleEntry_t;

typedef struct {
	// windProfile JSON 루트
	// version/json_file 키는 내부 관리용이므로 본 구조체엔 포함하지 않음
	uint8_t				 preset_count = 0;
	uint8_t				 style_count  = 0;
	ST_A10_PresetEntry_t presets[16];  // 충분히 여유있게
	ST_A10_StyleEntry_t	 styles[16];
} ST_A10_WindProfileDict_t;

/* ======================================================
 * 공통: Motion, AutoOff
 * ====================================================== */
typedef struct {
	bool	enabled	 = false;
	int32_t hold_sec = 0;
} ST_A10_PIR_t;

typedef struct {
	bool	enabled		   = false;
	int32_t rssi_threshold = -70;
	int32_t hold_sec	   = 0;
} ST_A10_BLE_t;

typedef struct {
	ST_A10_PIR_t pir;
	ST_A10_BLE_t ble;
} ST_A10_Motion_t;

// userProfiles의 autoOff(통합형)
typedef struct {
	struct {
		bool	 enabled = false;
		uint32_t minutes = 0;
	} timer;
	struct {
		bool enabled = false;
		char time[6] = {0};
	} offTime;	// "HH:MM"
	struct {
		bool  enabled = false;
		float temp	  = 0.0f;
	} offTemp;
} ST_A10_AutoOff_t;

// schedules의 autoOffTimer (동일 스펙으로 통일 사용)
typedef ST_A10_AutoOff_t ST_A10_SchAutoOff_t;

/* ======================================================
 * Segment 조정(Adjust)
 *  - JSON에서는 adjust.wind_intensity / wind_variability 등 사용
 *  - 모든 값은 "베이스에서 델타" (상대 변화, 단위 동일)
 * ====================================================== */
typedef struct {
	// 필요한 항목만 사용해도 되도록 기본 0
	float wind_intensity			 = 0.0f;  // ±
	float gust_frequency			 = 0.0f;  // ±
	float wind_variability			 = 0.0f;  // ±
	float fan_limit					 = 0.0f;  // ±
	float min_fan					 = 0.0f;  // ±
	float turbulence_length_scale	 = 0.0f;  // ±
	float turbulence_intensity_sigma = 0.0f;  // ±
} ST_A10_AdjustDelta_t;

/* ======================================================
 * Schedule JSON 구조
 * ====================================================== */
typedef struct {
	bool	enabled		  = false;
	uint8_t days[7]		  = {1, 1, 1, 1, 1, 1, 1};
	char	start_time[6] = {0};  // "HH:MM"
	char	end_time[6]	  = {0};  // "HH:MM" (익일 교차 가능)
} ST_A10_SchedulePeriod_t;

typedef struct {
	uint16_t segNo		 = 0;
	uint16_t on_minutes	 = 0;
	uint16_t off_minutes = 0;

	EN_A10_segment_mode_t mode = EN_A10_SEG_MODE_PRESET;

	// PRESET 모드
	char				 presetCode[A10_Const::MAX_CODE_LEN] = {0};
	char				 styleCode[A10_Const::MAX_CODE_LEN]	 = {0};
	ST_A10_AdjustDelta_t adjust;

	// FIXED 모드
	float fixed_speed = 0.0f;  // 0~100
} ST_A10_ScheduleSegment_t;

typedef struct {
	uint16_t schNo						   = 0;
	char	 name[A10_Const::MAX_NAME_LEN] = {0};
	bool	 enabled					   = true;

	ST_A10_SchedulePeriod_t	 period;  // period.enabled=false면 시간대 무시(항상 활성)
	uint8_t					 seg_count = 0;
	ST_A10_ScheduleSegment_t segments[A10_Const::MAX_SEGMENTS_PER_SCHEDULE];

	ST_A10_SchAutoOff_t autoOff;  // schedules도 userProfiles와 동일 구조 사용
	ST_A10_Motion_t		motion;
} ST_A10_ScheduleItem_t;

typedef struct {
	// { "version", "jsonFile" } 등은 파일 메타이므로 구조에 포함 안함
	uint8_t				  count = 0;
	ST_A10_ScheduleItem_t items[A10_Const::MAX_SCHEDULES];
} ST_A10_SchedulesRoot_t;

/* ======================================================
 * UserProfiles JSON 구조
 * ====================================================== */
typedef struct {
	uint16_t segNo		 = 0;
	uint16_t on_minutes	 = 0;
	uint16_t off_minutes = 0;

	EN_A10_segment_mode_t mode = EN_A10_SEG_MODE_PRESET;

	char				 presetCode[A10_Const::MAX_CODE_LEN] = {0};
	char				 styleCode[A10_Const::MAX_CODE_LEN]	 = {0};
	ST_A10_AdjustDelta_t adjust;

	float fixed_speed = 0.0f;  // FIXED일 때
} ST_A10_UserProfileSegment_t;

typedef struct {
	uint8_t profileNo					  = 0;
	char	name[A10_Const::MAX_NAME_LEN] = {0};
	bool	enabled						  = true;
	bool	repeatSegments				  = true;

	uint8_t						seg_count = 0;
	ST_A10_UserProfileSegment_t segments[A10_Const::MAX_SEGMENTS_PER_PROFILE];

	ST_A10_AutoOff_t autoOff;
	ST_A10_Motion_t	 motion;
} ST_A10_UserProfileItem_t;

typedef struct {
	uint8_t					 count = 0;	 // 최대 6
	ST_A10_UserProfileItem_t items[A10_Const::MAX_USER_PROFILES];
} ST_A10_UserProfilesRoot_t;

/* ======================================================
 * 시뮬레이션에 전달할 "해석된 파라미터"
 *  - preset(base) × style(factor) + adjust(delta)
 * ====================================================== */
typedef struct {
	char presetCode[A10_Const::MAX_CODE_LEN];
	char styleCode[A10_Const::MAX_CODE_LEN];

	// 최종 수치 (0~100 범위는 clamp)
	float wind_intensity;
	float gust_frequency;
	float wind_variability;
	float fan_limit;
	float min_fan;
	float turbulence_length_scale;
	float turbulence_intensity_sigma;
	float thermal_bubble_strength;
	float thermal_bubble_radius;
} ST_A10_ResolvedWind_t;

/* ======================================================
 * Config Root (전역 보관)
 * ====================================================== */
typedef struct {
	ST_A10_WindProfileDict_t  windDict;		 // presets/styles 사전
	ST_A10_SchedulesRoot_t	  schedules;	 // 스케줄 모음
	ST_A10_UserProfilesRoot_t userProfiles;	 // 사용자 프로파일 모음
} ST_A10_ConfigRoot_t;

extern ST_A10_ConfigRoot_t g_A10_config_root;

/* ======================================================
 * 헬퍼 함수 선언
 * ====================================================== */
inline float A10_clampf(float v, float lo, float hi) {
	if (v < lo)
		return lo;
	if (v > hi)
		return hi;
	return v;
}

inline void A10_safe_strlcpy(char* dst, const char* src, size_t n) {
	if (!dst || n == 0)
		return;
	if (!src) {
		dst[0] = '\0';
		return;
	}
	strlcpy(dst, src, n);
}

// windDict 탐색
int16_t C10_findPresetIndexByCode(const ST_A10_WindProfileDict_t& dict, const char* code);
int16_t C10_findStyleIndexByCode(const ST_A10_WindProfileDict_t& dict, const char* code);

// 해석 유틸: preset × style × adjust → ResolvedWind
bool C10_resolveWindParams(const ST_A10_WindProfileDict_t& dict,
						   const char*					   presetCode,
						   const char*					   styleCode,
						   const ST_A10_AdjustDelta_t*	   adj,
						   ST_A10_ResolvedWind_t&		   outResolved);
