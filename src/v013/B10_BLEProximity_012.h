#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : B10_BLEProximity_012.h
 * 모듈약어 : B10
 * 모듈명 : Smart Nature Wind BLE 근접 스캐너 (v012)
 * ------------------------------------------------------
 * 기능 요약:
 *  - NimBLE-Arduino 기반 패시브 스캔
 *  - 화이트리스트(이름/정확 MAC/제조사 프리픽스) 매칭
 *  - RSSI 히스테리시스(on/off), 이동 평균, 지속 카운트
 *  - FSM: Idle → Presence → ExitDelay (지연 후 OFF)
 *  - Exit Delay, Fail-safe(미감지 시 기본풍 선택 여지)
 *  - BLE 스택 장시간 운용 안정화(주기적 재시작)
 *  - 설정: cfg_motion_022.json + (필요 시) control의 rssi 정책
 *  - JSON 직렬화: 상태/매칭/평균값
 * ------------------------------------------------------
  * [구현 규칙]
 *  - 항상 소스 시작 주석 체계 유지
 *  - ArduinoJson v7.x.x 사용 (v6 이하 사용 금지)
 *  - JsonDocument 단일 타입만 사용
 *  - createNestedArray/Object/containsKey 사용 금지
 *  - memset + strlcpy 기반 안전 초기화
 *  - 주석/필드명은 JSON 구조와 동일하게 유지
 *  - 모듈별 단일 헤더(h)파일로만 구성 (cpp 없음)
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
 *  - A10_Const_011.h, C10_ConfigManager_011.h (022 스키마)
 *  - D10_Logger_010.h
 *  - NimBLE-Arduino
 *  - ArduinoJson v7 (JsonDocument만 사용)
 * ------------------------------------------------------
 */

#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>
#include <cstring>

#include "A10_Const_011.h"
#include "C10_ConfigManager_011.h"
#include "D10_Logger_010.h"

#include <NimBLEDevice.h>   // NimBLE-Arduino

// ======================================================
// 기본 상수 (디폴트 정책)
// ======================================================
#define G_B10_RSSI_ON_DEFAULT        (-65)
#define G_B10_RSSI_OFF_DEFAULT       (-75)
#define G_B10_AVG_COUNT_DEFAULT      (8)    // 이동 평균 샘플 수
#define G_B10_PERSIST_COUNT_DEFAULT  (5)    // 연속 만족 횟수
#define G_B10_EXIT_DELAY_DEFAULT     (12)   // sec
#define G_B10_SCAN_INTERVAL_DEFAULT  (5)    // sec (cfg_system_022.hw.ble.scan_interval)

#define G_B10_MAX_TRUSTED            (12)
#define G_B10_STACK_RESTART_MIN      (30*60) // 30분마다 소프트 재시작 권장(초)
#define G_B10_NAME_MAX               24

// ======================================================
// 열거/구조체
// ======================================================
typedef enum : uint8_t {
	EN_B10_FSM_IDLE = 0,
	EN_B10_FSM_PRESENCE = 1,
	EN_B10_FSM_EXIT_DELAY = 2
} EN_B10_fsm_state_t;

typedef struct {
	char alias[16]         = {0};
	char name[G_B10_NAME_MAX] = {0};     // 광고 Name 매칭용(선택)
	char mac[18]           = {0};        // "AA:BB:CC:11:22:33" 정확 매칭(선택)
	char manuf_prefix[16]  = {0};        // "4C0002" 등 HEX 문자열(선택)
	uint8_t prefix_len     = 0;          // 바이트 단위 길이(3 = "4C 00 02")
	bool enabled           = true;

	// 런타임
	int   last_rssi        = -127;
	int   avg_rssi         = -127;
	uint8_t avg_count      = 0;
	uint8_t persist_on     = 0;
	uint8_t persist_off    = 0;
	unsigned long last_seen_ms = 0;
	bool  near             = false;      // 히스테리시스 고려 근접 판정
} ST_B10_trusted_t;

typedef struct {
	int  rssi_on           = G_B10_RSSI_ON_DEFAULT;
	int  rssi_off          = G_B10_RSSI_OFF_DEFAULT;
	uint8_t avg_count      = G_B10_AVG_COUNT_DEFAULT;
	uint8_t persist_count  = G_B10_PERSIST_COUNT_DEFAULT;
	uint16_t exit_delay_sec= G_B10_EXIT_DELAY_DEFAULT;
} ST_B10_policy_t;

// ======================================================
// 스캔 콜백
// ======================================================
class CL_B10_ScanCallbacks : public NimBLEAdvertisedDeviceCallbacks {
public:
	explicit CL_B10_ScanCallbacks(class CL_B10_BLEProximity* p_owner) : _owner(p_owner) {}
	void onResult(NimBLEAdvertisedDevice* p_adv) override;
private:
	class CL_B10_BLEProximity* _owner;
};

// ======================================================
// 본체 클래스
// ======================================================
class CL_B10_BLEProximity {
public:
	CL_B10_BLEProximity() = default;

	// ----------------------------------------------
	// 초기화 / 시작/정지
	// ----------------------------------------------
	bool begin(const char* p_devName = "NatureWind-BLE") {
		_applyConfigFromJson();

		// NimBLE 초기화
		NimBLEDevice::init(p_devName ? p_devName : "NatureWind-BLE");
		NimBLEDevice::setPower(ESP_PWR_LVL_P9);     // 필요 시 조정
		NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

		_scan = NimBLEDevice::getScan();
		if (!_scan) {
			CL_D10_Logger::log(EN_L10_LOG_ERROR, "[BLE] getScan() failed");
			return false;
		}
		_scan->setAdvertisedDeviceCallbacks(&_callbacks, false /*dup*/);
		_scan->setActiveScan(false);                // Passive Scan ✅
		_scan->setInterval(45);                     // 0.625ms units → 약 28ms
		_scan->setWindow(30);                       // 0.625ms units → 약 19ms

		_callbacks = CL_B10_ScanCallbacks(this);

		_state = EN_B10_FSM_IDLE;
		_present = false;
		_exitStartMs = 0;
		_lastStackRestartSec = (millis()/1000UL);

		CL_D10_Logger::log(EN_L10_LOG_INFO, "[BLE] begin() ok, trusted=%u, policy: on=%d off=%d avg=%u persist=%u exit=%us",
		                   (unsigned)_trusted.size(), _policy.rssi_on, _policy.rssi_off,
		                   _policy.avg_count, _policy.persist_count, _policy.exit_delay_sec);
		return true;
	}

	void start() {
		if (!_scan) return;
		_scan->clearResults();
		_scan->start(_scanSeconds(), false /*is_continue*/);
		_lastScanStartMs = millis();
		_scanning = true;
	}

	void stop() {
		_scanning = false;
		if (_scan) _scan->stop();
	}

	// ----------------------------------------------
	// 주기 호출
	// ----------------------------------------------
	void tick() {
		// 스캔 주기 관리
		_manageScanLoop();
		// 상태 기계 갱신
		_updateFSM();
		// 스택 재시작 (장시간 안정 운용)
		_maybeRestartStack();
	}

	// ----------------------------------------------
	// 쿼리/제어
	// ----------------------------------------------
	bool isPresent() const { return _present; }
	EN_B10_fsm_state_t state() const { return _state; }

	void toJson(JsonDocument& p_doc) const {
		JsonObject o = p_doc["ble"].to<JsonObject>();
		o["present"]   = _present;
		o["state"]     = (_state==EN_B10_FSM_IDLE?"IDLE":(_state==EN_B10_FSM_PRESENCE?"PRESENCE":"EXIT_DELAY"));
		o["scan_sec"]  = _scanSeconds();
		o["rssi_on"]   = _policy.rssi_on;
		o["rssi_off"]  = _policy.rssi_off;
		o["avg_count"] = _policy.avg_count;
		o["persist"]   = _policy.persist_count;
		o["exit_delay"]= _policy.exit_delay_sec;

		JsonArray arr = o["trusted"].to<JsonArray>();
		for (auto &d : _trusted) {
			JsonObject t = arr.add<JsonObject>();
			t["alias"]      = d.alias;
			t["name"]       = d.name;
			t["mac"]        = d.mac;
			t["manuf_pref"] = d.manuf_prefix;
			t["pref_len"]   = d.prefix_len;
			t["enabled"]    = d.enabled;
			t["last_rssi"]  = d.last_rssi;
			t["avg_rssi"]   = d.avg_rssi;
			t["near"]       = d.near;
			t["last_ms"]    = d.last_seen_ms;
		}
	}

	// 정책/파라미터 런타임 조정 (Web UI 등에서 사용)
	void setPolicy(const ST_B10_policy_t& p) { _policy = p; }
	void setExitDelay(uint16_t p_sec) { _policy.exit_delay_sec = p_sec; }
	void setHysteresis(int p_on, int p_off) { _policy.rssi_on = p_on; _policy.rssi_off = p_off; }
	void setAveraging(uint8_t p_cnt) { _policy.avg_count = (p_cnt==0?1:p_cnt); }
	void setPersist(uint8_t p_cnt) { _policy.persist_count = (p_cnt==0?1:p_cnt); }

	// 설정 재적용(파일 갱신 후)
	void reloadConfig() { _applyConfigFromJson(); }

	// 디버그
	void debugPrint() const {
		CL_D10_Logger::log(EN_L10_LOG_INFO, "[BLE] present=%d state=%d trusted=%u",
		                   (int)_present, (int)_state, (unsigned)_trusted.size());
		for (auto &d : _trusted) {
			CL_D10_Logger::log(EN_L10_LOG_INFO, "  - %s mac=%s rssi=%d avg=%d near=%d",
			                   d.alias, d.mac, d.last_rssi, d.avg_rssi, (int)d.near);
		}
	}

	// 내부에서 사용: 스캔 콜백 진입점
	void _onScanResult(const NimBLEAdvertisedDevice* p_adv) {
		if (!p_adv) return;

		// 기본 속성
		int v_rssi = p_adv->getRSSI();
		std::string v_nameStd = p_adv->getName();
		const char* v_name = v_nameStd.empty() ? "" : v_nameStd.c_str();

		// MAC
		char v_mac[18] = {0};
		if (p_adv->getAddressType() != BLE_ADDR_PUBLIC && p_adv->getAddressType() != BLE_ADDR_RANDOM) {
			// 방어 (일반적으로 PUBLIC/RANDOM)
		}
		snprintf(v_mac, sizeof(v_mac), "%s", p_adv->getAddress().toString().c_str());
		_toUpperMac(v_mac);

		// 제조사 데이터(HEX 문자열로 변환)
		std::string v_md = p_adv->getManufacturerData();
		char v_mdHex[64] = {0};
		_toHex(v_md.data(), v_md.size(), v_mdHex, sizeof(v_mdHex));

		// 화이트리스트 매칭
		for (auto &d : _trusted) {
			if (!d.enabled) continue;

			bool v_match = false;
			// 1) 정확 MAC (우선)
			if (d.mac[0]) {
				if (strncmp(d.mac, v_mac, 17) == 0) v_match = true;
			}
			// 2) 제조사 프리픽스 (다음)
			if (!v_match && d.manuf_prefix[0] && d.prefix_len > 0) {
				size_t v_need = (size_t)d.prefix_len * 2; // HEX 문자열 길이
				if (strlen(v_mdHex) >= v_need && strncmp(v_mdHex, d.manuf_prefix, v_need) == 0) {
					v_match = true;
				}
			}
			// 3) 디바이스 이름 (마지막)
			if (!v_match && d.name[0] && v_name && *v_name) {
				if (strncmp(d.name, v_name, G_B10_NAME_MAX-1) == 0) v_match = true;
			}

			if (!v_match) continue;

			// 이동 평균 업데이트
			d.last_rssi = v_rssi;
			d.last_seen_ms = millis();
			if (d.avg_count == 0) {
				d.avg_rssi = v_rssi;
				d.avg_count = 1;
			} else {
				// 간단한 EMA 유사: avg = avg + (rssi - avg)/min(k,avg_count)
				uint8_t v_k = (_policy.avg_count == 0 ? 1 : _policy.avg_count);
				d.avg_rssi = d.avg_rssi + (v_rssi - d.avg_rssi) / (int)v_k;
				if (d.avg_count < 200) d.avg_count++;
			}

			// 히스테리시스 + 지속 카운트
			if (d.avg_rssi >= _policy.rssi_on) {
				d.persist_on = (d.persist_on < 250) ? (d.persist_on + 1) : d.persist_on;
				d.persist_off = 0;
				if (!d.near && d.persist_on >= _policy.persist_count) {
					d.near = true;
					CL_D10_Logger::log(EN_L10_LOG_INFO, "[BLE] NEAR: %s mac=%s avg=%d", d.alias, d.mac, d.avg_rssi);
				}
			} else if (d.avg_rssi <= _policy.rssi_off) {
				d.persist_off = (d.persist_off < 250) ? (d.persist_off + 1) : d.persist_off;
				d.persist_on = 0;
				if (d.near && d.persist_off >= _policy.persist_count) {
					// near → false는 FSM에서 ExitDelay로 넘김 (즉시 끄지 않음)
					d.near = false;
					CL_D10_Logger::log(EN_L10_LOG_INFO, "[BLE] FAR: %s mac=%s avg=%d", d.alias, d.mac, d.avg_rssi);
				}
			} else {
				// 히스테리시스 중간 영역: 카운트 저하
				if (d.persist_on > 0) d.persist_on--;
				if (d.persist_off > 0) d.persist_off--;
			}
		}
	}

private:
	// ==================================================
	// 설정 반영
	// ==================================================
	void _applyConfigFromJson() {
		// 1) 정책 기본값
		_policy = {};
		// control(Continuous.motion.ble.*) 또는 별도 motion 정책(rssi.*) 지원
		// 우선순위: control → motion → defaults
		// (실 프로젝트에서는 C10_ConfigManager에서 이미 머지했을 수 있음)

		// system scan interval
		_scanIntervalSec = G_B10_SCAN_INTERVAL_DEFAULT;
		if (g_A10_config_root.core.hw.ble.enabled) {
			if (g_A10_config_root.core.hw.ble.scan_interval > 0)
				_scanIntervalSec = g_A10_config_root.core.hw.ble.scan_interval;
		}

		// motion.json: trusted_devices[], rssi.{on,off,avg_count,persist_count,exit_delay_sec}
		_trusted.clear();
		if (g_A10_config_root.motion) {
			// rssi 정책
			if (g_A10_config_root.motion->rssi.on != 0)         _policy.rssi_on = g_A10_config_root.motion->rssi.on;
			if (g_A10_config_root.motion->rssi.off != 0)        _policy.rssi_off = g_A10_config_root.motion->rssi.off;
			if (g_A10_config_root.motion->rssi.avg_count != 0)  _policy.avg_count = g_A10_config_root.motion->rssi.avg_count;
			if (g_A10_config_root.motion->rssi.persist_count != 0) _policy.persist_count = g_A10_config_root.motion->rssi.persist_count;
			if (g_A10_config_root.motion->rssi.exit_delay_sec != 0) _policy.exit_delay_sec = g_A10_config_root.motion->rssi.exit_delay_sec;

			// trusted devices
			uint8_t v_cnt = min<uint8_t>(g_A10_config_root.motion->ble.device_count, G_B10_MAX_TRUSTED);
			_trusted.reserve(v_cnt);
			for (uint8_t v_i=0; v_i<v_cnt; ++v_i) {
				ST_B10_trusted_t v{};
				strlcpy(v.alias, g_A10_config_root.motion->ble.devices[v_i].alias, sizeof(v.alias));
				strlcpy(v.name,  g_A10_config_root.motion->ble.devices[v_i].name,  sizeof(v.name));
				strlcpy(v.mac,   g_A10_config_root.motion->ble.devices[v_i].mac,   sizeof(v.mac));
				_toUpperMac(v.mac);
				strlcpy(v.manuf_prefix, g_A10_config_root.motion->ble.devices[v_i].manuf_prefix, sizeof(v.manuf_prefix));
				_toUpperHex(v.manuf_prefix);
				v.prefix_len = g_A10_config_root.motion->ble.devices[v_i].prefix_len;
				v.enabled    = g_A10_config_root.motion->ble.devices[v_i].enabled;
				_trusted.push_back(v);
			}
		}

		// control 통합 정책(옵션): Continuous.motion.ble.{rssi_on, rssi_off, ...}가 있으면 덮어쓰기
		if (g_A10_config_root.control && g_A10_config_root.control->continuous.motion.ble.enabled) {
			const auto &b = g_A10_config_root.control->continuous.motion.ble;
			if (b.rssi_on)          _policy.rssi_on = b.rssi_on;
			if (b.rssi_off)         _policy.rssi_off = b.rssi_off;
			if (b.avg_count)        _policy.avg_count = b.avg_count;
			if (b.persist_count)    _policy.persist_count = b.persist_count;
			if (b.exit_delay_sec)   _policy.exit_delay_sec = b.exit_delay_sec;
		}

		// 안전 범위 클램프
		if (_policy.rssi_on > -30) _policy.rssi_on = -30;
		if (_policy.rssi_on < -95) _policy.rssi_on = -95;
		if (_policy.rssi_off > -35) _policy.rssi_off = -35;
		if (_policy.rssi_off < -100)_policy.rssi_off = -100;
		if (_policy.avg_count == 0) _policy.avg_count = 1;
		if (_policy.persist_count == 0) _policy.persist_count = 1;
		if (_policy.exit_delay_sec < 2) _policy.exit_delay_sec = 2;

		CL_D10_Logger::log(EN_L10_LOG_INFO, "[BLE] config applied: on=%d off=%d avg=%u persist=%u exit=%us scanInt=%us trusted=%u",
		                   _policy.rssi_on, _policy.rssi_off, _policy.avg_count, _policy.persist_count,
		                   _policy.exit_delay_sec, _scanIntervalSec, (unsigned)_trusted.size());
	}

	// ==================================================
	// FSM 업데이트
	// ==================================================
	void _updateFSM() {
		bool v_anyNear = false;
		unsigned long v_now = millis();

		for (auto &d : _trusted) {
			// 최근 본 시각이 오래되면 near 해제 후보
			bool v_recent = (v_now - d.last_seen_ms) <= (_policy.exit_delay_sec * 1000UL);
			if (d.near && v_recent) v_anyNear = true;
		}

		switch (_state) {
			case EN_B10_FSM_IDLE:
				if (v_anyNear) {
					_state = EN_B10_FSM_PRESENCE;
					_present = true;
					CL_D10_Logger::log(EN_L10_LOG_INFO, "[BLE] FSM: IDLE → PRESENCE");
				}
				break;

			case EN_B10_FSM_PRESENCE:
				if (!v_anyNear) {
					_state = EN_B10_FSM_EXIT_DELAY;
					_exitStartMs = v_now;
					CL_D10_Logger::log(EN_L10_LOG_INFO, "[BLE] FSM: PRESENCE → EXIT_DELAY");
				}
				break;

			case EN_B10_FSM_EXIT_DELAY:
				// Exit Delay 동안 near 재검출 시 복귀
				if (v_anyNear) {
					_state = EN_B10_FSM_PRESENCE;
					CL_D10_Logger::log(EN_L10_LOG_INFO, "[BLE] FSM: EXIT_DELAY → PRESENCE (re-detect)");
					break;
				}
				if (v_now - _exitStartMs >= (_policy.exit_delay_sec * 1000UL)) {
					_state = EN_B10_FSM_IDLE;
					_present = false;
					CL_D10_Logger::log(EN_L10_LOG_INFO, "[BLE] FSM: EXIT_DELAY → IDLE (timeout)");
				}
				break;
		}
	}

	// ==================================================
	// 스캔 루프 관리 (패시브 스캔 반복 + 주기)
	// ==================================================
	void _manageScanLoop() {
		if (!_scan) return;

		// 스캔이 끝나면 즉시 재시작(패시브, 저전력·연속)
		if (_scanning && !_scan->isScanning() && (millis() - _lastScanStartMs) > 200) {
			_scan->clearResults();
			_scan->start(_scanSeconds(), false);
			_lastScanStartMs = millis();
		}

		// 필요 시 스캔 간 인터벌을 둘 수도 있음(현재는 연속)
		(void)_scanIntervalSec;
	}

	// ==================================================
	// BLE 스택 재시작(장시간 운용 안정)
	// ==================================================
	void _maybeRestartStack() {
		unsigned long v_nowSec = millis()/1000UL;
		if (v_nowSec - _lastStackRestartSec >= G_B10_STACK_RESTART_MIN) {
			CL_D10_Logger::log(EN_L10_LOG_WARN, "[BLE] stack soft-restart");
			// 소프트 재시작: 스캔 재시작만으로도 충분한 보드가 많음
			stop();
			delay(50);
			start();
			_lastStackRestartSec = v_nowSec;
		}
	}

	// ==================================================
	// 유틸
	// ==================================================
	static void _toUpperHex(char* p) {
		for (size_t v=0; p[v]; ++v) p[v] = (char)toupper((unsigned char)p[v]);
	}
	static void _toUpperMac(char* p_mac) {
		for (size_t v=0; p_mac[v]; ++v) {
			char c = p_mac[v];
			if (c >= 'a' && c <= 'f') p_mac[v] = (char)(c - 32);
			else p_mac[v] = (char)toupper((unsigned char)c);
		}
	}
	static void _toHex(const void* p_data, size_t p_len, char* p_out, size_t p_outLen) {
		static const char* HEX="0123456789ABCDEF";
		size_t v_max = (p_outLen-1)/2;
		size_t v_n = (p_len < v_max) ? p_len : v_max;
		const uint8_t* v_b = (const uint8_t*)p_data;
		for (size_t i=0;i<v_n;i++) {
			p_out[2*i]   = HEX[(v_b[i]>>4)&0xF];
			p_out[2*i+1] = HEX[v_b[i]&0xF];
		}
		p_out[2*v_n] = '\0';
	}

	uint32_t _scanSeconds() const {
		// NimBLE::start(duration) 단위는 seconds
		return (_scanIntervalSec > 0 ? _scanIntervalSec : G_B10_SCAN_INTERVAL_DEFAULT);
	}

private:
	// 런타임 상태
	bool                    _present = false;
	EN_B10_fsm_state_t      _state = EN_B10_FSM_IDLE;
	unsigned long           _exitStartMs = 0;

	// 정책/화이트리스트
	ST_B10_policy_t         _policy{};
	std::vector<ST_B10_trusted_t> _trusted;

	// 스캔
	NimBLEScan*             _scan = nullptr;
	CL_B10_ScanCallbacks    _callbacks{this};
	bool                    _scanning = false;
	unsigned long           _lastScanStartMs = 0;
	uint32_t                _scanIntervalSec = G_B10_SCAN_INTERVAL_DEFAULT;

	// 안정화
	unsigned long           _lastStackRestartSec = 0;
};

// ======================================================
// 콜백 구현
// ======================================================
inline void CL_B10_ScanCallbacks::onResult(NimBLEAdvertisedDevice* p_adv) {
	if (_owner) _owner->_onScanResult(p_adv);
}
