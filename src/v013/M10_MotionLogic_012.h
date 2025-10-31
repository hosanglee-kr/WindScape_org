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



#pragma once
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

#include <Arduino.h>
#include <ArduinoJson.h>
#include <cstring>
#include "A10_Const_011.h"
#include "C10_ConfigManager_011.h"
#include "D10_Logger_011.h"

// ---------------------------------------------------------------------
// 기본 상수(컨피그 누락 시 안전 디폴트)
// ---------------------------------------------------------------------
#define G_M10_MAX_BLE_DEV            8
#define G_M10_DEFAULT_PIR_HOLD_S     120U
#define G_M10_DEFAULT_PIR_DEBOUNCE_S 5U
#define G_M10_DEFAULT_BLE_HOLD_S     120U

// ---------------------------------------------------------------------
// PIR / BLE 상태 구조체
// ---------------------------------------------------------------------
typedef struct {
	bool          enabled         = false;
	uint8_t       pin             = 255;   // 255 = 미사용
	uint16_t      debounce_sec    = G_M10_DEFAULT_PIR_DEBOUNCE_S;
	uint16_t      hold_sec        = G_M10_DEFAULT_PIR_HOLD_S;
	bool          active          = false; // hold 윈도 내 활성
	bool          _debounceArmed  = false;
	unsigned long _lastEdgeMs     = 0;
	unsigned long lastDetectMs    = 0;
} ST_M10_PIRState_t;

typedef struct {
	char          mac[18]   = {0};   // "AA:BB:CC:11:22:33"
	char          alias[16] = {0};
	bool          enabled   = true;
	int           lastRSSI  = -127;  // 최근 RSSI (참고용, 판정은 B10이 수행)
	unsigned long lastSeenMs= 0;     // 마지막 탐지 시각(millis)
	bool          active    = false; // hold 윈도 내 "최근 감지됨"
} ST_M10_BLEDevice_t;

typedef struct {
	bool          enabled       = false;
	uint16_t      hold_sec      = G_M10_DEFAULT_BLE_HOLD_S; // 최근 탐지 유지 시간
	// NOTE: rssi_threshold는 참조 필드로만 유지(직접 사용 X; B10에서 사용)
	int           rssi_threshold= -70;
	uint8_t       device_count  = 0;
	ST_M10_BLEDevice_t devices[G_M10_MAX_BLE_DEV];
} ST_M10_BLEState_t;

// ---------------------------------------------------------------------
// 모션 매니저 (저수준 입력 집계 + hold 유지 담당)
// ---------------------------------------------------------------------
class CL_M10_MotionManager {
public:
	// ===== 생명주기 =====
	void begin() {
		_applyConfigFromJson();
		if (_pir.enabled && _pir.pin != 255) {
			pinMode(_pir.pin, INPUT);
		}
		CL_D10_Logger::log(EN_L10_LOG_INFO,
		                   "[MOTION] begin() ok (PIR:%d, pin:%u, BLE:%d, dev:%u, hold_pir:%us, hold_ble:%us)",
		                   _pir.enabled, _pir.pin, _ble.enabled, _ble.device_count,
		                   _pir.hold_sec, _ble.hold_sec);
	}

	// 주기 실행
	void tick() {
		_tickPir();
		_tickBleHold();
	}

	// ===== 외부 이벤트(BLE 스캐너 결과 피드) =====
	/**
	 * @brief 외부 BLE 스캐너에서 탐지 결과 피드 (판정은 B10이 수행)
	 * @param p_mac  "AA:BB:..." 대소문자·콜론 포맷 허용(정규화됨)
	 * @param p_rssi RSSI(dBm), 평균/퍼시스턴스/임계는 B10에서 계산
	 */
	void feedBLE(const char* p_mac, int p_rssi) {
		if (!_ble.enabled || !p_mac) return;

		char v_mac[18] = {0};
		_normalizeMac(p_mac, v_mac, sizeof(v_mac));

		for (uint8_t v_i = 0; v_i < _ble.device_count; ++v_i) {
			auto &v_d = _ble.devices[v_i];
			if (!v_d.enabled) continue;
			if (_macEqual(v_d.mac, v_mac)) {
				v_d.lastRSSI   = p_rssi;           // 참고용
				v_d.lastSeenMs = millis();          // 최근 본 시각
				v_d.active     = true;              // hold 윈도 동안 "최근 감지됨"
				return;
			}
		}
	}

	// ===== 상태 질의 =====
	/** @brief PIR 또는 BLE 중 하나라도 hold 내 "활성"이면 true */
	bool isActive() const {
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
		jb["enabled"]       = _ble.enabled;
		jb["hold_s"]        = _ble.hold_sec;
		jb["rssi_threshold"]= _ble.rssi_threshold; // 참고용(직접 사용 X)

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

	// ===== 런타임 파라미터 조정(CT10에서 상황별 덮어쓰기) =====
	void setPirHold(uint16_t p_holdSec)   { _pir.hold_sec = (p_holdSec>0)?p_holdSec:G_M10_DEFAULT_PIR_HOLD_S; }
	void setPirDebounce(uint16_t p_sec)   { _pir.debounce_sec = (p_sec>0)?p_sec:G_M10_DEFAULT_PIR_DEBOUNCE_S; }
	void setBleHold(uint16_t p_holdSec)   { _ble.hold_sec = (p_holdSec>0)?p_holdSec:G_M10_DEFAULT_BLE_HOLD_S; }
	// NOTE: rssi_threshold는 B10에서만 사용 (여기서는 저장만)
	void setBleRefThreshold(int p_th)     { _ble.rssi_threshold = p_th; }

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
		if (g_A10_config_root.system.hw.pir.enabled) {
			_pir.enabled      = true;
			_pir.pin          = g_A10_config_root.system.hw.pir.pin;
			_pir.debounce_sec = (g_A10_config_root.system.hw.pir.debounce_sec>0)
			                    ? g_A10_config_root.system.hw.pir.debounce_sec
			                    : G_M10_DEFAULT_PIR_DEBOUNCE_S;
			// hold_sec은 기본값 유지(운영 모드에 따라 CT10에서 덮어쓰기)
		} else {
			_pir.enabled = false;
			_pir.pin     = 255;
		}

		_ble.enabled = g_A10_config_root.system.hw.ble.enabled;
		// scan_interval은 스캐너(B10)에서 사용, 본 모듈은 hold만 관리
		_ble.rssi_threshold = -70; // 참고용 기본값(직접 사용 X)

		// ----- 모션 cfg: devices[] -----
		_ble.device_count = 0;
		memset(_ble.devices, 0, sizeof(_ble.devices));

		if (g_A10_config_root.motion) {
			const auto &v_m = *g_A10_config_root.motion;
			if (v_m.ble.device_count > 0) {
				for (uint8_t v_i=0; v_i< v_m.ble.device_count && _ble.device_count < G_M10_MAX_BLE_DEV; ++v_i) {
					auto &dst = _ble.devices[_ble.device_count++];
					strlcpy(dst.mac,   v_m.ble.devices[v_i].mac,   sizeof(dst.mac));
					_toUpperHexMac(dst.mac);
					strlcpy(dst.alias, v_m.ble.devices[v_i].alias, sizeof(dst.alias));
					dst.enabled    = v_m.ble.devices[v_i].enabled;
					dst.lastRSSI   = -127;
					dst.lastSeenMs = 0;
					dst.active     = false;
				}
			}
		}

		// 상태 초기화
		_pir.active = false;
		_pir._debounceArmed = false;
		_pir._lastEdgeMs = 0;
		_pir.lastDetectMs = 0;
		for (uint8_t v_i=0; v_i<_ble.device_count; ++v_i) {
			_ble.devices[v_i].active = false;
			_ble.devices[v_i].lastSeenMs = 0;
			_ble.devices[v_i].lastRSSI = -127;
		}
	}

	// -----------------------------------------------------------------
	// 내부: PIR 처리 (디바운스 + hold)
	// -----------------------------------------------------------------
	void _tickPir() {
		if (!_pir.enabled || _pir.pin==255) { _pir.active = false; return; }

		unsigned long v_now = millis();
		int v_in = digitalRead(_pir.pin);

		// 엣지 감지 → 디바운스 윈도 시작
		if (v_in == HIGH) {
			if (!_pir._debounceArmed) {
				_pir._debounceArmed = true;
				_pir._lastEdgeMs = v_now;
			} else {
				// 디바운스 경과 시 유효 감지 확정
				if ((v_now - _pir._lastEdgeMs) >= (_pir.debounce_sec * 1000UL)) {
					_pir.lastDetectMs = v_now;
					_pir.active = true;
				}
			}
		}

		// hold 만료
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
	// 내부: BLE hold 처리 (최근 본 시각 기반)
	// -----------------------------------------------------------------
	void _tickBleHold() {
		if (!_ble.enabled) return;

		unsigned long v_now = millis();
		unsigned long v_holdMs = (_ble.hold_sec>0 ? _ble.hold_sec : G_M10_DEFAULT_BLE_HOLD_S) * 1000UL;

		for (uint8_t v_i=0; v_i<_ble.device_count; ++v_i) {
			auto &v_d = _ble.devices[v_i];
			if (!v_d.enabled) { v_d.active = false; continue; }

			bool v_recent = (v_now - v_d.lastSeenMs) <= v_holdMs;
			// ⚠️ RSSI 임계/평균/퍼시스턴스 판정은 B10에서 수행
			if (v_recent) {
				v_d.active = true;
			} else {
				if (v_d.active) {
					CL_D10_Logger::log(EN_L10_LOG_INFO,
						"[MOTION] BLE '%s' hold expired (lastSeen=%lu ms ago)",
						v_d.alias, (unsigned long)(v_now - v_d.lastSeenMs));
				}
				v_d.active = false;
			}
		}
	}

	// -----------------------------------------------------------------
	// 내부: MAC 유틸
	// -----------------------------------------------------------------
	static void _normalizeMac(const char* p_src, char* p_dst, size_t p_dstLen) {
		strlcpy(p_dst, p_src, p_dstLen); // 원형 복사
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
		// 콜론 포함 그대로 비교(대문자화 전제, 17자)
		return (strncmp(a, b, 17) == 0);
	}

private:
	ST_M10_PIRState_t _pir;
	ST_M10_BLEState_t _ble;
};
