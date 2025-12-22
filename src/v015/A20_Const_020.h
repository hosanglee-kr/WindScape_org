#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : A20_Const_020.h
 * 모듈약어 : A20
 * 모듈명 : Smart Nature Wind 공용 상수/타입/구조체 선언 (v015)
 * ------------------------------------------------------
 * 기능 요약
 *  - 파일 경로/버퍼/개수 제한 상수
 *  - 프리셋/스타일 코드 상수 및 인덱싱 유틸
 *  - Schedule / UserProfile / WindProfile JSON 구조체
 *  - 공용 헬퍼 (clamp, safe strlcpy 등)
 *  - 전역 구성 루트 객체 선언(g_A20_config_root)
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
 *   - namespace 명        : 모듈약어_ 접두사
 *   - namespace 내 상수    : 모둘약어 접두시 미사용
 *   - 전역 상수,매크로      : G_모듈약어_ 접두사
 *   - 전역 변수             : g_모듈약어_ 접두사
 *   - 전역 함수             : 모듈약어_ 접두사
 *   - type                  : T_모듈약어_ 접두사
 *   - typedef               : _t  접미사
 *   - enum 상수             : EN_모듈약어_ 접두사
 *   - 구조체                : ST_모듈약어_ 접두사
 *   - 클래스명              : CL_모듈약어_ 접두사 , 버전 제거
 *   - 클래스 private 멤버   : _ 접두사
 *   - 클래스 멤버(함수/변수) : 모듈약어 접두사 미사용
 *   - 클래스 정적 멤버      : s_ 접두사
 *   - 함수 로컬 변수        : v_ 접두사
 *   - 함수 인자             : p_ 접두사
 * ------------------------------------------------------
 */

#include <Arduino.h>
#include <ArduinoJson.h>
// #include <string.h>

/* ======================================================
 * 경로/파일 이름 (최신 스펙)
 * ====================================================== */
namespace A20_Const {

// 펌웨어/파일버전
constexpr char	  FW_VERSION[]				= "FW_Ver_1.0.0";

constexpr char	  CFG_JSON_FILE[]			= "10_cfg_jsonFile.json";

// 문자열 및 배열 길이 정의
constexpr uint8_t LEN_NAME					= 64;
constexpr uint8_t LEN_PATH					= 160;
constexpr uint8_t LEN_SSID					= 32;
constexpr uint8_t LEN_PASS					= 32;
constexpr uint8_t LEN_PRESET				= 32;
constexpr uint8_t LEN_ALIAS					= 32;
constexpr uint8_t LEN_TIME					= 8;
constexpr uint8_t LEN_LEVEL					= 16;

// 배열 개수
constexpr uint8_t MAX_BLE_DEVICES			= 8;
constexpr uint8_t MAX_STA_NETWORKS			= 5;

constexpr uint8_t WIND_PRESETS_MAX			= 16;
constexpr uint8_t WIND_STYLES_MAX			= 16;

// 배열 개수 제한
constexpr uint8_t MAX_SCHEDULES				= 8;
constexpr uint8_t MAX_SEGMENTS_PER_SCHEDULE = 8;
constexpr uint8_t MAX_USER_PROFILES			= 6;  // 최신 스펙: 6개
constexpr uint8_t MAX_SEGMENTS_PER_PROFILE	= 8;

// 문자열 길이 제한
constexpr size_t  MAX_NAME_LEN				= 32;
constexpr size_t  MAX_CODE_LEN				= 24;

}  // namespace A20_Const

// ======================================================
// ENUM 정의
// ======================================================
typedef enum : uint8_t {
	EN_A20_WIFI_MODE_AP		= 0,
	EN_A20_WIFI_MODE_STA	= 1,
	EN_A20_WIFI_MODE_AP_STA = 2,
} EN_A20_WIFI_MODE_t;

// 0=Continuous / 1=Schedule
typedef enum : uint8_t {
	EN_A20_CONTROL_RUN_CONTINUE = 0,
	EN_A20_CONTROL_RUN_SCHEDULE = 1,
} T_A20_control_runMode_t;

// 바람 단계(참고용)
typedef enum : uint8_t {
	EN_A20_WEATHER_PHASE_CALM = 0,
	EN_A20_WEATHER_PHASE_NORMAL,
	EN_A20_WEATHER_PHASE_STRONG,
	EN_A20_WEATHER_PHASE_COUNT,
} T_A20_WindPhase_t;

inline constexpr const char* g_A20_WEATHER_PHASE_NAMES_Arr[] = {
	"CALM",
	"NORMAL",
	"STRONG",
};

// 프리셋 (레거시/참고용: 신규 sim은 문자열 preset 사용)
typedef enum : uint8_t {
	EN_A20_PRESET_OFF			= 0,
	EN_A20_PRESET_COUNTRY		= 1,
	EN_A20_PRESET_MEDITERRANEAN = 2,
	EN_A20_PRESET_OCEAN			= 3,
	EN_A20_PRESET_MOUNTAIN		= 4,
	EN_A20_PRESET_PLAINS		= 5,
	EN_A20_PRESET_HARBOR_BREEZE = 6,
	EN_A20_PRESET_FOREST_CANOPY = 7,
	EN_A20_PRESET_URBAN_SUNSET	= 8,
	EN_A20_PRESET_TROPICAL_RAIN = 9,
	EN_A20_PRESET_DESERT_NIGHT	= 10,
	EN_A20_PRESET_COUNT
} T_A20_PresetMode_t;

// 프리셋 코드 배열 정의 (문자열 상수)
inline constexpr const char* g_A20_PRESET_CODES[] = {
	"OFF",
	"COUNTRY",
	"MEDITERRANEAN",
	"OCEAN",
	"MOUNTAIN",
	"PLAINS",
	"HARBOR_BREEZE",
	"FOREST_CANOPY",
	"URBAN_SUNSET",
	"TROPICAL_RAIN",
	"DESERT_NIGHT",
};

inline constexpr const char* g_A20_PRESET_MODE_NAMES_Arr[] = {
	"OFF",
	"COUNTRY",
	"MEDITERRANEAN",
	"OCEAN",
	"MOUNTAIN",
	"PLAINS",
	"HARBOR_BREEZE",
	"FOREST_CANOPY",
	"URBAN_SUNSET",
	"TROPICAL_RAIN",
	"DESERT_NIGHT",
};

// ======================================================
// 구조체 정의
// ======================================================

// A20_Const_020.h (ST_A20_cfg_jsonFile_t 정의부 교체/수정)
typedef struct ST_A20_cfg_jsonFile_t {
	char system[A20_Const::LEN_PATH];
	char wifi[A20_Const::LEN_PATH];
	char motion[A20_Const::LEN_PATH];
	char nvsSpec[A20_Const::LEN_PATH];
	char schedules[A20_Const::LEN_PATH];
	char uzOpProfile[A20_Const::LEN_PATH];
	char dft_windProfile[A20_Const::LEN_PATH];
	char webPages[A20_Const::LEN_PATH];
} ST_A20_cfg_jsonFile_t;

typedef struct {
	uint8_t startPercentMin;	// 시동이 확실히 거는 최소 구간 (예: 18)
	uint8_t comfortPercentMin;	// “편안한 바람” 구간 시작 (예: 22), “체감 바람” 시작점 (예: 22%)
	uint8_t comfortPercentMax;	// “편안한 바람” 구간 끝   (예: 65)  소음/체감 균형 좋은 상한 (예: 65%)
	uint8_t hardPercentMax;		// 팬/소음/내구성 상으로 무리 없는 상한 (예: 90)
} ST_A20_FanConfig_t;

// ------------------------------------------------------
// SYSTEM 설정 (cfg_system_022.json)
// ------------------------------------------------------
typedef struct {
	struct {
		char version[A20_Const::LEN_NAME];
		char device_name[A20_Const::LEN_NAME];
		char last_update[A20_Const::LEN_NAME];
	} meta;

	struct {
		// char webPagesJson[A20_Const::LEN_PATH];
		struct {
			char	 level[A20_Const::LEN_LEVEL];
			uint16_t max_entries;
		} logging;
	} system;

	struct {
		struct {
			int16_t	 pin;
			uint8_t	 channel;
			uint32_t freq;
			uint8_t	 res;
		} fan_pwm;

		//  팬 특성 모델
		ST_A20_FanConfig_t fanConfig;

		struct {
			bool	 enabled;
			int16_t	 pin;
			uint16_t debounce_sec;
            uint16_t hold_sec; 
		} pir;
		struct {
			bool	 enabled;
			char	 type[16];
			int16_t	 pin;
			uint16_t interval_sec;
		} tempHum;
		struct {
			bool	 enabled;
			uint16_t scan_interval;
		} ble;
	} hw;
	struct {
		char api_key[64];
	} security;
	struct {
		char	 ntp_server[64];
		char	 timezone[32];
		uint16_t sync_interval_min;
	} time;
} ST_A20_SystemConfig;

// ------------------------------------------------------
// WIFI 설정 (cfg_wifi_022.json)
// ------------------------------------------------------
typedef struct {
	char ssid[A20_Const::LEN_SSID];
	char pass[A20_Const::LEN_PASS];
} ST_A20_STANetwork_t;	// 1. STA 네트워크 개별 항목 정의

typedef struct {
	EN_A20_WIFI_MODE_t wifiMode;
	char			   wifiModeDesc[48];
	struct {
		char ssid[A20_Const::LEN_SSID];
		char password[A20_Const::LEN_PASS];
	} ap;
	ST_A20_STANetwork_t sta[A20_Const::MAX_STA_NETWORKS];
	uint8_t				sta_count;
} ST_A20_WifiConfig;

// ------------------------------------------------------
// MOTION 설정 (cfg_motion_022.json)
//  - JSON 스키마(최종 확정)
//    motion.pir.enabled, motion.pir.hold_sec
//    motion.ble.enabled, motion.ble.trusted_devices[], motion.ble.rssi{on,off,avg_count,persist_count,exit_delay_sec}
// ------------------------------------------------------
typedef struct {
	char	alias[A20_Const::LEN_ALIAS];  // "MyPhone"
	char	name[A20_Const::LEN_NAME];	  // "iPhone15" 등
	char	mac[20];					  // "AA:BB:CC:11:22:33" 또는 ""(랜덤화시)
	char	manuf_prefix[9];			  // "4C0002" (최대 8 chars + NUL)
	uint8_t prefix_len;					  // 바이트 단위(예: 3 → "4C0002" 3바이트)
	bool	enabled;					  // 디바이스 화이트리스트 on/off
} ST_A20_BLETrustedDevice;

typedef struct {
	bool enabled;  // 전체 motion 기능 마스터 스위치(옵션)
	struct {	   // motion.pir
		bool	 enabled;
		uint16_t hold_sec;
	} pir;
	struct {  // motion.ble
		bool					enabled;
		// motion.ble.trusted_devices[]
		ST_A20_BLETrustedDevice trusted_devices[A20_Const::MAX_BLE_DEVICES];
		uint8_t					trusted_count;
		// motion.ble.rssi { on/off/avg_count/persist_count/exit_delay_sec }
		struct {
			int16_t	 on;			  // RSSI ON Threshold
			int16_t	 off;			  // RSSI OFF Threshold
			uint8_t	 avg_count;		  // 이동 평균 샘플 수
			uint8_t	 persist_count;	  // 상태 전이 연속 조건
			uint16_t exit_delay_sec;  // Presence→Idle 지연
		} rssi;
	} ble;
} ST_A20_MotionConfig;

/* ======================================================
 * Enum/Code: Segment Mode
 * ====================================================== */
typedef enum : uint8_t {
	EN_A20_SEG_MODE_PRESET = 0,	 // 프리셋/스타일 기반
	EN_A20_SEG_MODE_FIXED  = 1,	 // 고정속도
	EN_A20_SEG_MODE_COUNT
} EN_A20_segment_mode_t;

/* ======================================================
 * Segment Mode <-> String 매핑 유틸
 * ====================================================== */
inline constexpr const char* g_A20_SEG_MODE_NAMES[] = { "PRESET", "FIXED" };

inline EN_A20_segment_mode_t A20_modeFromString(const char* p_str) {
	if (!p_str)
		return EN_A20_SEG_MODE_PRESET;
	for (uint8_t v_i = 0; v_i < EN_A20_SEG_MODE_COUNT; v_i++) {
		if (strcasecmp(p_str, g_A20_SEG_MODE_NAMES[v_i]) == 0)
			return static_cast<EN_A20_segment_mode_t>(v_i);
	}
	return EN_A20_SEG_MODE_PRESET;	// fallback
}

inline const char* A20_modeToString(EN_A20_segment_mode_t p_mode) {
	if (p_mode >= EN_A20_SEG_MODE_COUNT)
		return "PRESET";
	return g_A20_SEG_MODE_NAMES[p_mode];
}

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
} ST_A20_WindBase_t;

typedef struct {
	char			  code[A20_Const::MAX_CODE_LEN];  // 예: "OCEAN"
	char			  name[A20_Const::MAX_NAME_LEN];  // 예: "Ocean"
	ST_A20_WindBase_t base;
} ST_A20_PresetEntry_t;

typedef struct {
	// 가중치 계수
	float intensity_factor;	 // intensity 곱
	float variability_factor;
	float gust_factor;
	float thermal_factor;
} ST_A20_StyleFactors_t;

typedef struct {
	char				  code[A20_Const::MAX_CODE_LEN];  // 예: "RELAX"
	char				  name[A20_Const::MAX_NAME_LEN];
	ST_A20_StyleFactors_t factors;
} ST_A20_StyleEntry_t;

typedef struct {
	// windProfile JSON 루트
	// version/json_file 키는 내부 관리용이므로 본 구조체엔 포함하지 않음
	uint8_t				 preset_count = 0;
	uint8_t				 style_count  = 0;
	ST_A20_PresetEntry_t presets[A20_Const::WIND_PRESETS_MAX];
	ST_A20_StyleEntry_t	 styles[A20_Const::WIND_STYLES_MAX];
} ST_A20_WindProfileDict_t;

/* ======================================================
 * 공통: Motion, AutoOff
 * ====================================================== */
typedef struct {
	bool	enabled	 = false;
	int32_t hold_sec = 0;
} ST_A20_PIR_t;

typedef struct {
	bool	enabled		   = false;
	int32_t rssi_threshold = -70;
	int32_t hold_sec	   = 0;
} ST_A20_BLE_t;

typedef struct {
	ST_A20_PIR_t pir;
	ST_A20_BLE_t ble;
} ST_A20_Motion_t;

// userProfiles의 autoOff(통합형)
typedef struct {
	struct {
		bool	 enabled = false;
		uint32_t minutes = 0;
	} timer;
	struct {
		bool enabled = false;
		char time[6] = { 0 };
	} offTime;	// "HH:MM"
	struct {
		bool  enabled = false;
		float temp	  = 0.0f;
	} offTemp;
} ST_A20_AutoOff_t;

// schedules의 autoOffTimer (동일 스펙으로 통일 사용)
typedef ST_A20_AutoOff_t ST_A20_SchAutoOff_t;

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
} ST_A20_AdjustDelta_t;

/* ======================================================
 * Schedule JSON 구조
 * ====================================================== */
typedef struct {
	//// bool	enabled		  = false;
	uint8_t days[7]		  = { 1, 1, 1, 1, 1, 1, 1 };
	char	start_time[6] = { 0 };	// "HH:MM"
	char	end_time[6]	  = { 0 };	// "HH:MM" (익일 교차 가능)
} ST_A20_SchedulePeriod_t;

typedef struct {
	uint8_t				  segId								  = 0;
	uint16_t			  segNo								  = 0;
	uint16_t			  on_minutes						  = 0;
	uint16_t			  off_minutes						  = 0;

	EN_A20_segment_mode_t mode								  = EN_A20_SEG_MODE_PRESET;

	// PRESET 모드
	char				  presetCode[A20_Const::MAX_CODE_LEN] = { 0 };
	char				  styleCode[A20_Const::MAX_CODE_LEN]  = { 0 };
	ST_A20_AdjustDelta_t  adjust;

	// FIXED 모드
	float				  fixed_speed = 0.0f;  // 0~100
} ST_A20_ScheduleSegment_t;

typedef struct {
	uint8_t					 schId						   = 0;
	uint16_t				 schNo						   = 0;
	char					 name[A20_Const::MAX_NAME_LEN] = { 0 };
	bool					 enabled					   = true;

	ST_A20_SchedulePeriod_t	 period;  // period.enabled=false면 시간대 무시(항상 활성)
	uint8_t					 seg_count = 0;
	ST_A20_ScheduleSegment_t segments[A20_Const::MAX_SEGMENTS_PER_SCHEDULE];
	bool					 repeatSegments = true;
	uint8_t					 repeatCount	= 0;

	ST_A20_SchAutoOff_t		 autoOff;  // schedules도 userProfiles와 동일 구조 사용
	ST_A20_Motion_t			 motion;
} ST_A20_ScheduleItem_t;

typedef struct {
	// { "version", "jsonFile" } 등은 파일 메타이므로 구조에 포함 안함
	uint8_t				  count = 0;
	ST_A20_ScheduleItem_t items[A20_Const::MAX_SCHEDULES];
} ST_A20_SchedulesRoot_t;

/* ======================================================
 * UserProfiles JSON 구조
 * ====================================================== */
typedef struct {
	uint8_t				  segId								  = 0;
	uint16_t			  segNo								  = 0;
	uint16_t			  on_minutes						  = 0;
	uint16_t			  off_minutes						  = 0;

	EN_A20_segment_mode_t mode								  = EN_A20_SEG_MODE_PRESET;

	char				  presetCode[A20_Const::MAX_CODE_LEN] = { 0 };
	char				  styleCode[A20_Const::MAX_CODE_LEN]  = { 0 };
	ST_A20_AdjustDelta_t  adjust;

	float				  fixed_speed = 0.0f;  // FIXED일 때
} ST_A20_UserProfileSegment_t;

typedef struct {
	uint8_t						profileId					  = 0;
	uint8_t						profileNo					  = 0;
	char						name[A20_Const::MAX_NAME_LEN] = { 0 };
	bool						enabled						  = true;
	bool						repeatSegments				  = true;
	uint8_t						repeatCount					  = 0;
	uint8_t						seg_count					  = 0;
	ST_A20_UserProfileSegment_t segments[A20_Const::MAX_SEGMENTS_PER_PROFILE];

	ST_A20_AutoOff_t			autoOff;
	ST_A20_Motion_t				motion;
} ST_A20_UserProfileItem_t;

typedef struct {
	uint8_t					 count = 0;	 // 최대 6
	ST_A20_UserProfileItem_t items[A20_Const::MAX_USER_PROFILES];
} ST_A20_UserProfilesRoot_t;

/* ======================================================
 * 시뮬레이션에 전달할 "해석된 파라미터"
 *  - preset(base) × style(factor) + adjust(delta)
 * ====================================================== */
typedef struct {
	char  presetCode[A20_Const::MAX_CODE_LEN];
	char  styleCode[A20_Const::MAX_CODE_LEN];

	bool  valid;	   // 유효성 플래그
	bool  fixedMode;   // 고정 속도 모드 여부
	float fixedSpeed;  // 고정 속도일 때 고정속도 값

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

} ST_A20_ResolvedWind_t;

/* ======================================================
 * Config Root (전역 보관)
 * ====================================================== */

typedef struct {
	ST_A20_SystemConfig*	   system		= nullptr;
	ST_A20_WifiConfig*		   wifi			= nullptr;
	ST_A20_MotionConfig*	   motion		= nullptr;
	ST_A20_WindProfileDict_t*  windDict		= nullptr;
	ST_A20_SchedulesRoot_t*	   schedules	= nullptr;
	ST_A20_UserProfilesRoot_t* userProfiles = nullptr;
} ST_A20_ConfigRoot_t;

extern ST_A20_ConfigRoot_t g_A20_config_root;

class CL_M10_MotionLogic;  // 전방 선언
// ... (ST_A20_ConfigRoot_t 및 g_A20_config_root 선언) ...
extern CL_M10_MotionLogic* g_M10_motionLogic;  // extern 선언

// extern CL_M10_MotionLogic* g_M10_motionLogic;

/* ======================================================
 * 헬퍼 함수 선언
 * ====================================================== */
inline float A20_clampf(float v, float lo, float hi) {
	if (v < lo)
		return lo;
	if (v > hi)
		return hi;
	return v;
}

inline void A20_safe_strlcpy(char* dst, const char* src, size_t n) {
	if (!dst || n == 0)
		return;
	if (!src) {
		dst[0] = '\0';
		return;
	}
	strlcpy(dst, src, n);
}

// ------------------------------------------------------
// [보완] 랜덤 및 프리셋 유틸 (S10, C10 등 공용)
// ------------------------------------------------------

inline float A20_getRandom01() {
	return (float)esp_random() / (float)UINT32_MAX;
}

inline float A20_randRange(float p_min, float p_max) {
	return p_min + (A20_getRandom01() * (p_max - p_min));
}

// ======================================================
// Default 초기화 헬퍼
//  - C10_ConfigManager.loadAll() 등에서 사용
// ======================================================

// System 기본값
inline void A20_resetSystemDefault(ST_A20_SystemConfig& p_cfg) {
	memset(&p_cfg, 0, sizeof(p_cfg));

	A20_safe_strlcpy(p_cfg.meta.version, A20_Const::FW_VERSION, sizeof(p_cfg.meta.version));
	A20_safe_strlcpy(p_cfg.meta.device_name, "SmartNatureWind", sizeof(p_cfg.meta.device_name));
	A20_safe_strlcpy(p_cfg.meta.last_update, "", sizeof(p_cfg.meta.last_update));

	// A20_safe_strlcpy(p_cfg.system.webPagesJson, "", sizeof(p_cfg.system.webPagesJson));

	A20_safe_strlcpy(p_cfg.system.logging.level, "INFO", sizeof(p_cfg.system.logging.level));
	p_cfg.system.logging.max_entries	 = 300;

	// HW: PWM
	p_cfg.hw.fan_pwm.pin				 = 6;
	p_cfg.hw.fan_pwm.channel			 = 0;
	p_cfg.hw.fan_pwm.freq				 = 25000;
	p_cfg.hw.fan_pwm.res				 = 10;

	// ★ HW: fanConfig 기본값
	p_cfg.hw.fanConfig.startPercentMin	 = 10;	// 모터 시동 하한
	p_cfg.hw.fanConfig.comfortPercentMin = 20;	// 체감 바람 시작
	p_cfg.hw.fanConfig.comfortPercentMax = 80;	// 체감/소음 밸런스 상한
	p_cfg.hw.fanConfig.hardPercentMax	 = 95;	// 안전상 절대 상한

	// HW: PIR
	p_cfg.hw.pir.enabled				 = true;
	p_cfg.hw.pir.pin					 = 13;
	p_cfg.hw.pir.debounce_sec			 = 5;
	p_cfg.hw.pir.hold_sec = 120;   // ★ add

   // HW: Temp/Hum
    p_cfg.hw.tempHum.enabled = true;
    A20_safe_strlcpy(p_cfg.hw.tempHum.type, "DHT22", sizeof(p_cfg.hw.tempHum.type));
    p_cfg.hw.tempHum.pin = 23;
    p_cfg.hw.tempHum.interval_sec = 30;

	// HW: BLE
	p_cfg.hw.ble.enabled				 = true;
	p_cfg.hw.ble.scan_interval			 = 5;

	// Security
	A20_safe_strlcpy(p_cfg.security.api_key, "", sizeof(p_cfg.security.api_key));

	// Time
	A20_safe_strlcpy(p_cfg.time.ntp_server, "pool.ntp.org", sizeof(p_cfg.time.ntp_server));
	A20_safe_strlcpy(p_cfg.time.timezone, "Asia/Seoul", sizeof(p_cfg.time.timezone));
	p_cfg.time.sync_interval_min = 60;
}

// WiFi 기본값
inline void A20_resetWifiDefault(ST_A20_WifiConfig& p_cfg) {
	memset(&p_cfg, 0, sizeof(p_cfg));

	p_cfg.wifiMode = EN_A20_WIFI_MODE_AP_STA;
	A20_safe_strlcpy(p_cfg.wifiModeDesc, "0=AP,1=STA,2=AP+STA", sizeof(p_cfg.wifiModeDesc));

	A20_safe_strlcpy(p_cfg.ap.ssid, "NatureWind", sizeof(p_cfg.ap.ssid));
	A20_safe_strlcpy(p_cfg.ap.password, "2540", sizeof(p_cfg.ap.password));

	p_cfg.sta_count = 0;  // STA 목록은 비워둠
}

// Motion 기본값
inline void A20_resetMotionDefault(ST_A20_MotionConfig& p_cfg) {
	memset(&p_cfg, 0, sizeof(p_cfg));

	p_cfg.enabled				  = true;

	p_cfg.pir.enabled			  = true;
	p_cfg.pir.hold_sec			  = 120;

	p_cfg.ble.enabled			  = true;
	p_cfg.ble.trusted_count		  = 0;
	p_cfg.ble.rssi.on			  = -65;
	p_cfg.ble.rssi.off			  = -75;
	p_cfg.ble.rssi.avg_count	  = 8;
	p_cfg.ble.rssi.persist_count  = 5;
	p_cfg.ble.rssi.exit_delay_sec = 12;
}

// WindProfile Dict 기본값
inline void A20_resetWindProfileDictDefault(ST_A20_WindProfileDict_t& p_dict) {
	memset(&p_dict, 0, sizeof(p_dict));

	// 최소 기본 Preset: OCEAN 기준 하나만이라도 보장 (필요 시 확장)
	p_dict.preset_count = 1;
	A20_safe_strlcpy(p_dict.presets[0].code, "OCEAN", sizeof(p_dict.presets[0].code));
	A20_safe_strlcpy(p_dict.presets[0].name, "Ocean Breeze", sizeof(p_dict.presets[0].name));
	p_dict.presets[0].base.wind_intensity			  = 70.0f;
	p_dict.presets[0].base.wind_variability			  = 50.0f;
	p_dict.presets[0].base.gust_frequency			  = 45.0f;
	p_dict.presets[0].base.fan_limit				  = 90.0f;
	p_dict.presets[0].base.min_fan					  = 10.0f;
	p_dict.presets[0].base.turbulence_length_scale	  = 40.0f;
	p_dict.presets[0].base.turbulence_intensity_sigma = 0.5f;
	p_dict.presets[0].base.thermal_bubble_strength	  = 2.0f;
	p_dict.presets[0].base.thermal_bubble_radius	  = 18.0f;

	// Style 기본 1개 (BALANCE)
	p_dict.style_count								  = 1;
	A20_safe_strlcpy(p_dict.styles[0].code, "BALANCE", sizeof(p_dict.styles[0].code));
	A20_safe_strlcpy(p_dict.styles[0].name, "Balance", sizeof(p_dict.styles[0].name));
	p_dict.styles[0].factors.intensity_factor	= 1.0f;
	p_dict.styles[0].factors.variability_factor = 1.0f;
	p_dict.styles[0].factors.gust_factor		= 1.0f;
	p_dict.styles[0].factors.thermal_factor		= 1.0f;
}

// Schedules 기본값 (비움 + 구조 일관성 보장)
inline void A20_resetSchedulesDefault(ST_A20_SchedulesRoot_t& p_cfg) {
	memset(&p_cfg, 0, sizeof(p_cfg));
	p_cfg.count = 0;
	// 필요 시 여기서 기본 스케줄 1~2개 정의 가능
}

// UserProfiles 기본값 (비움 + 구조 일관성 보장)
inline void A20_resetUserProfilesDefault(ST_A20_UserProfilesRoot_t& p_cfg) {
	memset(&p_cfg, 0, sizeof(p_cfg));
	p_cfg.count = 0;
	// 필요 시 기본 프로파일 추가 가능
}

// ------------------------------------------------------
// A20_resetToDefault
//  - 전체 ConfigRoot 기본 초기화
//  - C10_ConfigManager::loadAll() 진입 전 호출 가정
// ------------------------------------------------------
inline void A20_resetToDefault(ST_A20_ConfigRoot_t& p_root) {
	if (p_root.system)
		A20_resetSystemDefault(*p_root.system);
	if (p_root.wifi)
		A20_resetWifiDefault(*p_root.wifi);
	if (p_root.motion)
		A20_resetMotionDefault(*p_root.motion);

	if (p_root.windDict)
		A20_resetWindProfileDictDefault(*p_root.windDict);
	if (p_root.schedules)
		A20_resetSchedulesDefault(*p_root.schedules);
	if (p_root.userProfiles)
		A20_resetUserProfilesDefault(*p_root.userProfiles);
}

// 프리셋 코드 → 인덱스 매핑 (S10용)
inline int8_t A20_getPresetIndexByCode(const char* code) {
	if (!code)
		return -1;
	for (int8_t i = 0; i < EN_A20_PRESET_COUNT; i++) {
		if (strcasecmp(code, g_A20_PRESET_CODES[i]) == 0)
			return i;
	}
	return -1;
}

// ------------------------------------------------------
// WindProfileDict 검색 유틸리티
// ------------------------------------------------------
inline int16_t A20_findPresetIndexByCode(const ST_A20_WindProfileDict_t& p_dict, const char* p_code) {
	if (!p_code || !p_code[0])
		return -1;
	for (uint8_t v_i = 0; v_i < p_dict.preset_count; v_i++) {
		if (strcasecmp(p_dict.presets[v_i].code, p_code) == 0)
			return (int16_t)v_i;
	}
	return -1;
}

inline int16_t A20_findStyleIndexByCode(const ST_A20_WindProfileDict_t& p_dict, const char* p_code) {
	if (!p_code || !p_code[0])
		return -1;
	for (uint8_t v_i = 0; v_i < p_dict.style_count; v_i++) {
		if (strcasecmp(p_dict.styles[v_i].code, p_code) == 0)
			return (int16_t)v_i;
	}
	return -1;
}
