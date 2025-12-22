/*
 * ------------------------------------------------------
 * 소스명 : C10_Config_Core_030.cpp
 * 모듈 약어 : C10
 * 모듈명 : Smart Nature Wind Configuration Manager - Core
 * ------------------------------------------------------
 * 기능 요약:
 *  - 전체 Config Root 로드/해제/저장 관리
 *  - LittleFS 기반 JSON I/O(ioLoadJson/ioSaveJson)
 *  - Dirty Flag 기반 saveDirtyConfigs / saveAll
 *  - 전체 JSON Export(toJson_All)
 *  - 공장 초기화(factoryResetFromDefault)
 *  - 공용 뮤텍스 관리 (_mutex_Acquire/_mutex_Release)
 * ------------------------------------------------------
 * [구현 규칙]
 *  - 항상 소스 시작 주석 체계 유지
 *  - ArduinoJson v7.x.x 사용 (v6 이하 사용 금지)
 *  - JsonDocument 단일 타입만 사용
 *  - createNestedArray/Object/containsKey 사용 금지
 *  - memset + strlcpy 기반 안전 초기화
 *  - 주석/필드명은 JSON 구조와 동일하게 유지
 *  - 모듈별 헤더(h) + 목적물별 cpp 분리 구성 (Core/System/Schedule)
 * ------------------------------------------------------
 * [코드 네이밍 규칙]
 *   - 전역 상수,매크로      : G_모듈약어_ 접두사
 *   - 전역 변수             : g_모듈약어_ 접두사
 *   - 전역 함수             : 모듈약어_ 접두사
 *   - type                  : T_모듈약어_ 접두사
 *   - typedef               : _t  접미사
 *   - enum 상수             : EN_모듈약어_ 접두사
 *   - 구조체                : ST_모듈약어_ 접두사
 *   - 클래스명              : CL_모듈약어_ 접두사
 *   - 클래스 private 멤버   : _ 접두사
 *   - 클래스 멤버(함수/변수) : 모듈약어 접두사 미사용
 *   - 클래스 정적 멤버      : s_ 접두사
 *   - 함수 로컬 변수        : v_ 접두사
 *   - 함수 인자             : p_ 접두사
 * ------------------------------------------------------
 */

#include <FS.h>
#include <LittleFS.h>

#include "C10_Config_030.h"

// ------------------------------------------------------
// 전역 Config Root 정의
// ------------------------------------------------------
ST_A20_ConfigRoot_t	  g_A20_config_root							= {};

// ------------------------------------------------------
// 정적 멤버 정의
// ------------------------------------------------------
bool				  CL_C10_ConfigManager::_dirty_system		= false;
bool				  CL_C10_ConfigManager::_dirty_wifi			= false;
bool				  CL_C10_ConfigManager::_dirty_motion		= false;
bool				  CL_C10_ConfigManager::_dirty_schedules	= false;
bool				  CL_C10_ConfigManager::_dirty_userProfiles = false;
bool				  CL_C10_ConfigManager::_dirty_windProfile	= false;

// cfg_jsonFile.json 매핑 초기값 (비어있는 상태)
ST_A20_cfg_jsonFile_t CL_C10_ConfigManager::s_cfgJsonFileMap{};

SemaphoreHandle_t	  CL_C10_ConfigManager::s_configMutex = nullptr;

// ------------------------------------------------------
// JSON IO Helper 구현
// ------------------------------------------------------

bool ioLoadJson(const char* p_path, JsonDocument& p_doc) {
    if (!p_path || !p_path[0]) {
        CL_D10_Logger::log(EN_L10_LOG_ERROR,
                           "[C10] ioLoadJson: invalid path (null/empty)");
        return false;
    }

    char v_bakPath[A20_Const::LEN_PATH + 5];
    snprintf(v_bakPath, sizeof(v_bakPath), "%s.bak", p_path);

    // 1) main 없으면 bak로 복구
    if (!LittleFS.exists(p_path)) {
        if (LittleFS.exists(v_bakPath)) {
            if (!LittleFS.rename(v_bakPath, p_path)) {
                CL_D10_Logger::log(EN_L10_LOG_ERROR,
                                   "[C10] Restore rename failed: %s -> %s",
                                   v_bakPath, p_path);
                return false;
            }
            CL_D10_Logger::log(EN_L10_LOG_WARN,
                               "[C10] Restored from backup: %s -> %s",
                               v_bakPath, p_path);
        } else {
            CL_D10_Logger::log(EN_L10_LOG_ERROR,
                               "[C10] Missing config & no backup: %s",
                               p_path);
            return false;
        }
    }

    // 2) main 파싱 시도
    {
        File v_f = LittleFS.open(p_path, "r");
        if (!v_f) {
            CL_D10_Logger::log(EN_L10_LOG_ERROR,
                               "[C10] Open failed: %s", p_path);
            return false;
        }

        DeserializationError v_e = deserializeJson(p_doc, v_f);
        v_f.close();

        if (!v_e) {
            return true; // 정상
        }

        CL_D10_Logger::log(EN_L10_LOG_WARN,
                           "[C10] Parse error(%s): %s. Try backup...",
                           p_path, v_e.c_str());
    }

    // 3) bak 재시도
    if (!LittleFS.exists(v_bakPath)) {
        CL_D10_Logger::log(EN_L10_LOG_ERROR,
                           "[C10] No backup exists: %s", v_bakPath);
        return false;
    }

    JsonDocument v_bakDoc;
    File v_fb = LittleFS.open(v_bakPath, "r");
    if (!v_fb) {
        CL_D10_Logger::log(EN_L10_LOG_ERROR,
                           "[C10] Backup open failed: %s", v_bakPath);
        return false;
    }

    DeserializationError v_eb = deserializeJson(v_bakDoc, v_fb);
    v_fb.close();

    if (v_eb) {
        CL_D10_Logger::log(EN_L10_LOG_ERROR,
                           "[C10] Backup parse error(%s): %s",
                           v_bakPath, v_eb.c_str());
        return false;
    }

    // 4) bak -> main 복원
    LittleFS.remove(p_path);
    if (!LittleFS.rename(v_bakPath, p_path)) {
        CL_D10_Logger::log(EN_L10_LOG_ERROR,
                           "[C10] Backup restore rename failed: %s -> %s",
                           v_bakPath, p_path);
        return false;
    }

    p_doc.clear();
    p_doc.set(v_bakDoc);

    CL_D10_Logger::log(EN_L10_LOG_WARN,
                       "[C10] Restored valid backup to main: %s",
                       p_path);
    return true;
}


bool ioSaveJson(const char* p_path, const JsonDocument& p_doc) {
    if (!p_path || !p_path[0]) {
        CL_D10_Logger::log(EN_L10_LOG_ERROR,
                           "[C10] ioSaveJson: invalid path (null/empty)");
        return false;
    }

    char v_bakPath[A20_Const::LEN_PATH + 5];
    snprintf(v_bakPath, sizeof(v_bakPath), "%s.bak", p_path);

    bool v_hadMain = LittleFS.exists(p_path);

    // 1) main -> bak
    if (v_hadMain) {
        if (LittleFS.exists(v_bakPath)) {
            LittleFS.remove(v_bakPath);
        }
        if (!LittleFS.rename(p_path, v_bakPath)) {
            CL_D10_Logger::log(EN_L10_LOG_ERROR,
                               "[C10] Backup rename failed: %s -> %s",
                               p_path, v_bakPath);
            return false;
        }
    }

    // 2) write new main
    File v_f = LittleFS.open(p_path, "w");
    if (!v_f) {
        CL_D10_Logger::log(EN_L10_LOG_ERROR,
                           "[C10] Save open failed: %s", p_path);

        // rollback
        if (v_hadMain && LittleFS.exists(v_bakPath)) {
            LittleFS.rename(v_bakPath, p_path);
        }
        return false;
    }

    size_t v_written = serializeJsonPretty(p_doc, v_f);
    v_f.close();

    if (v_written == 0) {
        CL_D10_Logger::log(EN_L10_LOG_ERROR,
                           "[C10] Save write failed: %s", p_path);

        // rollback
        if (LittleFS.exists(p_path)) {
            LittleFS.remove(p_path);
        }
        if (v_hadMain && LittleFS.exists(v_bakPath)) {
            LittleFS.rename(v_bakPath, p_path);
        }
        return false;
    }

    return true;
}

/*
bool ioLoadJson(const char* p_path, const char* p_bak, JsonDocument& p_doc) {
	if (!p_path || !p_path[0]) {
		CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] ioLoadJson: invalid path (null/empty)");
		return false;
	}

	// p_bak == nullptr 이면 자동으로 ".bak" 경로 생성
	const char* v_bakPath = p_bak;
	String		v_bakStr;
	if (!v_bakPath) {
		v_bakStr  = String(p_path) + ".bak";
		v_bakPath = v_bakStr.c_str();
	}

	if (!LittleFS.exists(p_path)) {
		if (v_bakPath && LittleFS.exists(v_bakPath)) {
			LittleFS.rename(v_bakPath, p_path);
			CL_D10_Logger::log(EN_L10_LOG_WARN, "[C10] Restored from backup: %s -> %s", v_bakPath, p_path);
		} else {
			CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] Missing config & no backup: %s", p_path);
			return false;
		}
	}

	File v_f = LittleFS.open(p_path, "r");
	if (!v_f) {
		CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] Open failed: %s", p_path);
		return false;
	}

	auto v_e = deserializeJson(p_doc, v_f);
	v_f.close();
	if (v_e) {
		CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] Parse error(%s): %s", p_path, v_e.c_str());
		return false;
	}
	return true;
}
bool ioSaveJson(const char* p_path, const char* p_bak, const JsonDocument& p_doc) {
	if (!p_path || !p_path[0]) {
		CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] ioSaveJson: invalid path (null/empty)");
		return false;
	}

	// p_bak == nullptr 이면 자동으로 ".bak" 경로 생성
	const char* v_bakPath = p_bak;
	String		v_bakStr;
	if (!v_bakPath) {
		v_bakStr  = String(p_path) + ".bak";
		v_bakPath = v_bakStr.c_str();
	}

	if (LittleFS.exists(p_path)) {
		if (v_bakPath) {
			if (LittleFS.exists(v_bakPath)) {
				LittleFS.remove(v_bakPath);
			}
			LittleFS.rename(p_path, v_bakPath);
		}
	}

	File v_f = LittleFS.open(p_path, "w");
	if (!v_f) {
		CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] Save open failed: %s", p_path);
		return false;
	}
	if (serializeJsonPretty(p_doc, v_f) == 0) {
		CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] Save write failed: %s", p_path);
		v_f.close();
		return false;
	}
	v_f.close();
	return true;
}
*/

bool CL_C10_ConfigManager::_loadCfgJsonFile() {
	JsonDocument v_doc;

	if (!ioLoadJson(A20_Const::CFG_JSON_FILE, v_doc)) {
		CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] Failed to load cfg json map: %s", A20_Const::CFG_JSON_FILE);
		return false;  // 옵션 A: 로드 실패시 에러로 종료
	}

	JsonObjectConst v_root = v_doc["configJsonFile"].as<JsonObjectConst>();
	if (v_root.isNull()) {
		CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] Invalid cfg json map: missing 'configJsonFile'");
		return false;
	}

	// 람다 함수 수정: std::string& 대신 char* 사용
	auto loadStr = [&](char* p_dst, const char* p_key, size_t p_size) {
		if (v_root[p_key].is<const char*>()) {
			const char* v_val = v_root[p_key].as<const char*>();
			// memset으로 초기화 후 strlcpy로 안전하게 복사
			memset(p_dst, 0, p_size);
			strlcpy(p_dst, v_val ? v_val : "", p_size);
		} else {
			// 키가 없거나 문자열이 아닐 경우 빈 문자열로 초기화
			p_dst[0] = '\0';
		}
	};

	// 각 멤버별 로드 (고정 크기 A20_Const::LEN_NAME 전달)
	loadStr(s_cfgJsonFileMap.system, "system", A20_Const::LEN_PATH);
	loadStr(s_cfgJsonFileMap.wifi, "wifi", A20_Const::LEN_PATH);
	loadStr(s_cfgJsonFileMap.motion, "motion", A20_Const::LEN_PATH);
	loadStr(s_cfgJsonFileMap.nvsSpec, "nvsSpec", A20_Const::LEN_PATH);
	loadStr(s_cfgJsonFileMap.schedules, "schedules", A20_Const::LEN_PATH);
	loadStr(s_cfgJsonFileMap.uzOpProfile, "uzOpProfile", A20_Const::LEN_PATH);
	loadStr(s_cfgJsonFileMap.dft_windProfile, "dft_windProfile", A20_Const::LEN_PATH);
	loadStr(s_cfgJsonFileMap.webPages, "webPages", A20_Const::LEN_PATH);

	// 로그 출력 수정: .c_str() 제거 (char 배열은 바로 %s로 출력 가능)
	CL_D10_Logger::log(EN_L10_LOG_INFO, "[C10] cfg_jsonFile loaded: sys=%s wifi=%s sch=%s userProfile=%s windDic=%s webPages=%s", s_cfgJsonFileMap.system, s_cfgJsonFileMap.wifi, s_cfgJsonFileMap.schedules, s_cfgJsonFileMap.uzOpProfile, s_cfgJsonFileMap.dft_windProfile, s_cfgJsonFileMap.webPages);

	return true;
}

// ------------------------------------------------------
// Mutex Helper 구현
// ------------------------------------------------------

bool CL_C10_ConfigManager::_mutex_Acquire(const char* p_funcName) {
	if (!s_configMutex) {
		s_configMutex = xSemaphoreCreateRecursiveMutex();
		if (!s_configMutex) {
			CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] %s() Mutex create failed!", p_funcName);
			return false;
		}
	}

	if (xSemaphoreTakeRecursive(s_configMutex, G_C10_MUTEX_TIMEOUT) != pdTRUE) {
		CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] %s() Mutex timeout!", p_funcName);
		return false;
	}
	return true;
}

void CL_C10_ConfigManager::_mutex_Release() {
	if (s_configMutex) {
		if (xSemaphoreGiveRecursive(s_configMutex) != pdTRUE) {
			CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] Mutex give failed (not owner?)");
		}
	}
}

// =====================================================
// 1. 전체 관리 (Load/Free/Save)
// =====================================================
bool CL_C10_ConfigManager::loadAll(ST_A20_ConfigRoot_t& p_root) {
	bool v_ok = true;

	// 0) cfg_jsonFile.json 먼저 로드 (옵션 A)
	if (!_loadCfgJsonFile()) {
		CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] loadAll: cfg_jsonFile load failed.");
		return false;  // 바로 실패 리턴
	}

	// 1) 섹션 객체 확보
	if (!p_root.system)
		p_root.system = new ST_A20_SystemConfig();
	if (!p_root.windDict)
		p_root.windDict = new ST_A20_WindProfileDict_t();
	if (!p_root.schedules)
		p_root.schedules = new ST_A20_SchedulesRoot_t();
	if (!p_root.userProfiles)
		p_root.userProfiles = new ST_A20_UserProfilesRoot_t();
	if (!p_root.wifi)
		p_root.wifi = new ST_A20_WifiConfig();
	if (!p_root.motion)
		p_root.motion = new ST_A20_MotionConfig();

	// 2) 기본값 초기화
	A20_resetSystemDefault(*p_root.system);
	A20_resetWindProfileDictDefault(*p_root.windDict);
	A20_resetSchedulesDefault(*p_root.schedules);
	A20_resetUserProfilesDefault(*p_root.userProfiles);
	A20_resetWifiDefault(*p_root.wifi);
	A20_resetMotionDefault(*p_root.motion);

	// 3) 실제 파일 로드 (섹션별 loadXxx 안에서 s_cfgJsonFileMap 사용 예정)
	if (!loadSystemConfig(*p_root.system))
		v_ok = false;
	if (!loadWindProfileDict(*p_root.windDict))
		v_ok = false;
	if (!loadWifiConfig(*p_root.wifi))
		v_ok = false;
	if (!loadMotionConfig(*p_root.motion))
		v_ok = false;
	if (!loadSchedules(*p_root.schedules))
		v_ok = false;
	if (!loadUserProfiles(*p_root.userProfiles))
		v_ok = false;

	CL_D10_Logger::log(EN_L10_LOG_INFO, "[C10] Config loaded (all sections, result=%d)", v_ok);
	return v_ok;
}

void CL_C10_ConfigManager::freeLazySection(const char* p_section, ST_A20_ConfigRoot_t& p_root) {
	if (strcmp(p_section, "wifi") == 0 && p_root.wifi) {
		delete p_root.wifi;
		p_root.wifi = nullptr;
		return;
	}
	if (strcmp(p_section, "motion") == 0 && p_root.motion) {
		delete p_root.motion;
		p_root.motion = nullptr;
		return;
	}
	if (strcmp(p_section, "schedules") == 0 && p_root.schedules) {
		delete p_root.schedules;
		p_root.schedules = nullptr;
		return;
	}
	if (strcmp(p_section, "userProfiles") == 0 && p_root.userProfiles) {
		delete p_root.userProfiles;
		p_root.userProfiles = nullptr;
		return;
	}
	if (strcmp(p_section, "windProfile") == 0 && p_root.windDict) {
		delete p_root.windDict;
		p_root.windDict = nullptr;
		return;
	}
}

void CL_C10_ConfigManager::freeAll(ST_A20_ConfigRoot_t& p_root) {
	if (p_root.system) {
		delete p_root.system;
		p_root.system = nullptr;
	}
	if (p_root.wifi) {
		delete p_root.wifi;
		p_root.wifi = nullptr;
	}
	if (p_root.motion) {
		delete p_root.motion;
		p_root.motion = nullptr;
	}
	if (p_root.windDict) {
		delete p_root.windDict;
		p_root.windDict = nullptr;
	}
	if (p_root.schedules) {
		delete p_root.schedules;
		p_root.schedules = nullptr;
	}
	if (p_root.userProfiles) {
		delete p_root.userProfiles;
		p_root.userProfiles = nullptr;
	}

	CL_D10_Logger::log(EN_L10_LOG_INFO, "[C10] All config objects freed");
}

// -----------------------------------------------------
// Dirty Config 저장
// -----------------------------------------------------
void CL_C10_ConfigManager::saveDirtyConfigs() {
	C10_MUTEX_ACQUIRE_VOID();

	if (_dirty_system && g_A20_config_root.system) {
		if (saveSystemConfig(*g_A20_config_root.system))
			_dirty_system = false;
	}
	if (_dirty_wifi && g_A20_config_root.wifi) {
		if (saveWifiConfig(*g_A20_config_root.wifi))
			_dirty_wifi = false;
	}
	if (_dirty_motion && g_A20_config_root.motion) {
		if (saveMotionConfig(*g_A20_config_root.motion))
			_dirty_motion = false;
	}
	if (_dirty_schedules && g_A20_config_root.schedules) {
		if (saveSchedules(*g_A20_config_root.schedules))
			_dirty_schedules = false;
	}
	if (_dirty_userProfiles && g_A20_config_root.userProfiles) {
		if (saveUserProfiles(*g_A20_config_root.userProfiles))
			_dirty_userProfiles = false;
	}
	if (_dirty_windProfile && g_A20_config_root.windDict) {
		if (saveWindProfileDict(*g_A20_config_root.windDict))
			_dirty_windProfile = false;
	}

	CL_D10_Logger::log(EN_L10_LOG_INFO, "[C10] All dirty configs saved to storage.");

	C10_MUTEX_RELEASE();
}

// 현재 Dirty 상태 조회
void CL_C10_ConfigManager::getDirtyStatus(JsonDocument& p_doc) {
	C10_MUTEX_ACQUIRE_VOID();

	p_doc["system"]		  = _dirty_system;
	p_doc["wifi"]		  = _dirty_wifi;
	p_doc["motion"]		  = _dirty_motion;
	p_doc["schedules"]	  = _dirty_schedules;
	p_doc["userProfiles"] = _dirty_userProfiles;
	p_doc["windProfile"]  = _dirty_windProfile;

	C10_MUTEX_RELEASE();
}

void CL_C10_ConfigManager::saveAll(const ST_A20_ConfigRoot_t& p_root) {
	C10_MUTEX_ACQUIRE_VOID();

	if (p_root.system)
		saveSystemConfig(*p_root.system);
	if (p_root.wifi)
		saveWifiConfig(*p_root.wifi);
	if (p_root.motion)
		saveMotionConfig(*p_root.motion);
	if (p_root.schedules)
		saveSchedules(*p_root.schedules);
	if (p_root.userProfiles)
		saveUserProfiles(*p_root.userProfiles);
	if (p_root.windDict)
		saveWindProfileDict(*p_root.windDict);

	C10_MUTEX_RELEASE();
}

// -----------------------------------------------------
// 3-2. All Config → JSON Export
// -----------------------------------------------------
void CL_C10_ConfigManager::toJson_All(const ST_A20_ConfigRoot_t& p, JsonDocument& p_doc, bool p_includeSystem, bool p_includeWifi, bool p_includeMotion, bool p_includeSchedules, bool p_includeUserProfiles) {
	// 여기서는 상위에서 Mutex 보호하는 것을 권장하지만,
	// read-only 성격이므로 별도 획득 없이 사용 (필요 시 상위 레벨에서 보호)
	if (p_includeSystem && p.system)
		toJson_System(*p.system, p_doc);
	if (p_includeWifi && p.wifi)
		toJson_Wifi(*p.wifi, p_doc);
	if (p_includeMotion && p.motion)
		toJson_Motion(*p.motion, p_doc);
	if (p_includeSchedules && p.schedules)
		toJson_Schedules(*p.schedules, p_doc);
	if (p_includeUserProfiles && p.userProfiles)
		toJson_UserProfiles(*p.userProfiles, p_doc);
	// windProfile은 필요 시 별도 toJson_WindProfileDict() 호출

	CL_D10_Logger::log(EN_L10_LOG_DEBUG, "[C10] Config export → JSON (sys=%d wifi=%d motion=%d sch=%d up=%d)", p_includeSystem, p_includeWifi, p_includeMotion, p_includeSchedules, p_includeUserProfiles);
}

// -----------------------------------------------------
// 8. Factory Reset 구현
// -----------------------------------------------------
bool CL_C10_ConfigManager::factoryResetFromDefault() {
#ifdef CFG_DEFAULT_FILE_EXISTS
	JsonDocument v_def;
	if (!ioLoadJson(A20_Const::CFG_DEFAULT_FILE, v_def)) {
		CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] Default file missing: %s", A20_Const::CFG_DEFAULT_FILE);
		return false;
	}

	if (v_def["system"].is<JsonObjectConst>()) {
		JsonDocument v_sys;
		v_sys["system"] = v_def["system"];
		ioSaveJson(A20_Const::CFG_SYSTEM_FILE, v_sys);
	}
	if (v_def["wifi"].is<JsonObjectConst>()) {
		JsonDocument v_wifi;
		v_wifi["wifi"] = v_def["wifi"];
		ioSaveJson(A20_Const::CFG_WIFI_FILE, v_wifi);
	}
	if (v_def["motion"].is<JsonObjectConst>()) {
		JsonDocument v_motion;
		v_motion["motion"] = v_def["motion"];
		ioSaveJson(A20_Const::CFG_MOTION_FILE, v_motion);
	}
	if (v_def["schedules"].is<JsonArrayConst>()) {
		JsonDocument v_sch;
		v_sch["schedules"] = v_def["schedules"];
		ioSaveJson(A20_Const::CFG_SCHEDULES_FILE, v_sch);
	}
	if (v_def["userProfiles"].is<JsonObjectConst>()) {
		JsonDocument v_up;
		v_up["userProfiles"] = v_def["userProfiles"];
		ioSaveJson(A20_Const::CFG_USER_PROFILES_FILE, v_up);
	}
	// windProfile은 펌웨어 내장 또는 별도 처리 (필요 시 확장 가능)

	CL_D10_Logger::log(EN_L10_LOG_INFO, "[C10] Factory reset completed from default");
	return true;
#else
	return false;
#endif
}
