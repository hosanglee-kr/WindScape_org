# 🌿 WindScape 시스템 통합 요구사항 명세서 (통합 v5.9)

**버전:** `SC10_FW_1.0.6`  
**디바이스:** `WindScape_XY-SK10`  
**작성일:** `2025-10-20`  
**작성 목적:**  
ESP32 기반 자연풍 시뮬레이터 **WindScape**의 펌웨어, WebUI, 설정 JSON 구조 및 기능 확장 요구사항을 체계화한다.  
(v5.8 전 내용 유지 + **온도 기반 제어(체감/대류) 2종 추가**)

---

## 🧩 1. 시스템 개요

WindScape는 일반 선풍기를 **스마트 자연풍 장치**로 전환한다.  
ESP32 펌웨어, LittleFS WebUI, PWM 제어, 환경 센서, BLE/PIR 모션 감지, 스케줄링을 통합 관리한다.

---

## ⚙️ 2. 주요 구성 요소

| 구분 | 모듈명 | 기능 요약 |
|---|---|---|
| **A10_Const_007.h** | 상수/타입 정의 | Wi-Fi/WEB/Phase/Preset/Config 구조체 |
| **C10_ConfigManager_013.h** | 설정 로드/저장/백업 | JSON 직렬화, 공장 초기화 |
| **M10_WiFiManager_007.h** | Wi-Fi AP/STA | 다중 STA, Smart Connect |
| **P10_PWM_ctrl_005.h** | PWM 제어 | 듀티/주파수/분해능 |
| **S10_Simulation_007.h** | 자연풍 시뮬레이션 | Phase/난류/열기포/프리셋 |
| **M10_FanMonitor_001.h** | 팬 모니터링 | PWM/Tach 수집·그래프 |
| **M20_EnvMonitor_001.h** | 환경 모니터링 | DHT22 수집·평균화 |
| **W10_WebAPI_007.h** | Web API | `/api/*` 엔드포인트 |
| **WS10_Main_007.h** | 메인 엔진 | 초기화/루프/통합 실행 |

> v5.9 추가 반영: **온도 기반 제어 2종**(체감 Fan Boost, 대류 Thermal 빈도 Boost) 관련 설정 항목 및 동작 정의.

---

## 🕹️ 3. 주요 기능 요약

### 3.1 시뮬레이션
- 자연풍(Phase, 난류, 열기포/열기둥 등) 구현  
- 프리셋 모드 & 고정 풍속 모드 지원  
- 프리셋별 **강도(Intensity)/변동성(Variability)/난류(Turbulence)** 조정

### 3.2 Wi-Fi & WebUI
- AP / STA / AP+STA 지원  
- LittleFS 정적 자산 서빙(`.html`, `.css`, `.js`)  
- **API Key 인증**(헤더), CORS, no-cache

### 3.3 스케줄
- NTP 시간 동기화  
- 복수 스케줄, 세그먼트 기반(`preset/fixed/off`)  
- 요일, 기간, 옵션(Intensity/Variability/Turbulence)

### 3.4 모션 감지
- PIR & BLE OR 동작  
- 공통: `scan_interval_sec`, `hold_sec`  
- PIR: `pin`, `debounce_sec`  
- BLE: `rssi_threshold`, `devices[]`

### 3.5 팬 모니터링 (fan_monitor)
- PWM 듀티 + Tach RPM 동시 모니터  
- 평균 필터, 듀얼축 차트 옵션

### 3.6 환경 모니터링 (env_monitor)
- DHT22 온·습도  
- 이동 평균, 범위 설정, 듀얼축 차트

### 3.7 **온도 기반 제어 (v5.9 신규)**
- **체감 반영:** 온도 상승 시 **팬 듀티%에 가산 부스트**
- **대류 반영:** 온도 상승 시 **열기포 발생 빈도 승수 가중**
- 두 기능은 **각각 개별 활성/비활성** 가능하며 서로 독립적으로 동작

---

## 🔧 4. JSON 설정 구조 (완전체; v5.9)

> 호환성: v5.8 구조 유지. v5.9에서 **온도 기반 6항목**이 상위 레벨에 추가됨.  
> 보안: `security.api_key`를 사용(일부 코드에서 `g_SC10_config.api_key` 접근 시 **security.api_key**를 참조하도록 매핑 필요).

```json
{
  "meta": {
    "version": "SC10_FW_1.0.6",
    "device_name": "WindScape_XY-SK10",
    "last_update": "2025-10-20T16:20:00+09:00"
  },
  "time": {
    "ntp_server": "pool.ntp.org",
    "timezone": "Asia/Seoul",
    "sync_interval_min": 60
  },
  "wifi": {
    "wifi_mode": 2,
    "ap_network": { "ap_ssid": "NatureWind", "ap_password": "2540" },
    "sta_networks": [{ "ssid": "MyHomeWiFi", "pass": "mypassword" }]
  },
  "hw": {
    "pwm_pin": 6,
    "pwm_channel": 0,
    "pwm_freq": 25000,
    "pwm_res": 10
  },
  "sim": {
    "intensity": 70.0,
    "gust_freq": 45.0,
    "variability": 50.0,
    "fan_limit": 90.0,
    "min_fan": 10.0,
    "turb_len": 40.0,
    "turb_sig": 0.5,
    "therm_str": 2.0,
    "therm_rad": 18.0,
    "preset": "COUNTRY_BREEZE"
  },
  "schedules": [
    {
      "id": 10,
      "name": "Morning Air",
      "enabled": true,
      "days": [1, 1, 1, 1, 1, 0, 0],
      "start": "08:00",
      "end": "12:00",
      "segments": [
        {
          "seq_no": 10,
          "enabled": true,
          "mode": "preset",
          "preset_name": "HARBOUR_BREEZE",
          "preset_options": { "intensity": 80, "variability": 60, "turbulence": 45 },
          "duration_minutes": 20
        },
        { "seq_no": 20, "enabled": true, "mode": "off", "duration_minutes": 10 },
        { "seq_no": 30, "enabled": true, "mode": "fixed", "fixed_speed": 55.0, "duration_minutes": 15 },
        { "seq_no": 40, "enabled": true, "mode": "off", "duration_minutes": 5 }
      ]
    }
  ],
  "motion": {
    "enabled": true,
    "scan_interval_sec": 5,
    "hold_sec": 120,
    "pir": { "enabled": true, "pin": 13, "debounce_sec": 3 },
    "ble": {
      "enabled": true,
      "rssi_threshold": -70,
      "devices": [
        { "mac": "AA:BB:CC:11:22:33", "alias": "MyPhone", "enabled": true },
        { "mac": "DD:EE:FF:44:55:66", "alias": "SmartBand", "enabled": false }
      ]
    }
  },
  "env_monitor": {
    "enabled": true,
    "type": "DHT22",
    "pin": 23,
    "interval_sec": 30,
    "records_max": 200,
    "avg_window": 5,
    "temp": { "enabled": true, "unit": "C", "range_min": -10, "range_max": 50 },
    "humid": { "unit": "%", "range_min": 0, "range_max": 100 },
    "chart_options": {
      "display_temp": true,
      "display_humid": true,
      "refresh_interval_sec": 5,
      "smooth_window": 3,
      "dual_axis": true
    }
  },
  "fan_monitor": {
    "enabled": true,
    "interval_sec": 5,
    "records_max": 300,
    "pwm": { "enabled": true, "min_duty": 0.0, "max_duty": 100.0 },
    "tach": { "enabled": true, "pin": 27, "pulses_per_rev": 2, "rpm_min": 200, "rpm_max": 3000, "avg_window": 5 },
    "chart_options": {
      "display_pwm_duty": true,
      "display_rpm": true,
      "refresh_interval_sec": 5,
      "smooth_window": 3,
      "dual_axis": true
    }
  },
  "security": { "api_key": "my_api_key_12345" },
  "system": {
    "web": {
      "html": "/html/SC10_main_017.html",
      "css": "/html/SC10_main_017.css",
      "js": "/html/SC10_main_017.js"
    },
    "logging": { "level": "INFO", "max_entries": 200 }
  },
  "enable_thermal_fan_boost": true,
  "thermal_fan_boost_base_temp": 25.0,
  "thermal_fan_boost_per_degree": 0.5,
  "enable_thermal_freq_boost": true,
  "thermal_freq_boost_base_temp": 25.0,
  "thermal_freq_boost_per_degree": 4.0
}
```

---

🧱 7. Config 구조체 (완성본)
```cpp
typedef struct ST_A10_Config_t {
  struct { char version[24]; char device_name[32]; char last_update[40]; } meta;
  struct { char ntp_server[64]; char timezone[32]; uint16_t sync_interval_min; } time;
  struct {
    uint8_t wifi_mode;
    struct { char ssid[32]; char password[64]; } ap_network;
    struct { char ssid[32]; char pass[64]; } sta_networks[4];
    uint8_t sta_count;
  } wifi;
  struct { uint8_t pwm_pin; uint8_t pwm_channel; uint32_t pwm_freq; uint8_t pwm_res; } hw;
  struct {
    float intensity, gust_freq, variability, fan_limit, min_fan, turb_len, turb_sig, therm_str, therm_rad;
    char preset[32];
  } sim;
  struct {
    bool enabled; uint8_t days[7]; char name[24]; char start[6]; char end[6];
    struct {
      uint16_t seq_no; bool enabled; char mode[8]; char preset_name[32];
      struct { int intensity, variability, turbulence; } preset_options;
      float fixed_speed; uint16_t duration_minutes;
    } segments[8]; uint8_t segment_count; uint16_t id;
  } schedules[6]; uint8_t schedule_count;
  struct {
    bool enabled; uint16_t scan_interval_sec, hold_sec;
    struct { bool enabled; uint8_t pin; uint16_t debounce_sec; } pir;
    struct { bool enabled; int rssi_threshold; struct { char mac[18]; char alias[24]; bool enabled; } devices[6]; uint8_t device_count; } ble;
  } motion;
  struct {
    bool enabled; char type[8]; uint8_t pin; uint16_t interval_sec, records_max; uint8_t avg_window;
    struct { bool enabled; char unit[2]; int range_min, range_max; } temp;
    struct { char unit[2]; int range_min, range_max; } humid;
    struct { bool display_temp, display_humid, dual_axis; uint16_t refresh_interval_sec; uint8_t smooth_window; } chart_options;
  } env_monitor;
  struct {
    bool enabled; uint16_t interval_sec, records_max;
    struct { bool enabled; float min_duty, max_duty; } pwm;
    struct { bool enabled; uint8_t pin, pulses_per_rev, avg_window; uint16_t rpm_min, rpm_max; } tach;
    struct { bool display_pwm_duty, display_rpm, dual_axis; uint16_t refresh_interval_sec; uint8_t smooth_window; } chart_options;
  } fan_monitor;
  struct { char api_key[64]; } security;
  struct { struct { char html[48], css[48], js[48]; } web; struct { char level[8]; uint16_t max_entries; } logging; } system;

  bool  enable_thermal_fan_boost;
  float thermal_fan_boost_base_temp;
  float thermal_fan_boost_per_degree;
  bool  enable_thermal_freq_boost;
  float thermal_freq_boost_base_temp;
  float thermal_freq_boost_per_degree;
} ST_A10_Config_t;

```
🚀 9. 향후 확장 계획 (v6.x)

카테고리	예정 기능

🌤️ 환경센서 확장	조도, 기압, VOC 추가
🪄 스마트 반응 제어	온·습도 급변 시 자동 속도 조절
🔔 알림 시스템	온도·RPM 임계 초과 시 Toast/Log
📡 OTA 업데이트	/api/update 엔드포인트
💾 데이터 내보내기	LittleFS → CSV 백업
🎛️ 대시보드 UI	팬·센서·온도부스트 상태 통합 뷰

