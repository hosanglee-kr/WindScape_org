물론입니다 👍
아래는 지금까지의 모든 요구사항을 Markdown 포맷 완성본으로 정리한 문서입니다.
이 버전은 바로 GitHub나 Notion, 문서 리포트에 그대로 복사해도 구조가 깨지지 않습니다.


---

🌿 WindScape 시스템 통합 요구사항 명세서 (v5.8 기준)

버전: SC10_FW_1.0.6
디바이스: WindScape_XY-SK10
최종 수정일: 2025-10-20
작성 목적:
ESP32 기반 자연풍 시뮬레이터 WindScape의 펌웨어, 웹 UI, 설정 JSON 구조 및 기능 확장 요구사항을 체계적으로 정의한다.


---

🧩 1. 시스템 개요

WindScape는 일반 선풍기를 자연풍 시뮬레이터로 전환하는 스마트 컨트롤러다.
ESP32S3를 기반으로 하며, Web UI를 통해 시뮬레이션, 스케줄, 모션감지, 환경센서 등을 통합 제어한다.


---

⚙️ 2. 주요 구성 모듈

모듈명	파일명	주요 기능

상수 정의	A10_Const_007.h	Wi-Fi, WEB, Phase, Preset 정의
설정 관리자	C10_ConfigManager_007.h	JSON 로드/저장/백업/공장 초기화
Wi-Fi 관리자	M10_WiFiManager_007.h	AP/STA 모드 및 Multi-STA 지원
PWM 제어기	P10_PWM_ctrl_005.h	팬 듀티, 주파수, 분해능 제어
시뮬레이터	S10_Simulation_007.h	Phase/난류/열기포/프리셋 적용
팬 모니터	S10_FanMonitor_001.h	PWM Duty/Tach RPM 측정 및 차트 표시
환경 모니터	S10_EnvMonitor_002.h	DHT22 온습도 센서 측정 및 평균화
웹 API	W10_WebAPI_007.h	/api/* 엔드포인트 관리
통합 실행 엔진	WS10_Main_007.h	초기화, 주기 실행, 로그 관리



---

🕹️ 3. 핵심 기능 요약

3.1 🌬️ 자연풍 시뮬레이션

Von Kármán 기반 난류 모델

프리셋(preset) 또는 고정 풍속(fixed_speed) 모드 지원

intensity, variability, gust_freq, thermal 값 조정 가능



---

3.2 📶 Wi-Fi 및 Web UI

AP / STA / AP+STA 모드 선택 가능

Web UI 정적 로딩 방식 (SC10_main_017.html 직접 참조)

API Key 인증, CORS, No-Cache 헤더 적용

/api/state, /api/config, /api/web, /api/logs 제공



---

3.3 ⏰ 스케줄 관리 기능

NTP 연동으로 시간 동기화 (ntp_server, timezone)

복수 스케줄 등록 가능 (id: 10부터 10씩 증가)

요일별 활성화 (days: [1,1,1,1,1,0,0])

각 스케줄에 여러 개의 세그먼트(segment) 등록 가능

세그먼트별:

mode: "preset", "fixed", "off"

seq_no, enabled, duration_minutes

preset_name / fixed_speed / preset_options 지정 가능


프리셋 모드시: intensity, variability, turbulence 조정



---

3.4 🚶 모션 감지 기능

PIR 센서 + BLE 신호세기 감지

두 방식은 OR 조건으로 동작

공통 설정:

scan_interval_sec, hold_sec


개별 설정:

PIR: pin, debounce_sec

BLE: rssi_threshold, devices[]


BLE 등록된 디바이스를 감지하면 팬 작동 유지



---

3.5 🌀 팬 모니터링 (fan_monitor)

PWM Duty / Tachometer RPM 동시 표시

PWM, Tach 개별 활성화 가능

avg_window 적용으로 평균값 표시

/api/fan_monitor 엔드포인트 제공

Chart.js 기반 그래프 표시 (dual_axis, smooth_window 지원)



---

3.6 🌡️ 환경 모니터링 (env_monitor)

DHT22 센서 기반 온도/습도 측정

avg_window로 이동평균 필터 적용

range_min / range_max 설정

/api/env_monitor API로 데이터 제공

Chart.js 기반 듀얼축 그래프 표시



---

🧱 4. 설정 JSON 구조 (완전체 예시)

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
