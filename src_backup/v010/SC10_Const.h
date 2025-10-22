// SC10_Const.h


#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// 전역 상수/경로/버전
namespace SC10_Const {
  constexpr char FW_VERSION[]      = "SC10_FW_1.0.0";
  constexpr char CONFIG_FILE[]     = "/json/config_003.json";
  constexpr char BACKUP_FILE[]     = "/json/config_003.json.bak";
  constexpr char HTML_FILE[]       = "/html/SC10_main_004.html";
  constexpr char JS_FILE[]         = "/html/SC10_main_003.js";
  constexpr int  MAX_STA_NETWORKS  = 5;
}

// Wi-Fi 모드
#define G_SC10_WIFI_MODE_AP   0
#define G_SC10_WIFI_MODE_STA  1

// 바람 단계(Phase)
typedef enum {
  SC10_WEATHER_PHASE_CALM = 0,
  SC10_WEATHER_PHASE_NORMAL = 1,
  SC10_WEATHER_PHASE_STRONG = 2,
  SC10_WEATHER_PHASE_COUNT
} SC10_WindWeatherPhase_t;

static const char* G_SC10_WEATHER_PHASE_NAMES[] = {"Calm","Normal","Strong"};

// 프리셋
typedef enum {
  SC10_PRESET_OFF = 0,
  SC10_PRESET_COUNTRY,
  SC10_PRESET_MEDITERRANEAN,
  SC10_PRESET_OCEAN,
  SC10_PRESET_MOUNTAIN,
  SC10_PRESET_PLAINS,
  SC10_PRESET_COUNT
} SC10_PresetMode_t;

static const char* G_SC10_PRESET_MODE_NAMES[] = {
  "Off","Countryside","Mediterranean","Ocean","Mountain","Plains"
};

// STA 자격증명
struct SC10_StaCredential {
  char ssid[32];
  char password[64];
};

// 전역 설정 구조체
struct WindConfig {
  // Wi-Fi
  int  wifi_mode = G_SC10_WIFI_MODE_STA;
  SC10_StaCredential sta_networks[SC10_Const::MAX_STA_NETWORKS];
  int  sta_network_count = 0;
  char ap_ssid[32]     = "SC10_Config_AP";
  char ap_password[64] = "newpassword";

  // PWM/핀
  int fan_pwm_pin    = 14;
  int fan_tach_pin   = 27;
  int pwm_frequency  = 25000;
  int pwm_channel    = 0;
  int pwm_resolution = 10;

  // 타이밍
  int wind_sim_interval_ms      = 250;
  int gust_check_interval_ms    = 500;
  int thermal_check_interval_ms = 2000;

  // 시뮬레이션 파라미터
  float wind_intensity             = 100.0f;
  float gust_frequency             = 30.0f;
  float wind_variability           = 40.0f;
  float fan_speed_limit            = 80.0f;
  float minimum_fan_speed          = 0.0f;
  float turbulence_length_scale    = 30.0f;
  float turbulence_intensity_sigma = 0.3f;
  float thermal_bubble_strength    = 1.8f;
  float thermal_bubble_radius      = 15.0f;

  // 프리셋
  int preset_mode_index            = SC10_PRESET_OCEAN;
};

// 전역 설정 인스턴스 (헤더 온리: inline로 ODR 방지)
inline WindConfig g_SC10_config;

// 난수 유틸 (0~1)
inline float SC10_getRandom01() {
  return (float)esp_random() / (float)UINT32_MAX;
}
// 난수 유틸 (범위)
inline float SC10_randRange(float a, float b) {
  return a + SC10_getRandom01() * (b - a);
}
