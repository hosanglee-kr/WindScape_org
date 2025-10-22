좋습니다 👍
WindScape v5.9의 설정(JSON) 구조는 매우 방대해졌기 때문에,
LittleFS 용량 절약·유지보수·확장성 관점에서 **“Config JSON 분리 전략”**을 설계하는 것이 현명합니다.
아래는 실제 ESP32 + LittleFS 기반 펌웨어에서 검증된 JSON 파일 분리 방안 검토서입니다.


---

🧩 WindScape 설정 파일 분리 방안 검토 (v5.9~v6.0 대응)

1️⃣ 분리의 필요성

문제점	설명

⚠️ 단일 config.json 크기 증가	시뮬레이션 + 스케줄 + 센서 + 모션 + 모니터링 + 보안 항목 등 4000~8000B 수준
💾 LittleFS 제한	ESP32 기본 1MB ~ 2MB 할당 시, HTML/CSS/JS 포함 → 여유 공간 부족
🧩 구조 복잡성	한 JSON에 다수의 중첩 객체 존재 → 파싱 시 RAM 점유 증가, 유지보수 어려움
🔄 변경 단위 불균형	스케줄만 변경해도 전체 파일 재기록 → Flash wear 증가


→ 결론: 논리적으로 독립된 기능별 JSON 분리 필요.


---

2️⃣ 제안하는 파일 구조 (분리 설계안)

파일명	주요 내용	로딩 타이밍	변경 빈도	비고

/config/system.json	펌웨어 버전, 장치명, 시간대, NTP, 보안 키	부팅 시 1회 로드	거의 없음	핵심 기본정보
/config/network.json	Wi-Fi 모드, STA 목록, AP 정보	부팅 시 1회	드묾	WiFiManager 연동
/config/sim.json	풍속·프리셋·난류·온도제어 관련	매 loop 캐싱	중간	S10_Simulation 사용
/config/schedule.json	스케줄 및 세그먼트	사용 시점 로드	잦음	사용자 편집 대상
/config/motion.json	PIR/BLE 설정	부팅 시 1회	가끔	모션 제어 모듈
/config/env.json	DHT22 등 환경 모니터링	부팅 시 1회	중간	센서 추가 대비
/config/fan.json	팬 모니터링 설정	부팅 시 1회	낮음	PWM/Tach 제어용
/config/ui.json	WebUI 차트·대시보드 옵션	웹 로딩 시	중간	시각화용 별도
/config/thermal.json	온도 기반 제어(체감/대류)	시뮬레이션 시점 로드	중간	v5.9 신규 항목 분리 권장
/config/backup.json	마지막 저장본 백업	자동 생성	자동	복원용



---

3️⃣ 펌웨어 구조 변화 제안

🔸 ConfigManager 리팩토링 구조

// C10_ConfigManager_013.h (예시)
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

> ✅ 장점

기능별 JSON 분리로 RAM 사용량 최소화

일부 모듈 오류 발생 시 전체 설정 무효화 방지

OTA 업데이트 시 변경 영향 최소화





---

4️⃣ WebUI 연동 개선 방안

기능	설명

/api/config/system	시스템 설정 조회/저장
/api/config/sim	바람/프리셋 파라미터 조회/수정
/api/config/schedule	스케줄 전용 CRUD API
/api/config/thermal	온도 기반 제어 UI 연결
/api/config/export	모든 설정 JSON 병합 다운로드
/api/config/import	외부 JSON 업로드 후 병합 저장


→ 웹 대시보드에서 탭별 구성으로 나누어 사용자 접근 단순화.


---

5️⃣ JSON 분리 로딩 시퀀스 (권장 플로우)

┌─────────────────────────────────────────────────────────┐
│                     BOOT SEQUENCE                      │
├─────────────────────────────────────────────────────────┤
│ Step 1. loadSystem() → meta/time/security              │
│ Step 2. loadNetwork() → WiFi 연결                      │
│ Step 3. loadSimulation() / loadThermal()               │
│ Step 4. loadMotion(), loadEnv(), loadFan()             │
│ Step 5. loadSchedule() (비동기/지연 로딩)              │
│ Step 6. 백업 파일 확인 후 필요 시 복원                 │
└─────────────────────────────────────────────────────────┘


---

6️⃣ 백업 및 무결성 관리

항목	구현 제안	설명

🧠 CRC32	각 JSON 파일 저장 시 CRC32 필드 추가	파일 무결성 검증
💾 자동 백업	saveAll() 호출 시 /backup/*.bak 자동 생성	비정상 종료 대비
🔁 복원 루틴	load*() 실패 시 동일 경로 .bak 복구	데이터 안정성 확보
🪶 버전 호환 관리	meta.version 기반 구조 자동 마이그레이션	구조 변경 시 자동 정렬



---

7️⃣ 운영 측면 권장 사항

system.json과 network.json은 변경 거의 없음 → Flash wear 방지용 별도 구역 저장

schedule.json, thermal.json은 유저 변경 많음 → 주기적 백업

thermal.json은 단독 테스트 및 실시간 튜닝용으로 UI에서 따로 노출 가능



---

✅ 요약

목표	실행 방안

구조 단순화	기능별 JSON 분리 (system/network/sim/schedule 등)
메모리 절감	필요 시점별 on-demand 로드
안정성 향상	백업/CRC/에러 복원 체계 구축
확장성 강화	향후 센서·MQTT·BLE 확장 시 병합 최소화
유지보수 용이	각 모듈의 Config 독립 관리, 코드 분할 구조 개선



---

원하신다면 다음 단계로
💡 “WindScape v6.0 JSON 분리 설계서” (각 파일 구조 + 예시 JSON + ConfigManager 계층 다이어그램)
형식으로 작성해드릴까요?
예: /config/thermal.json, /config/schedule.json 각각의 실제 JSON 예시 포함.
