#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : M10_MotionManager_012.h
 * 모듈약어 : M10
 * 모듈명 : Smart Nature Wind 모션 감지 Manager (v012)
 * ------------------------------------------------------
 * 기능 요약:
 *  - PIR 디바운스/홀드 기반 모션 감지
 *  - BLE 디바이스 RSSI 기반 근접 판단(피드 호출 기반)
 *  - cfg_system_022.json(h/w: pir, ble), cfg_motion_022.json(devices[]) 반영
 *  - 통합 활성 플래그 산출(isActive) 및 상태 JSON 직렬화
 *  - CT10(ControlManager)에서 게이팅 신호로 사용
 * ------------------------------------------------------
 * [코드 네이밍 규칙]
 * 		- 전역 상수,매크로      : G_모듈약어_ 접두사
 * 		- 전역 변수             : g_모듈약어_ 접두사
 * 		- 전역 함수             : 모듈약어_ 접두사
 * 		- type                  : T_모듈약어_ 접두사
 *      - typedef               : _t  접미사
 * 		- enum 상수             : EN_모듈약어_ 접두사
 * 		- 구조체                : ST_모듈약어_ 접두사
 * 		- 클래스명              : CL_모듈약어_ 접두사
 * 		- 클래스 private 멤버   : _ 접두사
 *      - 클래스 멤버 함수,변수   : 모듈약어 접두사 미시용
 * 		- 클래스 정적 멤버      : s_ 접두사
 * 		- 함수 로컬 변수             : v_ 접두사
 * 		- 함수 인자             : p_ 접두사
 * ------------------------------------------------------
 * 의존:
 *  - A10_Const_011.h (022 JSON 스키마 반영)
 *  - C10_ConfigManager_011.h (load/ptr 접근)
 *  - D10_Logger_010.h (로그)
 *  - ArduinoJson v7 (JsonDocument만 사용)
 * ------------------------------------------------------
 */




/*
 * ------------------------------------------------------
 * 소스명 : M10_MotionManager_013.h
 * 모듈약어 : M10
 * 모듈명 : Smart Nature Wind 모션 감지 Manager (v013)
 * ------------------------------------------------------
 * 기능 요약:
 *  - PIR: 디바운스/홀드 기반 모션 판정(핀 입력)
 *  - BLE: 외부 스캐너(B10)로부터 RSSI/LastSeen만 수신 → hold 기반 "최근 감지" 상태 유지
 *  - cfg_system_022.json(h/w: pir, ble), cfg_motion_022.json(motion.ble.devices[]) 반영
 *  - 통합 활성 플래그 산출(isActive) 및 상태 JSON 직렬화
 *  - ⚠️ BLE 임계/평균/히스테리시스/퍼시스턴스는 B10에서 수행, M10은 저장·hold만 담당
 * ------------------------------------------------------
 * 규칙:
 *  - ArduinoJson v7.x.x / JsonDocument만 사용
 *  - createNestedArray/Object/containsKey 사용 금지
 *  - memset + strlcpy 기반 안전 초기화
 *  - 주석/필드명은 JSON 구조와 동일하게 유지
 *  - 단일 헤더(h) 파일 구성
 * ------------------------------------------------------
 * [코드 네이밍 규칙]
 * 		- 전역 상수,매크로      : G_모듈약어_ 접두사
 * 		- 전역 변수             : g_모듈약어_ 접두사
 * 		- 전역 함수             : 모듈약어_ 접두사
 * 		- type                  : T_모듈약어_ 접두사
 *      - typedef               : _t  접미사
 * 		- enum 상수             : EN_모듈약어_ 접두사
 * 		- 구조체                : ST_모듈약어_ 접두사
 * 		- 클래스명              : CL_모듈약어_ 접두사
 * 		- 클래스 private 멤버   : _ 접두사
 *      - 클래스 멤버 함수/변수 : 모듈약어 접두사 미사용
 * 		- 클래스 정적 멤버      : s_ 접두사
 * 		- 함수 로컬 변수        : v_ 접두사
 * 		- 함수 인자             : p_ 접두사
 * ------------------------------------------------------
 * 의존:
 *  - A10_Const_011.h (022 JSON 스키마 반영)
 *  - C10_ConfigManager_011.h (load/ptr 접근)
 *  - D10_Logger_011.h (로그)
 * ------------------------------------------------------
 */




/*
 * ------------------------------------------------------
 * 소스명 : M10_MotionLogic_012.h
 * 모듈약어 : M10
 * 모듈명 : Smart Nature Wind Motion Logic (PIR + BLE)
 * ------------------------------------------------------
 * 기능 요약:
 *  - PIR 디지털 입력 기반 모션 검출 (디바운스 + Hold 유지)
 *  - BLE 근접(B10) 기반 모션 검출 (Exit Delay/TTL 활용)
 *  - Unified Motion Present 상태 계산(PIR OR BLE)
 *  - Feed 모드(테스트 시 PIR/ BLE 강제 입력)
 *  - cfg_system_022 & cfg_motion_022 기반 파라미터 적용
 * ------------------------------------------------------
 * 구현 규칙:
 *  - ArduinoJson v7, JsonDocument 단일
 *  - memset + strlcpy 안전 초기화
 *  - 외부 스캐너/BLE 모듈 주입 방식 (B10)
 *  - 단일 헤더 구성(CPP분리 없음)
 * ------------------------------------------------------
 * 인터페이스:
 *  - void begin()
 *  - void tick()
 *  - bool M10_isMotionPresent()
 *  - void setBLE(CL_B10_BLEScanner*)
 *  - void feedPIR(bool), feedBLE(bool)
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
#define G_M10_DEFAULT_PIR_DEBOUNCE_MS  (300)      // ms
#define G_M10_DEFAULT_PIR_HOLD_SEC     (60)
#define G_M10_DEFAULT_TTL_SEC          (12)       // BLE/ PIR 통합 유지

// ------------------------------------------------------
// 런타임 상태 구조체
// ------------------------------------------------------
typedef struct {
	bool          pirEnabled     = false;
	uint8_t       pirPin         = 255;
	uint32_t      pirDebounce_ms = G_M10_DEFAULT_PIR_DEBOUNCE_MS;
	uint32_t      pirHold_sec    = G_M10_DEFAULT_PIR_HOLD_SEC;

	bool          bleEnabled     = false;

	bool          pirActive      = false;
	bool          bleActive      = false;
	bool          motionActive   = false;

	unsigned long lastPirMs      = 0;
	unsigned long lastMotionMs   = 0;

	// feed override
	bool          feedPIRActive  = false;
	bool          feedBLEActive  = false;
	bool          feedMode       = false;
} ST_M10_Runtime_t;


// ------------------------------------------------------
// CL_M10_MotionLogic
// ------------------------------------------------------
class CL_M10_MotionLogic {
public:
	// ==================================================
	// Public API
	// ==================================================
	void begin() {
		memset(&_rt, 0, sizeof(_rt));

		// Load config
		if (g_A10_config_root.system.hw.pir.enabled) {
			_rt.pirEnabled = true;
			_rt.pirPin = g_A10_config_root.system.hw.pir.pin;
			_rt.pirDebounce_ms = g_A10_config_root.system.hw.pir.debounce_sec * 1000UL;
			_rt.pirHold_sec = g_A10_config_root.system.hw.pir.hold_sec;
		}
		if (g_A10_config_root.system.hw.ble.enabled) {
			_rt.bleEnabled = true;
		}

		if (_rt.pirEnabled && _rt.pirPin != 255) {
			pinMode(_rt.pirPin, INPUT);
		}

		_rt.lastPirMs = 0;
		_rt.lastMotionMs = 0;
		_rt.pirActive = false;
		_rt.bleActive = false;
		_rt.motionActive = false;
		_rt.feedMode = false;

		CL_D10_Logger::log(EN_L10_LOG_INFO,
			"[M10] begin PIR=%d pin=%u BLE=%d",
			_rt.pirEnabled, _rt.pirPin, _rt.bleEnabled
		);
	}

	void setBLE(CL_B10_BLEScanner* p_scanner) {
		_ble = p_scanner;
	}

	void tick() {
		_tickPIR();
		_tickBLE();
		_updateUnifiedState();
	}

	bool M10_isMotionPresent() const {
		return _rt.motionActive;
	}

	// ==================================================
	// Feed 모드 (테스트용)
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
	// JSON 상태 출력
	// ==================================================
	void toJson(JsonDocument& p_doc) const {
		JsonObject o = p_doc["motion"].to<JsonObject>();
		o["pir"] = _rt.pirActive;
		o["ble"] = _rt.bleActive;
		o["motion"] = _rt.motionActive;
		o["last_ms"] = _rt.lastMotionMs;
		o["feed"] = _rt.feedMode;
	}

private:
	// ==================================================
	// 내부 상태
	// ==================================================
	ST_M10_Runtime_t  _rt;
	CL_B10_BLEScanner* _ble = nullptr;

	// ==================================================
	// PIR 처리
	// ==================================================
	void _tickPIR() {
		if (!_rt.pirEnabled || _rt.pirPin == 255) return;

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
			// hold logic
			if (_rt.pirActive && (v_now - _rt.lastPirMs) > (_rt.pirHold_sec * 1000UL)) {
				_rt.pirActive = false;
			}
		}
	}

	// ==================================================
	// BLE 존재 여부
	// ==================================================
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
		if (_rt.bleActive) {
			_rt.lastMotionMs = millis();
		}
	}

	// ==================================================
	// PIR OR BLE → Unified motion
	// ==================================================
	void _updateUnifiedState() {
		unsigned long v_now = millis();
		bool v_any = (_rt.pirActive || _rt.bleActive);

		if (v_any) {
			_rt.motionActive = true;
			_rt.lastMotionMs = v_now;
			return;
		}

		// TTL 유지
		uint32_t v_ttl = G_M10_DEFAULT_TTL_SEC * 1000UL;
		if ((v_now - _rt.lastMotionMs) <= v_ttl) {
			_rt.motionActive = true;
		} else {
			_rt.motionActive = false;
		}
	}
};

//
// 외부 사용 함수 wrapper (CT10 expects)
//
static CL_M10_MotionLogic* g_M10_instance = nullptr;

inline void M10_setInstance(CL_M10_MotionLogic* p_i) { g_M10_instance = p_i; }
inline bool M10_motionDetected() {
	return (g_M10_instance) ? g_M10_instance->M10_isMotionPresent() : false;
}
