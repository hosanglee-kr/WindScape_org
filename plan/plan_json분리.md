# 🌿 WindScape 설정 JSON 파일 분리 방안 검토서 (v5.9 → v6.0)

---

## 🧩 1️⃣ 분리의 필요성

| 문제점 | 설명 |
|---------|------|
| ⚠️ 단일 `config.json` 크기 증가 | 시뮬레이션 + 스케줄 + 센서 + 모션 + 보안 항목 등 4000~8000B 수준 |
| 💾 LittleFS 용량 제한 | ESP32 기본 1~2MB 할당 시, HTML/CSS/JS 포함 시 여유 공간 부족 |
| 🧩 구조 복잡성 | 중첩 객체 많아 파싱 시 RAM 점유 증가, 유지보수 어려움 |
| 🔄 변경 단위 불균형 | 스케줄 수정만으로 전체 파일 재기록 → Flash wear 증가 |

👉 **결론:** 논리적으로 독립된 기능별 JSON 분리 필요

---

## ⚙️ 2️⃣ 제안 파일 구조

| 파일명 | 주요 내용 | 로딩 타이밍 | 변경 빈도 | 비고 |
|---------|------------|-------------|-------------|------|
| `/config/system.json` | 버전, 장치명, 시간대, NTP, 보안 키 | 부팅 시 | 거의 없음 | 시스템 기본정보 |
| `/config/network.json` | Wi-Fi 모드, STA/AP 설정 | 부팅 시 | 드묾 | WiFiManager 연동 |
| `/config/sim.json` | 바람 시뮬레이션 파라미터 | 초기화 시 | 중간 | S10_Simulation 사용 |
| `/config/schedule.json` | 스케줄 및 세그먼트 | 사용 시점 로드 | 잦음 | 사용자 편집 대상 |
| `/config/motion.json` | PIR/BLE 감지 설정 | 부팅 시 | 가끔 | 모션 모듈 |
| `/config/env.json` | 온·습도 모니터링 | 부팅 시 | 중간 | 센서 추가 대비 |
| `/config/fan.json` | 팬 모니터링 | 부팅 시 | 낮음 | PWM/Tach 설정 |
| `/config/thermal.json` | 온도 기반 제어 (체감/대류) | 시뮬레이션 시 | 중간 | v5.9 신규 항목 |
| `/config/ui.json` | WebUI/대시보드 옵션 | 웹 로딩 시 | 중간 | 차트/테마 |
| `/config/backup.json` | 백업 데이터 | 자동 생성 | 자동 | 복구용 |

---

## 🧠 3️⃣ 펌웨어 구조 리팩토링 제안

```cpp
// C10_ConfigManager_013.h (분리 설계 예시)
class ConfigManager {
public:
  bool loadSystem();
  bool loadNetwork();
  bool loadSimulation();
  bool loadSchedule();
  bool loadMotion();
  bool loadEnv();
  bool loadFan();
  bool loadThermal();
  
  bool saveSystem();
  bool saveSchedule();
  bool saveAll();  // 전체 저장 시 백업 자동 생성

private:
  bool readJsonFile(const char* path, JsonDocument& doc);
  bool writeJsonFile(const char* path, const JsonDocument& doc);
};

✅ 장점

RAM 절약(필요 파일만 on-demand 로딩)

모듈별 오류 격리(한 파일 손상 → 전체 기능 마비 방지)

OTA 업데이트 영향 최소화

설정 변경 시 Flash wear 감소



---

🌐 4️⃣ WebAPI / UI 연동 구조

API 경로	기능	설명

/api/config/system	시스템 설정 조회/저장	버전, NTP, 보안
/api/config/network	네트워크 설정	Wi-Fi 모드, STA 목록
/api/config/sim	시뮬레이션 파라미터 제어	강도, 변동성, 프리셋
/api/config/thermal	온도 기반 제어 설정	체감/대류 파라미터
/api/config/schedule	스케줄 CRUD	등록/수정/삭제
/api/config/env	센서 상태	DHT, 온습도 범위
/api/config/fan	팬 모니터링	PWM/Tach
/api/config/export	전체 설정 병합 다운로드	백업용
/api/config/import	외부 설정 업로드	복원용


👉 WebUI에서는 탭 구조로 각 설정 파일별 페이지 구성 권장:

시스템

네트워크

자연풍 시뮬레이션

온도제어(체감/대류)

스케줄

센서

팬모니터

고급설정



---

🔁 5️⃣ 로딩 시퀀스 (권장 부팅 플로우)

┌──────────────────────────────────────────┐
│             WindScape Boot Flow          │
├──────────────────────────────────────────┤
│ Step 1. loadSystem() → meta/time/security│
│ Step 2. loadNetwork() → Wi-Fi 연결 시도  │
│ Step 3. loadSimulation() / loadThermal() │
│ Step 4. loadMotion(), loadEnv(), loadFan()│
│ Step 5. loadSchedule() (지연 로딩 가능)  │
│ Step 6. 백업 파일 확인 및 필요시 복원    │
└──────────────────────────────────────────┘


---

💾 6️⃣ 백업 및 무결성 관리

항목	제안	설명

✅ CRC32 무결성 필드	저장 시 각 JSON에 "crc32": "xxxx" 추가	파일 손상 검증
💾 자동 백업 생성	saveAll() 시 /backup/*.bak 자동 저장	비정상 종료 대비
🔁 복원 루틴	load*() 실패 시 .bak 복원	데이터 안정성
🧠 버전 자동 마이그레이션	meta.version 기반 변환 로직 추가	구조 변경 호환성
🔐 보안키 해시 저장	SHA256으로 저장 후 비교	보안 강화



---

🧩 7️⃣ 추가 기술 검토

영역	개선 방향	설명

🔥 온도제어 반응 최적화	히스테리시스(±0.3°C) 도입	불필요한 듀티 변동 억제
🌬️ Variability-Temperature 연동	온도 상승 시 변동성 증가	자연스러운 체감 구현
🪶 BLE 장치 관리 확장	devices.json 분리 저장	페어링 수 증가 대비
🧱 Config 기본값 복원 기능	factoryResetDefaults() 통합	모듈별 기본값 로드 가능
☁️ 클라우드 확장 준비	MQTT/WebSocket 연동	원격 모니터링 확장



---

📊 8️⃣ JSON 구조 예시 (thermal.json)

{
  "enable_thermal_fan_boost": true,
  "thermal_fan_boost_base_temp": 25.0,
  "thermal_fan_boost_per_degree": 0.5,
  "enable_thermal_freq_boost": true,
  "thermal_freq_boost_base_temp": 25.0,
  "thermal_freq_boost_per_degree": 4.0,
  "hysteresis": 0.3,
  "last_update": "2025-10-21T15:00:00+09:00"
}


---

🧩 9️⃣ 장점 요약

분류	효과

🧱 구조적 안정성	기능별 독립 관리, 오류 격리
💾 메모리 절감	필요한 JSON만 동적 로딩
🔁 내구성 향상	Flash 쓰기 최소화
🧠 유지보수 용이	JSON 스키마 버전별 확장 용이
🌐 확장성 강화	MQTT·BLE·UI 확장에 적합



---

✅ 10️⃣ 요약

목표	실행 전략

가독성 향상	기능별 JSON 분리 관리
안정성 강화	CRC·백업·복원 체계 도입
확장성 확보	thermal_control 등 독립 파일화
RAM 절약	필요 시점별 on-demand 로딩
유지보수 단순화	ConfigManager 세분화 및 API 정리



---

🔜 다음 단계 제안

> 💡 “WindScape v6.0 JSON 분리 설계서” 작성:

각 JSON 파일별 구조 정의

ConfigManager 클래스 계층 다이어그램

저장/로드 시퀀스

예시 JSON 및 에러 복원 흐름


요청 시 전체 Markdown 문서 형식으로 생성 가능.
