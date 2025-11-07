#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : M10_MotionLogic_014.h
 * 모듈약어 : M10
 * 모듈명 : Smart Nature Wind Motion Logic Manager (v014)
 * ------------------------------------------------------
 * 기능 요약
 *  - PIR 센서 및 BLE RSSI 근접 감지 통합 제어
 *  - 각 감지의 hold_sec 시간 유지 (최근 감지 후 일정 시간 활성 유지)
 *  - 활성 상태(motionActive)는 PIR 또는 BLE 중 하나라도 활성 시 true
 *  - CT10_ControlManager에서 주기적으로 tick() 호출
 *  - /json/cfg_schedules_xxx.json 또는 cfg_uzOpProfile_xxx.json의 motion 설정 반영
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
#include <string.h>
#include "D10_Logger_014.h"

// ------------------------------------------------------
// 구조체 정의
// ------------------------------------------------------
typedef struct {
	bool     enabled;
	uint32_t hold_sec;
	uint32_t lastDetected_ms;
	bool     active;
} ST_M10_PIR_t;

typedef struct {
	bool     enabled;
	int16_t  rssi_threshold;
	uint32_t hold_sec;
	uint32_t lastDetected_ms;
	bool     active;
} ST_M10_BLE_t;

typedef struct {
	bool active;        // PIR 또는 BLE 중 하나라도 활성 시 true
	uint32_t lastUpdate_ms;
} ST_M10_MotionState_t;

// ------------------------------------------------------
// CL_M10_MotionLogic
// ------------------------------------------------------
class CL_M10_MotionLogic {
public:
	CL_M10_MotionLogic() {
		memset(&_pir, 0, sizeof(_pir));
		memset(&_ble, 0, sizeof(_ble));
		memset(&_state, 0, sizeof(_state));
	}

	// ==================================================
	// 초기화 (설정 구조 기반)
	// ==================================================
	void begin(const bool p_pirEnabled, uint32_t p_pirHoldSec,
			   const bool p_bleEnabled, int16_t p_bleRssi, uint32_t p_bleHoldSec) {
		memset(&_pir, 0, sizeof(_pir));
		memset(&_ble, 0, sizeof(_ble));
		memset(&_state, 0, sizeof(_state));

		_pir.enabled = p_pirEnabled;
		_pir.hold_sec = p_pirHoldSec;
		_pir.active = false;
		_pir.lastDetected_ms = 0;

		_ble.enabled = p_bleEnabled;
		_ble.rssi_threshold = p_bleRssi;
		_ble.hold_sec = p_bleHoldSec;
		_ble.active = false;
		_ble.lastDetected_ms = 0;

		_state.active = false;
		_state.lastUpdate_ms = millis();

		CL_D10_Logger::log(EN_L10_LOG_INFO,
			"[M10] begin pir=%d hold=%lu ble=%d rssi=%d hold=%lu",
			(int)p_pirEnabled, (unsigned long)p_pirHoldSec,
			(int)p_bleEnabled, (int)p_bleRssi, (unsigned long)p_bleHoldSec
		);
	}

	// ==================================================
	// PIR 감지 이벤트 갱신
	// ==================================================
	void notifyPIRDetected() {
		if (!_pir.enabled) return;
		_pir.lastDetected_ms = millis();
		_pir.active = true;
	}

	// ==================================================
	// BLE RSSI 갱신
	// ==================================================
	void updateBLE_RSSI(int16_t p_rssi) {
		if (!_ble.enabled) return;
		if (p_rssi >= _ble.rssi_threshold) {
			_ble.lastDetected_ms = millis();
			_ble.active = true;
		}
	}

	// ==================================================
	// 주기적 tick (1초 주기 권장)
	// ==================================================
	void tick() {
		uint32_t v_now = millis();

		if (_pir.enabled) {
			if (_pir.active && (v_now - _pir.lastDetected_ms > _pir.hold_sec * 1000UL)) {
				_pir.active = false;
			}
		}

		if (_ble.enabled) {
			if (_ble.active && (v_now - _ble.lastDetected_ms > _ble.hold_sec * 1000UL)) {
				_ble.active = false;
			}
		}

		bool v_newActive = (_pir.active || _ble.active);
		if (v_newActive != _state.active) {
			_state.active = v_newActive;
			_state.lastUpdate_ms = v_now;

			CL_D10_Logger::log(EN_L10_LOG_DEBUG,
				"[M10] motionActive changed: %d (PIR=%d BLE=%d)",
				(int)_state.active, (int)_pir.active, (int)_ble.active
			);
		}
	}

	// ==================================================
	// 활성 상태 조회
	// ==================================================
	bool isActive() const {
		return _state.active;
	}

	// ==================================================
	// 상태 구조 조회 (필요 시 UI 표시용)
	// ==================================================
	void getState(ST_M10_MotionState_t& p_out) const {
		p_out = _state;
	}

private:
	ST_M10_PIR_t        _pir;
	ST_M10_BLE_t        _ble;
	ST_M10_MotionState_t _state;
};
