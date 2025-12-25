#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : M10_MotionLogic_040.h
 * 모듈약어 : M10
 * 모듈명 : Smart Nature Wind Motion Logic Manager (v016)
 * ------------------------------------------------------
 * 기능 요약
 *  - PIR 센서 및 BLE RSSI 근접 감지 통합 제어
 *  - holdSec 기반 유지 로직 (최근 감지 후 일정시간 활성 유지)
 *  - BLE 신호는 외부 스캐너에서 RSSI 업데이트만 전달받음
 *  - CT10_ControlManager에서 tick() 호출 및 상태 조회
 *  - JSON 직렬화(toJson) 및 cfg_system_xxx.json 로드(loadFromJson)
 *  - 상태 변화 시 콜백(OnChange) 제공 (CT10 등에서 WebSocket diffOnly 활용 가능)
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

#include "A20_Const_040.h"
#include "D10_Logger_040.h"

// ------------------------------------------------------
// 구조체 정의
// ------------------------------------------------------
typedef struct {
	bool	 enabled;
	uint32_t holdSec;
	uint32_t lastDetected_ms;
	bool	 active;
} ST_M10_PIR_t;

typedef struct {
	bool	 enabled;
	int16_t	 rssi_threshold;
	uint32_t holdSec;
	uint32_t lastDetected_ms;
	bool	 active;
	int16_t	 last_rssi;
} ST_M10_BLE_t;

typedef struct {
	bool	 active;
	bool	 pirActive;
	bool	 bleActive;
	uint32_t lastChange_ms;
} ST_M10_MotionState_t;

// 전역 인스턴스 포인터 선언
extern CL_M10_MotionLogic* g_M10_motionLogic;

// 콜백 타입 (상태 변경 시 통지용)
//  - CT10 등에서 등록하여 diffOnly WebSocket 브로드캐스트 등에 활용
typedef void (*T_M10_OnChangeCallback_t)(const ST_M10_MotionState_t& p_state);

// ------------------------------------------------------
// CL_M10_MotionLogic
// ------------------------------------------------------
class CL_M10_MotionLogic {
  public:
	// [추가됨] 정적 초기화 함수 (A00/Main 진입용)
	// --------------------------------------------------
	static void M10_begin() {
		static CL_M10_MotionLogic s_instance;
		g_M10_motionLogic = &s_instance;
		CL_D10_Logger::log(EN_L10_LOG_INFO, "[M10] MotionLogic initialized");
	}

	CL_M10_MotionLogic() {
		memset(&_pir, 0, sizeof(_pir));
		memset(&_ble, 0, sizeof(_ble));
		memset(&_state, 0, sizeof(_state));
		_onChange = nullptr;
	}

	// --------------------------------------------------
	// [추가됨] PIR 감지 상태 전달 (웹/MQTT 용)
	// --------------------------------------------------
	/*
	 * @brief 외부 소스로부터 PIR 감지 상태를 전달받아 처리합니다.
	 * @param p_detected 감지 여부 (true일 경우 notifyPIRDetected() 호출)
	 */
	void feedPIR(bool p_detected) {
		if (p_detected) {
			// true일 경우만 기존 감지 로직(타이머 리셋)을 실행합니다.
			notifyPIRDetected();
		}
		// false인 경우, 센서의 감지 타이머는 tick()에 의해 자연스럽게 만료되도록 둡니다.
	}

	// --------------------------------------------------
	// [추가됨] BLE 감지 상태 전달 (웹/MQTT 용)
	// --------------------------------------------------
	/**
	 * @brief 외부 소스로부터 BLE 감지 상태를 전달받아 처리합니다.
	 * @param p_detected 감지 여부 (true일 경우 BLE 로직을 활성화합니다. RSSI 값은 무시)
	 */
	void feedBLE(bool p_detected) {
		if (p_detected) {
			// 웹 API에서는 RSSI 값이 아닌, 감지 여부(true)만 전달하므로,
			// 임시로 활성 임계값을 만족하는 RSSI를 가정하여 updateBLE_RSSI를 호출합니다.

			// NOTE: _ble.rssi_threshold가 -70이라고 가정할 때, -60이 threshold보다 크므로 활성화됩니다.
			// 0이 더 안전하지만, RSSI는 음수이므로, -1을 사용합니다.
			int16_t v_activeRssi = -1;

			if (_ble.enabled) {
				_ble.last_rssi		 = v_activeRssi;  // 마지막 RSSI 기록
				_ble.lastDetected_ms = millis();
				_ble.active			 = true;
			}
		}
		// false인 경우, 센서의 감지 타이머는 tick()에 의해 자연스럽게 만료되도록 둡니다.
	}

	// --------------------------------------------------
	// JSON 로드 초기화 (cfg_system_xxx.json)
	// --------------------------------------------------
	bool loadFromJson(JsonDocument& p_doc) {
		if (!p_doc["motion"].is<JsonObjectConst>())
			return false;
		JsonObjectConst v_m = p_doc["motion"].as<JsonObjectConst>();

		_pir.enabled		= v_m["pir"]["enabled"] | false;
		_pir.holdSec		= v_m["pir"]["holdSec"] | 20;

		_ble.enabled		= v_m["ble"]["enabled"] | false;
		_ble.rssi_threshold = v_m["ble"]["rssi_threshold"] | -70;
		_ble.holdSec		= v_m["ble"]["holdSec"] | 15;

		memset(&_pir.lastDetected_ms, 0, sizeof(_pir.lastDetected_ms));
		_pir.active = false;

		memset(&_ble.lastDetected_ms, 0, sizeof(_ble.lastDetected_ms));
		_ble.active	   = false;
		_ble.last_rssi = 0;

		memset(&_state, 0, sizeof(_state));

		CL_D10_Logger::log(EN_L10_LOG_INFO, "[M10] loadFromJson pir=%d hold=%lu ble=%d rssi=%d hold=%lu", (int)_pir.enabled, (unsigned long)_pir.holdSec, (int)_ble.enabled, (int)_ble.rssi_threshold, (unsigned long)_ble.holdSec);
		return true;
	}

	// --------------------------------------------------
	// PIR 감지 이벤트
	// --------------------------------------------------
	void notifyPIRDetected() {
		if (!_pir.enabled)
			return;
		_pir.lastDetected_ms = millis();
		_pir.active			 = true;
	}

	// --------------------------------------------------
	// BLE RSSI 입력 갱신
	// --------------------------------------------------
	void updateBLE_RSSI(int16_t p_rssi) {
		if (!_ble.enabled)
			return;

		_ble.last_rssi = p_rssi;
		if (p_rssi >= _ble.rssi_threshold) {
			_ble.lastDetected_ms = millis();
			_ble.active			 = true;
		}
	}

	// --------------------------------------------------
	// tick 루프 (CT10에서 주기 호출)
	// --------------------------------------------------
	void tick() {
		uint32_t v_now = millis();

		// PIR timeout
		if (_pir.enabled && _pir.active) {
			uint32_t v_pirElapsed = v_now - _pir.lastDetected_ms;
			if (v_pirElapsed > _pir.holdSec * 1000UL) {
				_pir.active = false;
			}
		}

		// BLE timeout
		if (_ble.enabled && _ble.active) {
			uint32_t v_bleElapsed = v_now - _ble.lastDetected_ms;
			if (v_bleElapsed > _ble.holdSec * 1000UL) {
				_ble.active = false;
			}
		}

		// 상태 변화 감지
		bool v_activeNew = (_pir.active || _ble.active);
		bool v_pirActive = _pir.active;
		bool v_bleActive = _ble.active;

		if (v_activeNew != _state.active || v_pirActive != _state.pirActive || v_bleActive != _state.bleActive) {
			_state.active		 = v_activeNew;
			_state.pirActive	 = v_pirActive;
			_state.bleActive	 = v_bleActive;
			_state.lastChange_ms = v_now;

			CL_D10_Logger::log(EN_L10_LOG_DEBUG, "[M10] motionActive=%d (PIR=%d BLE=%d)", (int)_state.active, (int)_state.pirActive, (int)_state.bleActive);

			// 외부 연동용 콜백 (CT10 등에서 diffOnly 푸시 활용)
			if (_onChange) {
				_onChange(_state);
			}
		}
	}

	// --------------------------------------------------
	// 상태 직렬화
	// --------------------------------------------------
	void toJson(JsonDocument& p_doc) const {
		JsonObject v_o			= p_doc["motion"].to<JsonObject>();
		v_o["active"]			= isActive();
		v_o["pirActive"]		= _state.pirActive;
		v_o["bleActive"]		= _state.bleActive;
		v_o["pirHold"]			= _pir.holdSec;
		v_o["bleHold"]			= _ble.holdSec;
		v_o["bleRssi"]			= _ble.last_rssi;
		// v_o["lastChange"] = _state.lastChange_ms;
		unsigned long v_now		= millis();
		uint32_t	  v_lastSec = (_state.lastChange_ms == 0) ? 0 : (uint32_t)((v_now - _state.lastChange_ms) / 1000UL);
		v_o["lastActiveSec"]	= v_lastSec;

		// 남은 hold 시간 (초 단위)
		uint32_t v_pirRemain	= 0;
		uint32_t v_bleRemain	= 0;

		if (_pir.enabled && _pir.active) {
			uint32_t v_pirElapsed = v_now - _pir.lastDetected_ms;
			if (v_pirElapsed < _pir.holdSec * 1000UL) {
				v_pirRemain = (_pir.holdSec * 1000UL - v_pirElapsed) / 1000UL;
			}
		}

		if (_ble.enabled && _ble.active) {
			uint32_t v_bleElapsed = v_now - _ble.lastDetected_ms;
			if (v_bleElapsed < _ble.holdSec * 1000UL) {
				v_bleRemain = (_ble.holdSec * 1000UL - v_bleElapsed) / 1000UL;
			}
		}

		v_o["pirHoldRemain"] = v_pirRemain;
		v_o["bleHoldRemain"] = v_bleRemain;
	}

	// --------------------------------------------------
	// 외부에서 활성여부 확인
	// --------------------------------------------------
	bool isActive() const {
		unsigned long v_now	 = millis();
		// holdSec 로직은 외부 config 사용 (예: g_A20_config_root.motion)
		uint32_t	  v_hold = 0;
		if (g_A20_config_root.motion) {
			v_hold = (uint32_t)g_A20_config_root.motion->pir.holdSec;
		}
		if (_state.pirActive || _state.bleActive)
			// if (pirActive || bleActive)
			return true;
		if (v_hold > 0 && (v_now - _state.lastChange_ms) < (v_hold * 1000UL))
			return true;
		return false;
	}

	// --------------------------------------------------
	// 상태 변경 콜백 등록
	//  - CT10 등에서 등록하여 상태 변경 시 diffOnly 브로드캐스트 트리거
	// --------------------------------------------------
	void setOnChangeCallback(T_M10_OnChangeCallback_t p_cb) {
		_onChange = p_cb;
	}

  private:
	ST_M10_PIR_t			 _pir;
	ST_M10_BLE_t			 _ble;
	ST_M10_MotionState_t	 _state;
	T_M10_OnChangeCallback_t _onChange;
};
