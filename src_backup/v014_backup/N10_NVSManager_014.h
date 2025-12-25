#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : N10_NVSManager_014.h
 * 모듈약어 : N10
 * 모듈명 : Smart Nature Wind NVS Manager (v014)
 * ------------------------------------------------------
 * 기능 요약
 *  - ESP32 NVS 기반 설정/상태 영구 저장 및 복원
 *  - Lazy-Update 방식 (변경 시에만 write)
 *  - JSON 기반 설정 파일(cfg_*.json)과 별도 관리
 *  - 저장 항목: 최근 userProfile, 마지막 풍속%, 마지막 preset/style 등
 *  - 잔여시간/오버라이드 타이머 저장 제거 (v014 최적화)
 * ------------------------------------------------------
 * [구현 규칙]
 *  - 항상 소스 시작 주석 부분 체계 유지 및 내용 업데이트
 *  - 소스 시작 주석 부분 구현규칙, 코드네이밍규칙 내용 그대로 유지, 수정금지
 *  - ArduinoJson v7.x.x 사용 (v6 이하 사용 금지)
 *  - JsonDocument 단일 타입만 사용
 *  - createNestedArray/Object/containsKey 사용 금지
 *  - memset + strlcpy 기반 안전 초기화
 *  - 주석/필드명은 JSON 구조와 동일하게 유지
 *  - 모듈별 단일 헤더(h)파일로만 구성 (cpp 없음)
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

#include <Arduino.h>
#include <Preferences.h>
#include <string.h>
#include "D10_Logger_011.h"

// ------------------------------------------------------
// NVS 저장 데이터 구조체
// ------------------------------------------------------
typedef struct {
	uint8_t  profileNo;		 // 마지막 사용한 userProfile 번호
	float	 lastDutyPercent; // 마지막 PWM 풍속(%)
	char	 presetCode[24];  // 마지막 프리셋 코드
	char	 styleCode[24];   // 마지막 스타일 코드
	bool	 motionEnable;	 // 모션 감지 사용 여부
	uint32_t lastSavedMs;	 // 마지막 저장 시각(ms)
} ST_N10_State_t;

// ------------------------------------------------------
// CL_N10_NVSManager
// ------------------------------------------------------
class CL_N10_NVSManager {
public:
	CL_N10_NVSManager() {
		memset(&_state, 0, sizeof(_state));
		_state.profileNo = 0;
		strlcpy(_state.presetCode, "OCEAN", sizeof(_state.presetCode));
		strlcpy(_state.styleCode, "BALANCE", sizeof(_state.styleCode));
		_state.motionEnable = true;
		_state.lastDutyPercent = 0.0f;
	}

	// ==================================================
	// 초기화
	// ==================================================
	bool begin(const char* p_namespace = "snw") {
		if (!_prefs.begin(p_namespace, false)) {
			CL_D10_Logger::log(EN_L10_LOG_ERROR, "[N10] NVS init failed");
			return false;
		}
		_loadFromNVS();
		return true;
	}

	// ==================================================
	// 상태 갱신 및 Lazy 저장
	// ==================================================
	void updateProfile(uint8_t p_profileNo) {
		if (p_profileNo != _state.profileNo) {
			_state.profileNo = p_profileNo;
			_saveIfChanged();
		}
	}

	void updateDuty(float p_duty) {
		if (fabs(p_duty - _state.lastDutyPercent) > 0.5f) { // 0.5% 이상 변화 시
			_state.lastDutyPercent = p_duty;
			_saveIfChanged();
		}
	}

	void updatePresetStyle(const char* p_preset, const char* p_style) {
		bool v_changed = false;
		if (strcmp(_state.presetCode, p_preset) != 0) {
			strlcpy(_state.presetCode, p_preset, sizeof(_state.presetCode));
			v_changed = true;
		}
		if (strcmp(_state.styleCode, p_style) != 0) {
			strlcpy(_state.styleCode, p_style, sizeof(_state.styleCode));
			v_changed = true;
		}
		if (v_changed) _saveIfChanged();
	}

	void updateMotionEnable(bool p_enable) {
		if (p_enable != _state.motionEnable) {
			_state.motionEnable = p_enable;
			_saveIfChanged();
		}
	}

	// ==================================================
	// 조회 API
	// ==================================================
	uint8_t getProfile() const { return _state.profileNo; }
	float	getDuty() const { return _state.lastDutyPercent; }
	const char* getPreset() const { return _state.presetCode; }
	const char* getStyle() const { return _state.styleCode; }
	bool	isMotionEnable() const { return _state.motionEnable; }

	// ==================================================
	// NVS 강제 저장 / 초기화
	// ==================================================
	void forceSave() { _saveToNVS(); }

	void factoryReset() {
		_prefs.clear();
		memset(&_state, 0, sizeof(_state));
		strlcpy(_state.presetCode, "OCEAN", sizeof(_state.presetCode));
		strlcpy(_state.styleCode, "BALANCE", sizeof(_state.styleCode));
		_state.motionEnable = true;
		_state.lastDutyPercent = 0.0f;
		_saveToNVS();
	}

	// ==================================================
	// 내부 상태 확인용 JSON-like 출력
	// ==================================================
	void printState() {
		CL_D10_Logger::log(EN_L10_LOG_INFO,
			"[N10] profile=%u preset=%s style=%s duty=%.1f motion=%d",
			_state.profileNo, _state.presetCode, _state.styleCode,
			_state.lastDutyPercent, (int)_state.motionEnable
		);
	}

private:
	Preferences	  _prefs;
	ST_N10_State_t _state;

	// --------------------------------------------------
	// Lazy-Update 내부 로직
	// --------------------------------------------------
	void _saveIfChanged() {
		uint32_t v_now = millis();
		if (v_now - _state.lastSavedMs < 5000UL) return; // 5초 이내 중복 저장 방지
		_saveToNVS();
		_state.lastSavedMs = v_now;
	}

	void _saveToNVS() {
		_prefs.putUChar("profileNo", _state.profileNo);
		_prefs.putFloat("duty", _state.lastDutyPercent);
		_prefs.putString("preset", _state.presetCode);
		_prefs.putString("style", _state.styleCode);
		_prefs.putBool("motion", _state.motionEnable);
		CL_D10_Logger::log(EN_L10_LOG_DEBUG, "[N10] NVS saved");
	}

	void _loadFromNVS() {
		_state.profileNo = _prefs.getUChar("profileNo", 0);
		_state.lastDutyPercent = _prefs.getFloat("duty", 0.0f);
		String v_p = _prefs.getString("preset", "OCEAN");
		String v_s = _prefs.getString("style", "BALANCE");
		strlcpy(_state.presetCode, v_p.c_str(), sizeof(_state.presetCode));
		strlcpy(_state.styleCode, v_s.c_str(), sizeof(_state.styleCode));
		_state.motionEnable = _prefs.getBool("motion", true);
		_state.lastSavedMs = millis();

		CL_D10_Logger::log(EN_L10_LOG_INFO,
			"[N10] NVS loaded profile=%u preset=%s style=%s duty=%.1f motion=%d",
			_state.profileNo, _state.presetCode, _state.styleCode,
			_state.lastDutyPercent, (int)_state.motionEnable
		);
	}
};
