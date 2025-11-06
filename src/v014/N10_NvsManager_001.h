#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : N10_NvsManager_001.h
 * 모듈약어 : N10
 * 모듈명 : Smart Nature Wind NVS Manager
 * ------------------------------------------------------
 * 기능 요약:
 *  - NVS 기반 설정값 저장/복원 관리
 *  - Dirty Flag 기반 Lazy Save (5분 주기)
 *  - runMode, profile, preset, style 등 핵심 상태 저장
 *  - Flash 마모 방지 (변경 시점에만 저장)
 *  - 공장 초기화 및 안전 복원 지원
 * ------------------------------------------------------
 * [구현 규칙]
 *  - ArduinoJson v7.x.x 사용 (v6 이하 사용 금지)
 *  - JsonDocument 단일 타입만 사용
 *  - memset + strlcpy 기반 안전 초기화
 *  - 모듈별 단일 헤더(h)파일로만 구성
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
 * ------------------------------------------------------
 */

#include <Arduino.h>
#include <Preferences.h>
#include <ArduinoJson.h>

// ------------------------------------------------------
// 상수 정의
// ------------------------------------------------------
#define G_N10_NAMESPACE "SNW"                // NVS namespace (Smart Nature Wind)
#define G_N10_SAVE_INTERVAL_MS 300000UL      // Lazy Save 주기: 5분
#define G_N10_JSON_SPEC_VER "028"            // 현재 NVS 구조 버전

// ------------------------------------------------------
// NVS 구조체
// ------------------------------------------------------
typedef struct {
    uint8_t runMode;             // 현재 실행 모드 (0=Idle,1=Schedule,2=UserProfile,3=Override)
    uint8_t activeProfileNo;     // 실행 중인 사용자 프로파일 번호
    uint8_t activeSegmentNo;     // 현재 동작 중인 세그먼트 번호
    char    presetCode[24];      // 마지막 적용된 바람 프리셋 코드 (예: "OCEAN")
    char    styleCode[24];       // 마지막 적용된 바람 스타일 코드 (예: "RELAX")
    bool    wifiConnected;       // 마지막 Wi-Fi 연결 상태
} ST_N10_nvsState_t;

// ------------------------------------------------------
// NVS Manager Class
// ------------------------------------------------------
class CL_N10_NvsManager {
public:
    CL_N10_NvsManager() = default;

    // --------------------------------------------------
    // 초기화
    // --------------------------------------------------
    void begin() {
        _prefs.begin(G_N10_NAMESPACE, false);
        _dirty = false;
        _lastSaveMs = millis();
        memset(&_state, 0, sizeof(_state));
        load();
    }

    // --------------------------------------------------
    // 주기적 Tick (5분 단위 Lazy Save)
    // --------------------------------------------------
    void tick() {
        if (_dirty && millis() - _lastSaveMs > G_N10_SAVE_INTERVAL_MS) {
            save();
        }
    }

    // --------------------------------------------------
    // 상태 저장 (Dirty 시만 commit)
    // --------------------------------------------------
    void save() {
        if (!_dirty) return;

        // 개별 항목 저장
        _prefs.putUChar("runMode", _state.runMode);                 // 실행 모드
        _prefs.putUChar("activeProfileNo", _state.activeProfileNo); // 사용자 프로파일 번호
        _prefs.putUChar("activeSegmentNo", _state.activeSegmentNo); // 세그먼트 번호
        _prefs.putString("presetCode", _state.presetCode);          // 바람 프리셋 코드
        _prefs.putString("styleCode", _state.styleCode);            // 바람 스타일 코드
        _prefs.putBool("wifiConnected", _state.wifiConnected);      // Wi-Fi 연결 여부

        _dirty = false;
        _lastSaveMs = millis();

        Serial.println(F("[N10] NVS saved (lazy commit)"));
    }

    // --------------------------------------------------
    // 전체 로드
    // --------------------------------------------------
    void load() {
        _state.runMode         = _prefs.getUChar("runMode", 0);
        _state.activeProfileNo = _prefs.getUChar("activeProfileNo", 0);
        _state.activeSegmentNo = _prefs.getUChar("activeSegmentNo", 0);

        String preset = _prefs.getString("presetCode", "");
        strlcpy(_state.presetCode, preset.c_str(), sizeof(_state.presetCode));

        String style = _prefs.getString("styleCode", "");
        strlcpy(_state.styleCode, style.c_str(), sizeof(_state.styleCode));

        _state.wifiConnected = _prefs.getBool("wifiConnected", false);

        Serial.println(F("[N10] NVS loaded"));
    }

    // --------------------------------------------------
    // 즉시 저장 (이벤트/버튼)
    // --------------------------------------------------
    void saveNow() {
        _dirty = true;
        save();
    }

    // --------------------------------------------------
    // Dirty 마킹
    // --------------------------------------------------
    void markDirty() { _dirty = true; }

    // --------------------------------------------------
    // Getter / Setter
    // --------------------------------------------------
    void setRunMode(uint8_t v) { 
        if (_state.runMode != v) { _state.runMode = v; markDirty(); }
    }
    void setActiveProfile(uint8_t v) { 
        if (_state.activeProfileNo != v) { _state.activeProfileNo = v; markDirty(); }
    }
    void setActiveSegment(uint8_t v) {
        if (_state.activeSegmentNo != v) { _state.activeSegmentNo = v; markDirty(); }
    }
    void setPresetCode(const char* v) {
        if (strcmp(_state.presetCode, v) != 0) { strlcpy(_state.presetCode, v, sizeof(_state.presetCode)); markDirty(); }
    }
    void setStyleCode(const char* v) {
        if (strcmp(_state.styleCode, v) != 0) { strlcpy(_state.styleCode, v, sizeof(_state.styleCode)); markDirty(); }
    }
    void setWifiConnected(bool v) {
        if (_state.wifiConnected != v) { _state.wifiConnected = v; markDirty(); }
    }

    uint8_t getRunMode() const { return _state.runMode; }
    uint8_t getActiveProfile() const { return _state.activeProfileNo; }
    uint8_t getActiveSegment() const { return _state.activeSegmentNo; }
    const char* getPresetCode() const { return _state.presetCode; }
    const char* getStyleCode() const { return _state.styleCode; }
    bool getWifiConnected() const { return _state.wifiConnected; }

    // --------------------------------------------------
    // 공장초기화 (모든 값 초기화)
    // --------------------------------------------------
    void clearAll() {
        _prefs.clear();
        memset(&_state, 0, sizeof(_state));
        _dirty = false;
        Serial.println(F("[N10] NVS cleared"));
    }

    // --------------------------------------------------
    // JSON Export (상태 출력용)
    // --------------------------------------------------
    void toJson(JsonDocument& doc) {
        JsonObject o = doc["nvs"].to<JsonObject>();
        o["runMode"]          = _state.runMode;         // 현재 실행 모드
        o["activeProfileNo"]  = _state.activeProfileNo; // 사용자 프로파일 번호
        o["activeSegmentNo"]  = _state.activeSegmentNo; // 세그먼트 번호
        o["presetCode"]       = _state.presetCode;      // 프리셋 코드명
        o["styleCode"]        = _state.styleCode;       // 스타일 코드명
        o["wifiConnected"]    = _state.wifiConnected;   // Wi-Fi 연결 상태
    }

private:
    Preferences _prefs;              // NVS 핸들
    ST_N10_nvsState_t _state;        // 현재 상태 캐시
    bool _dirty = false;             // 변경 감지 플래그
    unsigned long _lastSaveMs = 0;   // 마지막 저장 시각
};
