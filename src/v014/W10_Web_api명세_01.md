### 🌐 1. 시스템 및 버전 (System & Version)

* **API:** /api/version
* **메서드:** GET
* **기능:** 장치의 현재 펌웨어 버전 및 빌드 정보를 조회
* **소스 파일:** W10_Web_Routes_024.cpp (routeVersion)
* **요청:** (없음)
* **응답:** {"module": "SmartNatureWind", "fw": "V024", ...}
---
* **API:** /api/system
* **메서드:** GET
* **기능:** 장치의 시스템 설정(장치 이름, 보안 키 등)을 조회
* **소스 파일:** W10_Web_Routes_024.cpp (routeSystem)
* **요청:** (없음)
* **응답:** {"system": {...}}
---
* **API:** /api/system
* **메서드:** POST
* **기능:** 장치의 시스템 설정을 업데이트
* **소스 파일:** W10_Web_Routes_024.cpp (routeSystem)
* **요청:** {"system": {...}}
* **응답:** {"success": true}
---
* **API:** /api/config/init
* **메서드:** POST
* **기능:** 모든 설정을 기본값으로 초기화하고 재부팅
* **소스 파일:** W10_Web_Routes_024.cpp (routeConfigInit)
* **요청:** (없음)
* **응답:** {"factory": true}
---
* **API:** /api/reload
* **메서드:** POST
* **기능:** 현재 설정 파일(LittleFS)을 다시 읽고 적용
* **소스 파일:** W10_Web_Routes_024.cpp (routeReload)
* **요청:** (없음)
* **응답:** {"result": "ok"}
---

### 📡 2. 네트워크 및 상태 (Network & Diagnostics)

* **API:** /api/state
* **메서드:** GET
* **기능:** 장치의 실시간 상태(Wi-Fi, 센서, 현재 제어 모드)를 조회
* **소스 파일:** W10_Web_Routes_024.cpp (routeState)
* **요청:** (없음)
* **응답:** {"state": {...}, "motion": {...}}
---
* **API:** /api/wifi
* **메서드:** GET
* **기능:** Wi-Fi 연결 상태 및 설정 정보를 조회
* **소스 파일:** W10_Web_Routes_024.cpp (routeWifi)
* **요청:** (없음)
* **응답:** {"wifi": {...}}
---
* **API:** /api/wifi
* **메서드:** POST
* **기능:** Wi-Fi 설정을 업데이트하고 연결 시도
* **소스 파일:** W10_Web_Routes_024.cpp (routeWifi)
* **요청:** {"wifi": {...}}
* **응답:** {"success": true}
---
* **API:** /api/scan
* **메서드:** GET
* **기능:** 주변 Wi-Fi 네트워크 목록을 스캔 및 조회
* **소스 파일:** W10_Web_Routes_024.cpp (routeScan)
* **요청:** (없음)
* **응답:** {"scan": [...]}
---
* **API:** /api/diag
* **메서드:** GET
* **기능:** 메모리 사용량, 파일 시스템 상태 등 진단 정보를 조회
* **소스 파일:** W10_Web_Routes_024.cpp (routeDiag)
* **요청:** (없음)
* **응답:** {"heap": ..., "fs_used": ..., "fs_total": ...}
---
* **API:** /api/logs
* **메서드:** GET
* **기능:** 저장된 시스템 로그의 최근 내용을 조회
* **소스 파일:** W10_Web_Routes_024.cpp (routeLogs)
* **요청:** (없음)
* **응답:** {"logs": ["...", "..."]}
---
* **API:** /api/metrics
* **메서드:** GET
* **기능:** 장치의 성능 지표(CPU, 태스크 등)를 조회
* **소스 파일:** W10_Web_Routes_024.cpp (routeMetrics)
* **요청:** (없음)
* **응답:** {"metrics": {...}}
---

### 💨 3. 제어 및 프로파일 (Control & Profiles)

* **API:** /api/control/summary
* **메서드:** GET
* **기능:** 현재 활성화된 제어 상태(오버라이드, 프로파일) 및 메트릭스를 요약 조회
* **소스 파일:** W10_Web_Routes_024.cpp (routeControlSummary)
* **요청:** (없음)
* **응답:** {"summary": {...}, "metrics": {...}}
---
* **API:** /api/control/profile/select
* **메서드:** POST
* **기능:** 특정 사용자 프로파일을 선택하여 실행
* **소스 파일:** W10_Web_Routes_024.cpp (routeControl)
* **요청:** (URL Query) ?id={profile_no}
* **응답:** {"result": "ok"}
---
* **API:** /api/control/profile/stop
* **메서드:** POST
* **기능:** 현재 실행 중인 사용자 프로파일을 정지
* **소스 파일:** W10_Web_Routes_024.cpp (routeControl)
* **요청:** (없음)
* **응답:** {"result": "ok"}
---
* **API:** /api/control/override/fixed
* **메서드:** POST
* **기능:** 지정된 시간(초) 동안 고정 속도(퍼센트)로 오버라이드
* **소스 파일:** W10_Web_Routes_024.cpp (routeControl)
* **요청:** (URL Query) ?percent={0.0~1.0}&seconds={duration}
* **응답:** {"result": "ok"}
---
* **API:** /api/control/override/preset
* **메서드:** POST
* **기능:** 지정된 시간(초) 동안 특정 Preset/Style로 오버라이드 (JSON Body 사용)
* **소스 파일:** W10_Web_Routes_024.cpp (routeControl)
* **요청:** {"presetCode": "CODE", "styleCode": "STYLE", "durationSec": 60, "adjust": {...}}
* **응답:** {"result": "ok"}
---
* **API:** /api/control/override/clear
* **메서드:** POST
* **기능:** 현재 적용 중인 오버라이드(Fixed/Preset)를 해제
* **소스 파일:** W10_Web_Routes_024.cpp (routeControl)
* **요청:** (없음)
* **응답:** {"result": "ok"}
---
* **API:** /api/windProfile
* **메서드:** GET
* **기능:** 저장된 바람 Preset 및 Style 목록을 조회
* **소스 파일:** W10_Web_Routes_024.cpp (routeWindProfile)
* **요청:** (없음)
* **응답:** {"windProfile": {"presets": [...], "styles": [...]}}
---
* **API:** /api/schedules
* **메서드:** GET
* **기능:** 스케줄 목록 설정을 조회
* **소스 파일:** W10_Web_Routes_024.cpp (routeSchedules)
* **요청:** (없음)
* **응답:** {"schedules": [...]}
---
* **API:** /api/schedules
* **메서드:** POST
* **기능:** 스케줄 목록 설정을 수정 및 저장 (JSON Patch 방식)
* **소스 파일:** W10_Web_Routes_024.cpp (routeSchedules)
* **요청:** {"schedules": [...]}
* **응답:** {"updated": true/false}
---
* **API:** /api/user_profiles
* **메서드:** GET
* **기능:** 사용자 프로파일 목록 설정을 조회
* **소스 파일:** W10_Web_Routes_024.cpp (routeUserProfiles)
* **요청:** (없음)
* **응답:** {"userProfiles": {"profiles": [...]}}
---
* **API:** /api/user_profiles
* **메서드:** POST
* **기능:** 사용자 프로파일 목록 설정을 수정 및 저장 (JSON Patch 방식)
* **소스 파일:** W10_Web_Routes_024.cpp (routeUserProfiles)
* **요청:** {"userProfiles": {"profiles": [...]}}
* **응답:** {"updated": true/false}
---

### 🚶 4. 모션 및 시뮬레이션 (Motion & Simulation)

* **API:** /api/motion
* **메서드:** GET
* **기능:** 모션 감지 로직 설정(PIR, BLE)을 조회
* **소스 파일:** W10_Web_Routes_024.cpp (routeMotion)
* **요청:** (없음)
* **응답:** {"motion": {...}}
---
* **API:** /api/motion
* **메서드:** POST
* **기능:** 모션 감지 로직 설정을 업데이트
* **소스 파일:** W10_Web_Routes_024.cpp (routeMotion)
* **요청:** {"motion": {...}}
* **응답:** {"success": true}
---
* **API:** /api/motion/pir/feed
* **메서드:** POST
* **기능:** 외부 소스에서 PIR 감지 상태(True/False)를 주입
* **소스 파일:** W10_Web_Routes_024.cpp (routeMotionFeed)
* **요청:** {"pir": true/false}
* **응답:** {"fed": true, "type": "pir"}
---
* **API:** /api/motion/ble/feed
* **메서드:** POST
* **기능:** 외부 소스에서 BLE 감지 상태(True/False)를 주입
* **소스 파일:** W10_Web_Routes_024.cpp (routeMotionFeed)
* **요청:** {"ble": true/false}
* **응답:** {"fed": true, "type": "ble"}
---
* **API:** /api/simulation
* **메서드:** POST
* **기능:** 장치를 시뮬레이션 모드로 전환하거나 설정 업데이트
* **소스 파일:** W10_Web_Routes_024.cpp (routeSimulation)
* **요청:** {"simulate": true, ...}
* **응답:** {"success": true}
---
* **API:** /api/sim/state
* **메서드:** GET
* **기능:** 시뮬레이션 상태 및 가상 센서 값을 조회
* **소스 파일:** W10_Web_Routes_024.cpp (routeSimState)
* **요청:** (없음)
* **응답:** {"sim": {...}, "summary": {...}}
---
* **API:** /api/sim/chart
* **메서드:** GET
* **기능:** 현재 시뮬레이션/제어 데이터 기반 차트 데이터를 조회
* **소스 파일:** W10_Web_Routes_024.cpp (routeSimulation)
* **요청:** (없음)
* **응답:** {"chart": [...]}
---

### ⏫ 5. 파일 및 펌웨어 업데이트 (Upload & Update)

* **API:** /upload
* **메서드:** POST
* **기능:** LittleFS에 정적 파일(Web UI 등)을 업로드
* **소스 파일:** W10_Web_Upload_024.cpp (routeUpload)
* **요청:** (multipart/form-data)
* **응답:** {"done": true}
---
* **API:** /update
* **메서드:** POST
* **기능:** OTA(Over-the-Air) 펌웨어 업데이트 수행
* **소스 파일:** W10_Web_Upload_024.cpp (routeUpdate)
* **요청:** (multipart/form-data - 펌웨어 바이너리)
* **응답:** {"ota": "ok"} 또는 {"ota": "fail", "error": "..."}
---

### 💬 6. WebSocket 엔드포인트

* **WS:** /ws/state
* **기능:** 장치의 실시간 상태 및 센서 값 업데이트를 수신
* **소스 파일:** W10_Web_WS_024.cpp (routeWebSocket)
* **데이터:** JSON 객체 (예: {"state": {...}, "motion": {...}})
---
* **WS:** /ws/log
* **기능:** 장치의 실시간 시스템 로그 출력을 수신
* **소스 파일:** W10_Web_WS_024.cpp (routeWebSocket)
* **데이터:** JSON 객체 (예: {"log": "INFO", "msg": "..."})
---
* **WS:** /ws/chart
* **기능:** 차트/그래프 데이터(바람 세기, 변동성 등)를 수신
* **소스 파일:** W10_Web_WS_024.cpp (routeWebSocket)
* **데이터:** JSON 객체
---
* **WS:** /ws/metrics
* **기능:** CPU, 메모리, 태스크 등 성능 지표를 수신
* **소스 파일:** W10_Web_WS_024.cpp (routeWebSocket)
* **데이터:** JSON 객체 (예: {"metrics": {...}})
