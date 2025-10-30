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

#include <Arduino.h>
#include <ArduinoJson.h>
#include <cstring>

#include "A10_Const_011.h"
#include "C10_ConfigManager_011.h"
#include "D10_Logger_011.h"

// ---------------------------------------------------------------------
// 기본 상수(컨피그 누락 시 안전 디폴트)
// ---------------------------------------------------------------------
#define G_M10_MAX_BLE_DEV          8
#define G_M10_DEFAULT_PIR_HOLD_S   120U
#define G_M10_DEFAULT_PIR_DEBOUNCE 5U
#define G_M10_DEFAULT_BLE_HOLD_S   120U
#define G_M10_DEFAULT_BLE_RSSI_TH  (-70)

// ---------------------------------------------------------------------
// PIR / BLE 상태 구조체
// ---------------------------------------------------------------------
typedef struct {
	bool          enabled         = false;
	uint8_t       pin             = 255;
	uint16_t      debounce_sec    = G_M10_DEFAULT_PIR_DEBOUNCE;
	uint16_t      hold_sec        = G_M10_DEFAULT_PIR_HOLD_S;
	bool          active          = false;
	bool          _debounceArmed  = false;
	unsigned long _lastEdgeMs     = 0;
	unsigned long lastDetectMs    = 0;
} ST_M10_PIRState_t;

typedef struct {
	char  mac[18]   = {0};   // "AA:BB:CC:11:22:33"
	char  alias[16] = {0};
	bool  enabled   = true;
	int   lastRSSI  = -127;
	unsigned long lastSeenMs = 0;
	bool  active    = false;
} ST_M10_BLEDevice_t;

typedef struct {
	bool          enabled       = false;
	uint16_t      hold_sec      = G_M10_DEFAULT_BLE_HOLD_S;
	int           rssi_threshold= G_M10_DEFAULT_BLE_RSSI_TH;
	uint8_t       device_count  = 0;
	ST_M10_BLEDevice_t devices[G_M10_MAX_BLE_DEV];
	unsigned long lastUpdateMs  = 0;
} ST_M10_BLEState_t;

// ---------------------------------------------------------------------
// 모션 매니저
// ---------------------------------------------------------------------
class CL_M10_MotionManager {
public:
	// ===== 생명주기 =====
	void begin() {
		_applyConfigFromJson();
		// PIR 핀모드 준비
		if (_pir.enabled && _pir.pin != 255) {
			pinMode(_pir.pin, INPUT);
		}
		CL_D10_Logger::log(EN_L10_LOG_INFO, "[MOTION] begin() ok (PIR:%d, BLE:%d, dev:%u)",
		                   _pir.enabled, _ble.enabled, _ble.device_count);
	}

	// 주기 실행
	void tick() {
		_tickPir();
		_tickBleAging();
	}

	// ===== 외부 이벤트(스캐너 등) 입력 =====
	/**
	 * @brief 외부 BLE 스캐너에서 탐지 결과 피드
	 * @param p_mac  "AA:BB:..." 대소문자 무관
	 * @param p_rssi RSSI (dBm)
	 */
	void feedBLE(const char* p_mac, int p_rssi) {
		if (!_ble.enabled || !p_mac) return;

		// MAC 정규화(대문자, 콜론 유지)
		char v_mac[18] = {0};
		_normalizeMac(p_mac, v_mac, sizeof(v_mac));

		for (uint8_t v_i = 0; v_i < _ble.device_count; ++v_i) {
			auto &v_d = _ble.devices[v_i];
			if (!v_d.enabled) continue;
			if (_macEqual(v_d.mac, v_mac)) {
				v_d.lastRSSI   = p_rssi;
				v_d.lastSeenMs = millis();
				// 임계치 초과 즉시 active 표기(홀드 시간은 aging에서 관리)
				if (p_rssi > _ble.rssi_threshold) {
					v_d.active = true;
				}
				return;
			}
		}
	}

	// ===== 상태 질의 =====
	bool isActive() const {
		// PIR or (BLE에 활성 dev 하나라도)
		if (_pir.active) return true;
		if (_ble.enabled) {
			for (uint8_t v_i = 0; v_i < _ble.device_count; ++v_i) {
				if (_ble.devices[v_i].enabled && _ble.devices[v_i].active) return true;
			}
		}
		return false;
	}

	// ===== JSON 직렬화 =====
	void toJson(JsonDocument& p_doc) const {
		JsonObject o = p_doc["motion"].to<JsonObject>();
		o["active"] = isActive();

		// PIR
		JsonObject jp = o["pir"].to<JsonObject>();
		jp["enabled"]     = _pir.enabled;
		jp["active"]      = _pir.active;
		jp["pin"]         = _pir.pin;
		jp["debounce_s"]  = _pir.debounce_sec;
		jp["hold_s"]      = _pir.hold_sec;
		jp["last_ms"]     = _pir.lastDetectMs;

		// BLE
		JsonObject jb = o["ble"].to<JsonObject>();
		jb["enabled"]      = _ble.enabled;
		jb["hold_s"]       = _ble.hold_sec;
		jb["rssi_threshold"]= _ble.rssi_threshold;

		bool v_bleActive = false;
		for (uint8_t v_i=0; v_i<_ble.device_count; ++v_i) {
			if (_ble.devices[v_i].enabled && _ble.devices[v_i].active) { v_bleActive = true; break; }
		}
		jb["active"] = v_bleActive;

		JsonArray arr = jb["devices"].to<JsonArray>();
		for (uint8_t v_i = 0; v_i < _ble.device_count; ++v_i) {
			const auto &d = _ble.devices[v_i];
			JsonObject jd = arr.add<JsonObject>();
			jd["alias"]     = d.alias;
			jd["mac"]       = d.mac;
			jd["enabled"]   = d.enabled;
			jd["rssi"]      = d.lastRSSI;
			jd["active"]    = d.active;
			jd["last_ms"]   = d.lastSeenMs;
		}
	}

	// ===== 런타임 파라미터 조정(CT10 등에서 호출 가능) =====
	void setPirHold(uint16_t p_holdSec)   { _pir.hold_sec = (p_holdSec>0)?p_holdSec:G_M10_DEFAULT_PIR_HOLD_S; }
	void setPirDebounce(uint16_t p_sec)   { _pir.debounce_sec = (p_sec>0)?p_sec:G_M10_DEFAULT_PIR_DEBOUNCE; }
	void setBleHold(uint16_t p_holdSec)   { _ble.hold_sec = (p_holdSec>0)?p_holdSec:G_M10_DEFAULT_BLE_HOLD_S; }
	void setBleRssiThreshold(int p_th)    { _ble.rssi_threshold = p_th; }

	// 설정 재적용(시스템/모션 JSON 변경 시)
	void reloadConfig() {
		_applyConfigFromJson();
		if (_pir.enabled && _pir.pin != 255) pinMode(_pir.pin, INPUT);
	}

private:
	// -----------------------------------------------------------------
	// 내부: 설정 반영
	// -----------------------------------------------------------------
	void _applyConfigFromJson() {
		// ----- 시스템 cfg: hw.pir / hw.ble -----
		if (g_A10_config_root.core.hw.pir.enabled) {
			_pir.enabled      = true;
			_pir.pin          = g_A10_config_root.core.hw.pir.pin;
			_pir.debounce_sec = (g_A10_config_root.core.hw.pir.debounce_sec>0)
			                    ? g_A10_config_root.core.hw.pir.debounce_sec
			                    : G_M10_DEFAULT_PIR_DEBOUNCE;
			// PIR hold는 control에서 주는 경우가 많아 기본값 유지. 필요 시 CT10에서 setPirHold 사용.
		} else {
			_pir.enabled = false;
		}

		if (g_A10_config_root.core.hw.ble.enabled) {
			_ble.enabled = true;
			// scan_interval은 스캐너 측(별도)에서 사용, 본 모듈은 aging만 수행
		} else {
			_ble.enabled = false;
		}

		// ----- 모션 cfg: devices[] -----
		_ble.device_count = 0;
		memset(_ble.devices, 0, sizeof(_ble.devices));

		if (g_A10_config_root.motion) {
			// cfg_motion_022.json: motion.ble.devices[]
			const auto &v_m = *g_A10_config_root.motion;
			// PIR hold/ble hold/rssi threshold가 motion/continuous/schedule에 있을 수 있으므로
			// 여기서는 디폴트 유지 → CT10이 상황별로 set* API로 덮어쓰기
			if (v_m.ble.device_count > 0) {
				for (uint8_t v_i=0; v_i< v_m.ble.device_count && _ble.device_count < G_M10_MAX_BLE_DEV; ++v_i) {
					auto &dst = _ble.devices[_ble.device_count++];
					strlcpy(dst.mac,   v_m.ble.devices[v_i].mac,   sizeof(dst.mac));
					_toUpperHexMac(dst.mac);
					strlcpy(dst.alias, v_m.ble.devices[v_i].alias, sizeof(dst.alias));
					dst.enabled = v_m.ble.devices[v_i].enabled;
					dst.lastRSSI = -127;
					dst.lastSeenMs = 0;
					dst.active = false;
				}
			}
		}

		_pir.active = false;
		for (uint8_t v_i=0; v_i<_ble.device_count; ++v_i) _ble.devices[v_i].active = false;
	}

	// -----------------------------------------------------------------
	// 내부: PIR 처리
	// -----------------------------------------------------------------
	void _tickPir() {
		if (!_pir.enabled || _pir.pin==255) { _pir.active = false; return; }

		unsigned long v_now = millis();
		int v_in = digitalRead(_pir.pin);

		// 엣지 → 디바운스 윈도우 시작
		if (v_in == HIGH) {
			if (!_pir._debounceArmed) {
				_pir._debounceArmed = true;
				_pir._lastEdgeMs = v_now;
			} else {
				// 디바운스 기간 통과 시 유효 감지로 확정
				if ((v_now - _pir._lastEdgeMs) >= (_pir.debounce_sec * 1000UL)) {
					_pir.lastDetectMs = v_now;
					_pir.active = true;
				}
			}
		}

		// 홀드 만료
		if (_pir.active) {
			unsigned long v_holdMs = (_pir.hold_sec>0 ? _pir.hold_sec : G_M10_DEFAULT_PIR_HOLD_S) * 1000UL;
			if ((v_now - _pir.lastDetectMs) > v_holdMs) {
				_pir.active = false;
				_pir._debounceArmed = false;
				CL_D10_Logger::log(EN_L10_LOG_INFO, "[MOTION] PIR hold expired");
			}
		}
	}

	// -----------------------------------------------------------------
	// 내부: BLE 에이징/홀드 처리
	// -----------------------------------------------------------------
	void _tickBleAging() {
		if (!_ble.enabled) return;

		unsigned long v_now = millis();
		unsigned long v_holdMs = (_ble.hold_sec>0 ? _ble.hold_sec : G_M10_DEFAULT_BLE_HOLD_S) * 1000UL;

		for (uint8_t v_i=0; v_i<_ble.device_count; ++v_i) {
			auto &v_d = _ble.devices[v_i];
			if (!v_d.enabled) { v_d.active = false; continue; }

			// RSSI 임계 초과이면서 최근에 본 것 → active 유지
			bool v_recent = (v_now - v_d.lastSeenMs) <= v_holdMs;
			if (v_recent && (v_d.lastRSSI > _ble.rssi_threshold)) {
				v_d.active = true;
			} else {
				// 홀드 만료/임계 미만 → 비활성
				if (v_d.active) {
					// 한번이라도 active였다가 내려갈 때만 로그
					if (!v_recent || v_d.lastRSSI <= _ble.rssi_threshold) {
						CL_D10_Logger::log(EN_L10_LOG_INFO,
							"[MOTION] BLE '%s' inactive (rssi=%d, recent=%d)",
							v_d.alias, v_d.lastRSSI, (int)v_recent);
					}
				}
				v_d.active = false;
			}
		}
	}

	// -----------------------------------------------------------------
	// 내부: MAC 유틸
	// -----------------------------------------------------------------
	static void _normalizeMac(const char* p_src, char* p_dst, size_t p_dstLen) {
		// 기대 포맷 "XX:XX:XX:XX:XX:XX"
		strlcpy(p_dst, p_src, p_dstLen);
		_toUpperHexMac(p_dst);
	}

	static void _toUpperHexMac(char* p_mac) {
		for (size_t v_i=0; p_mac[v_i]; ++v_i) {
			char c = p_mac[v_i];
			if (c >= 'a' && c <= 'f') p_mac[v_i] = (char)(c - 32);
			else p_mac[v_i] = (char)toupper((unsigned char)c);
		}
	}

	static bool _macEqual(const char* a, const char* b) {
		// 콜론 포함 그대로 비교(대문자화 전제)
		return (strncmp(a, b, 17) == 0);
	}

private:
	ST_M10_PIRState_t _pir;
	ST_M10_BLEState_t _ble;
};
