#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : A10_Const_010.h
 * 모듈명 : Smart Nature Wind Const Manager
 * ------------------------------------------------------
 * 기능 요약:
 *  - Smart Nature Wind 공통 전역 상수/타입/구조체/유틸 정의
 *  - JSON 분리 기준(코어/와이파이/시뮬/스케줄/모션) 반영
 *  - 메모리 효율 위한 "하이브리드 구성" 지원:
 *      * Core(+HW), Motion은 런타임 상시 필요 시 상주시/포인터 선택
 *      * WiFi/Sim/Schedule은 Lazy-Load 포인터 권장
 *  - ENV(JSON) 제외(구현 범위 밖)
 * ------------------------------------------------------
 * [구현 시 반영사항]
 *  - 항상 소스 시작 주석 체께 유지 및 내용 update
 *  - arduinojson v7.x.x 버전 사용 
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

// ------------------------------------------------------
// JSON 파일 버전 접미사 (cfg_*_021.json 생성용)
// ------------------------------------------------------
#define G_A10_CFG_JSON_FILE_VER "021"

namespace A10_Const {
    // 펌웨어/버전
    constexpr char FW_VERSION[] = "SC10_FW_1.0.0";

    // JSON 파일(분리 저장) 경로 (ENV 제외)
    constexpr char CFG_CORE_FILE[]      = "/json/cfg_core_"      G_A10_CFG_JSON_FILE_VER ".json";
    constexpr char CFG_WIFI_FILE[]      = "/json/cfg_wifi_"      G_A10_CFG_JSON_FILE_VER ".json";
    constexpr char CFG_SIM_FILE[]       = "/json/cfg_sim_"       G_A10_CFG_JSON_FILE_VER ".json";
    constexpr char CFG_SCHEDULE_FILE[]  = "/json/cfg_schedule_"  G_A10_CFG_JSON_FILE_VER ".json";
    constexpr char CFG_MOTION_FILE[]    = "/json/cfg_motion_"    G_A10_CFG_JSON_FILE_VER ".json";

    // 각 파일의 백업 경로 (.bak)
    constexpr char CFG_CORE_FILE_BAK[]      = "/json/cfg_core_"      G_A10_CFG_JSON_FILE_VER ".json.bak";
    constexpr char CFG_WIFI_FILE_BAK[]      = "/json/cfg_wifi_"      G_A10_CFG_JSON_FILE_VER ".json.bak";
    constexpr char CFG_SIM_FILE_BAK[]       = "/json/cfg_sim_"       G_A10_CFG_JSON_FILE_VER ".json.bak";
    constexpr char CFG_SCHEDULE_FILE_BAK[]  = "/json/cfg_schedule_"  G_A10_CFG_JSON_FILE_VER ".json.bak";
    constexpr char CFG_MOTION_FILE_BAK[]    = "/json/cfg_motion_"    G_A10_CFG_JSON_FILE_VER ".json.bak";

    // 사이즈/개수 제한
    constexpr uint8_t  MAX_STA_NETWORKS             = 5;
    constexpr uint8_t  MAX_SCHEDULES                = 5;
    constexpr uint8_t  MAX_SEGMENTS_PER_SCHEDULE    = 5;
    constexpr uint8_t  MAX_BLE_DEVICES              = 5;

    // JsonDocument 기본 메모리(섹션별 파싱용) - 필요 시 조정
    constexpr size_t DOC_CORE_CAP       = 8 * 1024;
    constexpr size_t DOC_WIFI_CAP       = 6 * 1024;
    constexpr size_t DOC_SIM_CAP        = 4 * 1024;
    constexpr size_t DOC_SCHEDULE_CAP   = 8 * 1024;
    constexpr size_t DOC_MOTION_CAP     = 6 * 1024;
} // namespace A10_Const

// ======================================================
// ENUM / 기본 상수
// ======================================================

// Wi-Fi 모드
typedef enum : uint8_t {
    EN_A10_WIFI_MODE_AP      = 0,  // AP 단독
    EN_A10_WIFI_MODE_STA     = 1,  // STA 단독
    EN_A10_WIFI_MODE_AP_STA  = 2   // AP+STA 동시
} T_A10_WifiMode_t;

// 바람 단계(참고용)
typedef enum : uint8_t {
    EN_A10_WEATHER_PHASE_CALM = 0,
    EN_A10_WEATHER_PHASE_NORMAL,
    EN_A10_WEATHER_PHASE_STRONG,
    EN_A10_WEATHER_PHASE_COUNT
} T_A10_WindPhase_t;

static const char* g_A10_WEATHER_PHASE_NAMES_Arr[] = {
    "CALM", "NORMAL", "STRONG"
};

// 프리셋 (레거시/참고용: 신규 sim은 문자열 preset 사용)
typedef enum : uint8_t {
    EN_A10_PRESET_OFF               = 0,
    EN_A10_PRESET_COUNTRY           = 1,
    EN_A10_PRESET_MEDITERRANEAN     = 2,
    EN_A10_PRESET_OCEAN             = 3,
    EN_A10_PRESET_MOUNTAIN          = 4,
    EN_A10_PRESET_PLAINS            = 5,
    EN_A10_PRESET_HARBOR_BREEZE     = 6,
    EN_A10_PRESET_FOREST_CANOPY     = 7,
    EN_A10_PRESET_URBAN_SUNSET      = 8,
    EN_A10_PRESET_TROPICAL_RAIN     = 9,
    EN_A10_PRESET_DESERT_NIGHT      = 10,
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

// ======================================================
// 구조체 정의 (섹션별) - ENV 제외
// ======================================================

// ---------------- STA 자격정보
typedef struct {
    char ssid[G_A10_WIFI_SSID_LEN];
    char pass[G_A10_WIFI_PWD_LEN];
} ST_A10_StaCredential;

// ---------------- Core(항상 상주): meta/system/hw/security/time
typedef struct {
    // [meta] 기기/펌웨어 메타
    struct {
        char     version[16];       // SC10_FW_1.0.0
        char     device_name[32];   // 예: WindScape_XY-SK10
        char     last_update[32];   // ISO8601
    } meta;

    // [system] 웹/로그
    struct {
        struct {
            char html[G_A10_WEB_PATH_LEN];
            char css[G_A10_WEB_PATH_LEN];
            char js [G_A10_WEB_PATH_LEN];
        } web;
        struct {
            char     level[8];      // INFO/WARN/DEBUG
            uint16_t max_entries;
        } logging;
    } system;

    // [hw] 하드웨어 핀 기본값 (팬 PWM, 센서 핀 등)
    struct {
        struct {  // 팬 PWM
            int16_t   pin;
            uint8_t   channel;
            uint32_t  freq;
            uint8_t   res;
        } fan_pwm;

        // 하드웨어 센서 연결 정보(코어 JSON에 위치)
        struct {
            struct { // PIR 입력 핀 및 디바운스
                bool     enabled;
                int16_t  pin;
                uint16_t debounce_sec;
            } pir;
            struct { // 온습도 센서 (hw: 스캔 간격 등 하드웨어 주기)
                bool     enabled;
                char     type[16];        // 예: "DHT22"
                int16_t  pin;
                uint16_t interval_sec;
            } tempHum;
            struct { // BLE 하드웨어 스캔 주기
                bool     enabled;
                uint16_t scan_interval;   // 초 단위
            } ble;
        } sensors;
    } hw;

    // [security] 보안키(헤더에 저장 시 UI 노출 주의)
    struct {
        char api_key[G_A10_API_KEY_MAX_LEN];
    } security;

    // [time] NTP/시간대/동기화 주기
    struct {
        char     ntp_server[64];
        char     timezone[32];
        uint16_t sync_interval_min;
    } time;

} ST_A10_CoreConfig;

// ---------------- Wi-Fi (Lazy-Load)
typedef struct {
    uint8_t  mode;  // EN_A10_WIFI_MODE_*
    struct {
        char ssid[G_A10_WIFI_SSID_LEN];
        char password[G_A10_WIFI_PWD_LEN];
    } ap;
    ST_A10_StaCredential sta[A10_Const::MAX_STA_NETWORKS];
    uint8_t  sta_count;
} ST_A10_WifiConfig;

// ---------------- Sim(바람 물리) (Lazy-Load) - cfg_sim_021.json
typedef struct {
    char   preset[32];           // 예: "COUNTRY_BREEZE"
    float  wind_intensity;       // %
    float  gust_frequency;       // %
    float  wind_variability;     // %
    float  fan_limit;            // %
    float  min_fan;              // %
    struct {                     // 난류 파라미터
        float length_scale;
        float intensity_sigma;
    } turbulence;
    struct {                     // 열기포 파라미터
        float bubble_strength;
        float bubble_radius;
    } thermal;
} ST_A10_SimConfig;

// ---------------- Schedule (Lazy-Load) - cfg_schedule_021.json
typedef struct {
    // 세그먼트: on/off 반복 규칙
    struct {
        uint16_t  no;                 // 세그먼트 개별 번호
        uint16_t  on_minutes;
        uint16_t  off_minutes;
        char      mode[16];           // "preset"/"fixed"
        char      preset_name[32];    // mode=preset일 때
        float     adj_intensity;      // preset 보정(%), 없으면 0
        float     adj_variability;    // preset 보정(%), 없으면 0
        float     fixed_speed;        // mode=fixed일 때 %
    } seg[A10_Const::MAX_SEGMENTS_PER_SCHEDULE];

    uint8_t  seg_count;               // 유효 세그먼트 수

    // 스케줄 메타
    uint16_t no;                      // 스케줄 개별 번호
    char     name[32];
    bool     enabled;
    uint8_t  days[7];                 // 월~일 1/0
    char     start_time[6];           // "HH:MM"
    char     end_time[6];             // "HH:MM"
} ST_A10_ScheduleItem;

typedef struct {
    ST_A10_ScheduleItem items[A10_Const::MAX_SCHEDULES];
    uint8_t             count;        // 유효 스케줄 수
} ST_A10_ScheduleConfig;

// ---------------- Motion (Lazy-Load) - cfg_motion_021.json
// (경량 사양: logic_mode/scan_interval 생략 버전에도 대응 가능)
typedef struct {
    bool     enabled;

    struct {                 // PIR
        bool     enabled;
        uint16_t hold_sec;       // 감지 후 유지시간
        // pin/debounce는 HW(core)에서 정의되지만, 런타임 오버라이드가 필요하면 확장 필드 추가 가능
    } pir;

    struct {                 // BLE
        bool     enabled;
        int16_t  rssi_threshold; // 예: -70 dBm
        uint16_t hold_sec;       // 감지 후 유지시간
        struct {
            char   mac[20];
            char   alias[32];
            bool   enabled;
        } devices[A10_Const::MAX_BLE_DEVICES];
        uint8_t  device_count;
    } ble;

} ST_A10_MotionConfig;

// ======================================================
// 통합 루트 구성(하이브리드)
//  - Core : by-value(상시 상주)
//  - WiFi/Sim/Schedule/Motion : 포인터 (필요 시만 동적할당)
// ======================================================
typedef struct {
    ST_A10_CoreConfig        core;           // 항상 상주

    ST_A10_WifiConfig*       wifi;           // nullptr이면 미로딩
    ST_A10_SimConfig*        sim;            // "
    ST_A10_ScheduleConfig*   schedule;       // "
    ST_A10_MotionConfig*     motion;         // "
} ST_A10_ConfigRoot;

// 전역 루트(선언만; 정의는 구현부 또는 사용처 .cpp)
inline ST_A10_ConfigRoot g_A10_config_root;

// ======================================================
// 유틸 함수
// ======================================================
inline float A10_getRandom01() {
    return static_cast<float>(esp_random()) / static_cast<float>(UINT32_MAX);
}
inline float A10_randRange(float p_a, float p_b) {
    return p_a + A10_getRandom01() * (p_b - p_a);
}
inline const char* A10_getPresetName(uint8_t p_index) {
    if (p_index >= EN_A10_PRESET_COUNT) return "UNKNOWN";
    return g_A10_PRESET_MODE_NAMES_Arr[p_index];
}
inline int8_t A10_getPresetIndex(const char* p_name) {
    for (uint8_t v_i = 0; v_i < EN_A10_PRESET_COUNT; ++v_i)
        if (strcasecmp(p_name, g_A10_PRESET_MODE_NAMES_Arr[v_i]) == 0) return static_cast<int8_t>(v_i);
    return -1;
}

// ======================================================
// 구조체별 기본값 초기화 함수
//  - memset(0) 후 의도된 기본값으로 설정
//  - 문자열은 안전한 strcpy/strlcpy 사용
// ======================================================

/**
 * @brief ST_A10_CoreConfig 기본값 초기화
 */
inline void A10_resetCoreDefault(ST_A10_CoreConfig& p_cfg) {
    memset(&p_cfg, 0, sizeof(p_cfg));

    // [meta]
    strlcpy(p_cfg.meta.version, A10_Const::FW_VERSION, sizeof(p_cfg.meta.version));
    strlcpy(p_cfg.meta.device_name, "WindScape_XY-SK10", sizeof(p_cfg.meta.device_name));
    // last_update는 런타임에 기록하도록 비워둠

    // [system]
    strlcpy(p_cfg.system.web.html, "/html/SC10_main_021.html", sizeof(p_cfg.system.web.html));
    strlcpy(p_cfg.system.web.css,  "/html/SC10_main_021.css",  sizeof(p_cfg.system.web.css));
    strlcpy(p_cfg.system.web.js,   "/html/SC10_main_021.js",   sizeof(p_cfg.system.web.js));
    strlcpy(p_cfg.system.logging.level, "INFO", sizeof(p_cfg.system.logging.level));
    p_cfg.system.logging.max_entries = 300;

    // [hw] - 팬 PWM
    p_cfg.hw.fan_pwm.pin     = 6;
    p_cfg.hw.fan_pwm.channel = 0;
    p_cfg.hw.fan_pwm.freq    = 25000;
    p_cfg.hw.fan_pwm.res     = 10;

    // [hw.sensors] - PIR / TempHum / BLE (코어 하드웨어 배선 정보)
    p_cfg.hw.sensors.pir.enabled      = true;
    p_cfg.hw.sensors.pir.pin          = 13;
    p_cfg.hw.sensors.pir.debounce_sec = 5;

    p_cfg.hw.sensors.tempHum.enabled      = true;
    strlcpy(p_cfg.hw.sensors.tempHum.type, "DHT22", sizeof(p_cfg.hw.sensors.tempHum.type));
    p_cfg.hw.sensors.tempHum.pin          = 23;
    p_cfg.hw.sensors.tempHum.interval_sec = 30;

    p_cfg.hw.sensors.ble.enabled       = true;
    p_cfg.hw.sensors.ble.scan_interval = 5;

    // [security]
    strlcpy(p_cfg.security.api_key, "my_api_key_12345", sizeof(p_cfg.security.api_key));

    // [time]
    strlcpy(p_cfg.time.ntp_server, "pool.ntp.org", sizeof(p_cfg.time.ntp_server));
    strlcpy(p_cfg.time.timezone,   "Asia/Seoul",   sizeof(p_cfg.time.timezone));
    p_cfg.time.sync_interval_min = 60;
}

/**
 * @brief ST_A10_WifiConfig 기본값 초기화
 */
inline void A10_resetWifiDefault(ST_A10_WifiConfig& p_cfg) {
    memset(&p_cfg, 0, sizeof(p_cfg));
    p_cfg.mode = EN_A10_WIFI_MODE_AP_STA;

    strlcpy(p_cfg.ap.ssid,     "NatureWind", sizeof(p_cfg.ap.ssid));
    strlcpy(p_cfg.ap.password, "2540",       sizeof(p_cfg.ap.password));

    p_cfg.sta_count = 0; // 빈 목록
}

/**
 * @brief ST_A10_SimConfig 기본값 초기화
 */
inline void A10_resetSimDefault(ST_A10_SimConfig& p_cfg) {
    memset(&p_cfg, 0, sizeof(p_cfg));

    strlcpy(p_cfg.preset, "COUNTRY_BREEZE", sizeof(p_cfg.preset));
    p_cfg.wind_intensity       = 70.0f;
    p_cfg.gust_frequency       = 45.0f;
    p_cfg.wind_variability     = 50.0f;
    p_cfg.fan_limit            = 90.0f;
    p_cfg.min_fan              = 10.0f;

    p_cfg.turbulence.length_scale    = 40.0f;
    p_cfg.turbulence.intensity_sigma = 0.5f;

    p_cfg.thermal.bubble_strength    = 2.0f;
    p_cfg.thermal.bubble_radius      = 18.0f;
}

/**
 * @brief ST_A10_ScheduleConfig 기본값 초기화 (비어있는 스케줄)
 */
inline void A10_resetScheduleDefault(ST_A10_ScheduleConfig& p_cfg) {
    memset(&p_cfg, 0, sizeof(p_cfg));
    p_cfg.count = 0;
}

/**
 * @brief ST_A10_MotionConfig 기본값 초기화 (경량 스펙)
 */
inline void A10_resetMotionDefault(ST_A10_MotionConfig& p_cfg) {
    memset(&p_cfg, 0, sizeof(p_cfg));

    p_cfg.enabled = true;

    // PIR - core.hw.sensors.pir에 핀/디바운스가 있으므로 여기선 유지시간만 기본 제공
    p_cfg.pir.enabled   = true;
    p_cfg.pir.hold_sec  = 120;

    // BLE - core.hw.sensors.ble에 하드웨어 스캔주기
    p_cfg.ble.enabled        = true;
    p_cfg.ble.rssi_threshold = -70;
    p_cfg.ble.hold_sec       = 120;
    p_cfg.ble.device_count   = 0;
}

/**
 * @brief ST_A10_ConfigRoot 전체를 기본값으로 초기화
 *        Core는 상시 상주, 나머지는 Lazy-Load 포인터를 nullptr로 설정
 */
inline void A10_resetToDefault(ST_A10_ConfigRoot& p_root) {
    // 1) Core
    A10_resetCoreDefault(p_root.core);

    // 2) Lazy 포인터 - 별도 로더에서 필요 시 할당/해제
    p_root.wifi     = nullptr;
    p_root.sim      = nullptr;
    p_root.schedule = nullptr;
    p_root.motion   = nullptr;
}

