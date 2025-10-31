#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : A10_Const_012.h
 * 모듈 약어 : A10
 * 모듈명 : Smart Nature Wind Const Manager (v011)
 * ------------------------------------------------------
 * 기능 요약:
 *  - Smart Nature Wind 공통 전역 상수/타입/구조체 정의
 *  - JSON 분리 기준 (System/WiFi/Motion/Control) 완전 반영
 *  - ArduinoJson v7.x.x 전용 구조체 매핑
 *  - Lazy-Load 하이브리드 구성 (필요 시 동적 로드)
 * ------------------------------------------------------
 * [구현 규칙]
 *  - 항상 소스 시작 주석 체계 유지
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

namespace A10_Const {
	// 펌웨어/파일버전
	constexpr char FW_VERSION[] = "SC10_FW_1.0.0";
	#define G_A10_CFG_JSON_FILE_VER "022"

	// JSON 파일 경로
	constexpr char CFG_SYSTEM_FILE[]   = "/json/cfg_system_"   G_A10_CFG_JSON_FILE_VER ".json";
	constexpr char CFG_WIFI_FILE[]     = "/json/cfg_wifi_"     G_A10_CFG_JSON_FILE_VER ".json";
	constexpr char CFG_CONTROL_FILE[]  = "/json/cfg_control_"  G_A10_CFG_JSON_FILE_VER ".json";
	constexpr char CFG_MOTION_FILE[]   = "/json/cfg_motion_"   G_A10_CFG_JSON_FILE_VER ".json";
	constexpr char CFG_DEFAULT_FILE[]  = "/json/cfg_default_"  G_A10_CFG_JSON_FILE_VER ".json";

	// 백업 경로
	constexpr char CFG_SYSTEM_FILE_BAK[]   = "/json/cfg_system_"   G_A10_CFG_JSON_FILE_VER ".json.bak";
	constexpr char CFG_WIFI_FILE_BAK[]     = "/json/cfg_wifi_"     G_A10_CFG_JSON_FILE_VER ".json.bak";
	constexpr char CFG_CONTROL_FILE_BAK[]  = "/json/cfg_control_"  G_A10_CFG_JSON_FILE_VER ".json.bak";
	constexpr char CFG_MOTION_FILE_BAK[]   = "/json/cfg_motion_"   G_A10_CFG_JSON_FILE_VER ".json.bak";

	// 문자열 및 배열 길이 정의
	constexpr uint8_t LEN_NAME   = 64;
	constexpr uint8_t LEN_PATH   = 128;
	constexpr uint8_t LEN_SSID   = 32;
	constexpr uint8_t LEN_PASS   = 32;
	constexpr uint8_t LEN_PRESET = 32;
	constexpr uint8_t LEN_ALIAS  = 32;
	constexpr uint8_t LEN_TIME   = 8;
	constexpr uint8_t LEN_LEVEL  = 16;

	// 배열 개수
	constexpr uint8_t MAX_BLE_DEVICES           = 8;
	constexpr uint8_t MAX_STA_NETWORKS          = 5;
	constexpr uint8_t MAX_SCHEDULES             = 5;
	constexpr uint8_t MAX_SEGMENTS_PER_SCHEDULE = 5;
}

// ======================================================
// ENUM 정의
// ======================================================
typedef enum : uint8_t {
	EN_A10_WIFI_MODE_AP     = 0,
	EN_A10_WIFI_MODE_STA    = 1,
	EN_A10_WIFI_MODE_AP_STA = 2
} EN_A10_WIFI_MODE_t;

// ======================================================
// 구조체 정의
// ======================================================

// ------------------------------------------------------
// SYSTEM 설정 (cfg_system_022.json)
// ------------------------------------------------------
typedef struct {
	struct {
		char version[A10_Const::LEN_NAME];
		char device_name[A10_Const::LEN_NAME];
		char last_update[A10_Const::LEN_NAME];
	} meta;

	struct {
		struct {
			char html[A10_Const::LEN_PATH];
			char css[A10_Const::LEN_PATH];
			char js[A10_Const::LEN_PATH];
		} web;
		struct {
			char     level[A10_Const::LEN_LEVEL];
			uint16_t max_entries;
		} logging;
	} system;

	struct {
		struct { int16_t pin; uint8_t channel; uint32_t freq; uint8_t res; } fan_pwm;
		struct { bool enabled; int16_t pin; uint16_t debounce_sec; } pir;
		struct { bool enabled; char type[16]; int16_t pin; uint16_t interval_sec; } tempHum;
		struct { bool enabled; uint16_t scan_interval; } ble;
	} hw;

	struct { char api_key[64]; } security;
	struct { char ntp_server[64]; char timezone[32]; uint16_t sync_interval_min; } time;
} ST_A10_SystemConfig;

// ------------------------------------------------------
// WIFI 설정 (cfg_wifi_022.json)
// ------------------------------------------------------
typedef struct {
	EN_A10_WIFI_MODE_t wifiMode;
	char wifiModeDesc[48];
	struct { char ssid[A10_Const::LEN_SSID]; char password[A10_Const::LEN_PASS]; } ap;
	struct { char ssid[A10_Const::LEN_SSID]; char pass[A10_Const::LEN_PASS]; } sta[A10_Const::MAX_STA_NETWORKS];
	uint8_t sta_count;
} ST_A10_WifiConfig;

// ------------------------------------------------------
// MOTION 설정 (cfg_motion_022.json)
//  - JSON 스키마(최종 확정)
//    motion.pir.enabled, motion.pir.hold_sec
//    motion.ble.enabled, motion.ble.trusted_devices[], motion.ble.rssi{on,off,avg_count,persist_count,exit_delay_sec}
// ------------------------------------------------------
typedef struct {
	char  alias[A10_Const::LEN_ALIAS];  // "MyPhone"
	char  name[A10_Const::LEN_NAME];    // "iPhone15" 등
	char  mac[20];                      // "AA:BB:CC:11:22:33" 또는 ""(랜덤화시)
	char  manuf_prefix[9];              // "4C0002" (최대 8 chars + NUL)
	uint8_t prefix_len;                 // 바이트 단위(예: 3 → "4C0002" 3바이트)
	bool  enabled;                      // 디바이스 화이트리스트 on/off
} ST_A10_BLETrustedDevice;

typedef struct {
	bool enabled;            // 전체 motion 기능 마스터 스위치(옵션)
	struct {                 // motion.pir
		bool     enabled;
		uint16_t hold_sec;
	} pir;
	struct {                 // motion.ble
		bool enabled;
		// motion.ble.trusted_devices[]
		ST_A10_BLETrustedDevice trusted_devices[A10_Const::MAX_BLE_DEVICES];
		uint8_t trusted_count;
		// motion.ble.rssi { on/off/avg_count/persist_count/exit_delay_sec }
		struct {
			int16_t  on;              // RSSI ON Threshold
			int16_t  off;             // RSSI OFF Threshold
			uint8_t  avg_count;       // 이동 평균 샘플 수
			uint8_t  persist_count;   // 상태 전이 연속 조건
			uint16_t exit_delay_sec;  // Presence→Idle 지연
		} rssi;
	} ble;
} ST_A10_MotionConfig;

// ------------------------------------------------------
// CONTROL 설정 (cfg_control_022.json)
// ------------------------------------------------------

// 세그먼트 단위
typedef struct {
	uint16_t segNo;
	uint16_t on_minutes;
	uint16_t off_minutes;
	char     mode[16];                                   // "preset" or "fixed"
	char     preset_name[A10_Const::LEN_PRESET];
	struct { int32_t intensity; int32_t variability; bool valid; } preset_adjust; // (+/-) 보정치, valid 플래그
	float    fixed_speed;                                // 0~100%
} ST_A10_ScheduleSegment;

// 스케줄 단위
typedef struct {
	uint16_t schNo;
	char     schName[A10_Const::LEN_NAME];
	bool     enabled;
	uint8_t  days[7];                                    // 0/1 (일~토)
	char     start_time[A10_Const::LEN_TIME];            // "HH:MM"
	char     end_time[A10_Const::LEN_TIME];              // "HH:MM"
	uint8_t  seg_count;
	ST_A10_ScheduleSegment segments[A10_Const::MAX_SEGMENTS_PER_SCHEDULE];
	struct {
		struct { bool enabled; uint16_t hold_sec; }             pir;
		struct { bool enabled; int16_t rssi_threshold; uint16_t hold_sec; } ble;
	} motion;
} ST_A10_ScheduleItem;

// 메인 Control 설정
typedef struct {
	uint8_t runMode;                // 0=Continuous / 1=Schedule
	char    runModeDesc[64];

	struct {
		struct {
			bool  enabled;
			char  preset[A10_Const::LEN_PRESET];
			float wind_intensity;
			float gust_frequency;
			float wind_variability;
			float fan_limit;
			float min_fan;
			float turbulence_length_scale;
			float turbulence_intensity_sigma;
			float thermal_bubble_strength;
			float thermal_bubble_radius;
		} wind;
		struct {
			struct { bool enabled; uint16_t hold_sec; }                 pir;
			struct { bool enabled; int16_t rssi_threshold; uint16_t hold_sec; } ble;
		} motion;
	} Continuous;

	ST_A10_ScheduleItem schedules[A10_Const::MAX_SCHEDULES];
	uint8_t             schedule_count;
} ST_A10_ControlConfig;

// ------------------------------------------------------
// 루트 구성
// ------------------------------------------------------
typedef struct {
	ST_A10_SystemConfig  system;    // 항상 로드
	ST_A10_WifiConfig*   wifi;      // Lazy-load
	ST_A10_MotionConfig* motion;    // Lazy-load
	ST_A10_ControlConfig* control;  // Lazy-load
} ST_A10_ConfigRoot;

inline ST_A10_ConfigRoot g_A10_config_root;

// ======================================================
// 유틸 함수
// ======================================================
inline float A10_rand01() {
	return static_cast<float>(esp_random()) / static_cast<float>(UINT32_MAX);
}
inline float A10_randRange(float a, float b) {
	return a + A10_rand01() * (b - a);
}

// ======================================================
// 기본값 초기화 함수
// ======================================================
inline void A10_resetSystemDefault(ST_A10_SystemConfig& s) {
	memset(&s, 0, sizeof(s));
	strlcpy(s.meta.version, A10_Const::FW_VERSION, sizeof(s.meta.version));
	strlcpy(s.meta.device_name, "WindScape_XY-SK10", sizeof(s.meta.device_name));

	strlcpy(s.system.web.html, "/html/SC10_main_021.html", sizeof(s.system.web.html));
	strlcpy(s.system.web.css,  "/html/SC10_main_021.css",  sizeof(s.system.web.css));
	strlcpy(s.system.web.js,   "/html/SC10_main_021.js",   sizeof(s.system.web.js));
	strlcpy(s.system.logging.level, "INFO", sizeof(s.system.logging.level));
	s.system.logging.max_entries = 300;

	s.hw.fan_pwm.pin = 6; s.hw.fan_pwm.channel = 0; s.hw.fan_pwm.freq = 25000; s.hw.fan_pwm.res = 10;
	s.hw.pir.enabled = true; s.hw.pir.pin = 13; s.hw.pir.debounce_sec = 5;
	s.hw.tempHum.enabled = true; strlcpy(s.hw.tempHum.type, "DHT22", sizeof(s.hw.tempHum.type));
	s.hw.tempHum.pin = 23; s.hw.tempHum.interval_sec = 30;
	s.hw.ble.enabled = true; s.hw.ble.scan_interval = 5;

	strlcpy(s.security.api_key, "my_api_key_12345", sizeof(s.security.api_key));
	strlcpy(s.time.ntp_server, "pool.ntp.org", sizeof(s.time.ntp_server));
	strlcpy(s.time.timezone, "Asia/Seoul", sizeof(s.time.timezone));
	s.time.sync_interval_min = 60;
}

inline void A10_resetWifiDefault(ST_A10_WifiConfig& w) {
	memset(&w, 0, sizeof(w));
	w.wifiMode = EN_A10_WIFI_MODE_AP_STA;
	strlcpy(w.wifiModeDesc, "0:AP, 1:STA, 2: AP+STA", sizeof(w.wifiModeDesc));
	strlcpy(w.ap.ssid, "NatureWind", sizeof(w.ap.ssid));
	strlcpy(w.ap.password, "2540", sizeof(w.ap.password));
	w.sta_count = 0;
}

inline void A10_resetMotionDefault(ST_A10_MotionConfig& m) {
	memset(&m, 0, sizeof(m));
	m.enabled       = true;

	// PIR
	m.pir.enabled   = true;
	m.pir.hold_sec  = 120;

	// BLE
	m.ble.enabled   = true;
	m.ble.trusted_count = 0;
	// RSSI 디폴트(스펙의 기본값과 일치)
	m.ble.rssi.on            = -65;
	m.ble.rssi.off           = -75;
	m.ble.rssi.avg_count     = 8;
	m.ble.rssi.persist_count = 5;
	m.ble.rssi.exit_delay_sec= 12;

	// 샘플 트러스트 템플릿(미사용: 빈 상태 유지)
	// 사용 시:
	// auto& d = m.ble.trusted_devices[m.ble.trusted_count++];
	// strlcpy(d.alias, "MyPhone", sizeof(d.alias));
	// strlcpy(d.name,  "iPhone15", sizeof(d.name));
	// strlcpy(d.mac,   "AA:BB:CC:11:22:33", sizeof(d.mac));
	// strlcpy(d.manuf_prefix, "4C0002", sizeof(d.manuf_prefix));
	// d.prefix_len = 3; d.enabled = true;
}

inline void A10_resetControlDefault(ST_A10_ControlConfig& c) {
	memset(&c, 0, sizeof(c));
	c.runMode = 0;
	strlcpy(c.runModeDesc, "0=Continuous Mode, 1=Schedule Mode", sizeof(c.runModeDesc));

	c.Continuous.wind.enabled = true;
	strlcpy(c.Continuous.wind.preset, "COUNTRY_BREEZE", sizeof(c.Continuous.wind.preset));
	c.Continuous.wind.wind_intensity = 70.0f;
	c.Continuous.wind.gust_frequency = 45.0f;
	c.Continuous.wind.wind_variability = 50.0f;
	c.Continuous.wind.fan_limit = 90.0f;
	c.Continuous.wind.min_fan   = 10.0f;
	c.Continuous.wind.turbulence_length_scale   = 40.0f;
	c.Continuous.wind.turbulence_intensity_sigma = 0.5f;
	c.Continuous.wind.thermal_bubble_strength   = 2.0f;
	c.Continuous.wind.thermal_bubble_radius     = 18.0f;

	c.Continuous.motion.pir.enabled = true;  c.Continuous.motion.pir.hold_sec = 120;
	c.Continuous.motion.ble.enabled = true;  c.Continuous.motion.ble.rssi_threshold = -70; c.Continuous.motion.ble.hold_sec = 120;

	c.schedule_count = 0;
}

// 루트 디폴트(포인터 lazy-load 초기화)
inline void A10_resetToDefault(ST_A10_ConfigRoot& root) {
	A10_resetSystemDefault(root.system);
	root.wifi    = nullptr;
	root.motion  = nullptr;
	root.control = nullptr;
}
