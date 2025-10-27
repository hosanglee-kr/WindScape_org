#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : A10_Const_011.h
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
 * ------------------------------------------------------
 * [코드 네이밍 규칙]
 *      - 현재 파일 모듈약어    : A10
 *      - 전역 상수,매크로      : G_모듈약어_ 접두사
 *      - 전역 변수             : g_모듈약어_ 접두사
 *      - 전역 함수             : 모듈약어_ 접두사
 *      - type                  : T_모듈약어_ 접두사
 *      - enum 상수             : EN_모듈약어_ 접두사
 *      - 구조체                : ST_모듈약어_ 접두사
 *      - 클래스명              : CL_모듈약어_ 접두사
 *      - 클래스 private 멤버   : _ 접두사,
 *      - 클래스 정적 멤버      : s_ 접두사
 *      - 로컬 변수             : v_ 접두사
 *      - 함수 인자             : p_ 접두사
 */

#include <Arduino.h>
#include <ArduinoJson.h>
#include <string.h>

namespace A10_Const {
    constexpr char FW_VERSION[] = "SC10_FW_1.0.0";

    #define G_A10_CFG_JSON_FILE_VER "022"

    // JSON 파일(분리 저장) 경로 (ENV 제외)
    constexpr char CFG_SYSTEM_FILE[]      = "/json/cfg_system_"      G_A10_CFG_JSON_FILE_VER ".json";
    constexpr char CFG_WIFI_FILE[]      = "/json/cfg_wifi_"      G_A10_CFG_JSON_FILE_VER ".json";
    constexpr char CFG_CONTROL_FILE[]      = "/json/cfg_control_"      G_A10_CFG_JSON_FILE_VER ".json";
  
    // constexpr char CFG_SIM_FILE[]       = "/json/cfg_sim_"       G_A10_CFG_JSON_FILE_VER ".json";
    //constexpr char CFG_SCHEDULE_FILE[]  = "/json/cfg_schedule_"  G_A10_CFG_JSON_FILE_VER ".json";
    constexpr char CFG_MOTION_FILE[]    = "/json/cfg_motion_"    G_A10_CFG_JSON_FILE_VER ".json";

    // 각 파일의 백업 경로 (.bak)
    constexpr char CFG_CORE_FILE_BAK[]      = "/json/cfg_core_"      G_A10_CFG_JSON_FILE_VER ".json.bak";
    constexpr char CFG_WIFI_FILE_BAK[]      = "/json/cfg_wifi_"      G_A10_CFG_JSON_FILE_VER ".json.bak";
    constexpr char CFG_CONTROL_FILE_BAK[]   = "/json/cfg_control_"      G_A10_CFG_JSON_FILE_VER ".json.bak";
  
    //constexpr char CFG_SIM_FILE_BAK[]       = "/json/cfg_sim_"       G_A10_CFG_JSON_FILE_VER ".json.bak";
    //constexpr char CFG_SCHEDULE_FILE_BAK[]  = "/json/cfg_schedule_"  G_A10_CFG_JSON_FILE_VER ".json.bak";
    constexpr char CFG_MOTION_FILE_BAK[]    = "/json/cfg_motion_"    G_A10_CFG_JSON_FILE_VER ".json.bak";


    // BLE 디바이스 최대 개수
    constexpr uint8_t MAX_BLE_DEVICES = 8;
    // STA 네트워크 최대 개수
    constexpr uint8_t MAX_STA_NETWORKS = 5;
    // 스케줄 및 세그먼트 최대 개수
    constexpr uint8_t MAX_SCHEDULES = 5;
    constexpr uint8_t MAX_SEGMENTS_PER_SCHEDULE = 5;
}


// ======================================================
// ENUM 정의
// ======================================================

// Wi-Fi 모드
typedef enum : uint8_t {
    EN_A10_WIFI_MODE_AP = 0,
    EN_A10_WIFI_MODE_STA = 1,
    EN_A10_WIFI_MODE_AP_STA = 2
} EN_A10_WIFI_MODE_t;

// ======================================================
// 구조체 정의
// ======================================================

// ------------------------------------------------------
// SYSTEM 설정 (core)
// ------------------------------------------------------
typedef struct {
    struct {
        char version[32];
        char device_name[64];
        char last_update[64];
    } meta;

    struct {
        struct {
            char html[64];
            char css[64];
            char js[64];
        } web;

        struct {
            char level[16];
            uint16_t max_entries;
        } logging;
    } system;

    struct {
        struct {
            int16_t pin;
            uint8_t channel;
            uint32_t freq;
            uint8_t res;
        } fan_pwm;

        struct {
            struct { bool enabled; int16_t pin; uint16_t debounce_sec; } pir;
            struct { bool enabled; char type[16]; int16_t pin; uint16_t interval_sec; } tempHum;
            struct { bool enabled; uint16_t scan_interval; } ble;
        } sensors;
    } hw;

    struct { char api_key[64]; } security;

    struct {
        char ntp_server[64];
        char timezone[32];
        uint16_t sync_interval_min;
    } time;
} ST_A10_CoreConfig;

// ------------------------------------------------------
// WIFI 설정
// ------------------------------------------------------
typedef struct {
    EN_A10_WIFI_MODE_t mode;
    struct {
        char ssid[32];
        char password[32];
    } ap;
    struct {
        char ssid[32];
        char pass[32];
    } sta[A10_Const::MAX_STA_NETWORKS];
    uint8_t sta_count;
} ST_A10_WifiConfig;

// ------------------------------------------------------
// MOTION 설정
// ------------------------------------------------------
typedef struct {
    bool enabled;
    struct { bool enabled; uint16_t hold_sec; } pir;
    struct {
        bool enabled;
        int16_t rssi_threshold;
        uint16_t hold_sec;
        struct {
            char mac[20];
            char alias[32];
            bool enabled;
        } devices[A10_Const::MAX_BLE_DEVICES];
        uint8_t device_count;
    } ble;
} ST_A10_MotionConfig;

// ------------------------------------------------------
// CONTROL 설정
// ------------------------------------------------------

// 세그먼트 단위
typedef struct {
    uint16_t segNo;
    uint16_t on_minutes;
    uint16_t off_minutes;
    char mode[16];            // "preset" or "fixed"
    char preset_name[32];
    struct { float intensity; float variability; } preset_adjust;
    float fixed_speed;
} ST_A10_ScheduleSegment;

// 스케줄 단위
typedef struct {
    uint16_t schNo;
    char schName[32];
    bool enabled;
    uint8_t days[7];
    char start_time[6];
    char end_time[6];
    uint8_t seg_count;
    ST_A10_ScheduleSegment segments[A10_Const::MAX_SEGMENTS_PER_SCHEDULE];
    struct {
        struct { bool enabled; uint16_t hold_sec; } pir;
        struct { bool enabled; int16_t rssi_threshold; uint16_t hold_sec; } ble;
    } motion;
} ST_A10_ScheduleItem;

// 메인 Control 설정
typedef struct {
    uint8_t runMode;                // 0=Continuous / 1=Schedule
    char runModeDesc[64];

    struct {
        struct {
            bool enabled;
            char preset[32];
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
            struct { bool enabled; uint16_t hold_sec; } pir;
            struct { bool enabled; int16_t rssi_threshold; uint16_t hold_sec; } ble;
        } motion;
    } continuous;

    ST_A10_ScheduleItem schedules[A10_Const::MAX_SCHEDULES];
    uint8_t schedule_count;
} ST_A10_ControlConfig;

// ------------------------------------------------------
// 루트 구성
// ------------------------------------------------------
typedef struct {
    ST_A10_CoreConfig core;
    ST_A10_WifiConfig* wifi;
    ST_A10_MotionConfig* motion;
    ST_A10_ControlConfig* control;
} ST_A10_ConfigRoot;

inline ST_A10_ConfigRoot g_A10_config_root;

// ======================================================
// 유틸 함수
// ======================================================
inline float A10_rand01() { return static_cast<float>(esp_random()) / static_cast<float>(UINT32_MAX); }
inline float A10_randRange(float p_a, float p_b) { return p_a + A10_rand01() * (p_b - p_a); }

// ======================================================
// 기본값 초기화 함수
// ======================================================
inline void A10_resetCoreDefault(ST_A10_CoreConfig& c) {
    memset(&c, 0, sizeof(c));
    strlcpy(c.meta.version, A10_Const::FW_VERSION, sizeof(c.meta.version));
    strlcpy(c.meta.device_name, "WindScape_XY-SK10", sizeof(c.meta.device_name));

    strlcpy(c.system.web.html, "/html/SC10_main_021.html", sizeof(c.system.web.html));
    strlcpy(c.system.web.css,  "/html/SC10_main_021.css",  sizeof(c.system.web.css));
    strlcpy(c.system.web.js,   "/html/SC10_main_021.js",   sizeof(c.system.web.js));
    strlcpy(c.system.logging.level, "INFO", sizeof(c.system.logging.level));
    c.system.logging.max_entries = 300;

    c.hw.fan_pwm.pin = 6; c.hw.fan_pwm.channel = 0; c.hw.fan_pwm.freq = 25000; c.hw.fan_pwm.res = 10;
    c.hw.sensors.pir.enabled = true; c.hw.sensors.pir.pin = 13; c.hw.sensors.pir.debounce_sec = 5;
    c.hw.sensors.tempHum.enabled = true;
    strlcpy(c.hw.sensors.tempHum.type, "DHT22", sizeof(c.hw.sensors.tempHum.type));
    c.hw.sensors.tempHum.pin = 23; c.hw.sensors.tempHum.interval_sec = 30;
    c.hw.sensors.ble.enabled = true; c.hw.sensors.ble.scan_interval = 5;

    strlcpy(c.security.api_key, "my_api_key_12345", sizeof(c.security.api_key));
    strlcpy(c.time.ntp_server, "pool.ntp.org", sizeof(c.time.ntp_server));
    strlcpy(c.time.timezone, "Asia/Seoul", sizeof(c.time.timezone));
    c.time.sync_interval_min = 60;
}

inline void A10_resetWifiDefault(ST_A10_WifiConfig& w) {
    memset(&w, 0, sizeof(w));
    w.mode = EN_A10_WIFI_MODE_AP_STA;
    strlcpy(w.ap.ssid, "NatureWind", sizeof(w.ap.ssid));
    strlcpy(w.ap.password, "2540", sizeof(w.ap.password));
    w.sta_count = 0;
}

inline void A10_resetMotionDefault(ST_A10_MotionConfig& m) {
    memset(&m, 0, sizeof(m));
    m.enabled = true;
    m.pir.enabled = true; m.pir.hold_sec = 120;
    m.ble.enabled = true; m.ble.rssi_threshold = -70; m.ble.hold_sec = 120;
    m.ble.device_count = 0;
}

inline void A10_resetControlDefault(ST_A10_ControlConfig& c) {
    memset(&c, 0, sizeof(c));
    c.runMode = 0;
    strlcpy(c.runModeDesc, "0=Continuous Mode, 1=Schedule Mode", sizeof(c.runModeDesc));

    // Continuous.wind
    c.continuous.wind.enabled = true;
    strlcpy(c.continuous.wind.preset, "COUNTRY_BREEZE", sizeof(c.continuous.wind.preset));
    c.continuous.wind.wind_intensity = 70.0f;
    c.continuous.wind.gust_frequency = 45.0f;
    c.continuous.wind.wind_variability = 50.0f;
    c.continuous.wind.fan_limit = 90.0f;
    c.continuous.wind.min_fan = 10.0f;
    c.continuous.wind.turbulence_length_scale = 40.0f;
    c.continuous.wind.turbulence_intensity_sigma = 0.5f;
    c.continuous.wind.thermal_bubble_strength = 2.0f;
    c.continuous.wind.thermal_bubble_radius = 18.0f;

    // Continuous.motion
    c.continuous.motion.pir.enabled = true; c.continuous.motion.pir.hold_sec = 120;
    c.continuous.motion.ble.enabled = true; c.continuous.motion.ble.rssi_threshold = -70; c.continuous.motion.ble.hold_sec = 120;

    c.schedule_count = 0;
}

inline void A10_resetToDefault(ST_A10_ConfigRoot& root) {
    A10_resetCoreDefault(root.core);
    root.wifi = nullptr;
    root.motion = nullptr;
    root.control = nullptr;
}
