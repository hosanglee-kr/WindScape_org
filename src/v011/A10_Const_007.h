#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : A10_Const_007.h
 * 모듈명 : WindScape Const Manager
 * ------------------------------------------------------
 * 기능 요약:
 *  - WindScape 공통 상수/타입/기본 구조 정의
 *  - Wi-Fi/AP/STA/WEB 파일 경로/시뮬/HW/타이밍 등
 * ------------------------------------------------------
 * - 코드 네이밍 규칙:
 *    - 모듈약어 : A10
 *    - 전역 상수/매크로: G_모듈약어_ 접두사
 *    - 전역 변수: g_모듈약어_ 접두사
 *    - 로컬 변수 : v_ 접두사
 *    - 함수 인자 : p_ 접두사
 *    - type은 T_모듈약어_ 접두사
 *    - enum 상수 : EN_모듈약어_ 접두사
 *    - 구조체 : ST_모듈약어_ 접두사
 *    - 클래스 : CL_모듈약어_ 접두사
 *    - 클래스 private 멤버: _ 접두사, 정적 멤버: s_
 *    - 전역함수 : 모듈약어_ 접두사
 */

#include <Arduino.h>
#include <ArduinoJson.h>

//#define G_A10_DYNIM_WEB_STATIC_FILE_USE  0

namespace A10_Const {
    constexpr char FW_VERSION[] = "SC10_FW_1.0.0";

    // 설정 파일
    constexpr char CONFIG_JSON_FILE[]         = "/json/config_014.json";
    constexpr char CONFIG_JSON_FILE_BACKUP[]  = "/json/config_014.json.bak";

        // 기본 WEB 파일 (설정값 없거나 오류 시 Fallback)
        constexpr char DEF_HTML_FILE[] = "/html/SC10_main_016.html";
        constexpr char DEF_HTML_URI[]  = "/main.html";
        constexpr char DEF_HTML_MIME[] = "text/html";

        constexpr char DEF_CSS_FILE[]  = "/html/SC10_main_016.css";
        constexpr char DEF_CSS_URI[]   = "/SC10_main_016.css";
        constexpr char DEF_CSS_MIME[]  = "text/css";

        constexpr char DEF_JS_FILE[]   = "/html/SC10_main_016.js";
        constexpr char DEF_JS_URI[]    = "/SC10_main_016.js";
        constexpr char DEF_JS_MIME[]   = "application/javascript";



    // STA 저장 최대 개수
    constexpr int  MAX_STA_NETWORKS = 5;
}

// Wi-Fi 모드 (0:AP, 1:STA, 2:AP+STA)
typedef enum {
    EN_A10_WIFI_MODE_AP      = 0,
    EN_A10_WIFI_MODE_STA     = 1,
    EN_A10_WIFI_MODE_AP_STA  = 2
} T_A10_WifiMode_t;

// 풍속 Phase
typedef enum {
    EN_A10_WEATHER_PHASE_CALM   = 0,
    EN_A10_WEATHER_PHASE_NORMAL = 1,
    EN_A10_WEATHER_PHASE_STRONG = 2,
    EN_A10_WEATHER_PHASE_COUNT
} T_A10_WindWeatherPhase_t;

static const char* g_A10_WEATHER_PHASE_NAMES_Arr[] = {
    "Calm-잔잔한", "Normal-보통", "Strong-강한"
};

// 프리셋
typedef enum {
    EN_A10_PRESET_OFF              = 0,
    EN_A10_PRESET_COUNTRY          = 1,
    EN_A10_PRESET_MEDITERRANEAN    = 2,
    EN_A10_PRESET_OCEAN            = 3,
    EN_A10_PRESET_MOUNTAIN         = 4,
    EN_A10_PRESET_PLAINS           = 5,
    EN_A10_PRESET_HARBOR_BREEZE    = 6,
    EN_A10_PRESET_FOREST_CANOPY    = 7,
    EN_A10_PRESET_URBAN_SUNSET     = 8,
    EN_A10_PRESET_TROPICAL_RAIN    = 9,
    EN_A10_PRESET_DESERT_NIGHT     = 10,
    EN_A10_PRESET_COUNT
} T_A10_PresetMode_t;

static const char* g_A10_PRESET_MODE_NAMES_Arr[] = {
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
    "DESERT_NIGHT"
};

// 길이 상수
#define G_A10_API_KEY_MAX_LEN   64
#define G_A10_WIFI_SSID_LEN     32
#define G_A10_WIFI_PWD_LEN      64
#define G_A10_WEB_PATH_LEN      96
#define G_A10_WEB_URI_LEN       48
#define G_A10_WEB_MIME_LEN      32

// Wi-Fi STA
typedef struct {
    char ssid[G_A10_WIFI_SSID_LEN];
    char password[G_A10_WIFI_PWD_LEN];
} ST_A10_StaCredential;

#ifdef G_A10_DYNIM_WEB_STATIC_FILE_USE
    // WEB 파일 3종
    typedef struct {
        char file[G_A10_WEB_PATH_LEN];
        char uri[G_A10_WEB_URI_LEN];
        char mime[G_A10_WEB_MIME_LEN];
    } ST_A10_WebFile;

    typedef struct {
        ST_A10_WebFile html_file;
        ST_A10_WebFile js_file;
        ST_A10_WebFile css_file;
    } ST_A10_WebConfig;
#endif

// 메인 설정 구조체 (packed로 정렬 최소화)
typedef struct __attribute__((packed)) {
    // [security]
    char api_key[G_A10_API_KEY_MAX_LEN + 1];

    #ifdef G_A10_DYNIM_WEB_STATIC_FILE_USE
        // [web]
        ST_A10_WebConfig web;
    #endif

    // [sim]
    float wind_intensity;
    float gust_frequency;
    float wind_variability;
    float fan_speed_limit;
    float minimum_fan_speed;
    float turbulence_length_scale;
    float turbulence_intensity_sigma;
    float thermal_bubble_strength;
    float thermal_bubble_radius;

    // [hw]
    int16_t   fan_pwm_pin;
    uint8_t   pwm_channel;
    uint8_t   pwm_resolution;
    uint32_t  pwm_frequency;

    // [timing]
    uint16_t wind_sim_interval_ms;
    uint16_t gust_check_interval_ms;
    uint16_t thermal_check_interval_ms;
    uint8_t  preset_mode_index;

    // [wifi]
    uint8_t wifi_mode; // EN_A10_WIFI_MODE_*
    char ap_ssid[G_A10_WIFI_SSID_LEN];
    char ap_password[G_A10_WIFI_PWD_LEN];

    uint8_t              sta_network_count;
    ST_A10_StaCredential sta_networks[A10_Const::MAX_STA_NETWORKS];
} ST_A10_WindConfig;

// 전역 설정 인스턴스
inline ST_A10_WindConfig g_A10_config;

// 유틸
inline float A10_getRandom01() { return (float)esp_random() / (float)UINT32_MAX; }
inline float A10_randRange(float p_a, float p_b) { return p_a + A10_getRandom01() * (p_b - p_a); }
