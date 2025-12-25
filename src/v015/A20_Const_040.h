#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : A20_Const_040.h
 * 모듈약어 : A20
 * 모듈명 : Smart Nature Wind 공용 상수/타입/구조체 선언 (v030)
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
 *  - 변수명은 가능한 해석 가능하게
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
#include <string.h>
#include <strings.h>

/* ======================================================
 * 경로/파일 이름 (최신 스펙)
 * ====================================================== */
namespace A20_Const {

// 펌웨어/파일버전
constexpr char FW_VERSION[]    = "FW_Ver_1.0.0";
constexpr char CFG_JSON_FILE[] = "10_cfg_jsonFile.json";

// 문자열 및 배열 길이 정의
constexpr uint8_t LEN_NAME   = 64;
constexpr uint8_t LEN_PATH   = 160;
constexpr uint8_t LEN_SSID   = 32;
constexpr uint8_t LEN_PASS   = 32;
constexpr uint8_t LEN_PRESET = 32;
constexpr uint8_t LEN_ALIAS  = 32;
constexpr uint8_t LEN_TIME   = 8;
constexpr uint8_t LEN_LEVEL  = 16;

// 배열 개수
constexpr uint8_t MAX_BLE_DEVICES  = 8;
constexpr uint8_t MAX_STA_NETWORKS = 5;

constexpr uint8_t WIND_PRESETS_MAX = 16;
constexpr uint8_t WIND_STYLES_MAX  = 16;

// 배열 개수 제한
constexpr uint8_t MAX_SCHEDULES             = 8;
constexpr uint8_t MAX_SEGMENTS_PER_SCHEDULE = 8;
constexpr uint8_t MAX_USER_PROFILES         = 6;  // 최신 스펙: 6개
constexpr uint8_t MAX_SEGMENTS_PER_PROFILE  = 8;

// 문자열 길이 제한
constexpr size_t MAX_NAME_LEN = 32;
constexpr size_t MAX_CODE_LEN = 24;

// NVS Spec 상수(배열 제한)
constexpr uint8_t MAX_NVS_ENTRIES      = 32;
constexpr uint8_t LEN_NVS_KEY          = 32;
constexpr uint8_t LEN_NVS_TYPE         = 16;
constexpr uint8_t LEN_NVS_DEFAULT_STR  = 64;
constexpr uint8_t LEN_NVS_NAMESPACE    = 16;

// WebPage 상수(배열 제한)
constexpr uint8_t MAX_PAGES            = 24;
constexpr uint8_t MAX_PAGE_ASSETS      = 8;
constexpr uint8_t MAX_REDIRECTS        = 32;
constexpr uint8_t MAX_COMMON_ASSETS    = 16;

constexpr uint8_t LEN_URI              = 96;
constexpr uint8_t LEN_LABEL            = 32;



}  // namespace A20_Const

// ======================================================
// ENUM 정의
// ======================================================
typedef enum : uint8_t {
	EN_A20_WIFI_MODE_AP     = 0,
	EN_A20_WIFI_MODE_STA    = 1,
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
	EN_A20_PRESET_OFF            = 0,
	EN_A20_PRESET_COUNTRY        = 1,
	EN_A20_PRESET_MEDITERRANEAN  = 2,
	EN_A20_PRESET_OCEAN          = 3,
	EN_A20_PRESET_MOUNTAIN       = 4,
	EN_A20_PRESET_PLAINS         = 5,
	EN_A20_PRESET_HARBOR_BREEZE  = 6,
	EN_A20_PRESET_FOREST_CANOPY  = 7,
	EN_A20_PRESET_URBAN_SUNSET   = 8,
	EN_A20_PRESET_TROPICAL_RAIN  = 9,
	EN_A20_PRESET_DESERT_NIGHT   = 10,
	EN_A20_PRESET_COUNT
} T_A20_PresetMode_t;

// 프리셋 코드 배열 정의 (문자열 상수)  (참고용)
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

// ------------------------------------------------------
// configJsonFile (10_cfg_jsonFile.json) : camelCase 정합
//   configJsonFile.system
//   configJsonFile.wifi
//   configJsonFile.motion
//   configJsonFile.nvsSpec
//   configJsonFile.schedules
//   configJsonFile.userProfiles
//   configJsonFile.windDict
//   configJsonFile.webPage
// ------------------------------------------------------
typedef struct ST_A20_cfg_jsonFile_t {
	char system[A20_Const::LEN_PATH];
	char wifi[A20_Const::LEN_PATH];
	char motion[A20_Const::LEN_PATH];
	char nvsSpec[A20_Const::LEN_PATH];
	char schedules[A20_Const::LEN_PATH];
	char userProfiles[A20_Const::LEN_PATH];
	char windDict[A20_Const::LEN_PATH];
	char webPage[A20_Const::LEN_PATH];
} ST_A20_cfg_jsonFile_t;

typedef struct {
	uint8_t startPercentMin;	// 시동이 확실히 거는 최소 구간 (예: 18)
	uint8_t comfortPercentMin;	// “편안한 바람” 구간 시작 (예: 22), “체감 바람” 시작점 (예: 22%)
	uint8_t comfortPercentMax;	// “편안한 바람” 구간 끝   (예: 65)  소음/체감 균형 좋은 상한 (예: 65%)
	uint8_t hardPercentMax;		// 팬/소음/내구성 상으로 무리 없는 상한 (예: 90)
} ST_A20_FanConfig_t;

// ------------------------------------------------------
// SYSTEM 설정 (cfg_system_xxx.json) : camelCase 정합
//   meta.version, meta.deviceName, meta.lastUpdate
//   system.logging.level, system.logging.maxEntries
//   hw.fanPwm{pin,channel,freq,res}
//   hw.fanConfig{startPercentMin,comfortPercentMin,comfortPercentMax,hardPercentMax}
//   hw.pir{enabled,pin,debounceSec,holdSec}
//   hw.tempHum{enabled,type,pin,intervalSec}
//   hw.ble{enabled,scanInterval}
//   security.apiKey
//   time.ntpServer, time.timezone, time.syncIntervalMin
// ------------------------------------------------------
typedef struct {
	struct {
		char version[A20_Const::LEN_NAME];
		char deviceName[A20_Const::LEN_NAME];
		char lastUpdate[A20_Const::LEN_NAME];
	} meta;

	struct {
		struct {
			char level[A20_Const::LEN_LEVEL];
			uint16_t maxEntries;
		} logging;
	} system;

	struct {
		struct {
			int16_t pin;
			uint8_t channel;
			uint32_t freq;
			uint8_t res;
		} fanPwm;

		ST_A20_FanConfig_t fanConfig;

		struct {
			bool enabled;
			int16_t pin;
			uint16_t debounceSec;
			uint16_t holdSec;
		} pir;

		struct {
			bool enabled;
			char type[16];
			int16_t pin;
			uint16_t intervalSec;
		} tempHum;

		struct {
			bool enabled;
			uint16_t scanInterval;
		} ble;
	} hw;

	struct {
		char apiKey[64];
	} security;

	struct {
		char ntpServer[64];
		char timezone[32];
		uint16_t syncIntervalMin;
	} time;
} ST_A20_SystemConfig_t;

// ------------------------------------------------------
// WIFI 설정 (cfg_wifi_xxx.json) : camelCase 정합
//   wifi.wifiMode, wifi.wifiModeDesc
//   wifi.ap{ssid,password}
//   wifi.sta[]{ssid,pass}
// ------------------------------------------------------
typedef struct {
	char ssid[A20_Const::LEN_SSID];
	char pass[A20_Const::LEN_PASS];
} ST_A20_STANetwork_t;

typedef struct {
	EN_A20_WIFI_MODE_t wifiMode;
	char wifiModeDesc[48];

	struct {
		char ssid[A20_Const::LEN_SSID];
		char pass[A20_Const::LEN_PASS];
	} ap;

	ST_A20_STANetwork_t sta[A20_Const::MAX_STA_NETWORKS];
	uint8_t staCount;  // 파싱 시 채움
} ST_A20_WifiConfig_t;

// ------------------------------------------------------
// MOTION 설정 (cfg_motion_xxx.json) : camelCase 정합
//  motion.pir.enabled, motion.pir.holdSec
//  motion.ble.enabled, motion.ble.trustedDevices[], motion.ble.rssi{on,off,avgCount,persistCount,exitDelaySec}
// ------------------------------------------------------
typedef struct {
	char alias[A20_Const::LEN_ALIAS];
	char name[A20_Const::LEN_NAME];
	char mac[20];
	char manufPrefix[9];
	uint8_t prefixLen;
	bool enabled;
} ST_A20_BLETrustedDevice_t;

typedef struct {
	struct {
		bool enabled;
		uint16_t holdSec;
	} pir;

	struct {
		bool enabled;

		ST_A20_BLETrustedDevice_t trustedDevices[A20_Const::MAX_BLE_DEVICES];
		uint8_t trustedCount;

		struct {
			int16_t on;
			int16_t off;
			uint8_t avgCount;
			uint8_t persistCount;
			uint16_t exitDelaySec;
		} rssi;
	} ble;
} ST_A20_MotionConfig_t;

/* ======================================================
 * Enum/Code: Segment Mode
 * ====================================================== */
typedef enum : uint8_t {
	EN_A20_SEG_MODE_PRESET = 0,
	EN_A20_SEG_MODE_FIXED  = 1,
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
	return EN_A20_SEG_MODE_PRESET;
}

inline const char* A20_modeToString(EN_A20_segment_mode_t p_mode) {
	if (p_mode >= EN_A20_SEG_MODE_COUNT)
		return "PRESET";
	return g_A20_SEG_MODE_NAMES[p_mode];
}

/* ======================================================
 * Wind Dict (cfg_windDict_xxx.json) : camelCase 정합
 * ====================================================== */
typedef struct {
	float windIntensity;
	float gustFrequency;
	float windVariability;
	float fanLimit;
	float minFan;
	float turbulenceLengthScale;
	float turbulenceIntensitySigma;
	float thermalBubbleStrength;
	float thermalBubbleRadius;
} ST_A20_WindBase_t;

typedef struct {
	char code[A20_Const::MAX_CODE_LEN];
	char name[A20_Const::MAX_NAME_LEN];
	ST_A20_WindBase_t base;
} ST_A20_PresetEntry_t;

typedef struct {
	float intensityFactor;
	float variabilityFactor;
	float gustFactor;
	float thermalFactor;
} ST_A20_StyleFactors_t;

typedef struct {
	char code[A20_Const::MAX_CODE_LEN];
	char name[A20_Const::MAX_NAME_LEN];
	ST_A20_StyleFactors_t factors;
} ST_A20_StyleEntry_t;

typedef struct {
	uint8_t presetCount = 0;
	uint8_t styleCount  = 0;
	ST_A20_PresetEntry_t presets[A20_Const::WIND_PRESETS_MAX];
	ST_A20_StyleEntry_t  styles[A20_Const::WIND_STYLES_MAX];
} ST_A20_WindProfileDict_t;

/* ======================================================
 * 공통: Motion, AutoOff  (userProfiles/schedules 공통) : camelCase 정합
 * ====================================================== */
typedef struct {
	bool enabled = false;
	int32_t holdSec = 0;
} ST_A20_PIR_t;

typedef struct {
	bool enabled = false;
	int32_t rssiThreshold = -70;
	int32_t holdSec = 0;
} ST_A20_BLE_t;

typedef struct {
	ST_A20_PIR_t pir;
	ST_A20_BLE_t ble;
} ST_A20_Motion_t;

typedef struct {
	struct {
		bool enabled = false;
		uint32_t minutes = 0;
	} timer;
	struct {
		bool enabled = false;
		char time[6] = { 0 };
	} offTime;  // "HH:MM"
	struct {
		bool enabled = false;
		float temp = 0.0f;
	} offTemp;
} ST_A20_AutoOff_t;

typedef ST_A20_AutoOff_t ST_A20_SchAutoOff_t;

/* ======================================================
 * Segment 조정(Adjust) : camelCase 정합
 *  - JSON에서는 adjust.windIntensity / windVariability 등 사용
 *  - 모든 값은 "베이스에서 델타" (상대 변화, 단위 동일)
 * ====================================================== */
typedef struct {
	float windIntensity = 0.0f;
	float gustFrequency = 0.0f;
	float windVariability = 0.0f;
	float fanLimit = 0.0f;
	float minFan = 0.0f;
	float turbulenceLengthScale = 0.0f;
	float turbulenceIntensitySigma = 0.0f;
	float thermalBubbleStrength = 0.0f;
	float thermalBubbleRadius = 0.0f;
} ST_A20_AdjustDelta_t;

/* ======================================================
 * Schedule JSON 구조 : camelCase 정합
 * ====================================================== */
typedef struct {
	uint8_t days[7] = { 1, 1, 1, 1, 1, 1, 1 };
	char startTime[6] = { 0 };
	char endTime[6] = { 0 };
} ST_A20_SchedulePeriod_t;

typedef struct {
	uint8_t segId = 0;
	uint16_t segNo = 0;
	uint16_t onMinutes = 0;
	uint16_t offMinutes = 0;

	EN_A20_segment_mode_t mode = EN_A20_SEG_MODE_PRESET;

	char presetCode[A20_Const::MAX_CODE_LEN] = { 0 };
	char styleCode[A20_Const::MAX_CODE_LEN]  = { 0 };
	ST_A20_AdjustDelta_t adjust;

	float fixedSpeed = 0.0f;
} ST_A20_ScheduleSegment_t;

typedef struct {
	uint8_t schId = 0;
	uint16_t schNo = 0;
	char name[A20_Const::MAX_NAME_LEN] = { 0 };
	bool enabled = true;

	ST_A20_SchedulePeriod_t period;
	uint8_t segCount = 0;
	ST_A20_ScheduleSegment_t segments[A20_Const::MAX_SEGMENTS_PER_SCHEDULE];

	bool repeatSegments = true;
	uint8_t repeatCount = 0;

	ST_A20_SchAutoOff_t autoOff;
	ST_A20_Motion_t motion;
} ST_A20_ScheduleItem_t;

typedef struct {
	uint8_t count = 0;
	ST_A20_ScheduleItem_t items[A20_Const::MAX_SCHEDULES];
} ST_A20_SchedulesRoot_t;

/* ======================================================
 * UserProfiles JSON 구조 : camelCase 정합
 * ====================================================== */
typedef struct {
	uint8_t segId = 0;
	uint16_t segNo = 0;
	uint16_t onMinutes = 0;
	uint16_t offMinutes = 0;

	EN_A20_segment_mode_t mode = EN_A20_SEG_MODE_PRESET;

	char presetCode[A20_Const::MAX_CODE_LEN] = { 0 };
	char styleCode[A20_Const::MAX_CODE_LEN]  = { 0 };
	ST_A20_AdjustDelta_t adjust;

	float fixedSpeed = 0.0f;
} ST_A20_UserProfileSegment_t;

typedef struct {
	uint8_t profileId = 0;
	uint8_t profileNo = 0;
	char name[A20_Const::MAX_NAME_LEN] = { 0 };
	bool enabled = true;

	bool repeatSegments = true;
	uint8_t repeatCount = 0;

	uint8_t segCount = 0;
	ST_A20_UserProfileSegment_t segments[A20_Const::MAX_SEGMENTS_PER_PROFILE];

	ST_A20_AutoOff_t autoOff;
	ST_A20_Motion_t motion;
} ST_A20_UserProfileItem_t;

typedef struct {
	uint8_t count = 0;
	ST_A20_UserProfileItem_t items[A20_Const::MAX_USER_PROFILES];
} ST_A20_UserProfilesRoot_t;

/* ======================================================
 * 시뮬레이션에 전달할 "해석된 파라미터" : camelCase 정합
 * ====================================================== */
typedef struct {
	char presetCode[A20_Const::MAX_CODE_LEN];
	char styleCode[A20_Const::MAX_CODE_LEN];

	bool valid = false;
	bool fixedMode = false;
	float fixedSpeed = 0.0f;

	float windIntensity = 0.0f;
	float gustFrequency = 0.0f;
	float windVariability = 0.0f;
	float fanLimit = 0.0f;
	float minFan = 0.0f;
	float turbulenceLengthScale = 0.0f;
	float turbulenceIntensitySigma = 0.0f;
	float thermalBubbleStrength = 0.0f;
	float thermalBubbleRadius = 0.0f;
} ST_A20_ResolvedWind_t;


// NVS Spec (cfg_nvsSpec_xxx.json) : camelCase 정합
typedef struct ST_A20_NvsEntry_t {
	char key[A20_Const::LEN_NVS_KEY];                 // "runMode"
	char type[A20_Const::LEN_NVS_TYPE];               // "uint8" | "bool" | "string" ...
	char defaultValue[A20_Const::LEN_NVS_DEFAULT_STR]; // 숫자/불리언/문자열 모두 문자열로 저장("0","false","")
} ST_A20_NvsEntry_t;

typedef struct ST_A20_NvsSpecConfig_t {
	char namespaceName[A20_Const::LEN_NVS_NAMESPACE]; // JSON tag: "namespace"
	uint8_t entryCount = 0;                           // entries[] 개수
	ST_A20_NvsEntry_t entries[A20_Const::MAX_NVS_ENTRIES];
} ST_A20_NvsSpecConfig_t;


//  ======================================================
// Web Page (cfg_pages_xxx.json) : camelCase 정합
typedef struct ST_A20_PageAsset_t {
	char uri[A20_Const::LEN_URI];     // "/P010_main_021.css"
	char path[A20_Const::LEN_PATH];   // "/html_v2/P010_main_021.css"
} ST_A20_PageAsset_t;

typedef struct ST_A20_PageItem_t {
	char uri[A20_Const::LEN_URI];       // "/P010_main_021.html"
	char path[A20_Const::LEN_PATH];     // "/html_v2/P010_main_021.html"
	char label[A20_Const::LEN_LABEL];   // "Main"
	bool enable = true;                 // JSON tag: "enable"
	bool isMain = false;                // JSON tag: "isMain"
	uint16_t order = 0;                 // JSON tag: "order"

	uint8_t pageAssetCount = 0;         // pageAssets[] 개수
	ST_A20_PageAsset_t pageAssets[A20_Const::MAX_PAGE_ASSETS]; // JSON tag: "pageAssets"
} ST_A20_PageItem_t;

typedef struct ST_A20_ReDirectItem_t {
	char uriFrom[A20_Const::LEN_URI];   // JSON tag: "uriFrom"
	char uriTo[A20_Const::LEN_URI];     // JSON tag: "uriTo"
} ST_A20_ReDirectItem_t;

typedef struct ST_A20_CommonAsset_t {
	char uri[A20_Const::LEN_URI];
	char path[A20_Const::LEN_PATH];
	bool isCommon = false;              // JSON tag: "isCommon"
} ST_A20_CommonAsset_t;

typedef struct ST_A20_WebPageConfig_t {
	// JSON 루트 그대로: pages[], reDirect[], assets[]
	uint8_t pageCount = 0;
	ST_A20_PageItem_t pages[A20_Const::MAX_PAGES];

	uint8_t reDirectCount = 0;
	ST_A20_ReDirectItem_t reDirect[A20_Const::MAX_REDIRECTS];  // JSON tag: "reDirect"

	uint8_t assetCount = 0;
	ST_A20_CommonAsset_t assets[A20_Const::MAX_COMMON_ASSETS];
} ST_A20_WebPageConfig_t;



/* ======================================================
 * Config Root (전역 보관)
 * ====================================================== */
typedef struct {
	ST_A20_SystemConfig_t*	   system		= nullptr;
	ST_A20_WifiConfig_t*	   wifi			= nullptr;
	ST_A20_MotionConfig_t*	   motion		= nullptr;
	ST_A20_NvsSpecConfig_t*	   nvsSpec		= nullptr;	// ★ add
	ST_A20_WindProfileDict_t*  windDict		= nullptr;
	ST_A20_SchedulesRoot_t*	   schedules	= nullptr;
	ST_A20_UserProfilesRoot_t* userProfiles = nullptr;
	ST_A20_WebPageConfig_t*	   webPage		= nullptr;	// ★ add
} ST_A20_ConfigRoot_t;

extern ST_A20_ConfigRoot_t g_A20_config_root;

class CL_M10_MotionLogic;  // 전방 선언
extern CL_M10_MotionLogic* g_M10_motionLogic;

/* ======================================================
 * 헬퍼 함수 선언
 * ====================================================== */
inline float A20_clampf(float v, float lo, float hi) {
	if (v < lo) return lo;
	if (v > hi) return hi;
	return v;
}

inline void A20_safe_strlcpy(char* dst, const char* src, size_t n) {
	if (!dst || n == 0) return;
	if (!src) { dst[0] = '\0'; return; }
	strlcpy(dst, src, n);
}

// ------------------------------------------------------
// 랜덤 유틸
// ------------------------------------------------------
inline float A20_getRandom01() {
	return (float)esp_random() / (float)UINT32_MAX;
}

inline float A20_randRange(float p_min, float p_max) {
	return p_min + (A20_getRandom01() * (p_max - p_min));
}

// ======================================================
// Default 초기화 헬퍼
// ======================================================

// System 기본값
inline void A20_resetSystemDefault(ST_A20_SystemConfig_t& p_cfg) {
	memset(&p_cfg, 0, sizeof(p_cfg));

	A20_safe_strlcpy(p_cfg.meta.version, A20_Const::FW_VERSION, sizeof(p_cfg.meta.version));
	A20_safe_strlcpy(p_cfg.meta.deviceName, "SmartNatureWind", sizeof(p_cfg.meta.deviceName));
	A20_safe_strlcpy(p_cfg.meta.lastUpdate, "", sizeof(p_cfg.meta.lastUpdate));

	A20_safe_strlcpy(p_cfg.system.logging.level, "INFO", sizeof(p_cfg.system.logging.level));
	p_cfg.system.logging.maxEntries = 300;

	// HW: fanPwm
	p_cfg.hw.fanPwm.pin     = 6;
	p_cfg.hw.fanPwm.channel = 0;
	p_cfg.hw.fanPwm.freq    = 25000;
	p_cfg.hw.fanPwm.res     = 10;

	// HW: fanConfig
	p_cfg.hw.fanConfig.startPercentMin   = 10;
	p_cfg.hw.fanConfig.comfortPercentMin = 20;
	p_cfg.hw.fanConfig.comfortPercentMax = 80;
	p_cfg.hw.fanConfig.hardPercentMax    = 95;

	// HW: pir
	p_cfg.hw.pir.enabled     = true;
	p_cfg.hw.pir.pin         = 13;
	p_cfg.hw.pir.debounceSec = 5;
	p_cfg.hw.pir.holdSec     = 120;

	// HW: tempHum
	p_cfg.hw.tempHum.enabled     = true;
	A20_safe_strlcpy(p_cfg.hw.tempHum.type, "DHT22", sizeof(p_cfg.hw.tempHum.type));
	p_cfg.hw.tempHum.pin         = 23;
	p_cfg.hw.tempHum.intervalSec = 30;

	// HW: ble
	p_cfg.hw.ble.enabled      = true;
	p_cfg.hw.ble.scanInterval = 5;

	// security
	A20_safe_strlcpy(p_cfg.security.apiKey, "", sizeof(p_cfg.security.apiKey));

	// time
	A20_safe_strlcpy(p_cfg.time.ntpServer, "pool.ntp.org", sizeof(p_cfg.time.ntpServer));
	A20_safe_strlcpy(p_cfg.time.timezone, "Asia/Seoul", sizeof(p_cfg.time.timezone));
	p_cfg.time.syncIntervalMin = 60;
}

// WiFi 기본값
inline void A20_resetWifiDefault(ST_A20_WifiConfig_t& p_cfg) {
	memset(&p_cfg, 0, sizeof(p_cfg));

	p_cfg.wifiMode = EN_A20_WIFI_MODE_AP_STA;
	A20_safe_strlcpy(p_cfg.wifiModeDesc, "0=AP,1=STA,2=AP+STA", sizeof(p_cfg.wifiModeDesc));

	A20_safe_strlcpy(p_cfg.ap.ssid, "NatureWind", sizeof(p_cfg.ap.ssid));
	A20_safe_strlcpy(p_cfg.ap.pass, "2540", sizeof(p_cfg.ap.pass));

	p_cfg.staCount = 0;
}

// Motion 기본값
inline void A20_resetMotionDefault(ST_A20_MotionConfig_t& p_cfg) {
	memset(&p_cfg, 0, sizeof(p_cfg));

	p_cfg.pir.enabled = true;
	p_cfg.pir.holdSec = 120;

	p_cfg.ble.enabled      = true;
	p_cfg.ble.trustedCount = 0;

	p_cfg.ble.rssi.on           = -65;
	p_cfg.ble.rssi.off          = -75;
	p_cfg.ble.rssi.avgCount     = 8;
	p_cfg.ble.rssi.persistCount = 5;
	p_cfg.ble.rssi.exitDelaySec = 12;
}

// WindProfile Dict 기본값
inline void A20_resetWindProfileDictDefault(ST_A20_WindProfileDict_t& p_dict) {
	memset(&p_dict, 0, sizeof(p_dict));

	p_dict.presetCount = 1;
	A20_safe_strlcpy(p_dict.presets[0].code, "OCEAN", sizeof(p_dict.presets[0].code));
	A20_safe_strlcpy(p_dict.presets[0].name, "Ocean Breeze", sizeof(p_dict.presets[0].name));

	p_dict.presets[0].base.windIntensity            = 70.0f;
	p_dict.presets[0].base.windVariability          = 50.0f;
	p_dict.presets[0].base.gustFrequency            = 45.0f;
	p_dict.presets[0].base.fanLimit                 = 90.0f;
	p_dict.presets[0].base.minFan                   = 10.0f;
	p_dict.presets[0].base.turbulenceLengthScale    = 40.0f;
	p_dict.presets[0].base.turbulenceIntensitySigma = 0.5f;
	p_dict.presets[0].base.thermalBubbleStrength    = 2.0f;
	p_dict.presets[0].base.thermalBubbleRadius      = 18.0f;

	p_dict.styleCount = 1;
	A20_safe_strlcpy(p_dict.styles[0].code, "BALANCE", sizeof(p_dict.styles[0].code));
	A20_safe_strlcpy(p_dict.styles[0].name, "Balance", sizeof(p_dict.styles[0].name));

	p_dict.styles[0].factors.intensityFactor   = 1.0f;
	p_dict.styles[0].factors.variabilityFactor = 1.0f;
	p_dict.styles[0].factors.gustFactor        = 1.0f;
	p_dict.styles[0].factors.thermalFactor     = 1.0f;
}

// Schedules 기본값
inline void A20_resetSchedulesDefault(ST_A20_SchedulesRoot_t& p_cfg) {
	memset(&p_cfg, 0, sizeof(p_cfg));
	p_cfg.count = 0;
}

// UserProfiles 기본값
inline void A20_resetUserProfilesDefault(ST_A20_UserProfilesRoot_t& p_cfg) {
	memset(&p_cfg, 0, sizeof(p_cfg));
	p_cfg.count = 0;
}

// ------------------------------------------------------
// NVS Spec 기본값
// ------------------------------------------------------
inline void A20_resetNvsSpecDefault(ST_A20_NvsSpecConfig_t& p_cfg) {
	memset(&p_cfg, 0, sizeof(p_cfg));

	// namespace
	A20_safe_strlcpy(p_cfg.namespaceName, "SNW", sizeof(p_cfg.namespaceName));

	// entries (기본 스펙)
	p_cfg.entryCount = 0;

	auto addEntry = [&](const char* p_key, const char* p_type, const char* p_def) {
		if (p_cfg.entryCount >= A20_Const::MAX_NVS_ENTRIES) return;
		ST_A20_NvsEntry_t& v_e = p_cfg.entries[p_cfg.entryCount];
		memset(&v_e, 0, sizeof(v_e));
		A20_safe_strlcpy(v_e.key, p_key, sizeof(v_e.key));
		A20_safe_strlcpy(v_e.type, p_type, sizeof(v_e.type));
		A20_safe_strlcpy(v_e.defaultValue, p_def, sizeof(v_e.defaultValue));
		p_cfg.entryCount++;
	};

	addEntry("runMode",         "uint8",  "0");
	addEntry("activeProfileNo", "uint8",  "0");
	addEntry("activeSegmentNo", "uint8",  "0");
	addEntry("presetCode",      "string", "");
	addEntry("styleCode",       "string", "");
	addEntry("wifiConnected",   "bool",   "false");
}

// ------------------------------------------------------
// WebPage 기본값
// ------------------------------------------------------
inline void A20_resetWebPageDefault(ST_A20_WebPageConfig_t& p_cfg) {
	memset(&p_cfg, 0, sizeof(p_cfg));

	// pages[] : 최소 1개 메인 페이지 기본값
	if (A20_Const::MAX_PAGES > 0) {
		p_cfg.pageCount = 1;

		ST_A20_PageItem_t& v_p = p_cfg.pages[0];
		memset(&v_p, 0, sizeof(v_p));

		A20_safe_strlcpy(v_p.uri,   "/P010_main_021.html", sizeof(v_p.uri));
		A20_safe_strlcpy(v_p.path,  "/html_v2/P010_main_021.html", sizeof(v_p.path));
		A20_safe_strlcpy(v_p.label, "Main", sizeof(v_p.label));

		v_p.enable = true;
		v_p.isMain = true;
		v_p.order  = 10;

		// pageAssets[] : css/js (있으면)
		v_p.pageAssetCount = 0;
		if (A20_Const::MAX_PAGE_ASSETS >= 1) {
			ST_A20_PageAsset_t& v_a0 = v_p.pageAssets[v_p.pageAssetCount++];
			memset(&v_a0, 0, sizeof(v_a0));
			A20_safe_strlcpy(v_a0.uri,  "/P010_main_021.css", sizeof(v_a0.uri));
			A20_safe_strlcpy(v_a0.path, "/html_v2/P010_main_021.css", sizeof(v_a0.path));
		}
		if (A20_Const::MAX_PAGE_ASSETS >= 2) {
			ST_A20_PageAsset_t& v_a1 = v_p.pageAssets[v_p.pageAssetCount++];
			memset(&v_a1, 0, sizeof(v_a1));
			A20_safe_strlcpy(v_a1.uri,  "/P010_main_021.js", sizeof(v_a1.uri));
			A20_safe_strlcpy(v_a1.path, "/html_v2/P010_main_021.js", sizeof(v_a1.path));
		}
	}

	// reDirect[] : 최소 홈 리다이렉트
	p_cfg.reDirectCount = 0;
	auto addRedirect = [&](const char* p_from, const char* p_to) {
		if (p_cfg.reDirectCount >= A20_Const::MAX_REDIRECTS) return;
		ST_A20_ReDirectItem_t& v_r = p_cfg.reDirect[p_cfg.reDirectCount];
		memset(&v_r, 0, sizeof(v_r));
		A20_safe_strlcpy(v_r.uriFrom, p_from, sizeof(v_r.uriFrom));
		A20_safe_strlcpy(v_r.uriTo,   p_to,   sizeof(v_r.uriTo));
		p_cfg.reDirectCount++;
	};

	addRedirect("/",           "/P010_main_021.html");
	addRedirect("/index.html", "/P010_main_021.html");
	addRedirect("/P010_main",  "/P010_main_021.html");

	// assets[] : 공통 자산 기본값
	p_cfg.assetCount = 0;
	auto addCommonAsset = [&](const char* p_uri, const char* p_path, bool p_isCommon) {
		if (p_cfg.assetCount >= A20_Const::MAX_COMMON_ASSETS) return;
		ST_A20_CommonAsset_t& v_c = p_cfg.assets[p_cfg.assetCount];
		memset(&v_c, 0, sizeof(v_c));
		A20_safe_strlcpy(v_c.uri,  p_uri,  sizeof(v_c.uri));
		A20_safe_strlcpy(v_c.path, p_path, sizeof(v_c.path));
		v_c.isCommon = p_isCommon;
		p_cfg.assetCount++;
	};

	addCommonAsset("/P000_common_001.css", "/html_v2/P000_common_001.css", true);
	addCommonAsset("/P000_common_006.js",  "/html_v2/P000_common_006.js",  true);
}

// ------------------------------------------------------
// A20_resetToDefault
// ------------------------------------------------------
inline void A20_resetToDefault(ST_A20_ConfigRoot_t& p_root) {
	if (p_root.system)       A20_resetSystemDefault(*p_root.system);
	if (p_root.wifi)         A20_resetWifiDefault(*p_root.wifi);
	if (p_root.motion)       A20_resetMotionDefault(*p_root.motion);
	if (p_root.windDict)     A20_resetWindProfileDictDefault(*p_root.windDict);
	if (p_root.schedules)    A20_resetSchedulesDefault(*p_root.schedules);
	if (p_root.userProfiles) A20_resetUserProfilesDefault(*p_root.userProfiles);

    if (p_root.webPage) A20_resetWebPageDefault(*p_root.webPage);
    if (p_root.nvsSpec) A20_resetNvsSpecDefault(*p_root.nvsSpec);
}

// 프리셋 코드 → 인덱스 매핑 (참고용)
inline int8_t A20_getPresetIndexByCode(const char* code) {
	if (!code) return -1;
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
	if (!p_code || !p_code[0]) return -1;
	for (uint8_t v_i = 0; v_i < p_dict.presetCount; v_i++) {
		if (strcasecmp(p_dict.presets[v_i].code, p_code) == 0)
			return (int16_t)v_i;
	}
	return -1;
}

inline int16_t A20_findStyleIndexByCode(const ST_A20_WindProfileDict_t& p_dict, const char* p_code) {
	if (!p_code || !p_code[0]) return -1;
	for (uint8_t v_i = 0; v_i < p_dict.styleCount; v_i++) {
		if (strcasecmp(p_dict.styles[v_i].code, p_code) == 0)
			return (int16_t)v_i;
	}
	return -1;
}
