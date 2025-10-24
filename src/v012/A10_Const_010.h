#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : A10_Const_010.h
 * 모듈명 : Smart Nature Wind Const Manager
 * ------------------------------------------------------
 * 기능 요약:
 *  - Smart Nature Wind 공통 전역 상수/타입/구조체/유틸 정의
 *  - JSON 분리 기준(코어/와이파이/시뮬/환경/스케줄/모션) 반영
 *  - 메모리 효율 위한 "하이브리드 구성" 지원:
 * ------------------------------------------------------
 * [코드 네이밍 규칙]
 * 		- 현재 파일 모듈약어    : A10
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
#include <ArduinoJson.h>

#define G_A10_CFG_JSON_FILE_VER		"012"

namespace A10_Const {
	// 펌웨어/파일 경로/용량 한계 -------------------------------------------
	constexpr char FW_VERSION[] = "SC10_FW_1.0.0";

	// JSON 파일(분리 저장) 경로
	constexpr char CFG_CORE_FILE[]	   = "/json/cfg_core_" 		G_A10_CFG_JSON_FILE_VER ".json";
	constexpr char CFG_WIFI_FILE[]	   = "/json/cfg_wifi_"		G_A10_CFG_JSON_FILE_VER ".json";
	constexpr char CFG_SIM_FILE[]	   = "/json/cfg_sim_" 		G_A10_CFG_JSON_FILE_VER ".json";
	constexpr char CFG_ENV_FILE[]	   = "/json/cfg_env_" 		G_A10_CFG_JSON_FILE_VER ".json";
	constexpr char CFG_SCHEDULE_FILE[] = "/json/cfg_schedule_"	G_A10_CFG_JSON_FILE_VER ".json";
	constexpr char CFG_MOTION_FILE[]   = "/json/cfg_motion_"	G_A10_CFG_JSON_FILE_VER ".json";

	// 각 파일의 백업 경로 (.bak) - 현재 버전 파일명에 ".bak" 추가
	constexpr char CFG_CORE_FILE_BAK[]	   = "/json/cfg_core_" 		G_A10_CFG_JSON_FILE_VER ".json.bak";
	constexpr char CFG_WIFI_FILE_BAK[]	   = "/json/cfg_wifi_"		G_A10_CFG_JSON_FILE_VER ".json.bak";
	constexpr char CFG_SIM_FILE_BAK[]	   = "/json/cfg_sim_" 		G_A10_CFG_JSON_FILE_VER ".json.bak";
	constexpr char CFG_ENV_FILE_BAK[]	   = "/json/cfg_env_" 		G_A10_CFG_JSON_FILE_VER ".json.bak";
	constexpr char CFG_SCHEDULE_FILE_BAK[] = "/json/cfg_schedule_"	G_A10_CFG_JSON_FILE_VER ".json.bak";
	constexpr char CFG_MOTION_FILE_BAK[]   = "/json/cfg_motion_"	G_A10_CFG_JSON_FILE_VER ".json.bak";

	// // JSON 파일(분리 저장) 경로
	// constexpr char CFG_CORE_FILE[]	   = "/json/cfg_core_021.json";
	// constexpr char CFG_WIFI_FILE[]	   = "/json/cfg_wifi_021.json";
	// constexpr char CFG_SIM_FILE[]	   = "/json/cfg_sim_021.json";
	// constexpr char CFG_ENV_FILE[]	   = "/json/cfg_env_021.json";
	// constexpr char CFG_SCHEDULE_FILE[] = "/json/cfg_schedule_021.json";
	// constexpr char CFG_MOTION_FILE[]   = "/json/cfg_motion_021.json";

	// // 각 파일의 백업 경로 (.bak)
	// constexpr char CFG_CORE_FILE_BAK[]	   = "/json/cfg_core_021.json.bak";
	// constexpr char CFG_WIFI_FILE_BAK[]	   = "/json/cfg_wifi_021.json.bak";
	// constexpr char CFG_SIM_FILE_BAK[]	   = "/json/cfg_sim_021.json.bak";
	// constexpr char CFG_ENV_FILE_BAK[]	   = "/json/cfg_env_021.json.bak";
	// constexpr char CFG_SCHEDULE_FILE_BAK[] = "/json/cfg_schedule_021.json.bak";
	// constexpr char CFG_MOTION_FILE_BAK[]   = "/json/cfg_motion_021.json.bak";

	// 사이즈/개수 제한
	constexpr int MAX_STA_NETWORKS			= 5;
	constexpr int MAX_SCHEDULES				= 5;
	constexpr int MAX_SEGMENTS_PER_SCHEDULE = 5;
	constexpr int MAX_TEMP_ZONES			= 5;
	constexpr int MAX_BLE_DEVICES			= 5;

	// JsonDocument 기본 메모리(섹션별 파싱용) - 필요 시 조정
	constexpr size_t DOC_CORE_CAP	  		= 8 * 1024;
	constexpr size_t DOC_WIFI_CAP	  		= 6 * 1024;
	constexpr size_t DOC_SIM_CAP	  		= 4 * 1024;
	constexpr size_t DOC_ENV_CAP	  		= 8 * 1024;
	constexpr size_t DOC_SCHEDULE_CAP 		= 8 * 1024;
	constexpr size_t DOC_MOTION_CAP	  		= 6 * 1024;
}  // namespace A10_Const

// ======================================================
// ENUM / 기본 상수
// ======================================================

// Wi-Fi 모드
typedef enum : uint8_t {
	EN_A10_WIFI_MODE_AP		= 0,  // AP 단독
	EN_A10_WIFI_MODE_STA	= 1,  // STA 단독
	EN_A10_WIFI_MODE_AP_STA = 2	  // AP+STA 동시
} T_A10_WifiMode_t;

// 바람 단계(참고용)
typedef enum : uint8_t {
	EN_A10_WEATHER_PHASE_CALM = 0,
	EN_A10_WEATHER_PHASE_NORMAL,
	EN_A10_WEATHER_PHASE_STRONG,
	EN_A10_WEATHER_PHASE_COUNT
} T_A10_WindPhase_t;

static const char* g_A10_WEATHER_PHASE_NAMES_Arr[] = {
	"CALM", 
	"NORMAL", 
	"STRONG"
};

	
// 프리셋
typedef enum {
    EN_A10_PRESET_OFF              = 0,     //	고정풍속       
    EN_A10_PRESET_COUNTRY          = 1,     //	들판
    EN_A10_PRESET_MEDITERRANEAN    = 2,     //	지중해
    EN_A10_PRESET_OCEAN            = 3,     //	바다
    EN_A10_PRESET_MOUNTAIN         = 4,     //	산바람
    EN_A10_PRESET_PLAINS           = 5,     //	평야
    EN_A10_PRESET_HARBOR_BREEZE    = 6,     //	숲속
    EN_A10_PRESET_FOREST_CANOPY    = 7,     //	항구바람
    EN_A10_PRESET_URBAN_SUNSET     = 8,     //	도심석양
    EN_A10_PRESET_TROPICAL_RAIN    = 9,     //	열대우림
    EN_A10_PRESET_DESERT_NIGHT     = 10,    //	사막밤   
    EN_A10_PRESET_COUNT
} T_A10_PresetMode_t;

static const char* g_A10_PRESET_MODE_NAMES_Arr[] = {
    "OFF"          ,            //	고정풍속                         
    "COUNTRY"      ,            //	들판 
    "MEDITERRANEAN",            //	지중해         
    "OCEAN"        ,            //	바다 
    "MOUNTAIN"     ,            //	산바람 
    "PLAINS"       ,            //	평야 
    "HARBOR_BREEZE",            //	숲속         
    "FOREST_CANOPY",            //	항구바람         
    "URBAN_SUNSET" ,            //	도심석양     
    "TROPICAL_RAIN",            //	열대우림         
    "DESERT_NIGHT" ,            //	사막밤                    
};

// 길이 상수
#define G_A10_API_KEY_MAX_LEN   64
#define G_A10_WIFI_SSID_LEN     32
#define G_A10_WIFI_PWD_LEN      64
#define G_A10_WEB_PATH_LEN      96
#define G_A10_WEB_URI_LEN       48
#define G_A10_WEB_MIME_LEN      32


// ======================================================
// 구조체 정의 (섹션별)
// ======================================================

// STA 자격정보
typedef struct {
	char ssid[32];
	char pass[64];
} ST_A10_StaCredential;

// ---------------- Core(항상 상주): meta/system/hw/security/time
typedef struct {
	// [meta] 기기/펌웨어 메타
	struct {
		char version[16];	   // SC10_FW_1.0.0
		char device_name[32];  // 예: WindScape_XY-SK10
		char last_update[32];  // ISO8601
	} meta;

	// [system] 웹/로그
	struct {
		struct {  // 웹 자원 경로
			char html[64];
			char css[64];
			char js[64];
		} web;
		struct {				// 로깅 레벨/엔트리수
			char	 level[8];	// INFO/WARN/DEBUG
			uint16_t max_entries;
		} logging;
	} system;

	// [hw] 하드웨어 핀 기본값 (팬 PWM, 센서 핀 등)
	struct {
		struct {  // 팬 PWM
			int pin;
			int channel;
			int freq;
			int res;
		} fan_pwm;

		struct {  // 센서 핀/타입(하드웨어 배치 정보)
			struct {
				int	 pin;
				bool enabled;
			} pir;
			struct {
				char type[16];
				int	 pin;
				int	 interval_sec;
			} temp;
			struct {
				int	 pin;
				bool shared;
			} humidity;	 // temp pin 공유 여부 등
		} sensors;
	} hw;

	// [security] 보안키(헤더에 저장 시 UI 노출 주의)
	struct {
		char api_key[64];
	} security;

	// [time] NTP/시간대/동기화 주기
	struct {
		char	 ntp_server[64];
		char	 timezone[32];
		uint16_t sync_interval_min;
	} time;

} ST_A10_CoreConfig;

// ---------------- Wi-Fi (Lazy-Load)
typedef struct {
	uint8_t mode;  // EN_A10_WIFI_MODE_*
	struct {
		char ssid[32];
		char password[64];
	} ap;
	ST_A10_StaCredential sta[A10_Const::MAX_STA_NETWORKS];
	uint8_t				 sta_count;
} ST_A10_WifiConfig;

// ---------------- Sim(바람 물리) (Lazy-Load)
typedef struct {
	char  preset[32];		 // 예: COUNTRY_BREEZE
	float wind_intensity;	 // %
	float gust_frequency;	 // %
	float wind_variability;	 // %
	float fan_limit;		 // %
	float min_fan;			 // %
	struct {				 // 난류 파라미터
		float length_scale;
		float intensity_sigma;
	} turbulence;
	struct {  // 열기포 파라미터
		float bubble_strength;
		float bubble_radius;
	} thermal;
} ST_A10_SimConfig;

// ---------------- Env(온도+습도) (항상 상주)
typedef struct {
	// 온도 제어
	struct {
		bool enabled;
		struct {
			float min_temp;
			float max_temp;
			char  mode[16];			// "off"/"fixed"/"preset"
			float fixed_speed;		// fixed일 때 %
			char  preset_name[32];	// preset일 때 이름
			float adj_intensity;	// preset 보정(%)
			float adj_variability;	// preset 보정(%)
			int	  hold_sec;			// 상태 유지 시간
		} zones[A10_Const::MAX_TEMP_ZONES];
		uint8_t zone_count;
	} temp_control;

	// 습도 제어
	struct {
		bool enabled;
		struct {
			float high;
			float low;
		} threshold;
		struct {			 // high 동작
			char  mode[16];	 // "preset"/"fixed"
			char  preset_name[32];
			float adj_intensity;
			float adj_variability;
			float fixed_speed;
		} action_high;
		struct {  // low 동작
			char  mode[16];
			char  preset_name[32];
			float adj_intensity;
			float adj_variability;
			float fixed_speed;
		} action_low;
	} humidity_control;

} ST_A10_EnvConfig;

// ---------------- Schedule (Lazy-Load)
typedef struct {
	// 하나의 세그먼트: on/off 반복 규칙
	struct {
		int	  on_minutes;
		int	  off_minutes;
		char  mode[16];	 // "preset"/"fixed"
		char  preset_name[32];
		float adj_intensity;
		float adj_variability;
		float fixed_speed;
	} seg[A10_Const::MAX_SEGMENTS_PER_SCHEDULE];

	uint8_t seg_count;
	// 스케줄 메타
	uint8_t id;
	char	name[32];
	bool	enabled;
	uint8_t days[7];   // 월~일 1/0
	char	start[6];  // "HH:MM"
	char	end[6];
} ST_A10_ScheduleItem;

typedef struct {
	ST_A10_ScheduleItem items[A10_Const::MAX_SCHEDULES];
	uint8_t				count;
} ST_A10_ScheduleConfig;

// ---------------- Motion (Lazy-Load)
typedef struct {
	bool enabled;
	char logic_mode[8];	 // "OR"/"AND"
	struct {			 // PIR
		bool enabled;
		int	 pin;
		int	 hold_sec;
		int	 debounce_sec;
	} pir;
	struct {  // BLE
		bool enabled;
		int	 scan_interval;
		int	 rssi_threshold;
		int	 hold_sec;
		struct {
			char mac[20];
			char alias[32];
			bool enabled;
		} devices[A10_Const::MAX_BLE_DEVICES];
		uint8_t device_count;
	} ble;
} ST_A10_MotionConfig;

// ======================================================
// 통합 루트 구성(하이브리드)
//  - Core / Env : by-value(상시 상주)
//  - Wifi/Sim/Schedule/Motion : 포인터 (필요 시만 동적할당)
// ======================================================
typedef struct {
	ST_A10_CoreConfig 		core;	 // 항상 상주
	ST_A10_EnvConfig  		env;	 // 항상 상주

	ST_A10_WifiConfig*	   	wifi;	  // nullptr이면 미로딩
	ST_A10_SimConfig*	   	sim;		  // "
	ST_A10_ScheduleConfig* 	schedule;  // "
	ST_A10_MotionConfig*   	motion;	  // "
} ST_A10_ConfigRoot;

// 전역 루트(선언만; 정의는 구현부 또는 사용처 .cpp)
inline ST_A10_ConfigRoot g_A10_config_root;

// ======================================================
// 유틸 함수
// ======================================================
inline float A10_getRandom01() {
	return static_cast<float>(esp_random()) / static_cast<float>(UINT32_MAX);
}
inline float A10_randRange(float a, float b) {
	return a + A10_getRandom01() * (b - a);
}

inline const char* A10_getPresetName(uint8_t index) {
	if (index >= EN_A10_PRESET_COUNT)
		return "UNKNOWN";
	return g_A10_PRESET_MODE_NAMES_Arr[index];
}

inline int8_t A10_getPresetIndex(const char* name) {
	for (uint8_t i = 0; i < EN_A10_PRESET_COUNT; ++i)
		if (strcasecmp(name, g_A10_PRESET_MODE_NAMES_Arr[i]) == 0)
			return i;
	return -1;
}


// ------------------------------------------------------
// 구조체별 초기화 함수
// ------------------------------------------------------

/**
 * @brief ST_A10_CoreConfig를 기본값으로 초기화합니다.
 * @param p_cfg 초기화할 CoreConfig 구조체 포인터
 */
inline void A10_resetCoreDefault(ST_A10_CoreConfig& p_cfg) {
    // 0으로 전체 초기화
    memset(&p_cfg, 0, sizeof(p_cfg));
    
    // [meta]
    strcpy(p_cfg.meta.version, A10_Const::FW_VERSION);
    strcpy(p_cfg.meta.device_name, "WindScape_001");
    // last_update는 빈 문자열 또는 현재 시간으로 설정 (여기선 빈 문자열)
    strcpy(p_cfg.meta.last_update, "");

    // [system]
    strcpy(p_cfg.system.web.html, "/index.html");
    strcpy(p_cfg.system.web.css, "/style.css");
    strcpy(p_cfg.system.web.js, "/app.js");
    strcpy(p_cfg.system.logging.level, "INFO");
    p_cfg.system.logging.max_entries = 50;

    // [hw] - 팬 PWM/센서 기본 핀 설정
    p_cfg.hw.fan_pwm.pin = 25;
    p_cfg.hw.fan_pwm.channel = 0;
    p_cfg.hw.fan_pwm.freq = 20000; // 20kHz
    p_cfg.hw.fan_pwm.res = 8; // 8bit resolution

    p_cfg.hw.sensors.pir.pin = 34;
    p_cfg.hw.sensors.pir.enabled = true;
    strcpy(p_cfg.hw.sensors.temp.type, "DHT22");
    p_cfg.hw.sensors.temp.pin = 33;
    p_cfg.hw.sensors.temp.interval_sec = 60;
    p_cfg.hw.sensors.humidity.pin = 33; // DHT 센서 핀 공유
    p_cfg.hw.sensors.humidity.shared = true;

    // [security]
    strcpy(p_cfg.security.api_key, "YOUR_DEFAULT_API_KEY"); // 실제 사용 시 보안에 주의

    // [time]
    strcpy(p_cfg.time.ntp_server, "pool.ntp.org");
    strcpy(p_cfg.time.timezone, "KST-9"); // 대한민국 표준시
    p_cfg.time.sync_interval_min = 60;
}

/**
 * @brief ST_A10_WifiConfig를 기본값으로 초기화합니다.
 * @param p_cfg 초기화할 WifiConfig 구조체 포인터
 */
inline void A10_resetWifiDefault(ST_A10_WifiConfig& p_cfg) {
    // 0으로 전체 초기화
    memset(&p_cfg, 0, sizeof(p_cfg));
    
    p_cfg.mode = EN_A10_WIFI_MODE_AP;
    strcpy(p_cfg.ap.ssid, "SmartNatureWind");
    strcpy(p_cfg.ap.password, "12345678");
    p_cfg.sta_count = 0;
    // sta 배열은 0으로 이미 초기화됨
}

/**
 * @brief ST_A10_SimConfig를 기본값으로 초기화합니다.
 * @param p_cfg 초기화할 SimConfig 구조체 포인터
 */
inline void A10_resetSimDefault(ST_A10_SimConfig& p_cfg) {
    // 0으로 전체 초기화
    memset(&p_cfg, 0, sizeof(p_cfg));
    
    strcpy(p_cfg.preset, "COUNTRY");
	
    p_cfg.wind_intensity = 50.0f;       // %
    p_cfg.gust_frequency = 0.5f;       // 0.0 ~ 1.0
    p_cfg.wind_variability = 0.5f;     // 0.0 ~ 1.0
    p_cfg.fan_limit = 100.0f;          // %
    p_cfg.min_fan = 20.0f;             // %
    
    // 난류 파라미터
    p_cfg.turbulence.length_scale = 0.3f;
    p_cfg.turbulence.intensity_sigma = 0.1f;
    
    // 열기포 파라미터
    p_cfg.thermal.bubble_strength = 0.5f;
    p_cfg.thermal.bubble_radius = 1.2f;
}

/**
 * @brief ST_A10_EnvConfig를 기본값으로 초기화합니다.
 * @param p_cfg 초기화할 EnvConfig 구조체 포인터
 */
inline void A10_resetEnvDefault(ST_A10_EnvConfig& p_cfg) {
    // 0으로 전체 초기화
    memset(&p_cfg, 0, sizeof(p_cfg));
    
    // 온도 제어
    p_cfg.temp_control.enabled = false;
    p_cfg.temp_control.zone_count = 1;
    // 첫 번째 존 기본값 (OFF)
    p_cfg.temp_control.zones[0].min_temp = 25.0f;
    p_cfg.temp_control.zones[0].max_temp = 30.0f;
    strcpy(p_cfg.temp_control.zones[0].mode, "off");
    p_cfg.temp_control.zones[0].fixed_speed = 0.0f;
    strcpy(p_cfg.temp_control.zones[0].preset_name, "");
    p_cfg.temp_control.zones[0].adj_intensity = 0.0f;
    p_cfg.temp_control.zones[0].adj_variability = 0.0f;
    p_cfg.temp_control.zones[0].hold_sec = 180;

    // 습도 제어
    p_cfg.humidity_control.enabled = false;
    p_cfg.humidity_control.threshold.high = 70.0f;
    p_cfg.humidity_control.threshold.low = 30.0f;

    // High Action
    strcpy(p_cfg.humidity_control.action_high.mode, "preset");
    strcpy(p_cfg.humidity_control.action_high.preset_name, "TROPICAL_RAIN");
    p_cfg.humidity_control.action_high.adj_intensity = 20.0f;
    p_cfg.humidity_control.action_high.adj_variability = 0.0f;
    p_cfg.humidity_control.action_high.fixed_speed = 0.0f;

    // Low Action
    strcpy(p_cfg.humidity_control.action_low.mode, "preset");
    strcpy(p_cfg.humidity_control.action_low.preset_name, "DESERT_NIGHT");
    p_cfg.humidity_control.action_low.adj_intensity = 0.0f;
    p_cfg.humidity_control.action_low.adj_variability = -20.0f;
    p_cfg.humidity_control.action_low.fixed_speed = 0.0f;
}


// ------------------------------------------------------
// 메인 초기화 함수
// ------------------------------------------------------

/**
 * @brief ST_A10_ConfigRoot 전체를 기본값으로 초기화합니다.
 * * core와 env는 by-value이므로 직접 초기화합니다.
 * wifi, sim, schedule, motion은 by-pointer이므로 nullptr로 초기화합니다.
 * @param p_root 초기화할 ST_A10_ConfigRoot 구조체 포인터
 */
inline void A10_resetToDefault(ST_A10_ConfigRoot& p_root) {
    // 1. by-value 멤버 초기화 (상시 상주)
    A10_resetCoreDefault(p_root.core);
    A10_resetEnvDefault(p_root.env);

    // 2. by-pointer 멤버 초기화 (Lazy-Load)
    // 메모리 해제 로직은 포함하지 않음. 호출 시점에 메모리가 이미 할당되어 있다면 누수 발생 가능성 있음.
    // 여기서는 기본값인 nullptr로 설정합니다.
    p_root.wifi = nullptr;
    p_root.sim = nullptr;
    p_root.schedule = nullptr;
    p_root.motion = nullptr;
}