#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : N10_NvsManager_040.h
 * 모듈약어 : N10
 * 모듈명 : Smart Nature Wind NVS Runtime Manager (v017)
 * ------------------------------------------------------
 * 기능 요약:
 * - 런타임 상태를 NVS(Flash)에 안전하게 저장/복원
 * - 현재 실행 모드, 마지막 스케줄/프로파일, AutoOff, Override 메타 관리
 * - Flash 수명 보호를 위한 Dirty Flag + 최소 주기(10초) Flush 정책
 * - C10_ConfigManager 연계로 schedules/userProfiles/system 등 병렬 Dirty Flush 지원
 * ------------------------------------------------------
 * NVS Key 설계 (namespace: "SNW_RUN"):
 * run_mode, run_src, sched_no, uprofile_no, autoOff_en, autoOff_min,
 * ovr_en, ovr_mode, ovr_fixed, ovr_preset, ovr_style
 * ------------------------------------------------------
 * [구현 규칙]
 * - 항상 소스 시작 주석 체계 유지
 * - ArduinoJson v7.x.x 사용 (v6 이하 사용 금지)
 * - JsonDocument 단일 타입만 사용
 * - createNestedArray/Object/containsKey 사용 금지
 * - memset + strlcpy 기반 안전 초기화
 * - 주석/필드명은 JSON/NVS Key 의미와 동일하게 유지
 * - 모듈별 단일 헤더(h)파일로만 구성 (cpp 없음) (<- 이번 분리 요청으로 변경됨)
 * ------------------------------------------------------
 * [코드 네이밍 규칙]
 * - 전역 상수,매크로       : G_모듈약어_ 접두사
 * - 전역 변수              : g_모듈약어_ 접두사
 * - 전역 함수              : 모듈약어_ 접두사
 * - type                   : T_모듈약어_ 접두사
 * - typedef                : _t 접미사
 * - enum 상수              : EN_모듈약어_ 접두사
 * - 구조체                 : ST_모듈약어_ 접두사
 * - 클래스명               : CL_모듈약어_ 접두사
 * - 클래스 private 멤버    : _ 접두사
 * - 클래스 멤버(함수/변수): 모듈약어 접두사 미사용
 * - 클래스 정적 멤버       : s_ 접두사
 * - 함수 로컬 변수         : v_ 접두사
 * - 함수 인자              : p_ 접두사
 * ------------------------------------------------------
 */

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <string.h>

#include "A20_Const_040.h"
#include "C10_Config_041.h"
#include "D10_Logger_040.h"

// ------------------------------------------------------
// N10 런타임 상태 구조체
// ------------------------------------------------------
typedef struct {
	uint8_t	 runMode;			 // 0=OFF,1=SCHEDULE,2=USER_PROFILE
	uint8_t	 runSource;			 // 0=UNKNOWN,1=BUTTON,2=WEB,3=API
	int16_t	 lastScheduleNo;	 // 마지막 선택 스케줄 번호
	int16_t	 lastUserProfileNo;	 // 마지막 선택 유저 프로파일 번호
	bool	 autoOffEnabled;
	uint32_t autoOffMinutes;
	bool	 overrideEnabled;
	uint8_t	 overrideMode;	// 0=NONE,1=FIXED,2=PRESET
	float	 overrideFixedPercent;
	char	 overridePresetCode[24];
	char	 overrideStyleCode[24];
} ST_N10_RuntimeState_t;

// ------------------------------------------------------
// Dirty Flag 구조체
// ------------------------------------------------------
typedef struct {
	bool runtime;
} ST_N10_DirtyFlags_t;

// ------------------------------------------------------
// N10 NVS Manager 클래스 선언
// ------------------------------------------------------
class CL_N10_NvsManager {
  public:
	static constexpr const char* G_N10_NS_RUNTIME		= "SNW_RUN";
	static constexpr uint32_t	 G_N10_SAVE_INTERVAL_MS = 10UL * 1000UL;  // 최소 저장 주기 10초

  public:
	// ==================================================
	// 초기화 / 종료
	// ==================================================
	static bool begin();
	static void end();
	static void clearAll();

	// ==================================================
	// Dirty 플래그 관리
	// ==================================================
	static void markDirty(const char* p_key, bool p_flag);
	static void flushIfNeeded();

	// ==================================================
	// Getter / JSON Export
	// ==================================================
	static ST_N10_RuntimeState_t getState();
	static void toJson(JsonDocument& p_doc);

	// ==================================================
	// 주기 Flush (loop용)
	// ==================================================
	static void tick();

	// ==================================================
	// Setter API (CT10 등에서 호출)
	// ==================================================
	static void setRunMode(uint8_t p_mode, uint8_t p_source);
	static void setLastSchedule(int16_t p_schNo);
	static void setLastUserProfile(int16_t p_profileNo);
	static void setAutoOff(bool p_enabled, uint32_t p_minutes);
	static void setOverrideFixed(bool p_enabled, float p_percent);
	static void setOverridePreset(bool p_enabled, const char* p_presetCode, const char* p_styleCode);
	static void clearOverride();
	static void resetRuntime();

  private:
	static Preferences			 s_prefs;
	static bool					 s_initialized;
	static ST_N10_RuntimeState_t s_state;
	static ST_N10_DirtyFlags_t	 s_dirty;
	static uint32_t				 s_lastSaveMs;

	// --------------------------------------------------
	// 내부: NVS 로드/저장
	// --------------------------------------------------
	static void loadRuntimeFromNvs();
	static void flush(bool p_force);
};

// 정적 멤버 변수는 .cpp 파일에서 정의됩니다.
