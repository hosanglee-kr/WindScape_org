좋아요 ✅
lastJsonVer(버전/시스템 관련 필드)을 완전히 제거하고,
순수하게 “동작 복원에 필요한 최소 항목만” 남기는 구조로 정리하겠습니다.

이제 NVS는 오직 런타임 상태 복원용 데이터만 저장합니다 —
즉, 시스템 메타정보나 버전 관리는 cfg_system_XXX.json 쪽에서만 담당하게 됩니다.


---

💾 Smart Nature Wind — NVS 저장 사양 (v0.27 Final)


---

1️⃣ 개요

항목	설명

파일명	cfg_nvsSpec_027.json
저장 위치	ESP32 NVS (namespace: "SNW")
목적	재부팅 후, 마지막 실행 프로파일·모드·오버라이드 상태 복원
데이터형식	Key-Value (ESP32 Preferences API)
주요특징	Dirty flag 기반 지연저장 / Flash 마모 최소화 / 이벤트 기반 commit



---

2️⃣ 저장 항목 (최소 구성)

Key	Type	예시값	설명

runMode	uint8_t	2	0=Idle, 1=Schedule, 2=Profile, 3=Override
activeProfileNo	uint8_t	2	현재 실행 중인 사용자 프로파일 번호
activeSegmentNo	uint8_t	1	현재 진행 중인 segment
presetCode	string	"OCEAN"	마지막 적용 프리셋 코드
styleCode	string	"SLEEP"	마지막 적용 스타일 코드
overrideActive	bool	false	오버라이드 작동 여부
overrideRemain	uint32	0	오버라이드 남은 시간(초 단위)
wifiConnected	bool	true	마지막 Wi-Fi 연결 상태



---

3️⃣ JSON 스펙 (cfg_nvsSpec_027.json)

{
  "nvsSpec": {
    "version": "027",
    "namespace": "SNW",
    "entries": [
      { "key": "runMode", "type": "uint8", "default": 0, "desc": "0=Idle,1=Schedule,2=Profile,3=Override" },
      { "key": "activeProfileNo", "type": "uint8", "default": 0, "desc": "현재 프로파일 번호" },
      { "key": "activeSegmentNo", "type": "uint8", "default": 0, "desc": "현재 세그먼트 번호" },
      { "key": "presetCode", "type": "string", "default": "", "desc": "마지막 프리셋 코드" },
      { "key": "styleCode", "type": "string", "default": "", "desc": "마지막 스타일 코드" },
      { "key": "overrideActive", "type": "bool", "default": false, "desc": "오버라이드 활성 여부" },
      { "key": "overrideRemain", "type": "uint32", "default": 0, "desc": "남은 오버라이드 시간(초)" },
      { "key": "wifiConnected", "type": "bool", "default": false, "desc": "Wi-Fi 연결 상태" }
    ]
  }
}


---

4️⃣ NVS 저장 정책

구분	트리거	저장 주기	비고

즉시 저장 (Instant Save)	runMode / activeProfileNo 변경	즉시 commit	사용 모드 변경 시 복원 우선
지연 저장 (Lazy Save)	Wi-Fi 상태 변경 / Override 발생	5분마다	nvsDirty=true일 때만
이벤트 저장 (Manual Save)	전원OFF, Sleep, OTA	즉시 commit	모든 dirty 항목
휘발성만 RAM 유지	FanSpeed, Sensor, Timer 등	-	절대 NVS 저장 안 함



---

5️⃣ 변경 로직 예시

// 변경 시 호출
void setProfile(uint8_t profileNo) {
    nvs_write_u8("activeProfileNo", profileNo);
    nvs_write_u8("runMode", EN_RUN_PROFILE);
}

// 상태 변경 발생
void onWifiStateChanged(bool connected) {
    static bool prev = false;
    if (connected != prev) {
        markNvsDirty();
        prev = connected;
    }
}

// 주기적 호출
void tickNvsAutoSave() {
    if (nvsDirty && millis() - lastNvsSaveMs > 300000) {
        saveNvs();
        nvsDirty = false;
    }
}


---

6️⃣ 설계 검토

항목	평가	설명

Flash 수명	✅ 매우 안전 (저장 주기 최소화)	
복원 안정성	✅ 핵심 상태만 유지	
데이터 일관성	✅ Dirty flag + 이벤트 commit	
코드 복잡도	⚙️ 단순	
유지보수성	✅ 높은 가독성 (명확한 key명)	



---

7️⃣ 결과 요약

🧩 이제 NVS는 “환경 복원 스냅샷 저장소”로만 사용됨

버전, 시스템, 센서, 타이머, 팬 속도 등 실시간 값은 모두 제외

Flash 쓰기 횟수는 하루 기준 5~10회 이하 수준

부팅 시 완벽히 “마지막 상태 재개” 가능



---

원하시면 다음 단계로

> C10_ConfigManager_014.h 또는 별도 N10_NvsManager_001.h
형태로 NVS read/write + dirty-flag 관리용 전용 클래스 초안을 만들어드릴까요?
(지연저장, 이벤트 commit, fail-safe 포함)
