#pragma once
/*
 * A10_Const_007.h
 * - WindScape 공통 상수/타입/기본 구조 정의
 * - 네이밍 규칙:
 *    - 전역 상수/매크로: G_A10_ 접두사
 *    - 전역 변수: g_A10_ 접두사
 *    - 로컬 변수 : v_ 접두사
 *    - 함수 인자 : p_ 접두사
 *    - type은 T_A10_ 접두사
 *    - enum 상수 : EN_A10_ 접두사
 *    - 구조체 : ST_A10_ 접두사
 *    - 클래스 : CL_A10_ 접두사
 *    - 클래스 private 멤버: _ 접두사, 정적 멤버: s_
 *    - 전역함수 : A10_ 접두사
 */

#include <Arduino.h>
#include <ArduinoJson.h>

namespace A10_Const {
    constexpr char FW_VERSION[]                 = "SC10_FW_1.0.7";
    constexpr char CONFIG_JSON_FILE[]           = "/json/config_014.json";
    constexpr char CONFIG_JSON_FILE_BACKUP[]    = "/json/config_014.json.bak";

    constexpr int  MAX_STA_NETWORKS             = 5;
    constexpr int  WIFI_SSID_LEN                = 32;
    constexpr int  WIFI_PWD_LEN                 = 64;
    constexpr int  API_KEY_MAX_LEN              = 64;

    // 기본 Web 파일(초기 부팅시 파일이 없을 경우 사용될 safe default)
    constexpr char DEF_HTML_FILE[]              = "/html/SC10_main_016.html";
    constexpr char DEF_HTML_URI[]               = "/main.html";
    constexpr char DEF_HTML_MIME[]              = "text/html";

    constexpr char DEF_JS_FILE[]                = "/html/SC10_main_016.js";
    constexpr char DEF_JS_URI[]                 = "/SC10_main_016.js";
    constexpr char DEF_JS_MIME[]                = "application/javascript";

    constexpr char DEF_CSS_FILE[]               = "/html/SC10_main_016.css";
    constexpr char DEF_CSS_URI[]                = "/SC10_main_016.css";
    constexpr char DEF_CSS_MIME[]               = "text/css";
} // namespace A10_Const

// ======================================================
// Wi-Fi 모드 (3가지)
// ======================================================
typedef enum {
    EN_A10_WIFI_MODE_AP      = 0,   // AP only
    EN_A10_WIFI_MODE_STA     = 1,   // STA only
    EN_A10_WIFI_MODE_AP_STA  = 2    // AP + STA 동시
} T_A10_WifiMode_t;

// ======================================================
// 프리셋 (Key는 ASCII만 사용; 한글 문제 회피)
// ======================================================
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

// 프리셋 표기 문자열(ASCII Key)
static const char* g_A10_PRESET_MODE_KEYS_Arr[] = {
    "OFF","COUNTRY","MEDITERRANEAN","OCEAN","MOUNTAIN","PLAINS",
    "HARBOR_BREEZE","FOREST_CANOPY","URBAN_SUNSET","TROPICAL_RAIN","DESERT_NIGHT"
};

// 옵션: UI용 라벨(원한다면 한글 혼용 가능, 백엔드 비교는 KEYS 사용)
static const char* g_A10_PRESET_MODE_LABELS_Arr[] = {
    "Off","Countryside-들판","Mediterranean-지중해","Ocean-해양성","Mountain-산악","Plains-대평원",
    "Harbor Breeze-항구","Forest Canopy-숲그늘","Urban Sunset-도시석양","Tropical Rain-열대소나기","Desert Night-사막밤"
};

// ======================================================
// 구조체
// ======================================================
typedef struct {
    char file[96];
    char uri[48];
    char mime[32];
} ST_A10_WebFile;

typedef struct {
    ST_A10_WebFile html_file;
    ST_A10_WebFile js_file;
    ST_A10_WebFile css_file;
} ST_A10_WebConfig;

typedef struct {
    char ssid[A10_Const::WIFI_SSID_LEN];
    char pass[A10_Const::WIFI_PWD_LEN];
} ST_A10_StaNetwork;

typedef struct {
    char ap_ssid[A10_Const::WIFI_SSID_LEN];
    char ap_password[A10_Const::WIFI_PWD_LEN];
} ST_A10_APNetwork;

typedef struct {
    uint8_t             wifi_mode;     // EN_A10_WIFI_MODE_*
    ST_A10_APNetwork    ap_network;
    uint8_t             sta_count;
    ST_A10_StaNetwork   sta_networks[A10_Const::MAX_STA_NETWORKS];
} ST_A10_WifiConfig;

typedef struct {
    int16_t   fan_pwm_pin;
    uint8_t   pwm_channel;
    uint8_t   pwm_resolution;
    uint32_t  pwm_frequency;
} ST_A10_HW;

typedef struct {
    uint16_t  sim_int;
    uint16_t  gust_int;
    uint16_t  thermal_int;
} ST_A10_Timing;

typedef struct {
    // 값 범위: 프론트에서 %단위 관리, 백엔드 내부에서는 0~1 변환하거나 있는 그대로 사용
    float     intensity;
    float     gust_freq;
    float     variability;
    float     fan_limit;
    float     min_fan;
    float     turb_len;
    float     turb_sig;
    float     therm_str;
    float     therm_rad;
    uint8_t   preset_index; // EN_A10_PRESET_*
} ST_A10_Sim;

typedef struct {
    char               api_key[A10_Const::API_KEY_MAX_LEN + 1];
    ST_A10_WebConfig   web;
    ST_A10_WifiConfig  wifi;
    ST_A10_HW          hw;
    ST_A10_Timing      timing;
    ST_A10_Sim         sim;
} ST_A10_WindConfig;

// 전역 설정 인스턴스 (헤더온리: inline)
inline ST_A10_WindConfig g_A10_config;

// 난수 유틸
inline float A10_getRandom01() {
    return (float)esp_random() / (float)UINT32_MAX;
}
inline float A10_randRange(float p_a, float p_b) {
    return p_a + A10_getRandom01() * (p_b - p_a);
}
