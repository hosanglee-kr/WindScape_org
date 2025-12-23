#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : C10_Config_030.h
 * 모듈 약어 : C10
 * 모듈명 : Smart Nature Wind Configuration Manager
 * ------------------------------------------------------
 * 기능 요약:
 *  - Smart Nature Wind 전체 설정(JSON 기반) 관리 매니저
 *  - 설정 파일 단위 분리 관리 (system / wifi / motion / schedules / userProfiles / windProfile)
 *  - 구조체 ↔ JSON 직렬화 및 역직렬화 (ArduinoJson v7 전용)
 *  - 파일 백업(.bak) / 복구 / 공장초기화(factoryResetFromDefault) 지원
 *  - PATCH 기반 부분 업데이트(patchConfigFromJson) 지원
 *  - Lazy-Load 하이브리드 구성 (필요 섹션만 동적 로드)
 *  - Wi-Fi 등 재초기화 판단 로직 확장 가능
 * ------------------------------------------------------
 * [구현 규칙]
 *  - 항상 소스 시작 주석 부분 체계 유지 및 내용 업데이트
 *  - 소스 시작 주석 부분 구현규칙, 코드네이밍규칙 내용 그대로 유지, 수정금지
 *  - ArduinoJson v7.x.x 사용 (v6 이하 사용 금지)
 *  - JsonDocument 단일 타입만 사용
 *  - createNestedArray/Object/containsKey 사용 금지
 *  - memset + strlcpy 기반 안전 초기화
 *  - 주석/필드명은 JSON 구조와 동일하게 유지
 *  - 변수명은 가능한 해석 가능하게
 * ------------------------------------------------------
 * [코드 네이밍 규칙]
 *   - namespace 명        : 모듈약어_ 접두사
 *   - namespace 내 상수    : 모둘약어 접두시 미사용
 *   - 전역 상수,매크로      : G_모듈약어_ 접두사
 *   - 전역 변수             : g_모듈약어_ 접두사
 *   - 전역 함수             : 모듈약어_ 접두사
 *   - type                  : T_모듈약어_ 접두사
 *   - typedef               : _t  접미사
 *   - enum 상수             : EN_모듈약어_ 접두사
 *   - 구조체                : ST_모듈약어_ 접두사
 *   - 클래스명              : CL_모듈약어_ 접두사 , 버전 제거
 *   - 클래스 private 멤버   : _ 접두사
 *   - 클래스 멤버(함수/변수) : 모듈약어 접두사 미사용
 *   - 클래스 정적 멤버      : s_ 접두사
 *   - 함수 로컬 변수        : v_ 접두사
 *   - 함수 인자             : p_ 접두사
 * ------------------------------------------------------
 */

#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "A20_Const_020.h"	 // ST_A20_ConfigRoot_t, ST_A20_* 구조체, 상수 정의
#include "D10_Logger_020.h"	 // CL_D10_Logger, EN_L10_LOG_*

// ------------------------------------------------------
// JSON I/O Helper 함수 선언 (Core cpp에서 구현)
// ------------------------------------------------------
bool ioLoadJson(const char* p_path, JsonDocument& p_doc);
bool ioSaveJson(const char* p_path, const JsonDocument& p_doc);
//:bool ioLoadJson(const char* p_path, const char* p_bak, JsonDocument& p_doc);
// bool ioSaveJson(const char* p_path, const char* p_bak, const JsonDocument& p_doc);

// Mutex Timeout 정의 (A20_Const_015.h에 미정의 시 기본값)
#ifndef G_C10_MUTEX_TIMEOUT
#	define G_C10_MUTEX_TIMEOUT pdMS_TO_TICKS(100)
#endif

// ------------------------------------------------------
// 뮤텍스 매크로 (bool / void 함수용 분리)
// ------------------------------------------------------
#define C10_MUTEX_ACQUIRE_BOOL()                           \
	if (!CL_C10_ConfigManager::_mutex_Acquire(__func__)) { \
		return false;                                      \
	}

#define C10_MUTEX_ACQUIRE_VOID()                           \
	if (!CL_C10_ConfigManager::_mutex_Acquire(__func__)) { \
		return;                                            \
	}

#define C10_MUTEX_RELEASE() CL_C10_ConfigManager::_mutex_Release();

// 전역 Config Root (Core cpp에서 정의)
extern ST_A20_ConfigRoot_t g_A20_config_root;

// ------------------------------------------------------
// C10 Config Manager Class
// ------------------------------------------------------
class CL_C10_ConfigManager {
  public:
	// =====================================================
	// 1. 전체 관리 (Load/Free/Save)
	// =====================================================

	// cfg_jsonFile.json 로드 상태 확인용 (필요 시 사용)
	static const ST_A20_cfg_jsonFile_t& getCfgJsonFileMap() {
		return s_cfgJsonFileMap;
	}

	static bool loadAll(ST_A20_ConfigRoot_t& p_root);
	static void freeLazySection(const char* p_section, ST_A20_ConfigRoot_t& p_root);
	static void freeAll(ST_A20_ConfigRoot_t& p_root);

	static bool factoryResetFromDefault();

	// =====================================================
	// 2. 목적물별 Load/Save
	// =====================================================
	static bool loadSystemConfig(ST_A20_SystemConfig& p_cfg);
	static bool loadWindProfileDict(ST_A20_WindProfileDict_t& p_cfg);
	static bool loadSchedules(ST_A20_SchedulesRoot_t& p_cfg);
	static bool loadUserProfiles(ST_A20_UserProfilesRoot_t& p_cfg);
	static bool loadWifiConfig(ST_A20_WifiConfig& p_cfg);
	static bool loadMotionConfig(ST_A20_MotionConfig& p_cfg);

	static bool saveSystemConfig(const ST_A20_SystemConfig& p_cfg);
	static bool saveWifiConfig(const ST_A20_WifiConfig& p_cfg);
	static bool saveMotionConfig(const ST_A20_MotionConfig& p_cfg);
	static bool saveSchedules(const ST_A20_SchedulesRoot_t& p_cfg);
	static bool saveUserProfiles(const ST_A20_UserProfilesRoot_t& p_cfg);
	static bool saveWindProfileDict(const ST_A20_WindProfileDict_t& p_cfg);

	static void saveDirtyConfigs();
	static void getDirtyStatus(JsonDocument& p_doc);
	static void saveAll(const ST_A20_ConfigRoot_t& p_root);

	// =====================================================
	// 3. JSON Export
	// =====================================================
	static void toJson_All(const ST_A20_ConfigRoot_t& p, JsonDocument& p_doc, bool p_includeSystem = true, bool p_includeWifi = true, bool p_includeMotion = true, bool p_includeSchedules = true, bool p_includeUserProfiles = true);

	static void toJson_System(const ST_A20_SystemConfig& p_cfg, JsonDocument& p_doc);
	static void toJson_Wifi(const ST_A20_WifiConfig& p_cfg, JsonDocument& p_doc);
	static void toJson_Motion(const ST_A20_MotionConfig& p_cfg, JsonDocument& p_doc);
	static void toJson_Schedules(const ST_A20_SchedulesRoot_t& p_cfg, JsonDocument& p_doc);
	static void toJson_UserProfiles(const ST_A20_UserProfilesRoot_t& p_cfg, JsonDocument& p_doc);
	static void toJson_WindProfileDict(const ST_A20_WindProfileDict_t& p_cfg, JsonDocument& p_doc);

	// =====================================================
	// 4. JSON Patch (System/Wifi/Motion/Schedules/UserProfiles/WindProfileDict)
	// =====================================================
	static bool patchSystemFromJson(ST_A20_SystemConfig& p_config, const JsonDocument& p_patch);
	static bool patchWifiFromJson(ST_A20_WifiConfig& p_config, const JsonDocument& p_patch);
	static bool patchMotionFromJson(ST_A20_MotionConfig& p_config, const JsonDocument& p_patch);

	static bool patchSchedulesFromJson(ST_A20_SchedulesRoot_t& p_cfg, const JsonDocument& p_patch);
	static bool patchUserProfilesFromJson(ST_A20_UserProfilesRoot_t& p_cfg, const JsonDocument& p_patch);
	static bool patchWindProfileDictFromJson(ST_A20_WindProfileDict_t& p_cfg, const JsonDocument& p_patch);

	// =====================================================
	// 5. CRUD - Schedules, UserProfiles, WindProfile
	//    (전역 g_A20_config_root를 대상으로 동작)
	// =====================================================
	// Schedules CRUD
	static int addScheduleFromJson(const JsonDocument& p_doc);
	static bool updateScheduleFromJson(uint16_t p_id, const JsonDocument& p_patch);
	static bool deleteSchedule(uint16_t p_id);

	// UserProfiles CRUD
	static int addUserProfilesFromJson(const JsonDocument& p_doc);
	static bool updateUserProfilesFromJson(uint16_t p_id, const JsonDocument& p_patch);
	static bool deleteUserProfiles(uint16_t p_id);

	// WindProfile CRUD (preset 중심)
	static int addWindProfileFromJson(const JsonDocument& p_doc);
	static bool updateWindProfileFromJson(uint16_t p_id, const JsonDocument& p_patch);
	static bool deleteWindProfile(uint16_t p_id);

  private:
	// Dirty Flag
	static bool					 _dirty_system;
	static bool					 _dirty_wifi;
	static bool					 _dirty_motion;
	static bool					 _dirty_schedules;
	static bool					 _dirty_userProfiles;
	static bool					 _dirty_windProfile;

	// cfg_jsonFile.json 매핑 (옵션 A)
	static ST_A20_cfg_jsonFile_t s_cfgJsonFileMap;

	// cfg_jsonFile.json 로더
	static bool _loadCfgJsonFile();

	// Mutex
	static SemaphoreHandle_t s_configMutex;

	// 뮤텍스 관리 헬퍼 (Core cpp에서 구현)
	static bool _mutex_Acquire(const char* p_funcName);
	static void _mutex_Release();
};
