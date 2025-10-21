

리코컨, 앱 설치 없이 스마트폰으로 사용 가능

미로 mf02 자연풍 선풍기 기능 밴치마킹
  - 타이머 기능 on/off 시간 설정
  - 움직임 감지 작동여부 옵션
  - 풍량 세게 100 단계

🧭 WindScape 확장 요구사항 정리 (v5.8 기준)

> 본 문서는 지금까지 논의된 모든 확장 요구사항을 정리 및 기록용으로 문서화한 명세입니다.
동일한 내용을 이후에도 재요청 시 이 문서만 참조하면 전체 구조와 기능 의도를 정확히 이해할 수 있습니다.




---

📦 1. 설정 JSON 구조 개요

파일 버전: SC10_FW_1.0.6

주요 목적:

자연풍 시뮬레이터의 Wi-Fi, PWM, 스케줄, 모션, 센서, 로깅 등을 통합 관리

프론트엔드/백엔드 공용으로 사용 가능한 구조 설계

확장성과 모듈 간 일관성 유지




---

🧱 2. 주요 확장 요구사항 요약

🕒 [A] 스케줄 관리

다중 스케줄 등록 가능

schedules[] 배열 기반, 각 스케줄별 id와 name 보유

id는 10부터 시작, 이후 10씩 증가


활성 여부 제어: enabled: true/false

요일 설정: [월~일] 배열 (1:활성, 0:비활성)

시간대 설정: start / end (HH:MM 포맷)

세그먼트 구조:

한 스케줄 내 복수의 segments[] 존재

각 세그먼트는 seq_no로 순서 지정

enabled: 개별 세그먼트 활성 여부

mode: "preset", "fixed", "off"

Preset 모드 시 옵션:

preset_name: 프리셋 명칭

preset_options: intensity, variability, turbulence


Fixed 모드 시 옵션:

fixed_speed: 0~100%


Off 모드는 별도 세그먼트로 구분

시간 단위: duration_minutes


스케줄 사용 여부 전역 제어 추가: schedule_enabled



---

👁️ [B] 모션 감지

모드: PIR / BLE / 둘 다 (OR 조건)

공통 설정:

enabled: 전체 감지 기능 활성화

scan_interval_sec: BLE/PIR 감지 주기

hold_sec: 감지 후 유지 시간


PIR 설정:

pin: 입력핀 번호

debounce_sec: 신호 안정화 시간


BLE 설정:

enabled: BLE 감지 활성화 여부

rssi_threshold: 감지 신호 세기 기준

devices[]: 등록된 BLE 장치 목록

mac, alias, enabled



조합 동작: PIR 또는 BLE 중 하나라도 감지되면 on (OR 조건)



---

🌡️ [C] 환경 모니터 (env_monitor)

기능 목적: 온도·습도 모니터링 및 그래프 표시

센서 측정용 필드:

type: "DHT22", "SHT31" 등 센서 유형

pin: 데이터 핀 번호

interval_sec: 측정 간격

records_max: 최대 저장 개수

avg_window: 이동평균 적용 크기


하위 항목:

temp: 온도 센서 세부 설정

unit: "C" / "F"

range_min, range_max


humid: 습도 센서 세부 설정

unit: "%", range_min, range_max



차트 옵션:

display_temp, display_humid

refresh_interval_sec

smooth_window

dual_axis


추가 검토 옵션 (선택):

sensor_model: 세부 센서명

error_threshold: 급격한 변동 시 필터링

records_persist: 부팅 후 데이터 유지 여부




---

💨 [D] 팬 모니터 (fan_monitor)

기능 목적: PWM duty 및 Tach 기반 팬 속도 그래프 표시

공통 설정:

enabled

interval_sec

records_max


PWM 하위 항목:

enabled: PWM 기반 모니터링 on/off

min_duty, max_duty


Tach 하위 항목:

enabled: Tach 입력 기반 RPM 계산 on/off

pin: Tach 신호 입력핀

pulses_per_rev: 회전당 펄스 수

rpm_min, rpm_max

avg_window: 최근 n회 평균 필터링


차트 옵션:

display_pwm_duty, display_rpm

refresh_interval_sec

smooth_window

dual_axis




---

🔐 [E] 공통 / 기타

보안 키: security.api_key

로그 설정: system.logging

level: "INFO", "DEBUG", "ERROR"

max_entries: 최대 로그 개수


웹 자산 설정: system.web

html, css, js 파일 경로 지정


NTP 시간 동기화:

time.ntp_server

time.timezone

time.sync_interval_min




---

🧩 3. WindScape JSON 설정 완전체

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
        { "seq_no": 10, "enabled": true, "mode": "preset", "preset_name": "HARBOUR_BREEZE",
          "preset_options": { "intensity": 80, "variability": 60, "turbulence": 45 },
          "duration_minutes": 20 },
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
    "temp": {
      "enabled": true,
      "unit": "C",
      "range_min": -10,
      "range_max": 50
    },
    "humid": {
      "unit": "%",
      "range_min": 0,
      "range_max": 100
    },
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
    "pwm": {
      "enabled": true,
      "min_duty": 0.0,
      "max_duty": 100.0
    },
    "tach": {
      "enabled": true,
      "pin": 27,
      "pulses_per_rev": 2,
      "rpm_min": 200,
      "rpm_max": 3000,
      "avg_window": 5
    },
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

🧩 4. 요약

분류	기능	상태

스케줄	다중등록, 세그먼트 단위 제어, 프리셋/고정/오프 세분화	✅ 완료
모션감지	PIR, BLE, OR 조건, 등록 BLE 관리	✅ 완료
환경모니터	온도/습도 그래프, 평균필터, 차트 옵션	✅ 완료
팬모니터	PWM/Tach, 듀얼축 그래프, 평균필터	✅ 완료
시간/NTP	타임존, 주기적 동기화	✅ 완료
로깅/보안	API Key + 로그 옵션	✅ 완료
웹 자산	HTML/CSS/JS 고정경로	✅ 완료



---

이 문서를 기준으로,
📘 C10_ConfigManager / W10_WebAPI / SC10_main_017.js 개발 시 동일 스펙을 재현할 수 있습니다.

원하신다면 다음 단계로

> 🔹 ConfigManager(013)
🔹 Fan/Env Monitor 백엔드 클래스 설계
🔹 Web UI (그래프/설정 페이지)



중 어느 부분을 다음에 제작할지 선택하시면 바로 이어서 작성드리겠습니다.
