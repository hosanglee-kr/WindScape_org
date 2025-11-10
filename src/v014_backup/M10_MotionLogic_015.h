#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : M10_MotionLogic_015.h
 * 모듈약어 : M10
 * 모듈명 : Smart Nature Wind Motion Logic Manager (v015)
 * ------------------------------------------------------
 * 기능 요약
 *  - PIR 센서 및 BLE RSSI 근접 감지 통합 제어
 *  - hold_sec 기반 유지 로직 (최근 감지 후 일정시간 활성 유지)
 *  - BLE 신호는 외부 스캐너에서 RSSI 업데이트만 전달받음
 *  - CT10_ControlManager에서 tick() 호출 및 상태 조회
 *  - JSON 직렬화(toJson) 및 cfg_system_xxx.json 로드(loadFromJson)
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
#include <ArduinoJson.h>
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
	int16_t  last_rssi;
} ST_M10_BLE_t;

typedef struct {
	bool        active;
	bool        pirActive;
	bool        bleActive;
	uint32_t    lastChange_ms;
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

	// --------------------------------------------------
	// JSON 로드 초기화 (cfg_system_xxx.json)
	// --------------------------------------------------
	bool loadFromJson(JsonDocument& p_doc) {
		if (!p_doc["motion"].is<JsonObjectConst>()) return false;
		auto v_m = p_doc["motion"].as<JsonObjectConst>();

		_pir.enabled     = v_m["pir"]["enabled"]      | false;
		_pir.hold_sec    = v_m["pir"]["hold_sec"]     | 20;
		_ble.enabled     = v_m["ble"]["enabled"]      | false;
		_ble.rssi_threshold = v_m["ble"]["rssi_threshold"] | -70;
		_ble.hold_sec    = v_m["ble"]["hold_sec"]     | 15;

		memset(&_state, 0, sizeof(_state));
		CL_D10_Logger::log(EN_L10_LOG_INFO,
			"[M10] loadFromJson pir=%d hold=%lu ble=%d rssi=%d hold=%lu",
			(int)_pir.enabled, (unsigned long)_pir.hold_sec,
			(int)_ble.enabled, (int)_ble.rssi_threshold,
			(unsigned long)_ble.hold_sec
		);
		return true;
	}

	// --------------------------------------------------
	// PIR 감지 이벤트
	// --------------------------------------------------
	void notifyPIRDetected() {
		if (!_pir.enabled) return;
		_pir.lastDetected_ms = millis();
		_pir.active = true;
	}

	// --------------------------------------------------
	// BLE RSSI 입력 갱신
	// --------------------------------------------------
	void updateBLE_RSSI(int16_t p_rssi) {
		if (!_ble.enabled) return;
		_ble.last_rssi = p_rssi;
		if (p_rssi >= _ble.rssi_threshold) {
			_ble.lastDetected_ms = millis();
			_ble.active = true;
		}
	}

	// --------------------------------------------------
	// tick 루프 (CT10에서 주기 호출)
	// --------------------------------------------------
	void tick() {
		uint32_t v_now = millis();

		// PIR timeout
		if (_pir.enabled && _pir.active) {
			if (v_now - _pir.lastDetected_ms > _pir.hold_sec * 1000UL) {
				_pir.active = false;
			}
		}

		// BLE timeout
		if (_ble.enabled && _ble.active) {
			if (v_now - _ble.lastDetected_ms > _ble.hold_sec * 1000UL) {
				_ble.active = false;
			}
		}

		// 상태 변화 감지
		bool v_activeNew = (_pir.active || _ble.active);
		if (v_activeNew != _state.active) {
			_state.active = v_activeNew;
			_state.lastChange_ms = v_now;
			_state.pirActive = _pir.active;
			_state.bleActive = _ble.active;

			CL_D10_Logger::log(EN_L10_LOG_DEBUG,
				"[M10] motionActive=%d (PIR=%d BLE=%d)",
				(int)_state.active, (int)_pir.active, (int)_ble.active
			);
		}
	}

	// --------------------------------------------------
	// 상태 직렬화
	// --------------------------------------------------
	void toJson(JsonDocument& p_doc) const {
		JsonObject o = p_doc["motion"].to<JsonObject>();
		o["active"]     = _state.active;
		o["pirActive"]  = _pir.active;
		o["bleActive"]  = _ble.active;
		o["pirHold"]    = _pir.hold_sec;
		o["bleHold"]    = _ble.hold_sec;
		o["bleRssi"]    = _ble.last_rssi;
		o["lastChange"] = _state.lastChange_ms;
	}

	// --------------------------------------------------
	// 외부에서 활성여부 확인
	// --------------------------------------------------
	bool isActive() const { return _state.active; }

private:
	ST_M10_PIR_t          _pir;
	ST_M10_BLE_t          _ble;
	ST_M10_MotionState_t  _state;
};
