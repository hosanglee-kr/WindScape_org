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
// ============================================================================
// 🌿 WindScape 통합 설정 구조체 (v5.9 기준 완성본)
// 파일명: A10_Const_007.h
// 목적: 시스템 전체 설정 구조 정의 (JSON <-> 구조체 직렬화 매핑)
// ============================================================================

typedef struct ST_A10_Config_t {

  // --------------------------------------------------------------------------
  // [1] 메타 정보 (기기 식별 및 버전)
  // --------------------------------------------------------------------------
  struct {
    char version[24];         // 펌웨어 버전명 (예: "SC10_FW_1.0.6")
    char device_name[32];     // 디바이스 이름 (예: "WindScape_XY-SK10")
    char last_update[40];     // 마지막 설정 저장 시각 (ISO-8601 형식)
  } meta;

  // --------------------------------------------------------------------------
  // [2] 시간 및 NTP 설정
  // --------------------------------------------------------------------------
  struct {
    char ntp_server[64];      // NTP 서버 주소 (예: "pool.ntp.org")
    char timezone[32];        // 시간대 (예: "Asia/Seoul")
    uint16_t sync_interval_min; // NTP 동기화 주기 (분 단위)
  } time;

  // --------------------------------------------------------------------------
  // [3] Wi-Fi 설정 (AP/STA 통합)
  // --------------------------------------------------------------------------
  struct {
    uint8_t wifi_mode;        // Wi-Fi 모드 (0:AP / 1:STA / 2:AP+STA)
    struct { 
      char ssid[32];          // AP 모드 SSID
      char password[64];      // AP 모드 비밀번호
    } ap_network;
    struct { 
      char ssid[32];          // STA 모드 SSID
      char pass[64];          // STA 모드 비밀번호
    } sta_networks[4];        // 다중 STA 저장용 배열 (최대 4개)
    uint8_t sta_count;        // STA 네트워크 등록 개수
  } wifi;

  // --------------------------------------------------------------------------
  // [4] 하드웨어 / PWM 설정
  // --------------------------------------------------------------------------
  struct {
    uint8_t pwm_pin;          // PWM 제어 핀 번호
    uint8_t pwm_channel;      // PWM 채널 번호
    uint32_t pwm_freq;        // PWM 주파수 (Hz 단위)
    uint8_t pwm_res;          // PWM 분해능 (bit)
  } hw;

  // --------------------------------------------------------------------------
  // [5] 시뮬레이션 파라미터 (바람 특성)
  // --------------------------------------------------------------------------
  struct {
    float intensity;          // 기본 풍속 강도 (%)
    float gust_freq;          // 돌풍 발생 빈도
    float variability;        // 풍속 변동성 (랜덤성)
    float fan_limit;          // 최대 팬 듀티 제한값 (%)
    float min_fan;            // 최소 팬 듀티값 (%)
    float turb_len;           // 난류 길이 스케일
    float turb_sig;           // 난류 시그마(분산)
    float therm_str;          // 열기포(대류) 강도
    float therm_rad;          // 열기포 반경
    char preset[32];          // 프리셋 이름 (예: "COUNTRY_BREEZE")
  } sim;

  // --------------------------------------------------------------------------
  // [6] 스케줄 관리 (자동 운전)
  // --------------------------------------------------------------------------
  struct {
    bool enabled;             // 해당 스케줄 활성화 여부
    uint8_t days[7];          // 요일별 작동 여부 (월~일; 1=활성)
    char name[24];            // 스케줄 이름
    char start[6];            // 시작 시각 ("HH:MM")
    char end[6];              // 종료 시각 ("HH:MM")

    // 세그먼트 단위 상세 설정
    struct {
      uint16_t seq_no;        // 순번 (10, 20, 30 등)
      bool enabled;           // 세그먼트 활성화 여부
      char mode[8];           // 작동 모드 ("preset", "fixed", "off")
      char preset_name[32];   // 프리셋 모드 시 사용되는 프리셋 이름
      struct {
        int intensity;        // 프리셋 강도 (%)
        int variability;      // 변동성 (%)
        int turbulence;       // 난류 수준 (%)
      } preset_options;
      float fixed_speed;      // fixed 모드일 때 고정 풍속 (%)
      uint16_t duration_minutes; // 세그먼트 지속 시간 (분)
    } segments[8];            // 최대 8개 세그먼트
    uint8_t segment_count;    // 세그먼트 개수
    uint16_t id;              // 스케줄 ID (10부터 10단위 증가)
  } schedules[6];             // 최대 6개 스케줄 등록 가능
  uint8_t schedule_count;     // 등록된 스케줄 총 수

  // --------------------------------------------------------------------------
  // [7] 모션 감지 설정 (PIR + BLE)
  // --------------------------------------------------------------------------
  struct {
    bool enabled;             // 모션 감지 기능 전체 활성화 여부
    uint16_t scan_interval_sec; // 감지 주기 (초)
    uint16_t hold_sec;        // 감지 유지 시간 (초)
    struct {
      bool enabled;           // PIR 센서 사용 여부
      uint8_t pin;            // PIR 연결 핀 번호
      uint16_t debounce_sec;  // 디바운스 시간 (초)
    } pir;
    struct {
      bool enabled;           // BLE 감지 기능 활성화 여부
      int rssi_threshold;     // BLE RSSI 감도 임계값 (dBm)
      struct {
        char mac[18];         // BLE 기기 MAC 주소
        char alias[24];       // 별칭
        bool enabled;         // 개별 등록 활성화 여부
      } devices[6];           // 등록된 BLE 기기 목록
      uint8_t device_count;   // BLE 등록 기기 수
    } ble;
  } motion;

  // --------------------------------------------------------------------------
  // [8] 환경 모니터링 설정 (온도/습도)
  // --------------------------------------------------------------------------
  struct {
    bool enabled;             // 환경 모니터링 활성화 여부
    char type[8];             // 센서 타입 (예: "DHT22")
    uint8_t pin;              // 센서 데이터 핀 번호
    uint16_t interval_sec;    // 측정 주기 (초)
    uint16_t records_max;     // 기록 최대 저장 개수
    uint8_t avg_window;       // 이동평균 윈도우 크기
    struct {
      bool enabled;           // 온도 센싱 활성화
      char unit[2];           // 단위 ("C")
      int range_min;          // 최소 온도 범위
      int range_max;          // 최대 온도 범위
    } temp;
    struct {
      char unit[2];           // 단위 ("%")
      int range_min;          // 최소 습도
      int range_max;          // 최대 습도
    } humid;
    struct {
      bool display_temp;      // 온도 그래프 표시 여부
      bool display_humid;     // 습도 그래프 표시 여부
      uint16_t refresh_interval_sec; // 그래프 갱신 주기
      uint8_t smooth_window;  // 그래프 스무딩 윈도우 크기
      bool dual_axis;         // 온/습도 듀얼축 여부
    } chart_options;
  } env_monitor;

  // --------------------------------------------------------------------------
  // [9] 팬 모니터링 설정
  // --------------------------------------------------------------------------
  struct {
    bool enabled;             // 팬 모니터링 기능 활성화 여부
    uint16_t interval_sec;    // 측정 주기 (초)
    uint16_t records_max;     // 기록 저장 최대 개수
    struct {
      bool enabled;           // PWM 모니터링 활성화 여부
      float min_duty;         // 최소 듀티 (%)
      float max_duty;         // 최대 듀티 (%)
    } pwm;
    struct {
      bool enabled;           // Tachometer 사용 여부
      uint8_t pin;            // Tach 입력 핀
      uint8_t pulses_per_rev; // 회전당 펄스 수
      uint16_t rpm_min;       // 최소 RPM
      uint16_t rpm_max;       // 최대 RPM
      uint8_t avg_window;     // 이동 평균 필터 크기
    } tach;
    struct {
      bool display_pwm_duty;  // 듀티 그래프 표시 여부
      bool display_rpm;       // RPM 그래프 표시 여부
      uint16_t refresh_interval_sec; // 갱신 주기
      uint8_t smooth_window;  // 스무딩 윈도우
      bool dual_axis;         // 듀얼 축 표시 여부
    } chart_options;
  } fan_monitor;

  // --------------------------------------------------------------------------
  // [10] 보안 및 시스템 파일 설정
  // --------------------------------------------------------------------------
  struct {
    char api_key[64];         // Web API 인증용 키
  } security;

  struct {
    struct { 
      char html[48];          // Web UI HTML 경로
      char css[48];           // CSS 파일 경로
      char js[48];            // JS 파일 경로
    } web;
    struct {
      char level[8];          // 로그 레벨 ("INFO", "DEBUG" 등)
      uint16_t max_entries;   // 로그 최대 저장 수
    } logging;
  } system;

  // --------------------------------------------------------------------------
  // [11] 온도 기반 제어 기능 (v5.9 신규 추가)
  // --------------------------------------------------------------------------
  bool  enable_thermal_fan_boost;      // [체감] 온도 기반 팬 부스트 활성화
  float thermal_fan_boost_base_temp;   // [체감] 부스트 시작 기준 온도 (℃)
  float thermal_fan_boost_per_degree;  // [체감] 1℃ 초과 시 듀티 추가율 (%)

  bool  enable_thermal_freq_boost;     // [대류] 열기포 빈도 부스트 활성화
  float thermal_freq_boost_base_temp;  // [대류] 부스트 시작 기준 온도 (℃)
  float thermal_freq_boost_per_degree; // [대류] 1℃ 초과 시 빈도 증가율 (%)

} ST_A10_Config_t;

// ============================================================================
// End of ST_A10_Config_t
// ============================================================================

```

🚀 9. 향후 확장 계획 (v6.x)

카테고리	예정 기능

🌤️ 환경센서 확장	조도, 기압, VOC 추가
🪄 스마트 반응 제어	온·습도 급변 시 자동 속도 조절
🔔 알림 시스템	온도·RPM 임계 초과 시 Toast/Log
📡 OTA 업데이트	/api/update 엔드포인트
💾 데이터 내보내기	LittleFS → CSV 백업
🎛️ 대시보드 UI	팬·센서·온도부스트 상태 통합 뷰

