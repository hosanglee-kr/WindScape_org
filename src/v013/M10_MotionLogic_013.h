#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : M10_MotionLogic_013.h
 * 모듈약어 : M10
 * 모듈명 : Smart Nature Wind Motion Logic (PIR + BLE)
 * ------------------------------------------------------
 * 기능 요약
 *  - PIR 디지털 입력 기반 모션 검출 (디바운스 + Hold 유지)
 *  - BLE(B10 스캐너) 기반 근접 감지 (BLE TTL 유지)
 *  - Unified Motion Present = PIR OR BLE
 *  - Feed 모드(테스트/강제 입력)
 *  - cfg_system_022.json (hw.pir, hw.ble) 반영
 *  - cfg_motion_022.json (motion.pir, motion.ble) 반영
 * ------------------------------------------------------
 * [구현 규칙]
 *  - 항상 소스 시작 주석 체계 유지
 *  - ArduinoJson v7.x.x 사용 (v6 이하 사용 금지)
 *  - JsonDocument 단일 타입만 사용
 *  - createNestedArray/Object/containsKey 사용 금지
 *  - memset + strlcpy 기반 안전 초기화
 *  - 주석/필드명은 JSON 구조와 동일하게 유지
 *  - 모듈별 단일 헤더(h)파일로만 구성 (cpp 없음)
 *  - 소스 앞부분 구현규칙, 코드네이밍규칙 변경 금지
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
#include <ArduinoJson.h>
#include <cstring>

#include "A10_Const_012.h"
#include "C10_ConfigManager_012.h"
#include "D10_Logger_011.h"
#include "B10_BLEScanner_012.h"

// ------------------------------------------------------
// 기본 상수
// ------------------------------------------------------
#define G_M10_DEFAULT_PIR_DEBOUNCE_MS  (300U)
#define G_M10_DEFAULT_PIR_HOLD_SEC     (60U)
#define G_M10_DEFAULT_TTL_SEC          (12U)   // Pir/BLE 통합 TTL 유지


// ------------------------------------------------------
// 런타임 상태 구조체
// ------------------------------------------------------
typedef struct {
	bool          pirEnabled     = false;
	uint8_t       pirPin         = 255;
	uint32_t      pirDebounce_ms = G_M10_DEFAULT_PIR_DEBOUNCE_MS;
	uint32_t      pirHold_sec    = G_M10_DEFAULT_PIR_HOLD_SEC;

	bool          bleEnabled     = false;

	// state flags
	bool          pirActive      = false;
	bool          bleActive      = false;
	bool          motionActive   = false;

	// timestamps
	unsigned long lastPirMs      = 0;
	unsigned long lastMotionMs   = 0;

	// feed override flags
	bool          feedMode       = false;
	bool          feedPIRActive  = false;
	bool          feedBLEActive  = false;

} ST_M10_Runtime_t;


// ------------------------------------------------------
// CL_M10_MotionLogic
// ------------------------------------------------------
class CL_M10_MotionLogic {
public:
	// ==================================================
	// 초기화
	// ==================================================
	void begin() {
		memset(&_rt, 0, sizeof(_rt));

		// System config 기반 설정
		if (g_A10_config_root.system.hw.pir.enabled) {
			_rt.pirEnabled     = true;
			_rt.pirPin         = g_A10_config_root.system.hw.pir.pin;
			_rt.pirDebounce_ms = g_A10_config_root.system.hw.pir.debounce_sec * 1000UL;
			_rt.pirHold_sec    = g_A10_config_root.system.hw.pir.hold_sec;
		}
		if (g_A10_config_root.system.hw.ble.enabled) {
			_rt.bleEnabled = true;
		}

		if (_rt.pirEnabled && _rt.pirPin != 255) {
			pinMode(_rt.pirPin, INPUT);
		}

		_rt.lastPirMs     = 0;
		_rt.lastMotionMs  = 0;
		_rt.pirActive     = false;
		_rt.bleActive     = false;
		_rt.motionActive  = false;
		_rt.feedMode      = false;

		CL_D10_Logger::log(EN_L10_LOG_INFO,
			"[M10] begin PIR=%d pin=%u BLE=%d",
			_rt.pirEnabled, _rt.pirPin, _rt.bleEnabled
		);
	}

	// BLE 스캐너 주입
	void setBLE(CL_B10_BLEScanner* p_scanner) {
		_ble = p_scanner;
	}

	// ==================================================
	// Tick
	// ==================================================
	void tick() {
		_tickPIR();
		_tickBLE();
		_updateUnifiedState();
	}

	// 모션 여부
	bool M10_isMotionPresent() const { return _rt.motionActive; }

	// ==================================================
	// Feed 모드(강제)
	// ==================================================
	void feedPIR(bool p_state) {
		_rt.feedMode = true;
		_rt.feedPIRActive = p_state;
		if (p_state) _rt.lastPirMs = millis();
	}

	void feedBLE(bool p_state) {
		_rt.feedMode = true;
		_rt.feedBLEActive = p_state;
		if (p_state) _rt.lastMotionMs = millis();
	}

	// ==================================================
	// JSON 직렬화
	// ==================================================
	void toJson(JsonDocument& p_doc) const {
		JsonObject o = p_doc["motion"].to<JsonObject>();
		o["pir"]    = _rt.pirActive;
		o["ble"]    = _rt.bleActive;
		o["active"] = _rt.motionActive;
		o["feed"]   = _rt.feedMode;
		o["lastMs"] = _rt.lastMotionMs;
	}

private:
	// --------------------------------------------------
	// 내부 상태
	// --------------------------------------------------
	ST_M10_Runtime_t   _rt;
	CL_B10_BLEScanner* _ble = nullptr;

	// --------------------------------------------------
	// PIR 처리
	// --------------------------------------------------
	void _tickPIR() {
		if (!_rt.pirEnabled || _rt.pirPin == 255) return;

		// Feed override
		if (_rt.feedMode) {
			if (_rt.feedPIRActive) {
				_rt.pirActive = true;
				_rt.lastPirMs = millis();
			}
			return;
		}

		int v_in = digitalRead(_rt.pirPin);
		unsigned long v_now = millis();

		if (v_in == HIGH) {
			if ((v_now - _rt.lastPirMs) >= _rt.pirDebounce_ms) {
				_rt.pirActive = true;
				_rt.lastPirMs = v_now;
			}
		} else {
			// hold time
			if (_rt.pirActive && (v_now - _rt.lastPirMs) > (_rt.pirHold_sec * 1000UL)) {
				_rt.pirActive = false;
			}
		}
	}

	// --------------------------------------------------
	// BLE 처리
	// --------------------------------------------------
	void _tickBLE() {
		if (!_rt.bleEnabled) {
			_rt.bleActive = false;
			return;
		}

		if (_rt.feedMode) {
			_rt.bleActive = _rt.feedBLEActive;
			return;
		}

		if (!_ble) {
			_rt.bleActive = false;
			return;
		}

		_rt.bleActive = _ble->isAnyPresent();
		if (_rt.bleActive) _rt.lastMotionMs = millis();
	}

	// --------------------------------------------------
	// Unified Motion State
	// --------------------------------------------------
	void _updateUnifiedState() {
		unsigned long v_now = millis();
		bool v_any = (_rt.pirActive || _rt.bleActive);

		if (v_any) {
			_rt.motionActive = true;
			_rt.lastMotionMs = v_now;
			return;
		}

		uint32_t v_ttl = G_M10_DEFAULT_TTL_SEC * 1000UL;
		_rt.motionActive = ((v_now - _rt.lastMotionMs) <= v_ttl);
	}
};


// ------------------------------------------------------
// 전역 단일 인스턴스 접근(CT10 호환)
// ------------------------------------------------------
static CL_M10_MotionLogic* g_M10_instance = nullptr;
inline void M10_setInstance(CL_M10_MotionLogic* p_i) { g_M10_instance = p_i; }
inline bool M10_motionDetected() {
	return (g_M10_instance) ? g_M10_instance->M10_isMotionPresent() : false;
}
