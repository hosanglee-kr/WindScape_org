# 🌿 WindScape 시스템 통합 요구사항 명세서 (v5.8 기준)

**버전:** `SC10_FW_1.0.6`  
**디바이스:** `WindScape_XY-SK10`  
**작성일:** `2025-10-20`  
**작성 목적:**  
ESP32 기반 자연풍 시뮬레이터 **WindScape**의 펌웨어, 웹UI, 설정 JSON 구조 및 기능 확장 요구사항을 체계적으로 정의함.

---

## 🧩 1. 시스템 개요

WindScape는 일반 선풍기를 스마트 자연풍 장치로 변환하는 시스템이다.  
ESP32 기반 펌웨어, LittleFS 기반 Web UI, PWM 제어, 환경센서, BLE/PIR 모션 감지, 스케줄링 기능 등을 통합 관리한다.

---

## ⚙️ 2. 주요 구성 요소

| 구분 | 모듈명 | 기능 요약 |
|------|---------|-----------|
| A10_Const_007.h | 상수/타입 정의 | Wi-Fi, WEB, Phase, Preset 정의 |
| C10_ConfigManager_007.h | 설정 로드/저장/백업 | JSON 직렬화, 공장 초기화 |
| M10_WiFiManager_007.h | Wi-Fi AP/STA 관리 | 다중 STA, Smart Connect |
| P10_PWM_ctrl_005.h | PWM 제어 | 듀티, 주파수, 분해능 설정 |
| S10_Simulation_007.h | 자연풍 시뮬레이션 | Phase/난류/열기포/프리셋 적용 |
| S10_FanMonitor_001.h | 팬 속도/듀티 모니터링 | PWM/Tach 데이터 수집 및 그래프 |
| S10_EnvMonitor_002.h | 온습도 모니터링 | DHT22 데이터 수집 및 평균화 |
| W10_WebAPI_007.h | Web API 라우팅 | `/api/*` 엔드포인트 관리 |
| WS10_Main_007.h | 메인 엔진 | 초기화, 루프, 통합 실행 관리 |

---

## 🕹️ 3. 주요 기능 요약

### 3.1 시뮬레이션
- 자연풍(Phase, 난류, 열기포 등) 구현
- 프리셋 모드 및 고정 풍속 모드 지원
- 프리셋별 강도·변동성 조정 가능

### 3.2 Wi-Fi 및 WebUI
- AP / STA / AP+STA 모드 지원  
- LittleFS 기반 정적 자산(`.html`, `.css`, `.js`) 서빙  
- API Key 인증, CORS, no-cache 헤더 지원  

### 3.3 스케줄 기능
- NTP 시간 동기화 지원
- 복수 스케줄 등록 가능 (`id`는 10부터 10씩 증가)
- 각 스케줄 내 다중 세그먼트(`segments`) 지원
- 세그먼트별:
  - `mode`: `"preset"`, `"fixed"`, `"off"`
  - `preset_name` 또는 `fixed_speed` 지정 가능
  - `enabled`, `seq_no`, `duration_minutes` 포함
  - 프리셋 모드 시 옵션(`intensity`, `variability`, `turbulence`) 설정 가능
- 요일별 동작 (`days` 필드)
- 스케줄 전체 활성화 여부(`enabled`) 포함

### 3.4 모션 감지
- PIR 센서와 BLE 신호 기반 감지
- 두 방식은 OR 조건으로 동작 가능
- 공통 설정: `scan_interval_sec`, `hold_sec`
- 개별 설정:
  - PIR: `pin`, `debounce_sec`
  - BLE: `rssi_threshold`, `devices[]` (등록/비활성 가능)
- 감지 후 일정 시간 동안 팬 작동 유지 가능

### 3.5 팬 모니터링 (fan_monitor)
- PWM 듀티 및 Tachometer RPM 동시 모니터링
- PWM, Tach 개별 활성화 가능
- Tach 평균 필터(`avg_window`) 적용
- 차트 옵션(`dual_axis`, `smooth_window`) 제공
- `/api/fan_monitor` API 제공 예정

### 3.6 환경 모니터링 (env_monitor)
- DHT22 센서 기반 온·습도 측정
- `avg_window`로 이동평균 필터 적용
- 온도/습도 범위 설정 (`range_min`, `range_max`)
- `/api/env_monitor` API 제공
- Chart.js 기반 듀얼축 그래프 표시

---

## 🔧 4. JSON 설정 구조 (완전체)

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
      "days": [1,1,1,1,1,0,0],
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
  }
}


---

🧠 5. 주요 구조적 특징

항목	설명

조합형 제어	스케줄 + 모션 감지 + 센서 입력 병행 가능
평균화 구조	avg_window로 펌웨어 측 데이터 안정화
시각화 구조	chart_options로 웹 그래프 설정
정적 리소스 로딩	JS/CSS 동적 로딩 제거 → HTML에서 직접 참조
BLE 장치 관리	등록/비활성/삭제 지원, MAC 기반 신호 감지
NTP 기반 동작	시스템 시간이 항상 동기화되어 스케줄 오차 최소화



---

🚀 6. 향후 확장 계획 (v6.x)

카테고리	예정 기능

🌤️ 환경센서 확장	조도(lux), 기압(hPa), VOC 등 추가
🪄 스마트 반응 제어	온도/습도 급변 시 자동 속도 조절
🔔 알림 시스템	온도·RPM 임계 초과 시 Toast/Log 표시
📡 OTA 업데이트	/api/update 엔드포인트 제공
💾 데이터 내보내기	LittleFS 데이터 CSV 백업 기능
🎛️ 대시보드 UI	팬·센서·스케줄 상태 통합 뷰 제공



---

이 명세서는 WindScape v5.8 통합 설정 및 구조 기준 문서입니다.
이 문서를 기반으로 펌웨어, 웹UI, ConfigManager를 모두 복원 및 확장 가능합니다.


---

원하신다면 다음 단계로
✅ C10_ConfigManager_013.h (이 JSON 구조의 파싱 및 기본값 생성 코드)
를 Markdown 형식으로 생성해드릴까요?
