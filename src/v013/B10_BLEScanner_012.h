#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : B10_BLEScanner_012.h
 * 모듈약어 : B10
 * 모듈명 : Smart Nature Wind BLE 스캐너 Manager (v012)
 * ------------------------------------------------------
 * 기능 요약:
 *  - 패시브 스캔 기반 화이트리스트 근접 감지 (Name/MAC/Manufacturer Prefix)
 *  - RSSI 히스테리시스(on/off), 이동평균(avg_count), 지속 카운트(persist_count)
 *  - Exit Delay(자리 비움 지연 꺼짐), Fail-safe(미감지→기본풍 전환 판단 근거 제공)
 *  - BLE 스택 주기적 리스타트(장시간 안정)
 *  - cfg_motion_022.json(trusted_devices[], rssi{...}) 우선, 부족분은 cfg_system_022.json.hw.ble 보완
 *  - NimBLE 사용 불가 환경에서는 feed 모드(외부 스캐너 결과 주입)로 동일 로직 수행
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
 *    - typedef               : _t  접미사
 * 		- enum 상수             : EN_모듈약어_ 접두사
 * 		- 구조체                : ST_모듈약어_ 접두사
 * 		- 클래스명              : CL_모듈약어_ 접두사
 * 		- 클래스 private 멤버   : _ 접두사
 *    - 클래스 멤버 함수,변수   : 모듈약어 접두사 미시용
 * 		- 클래스 정적 멤버      : s_ 접두사
 * 		- 함수 로컬 변수             : v_ 접두사
 * 		- 함수 인자             : p_ 접두사
 * ------------------------------------------------------
 */

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ctype.h>
#include <string.h>

#include "A10_Const_012.h"
#include "C10_ConfigManager_012.h"
#include "D10_Logger_011.h"



#include <NimBLEDevice.h>
#include <NimBLEAdvertisedDevice.h>

// ===== 기본 상수 =====
#define G_B10_MAX_TRUSTED_DEV			10
#define G_B10_MAX_SAMPLES_PER_DEV		16
#define G_B10_DEFAULT_ON_TH				(-65)
#define G_B10_DEFAULT_OFF_TH			(-75)
#define G_B10_DEFAULT_AVG_COUNT			8
#define G_B10_DEFAULT_PERSIST_COUNT 	5
#define G_B10_DEFAULT_EXIT_DELAY_S		12
#define G_B10_DEFAULT_SCAN_ITVL_S		5
#define G_B10_STACK_RESTART_MIN			90	// NimBLE 안정성: 90분마다 재기동

static const char G_B10_HEX_CHARS[] = "0123456789ABCDEF";


// ===== 데이터 타입 =====
typedef struct {
	char		alias[24];		  	// "MyPhone"
	char		name[24];		  	// "UserPhone"
	char		mac[18];		  	// "AA:BB:CC:11:22:33" (optional)
	char		manuf_prefix[9];  	// "4C0002" 최대 8chars(+NUL)
	uint8_t 	prefix_len;		  	// prefix 바이트 길이(0~8/2)
	bool		enabled;
} ST_B10_TrustedDev_t;

typedef struct {
	int		 on;			  // RSSI ON Threshold
	int		 off;			  // RSSI OFF Threshold
	uint8_t	 avg_count;		  // 이동 평균 샘플 수
	uint8_t	 persist_count;	  // 상태 전이 연속 조건
	uint16_t exit_delay_sec;  // Presence→Idle 지연
} ST_B10_RssiCfg_t;

typedef enum : uint8_t {
	EN_B10_PRESENCE_IDLE	   = 0,
	EN_B10_PRESENCE_DETECT	   = 1,	 // 근접 감지
	EN_B10_PRESENCE_EXIT_DELAY = 2
} EN_B10_presence_state_t;

typedef struct {
	// 런타임 상태
	bool		  	present;		 // 디바이스 단위 현재 근접 판단
	bool		  	seenRecently;	 // 최근 광고 관측 여부
	unsigned long 	lastSeenMs;	 // 최종 수신 시각
	int			  	lastRssi;		 // 최근 RSSI(raw)
	// 평균/지속카운트
	int				samples[G_B10_MAX_SAMPLES_PER_DEV];
	uint8_t 		sampCount;
	uint8_t 		idx;
	uint8_t 		persistOn;
	uint8_t 		persistOff;
} ST_B10_RuntimeDev_t;

typedef struct {
	EN_B10_presence_state_t presenceState;
	bool					anyPresent;
	unsigned long			stateSinceMs;  // 상태 진입 시각
	unsigned long			lastStackRestartMs;
} ST_B10_GlobalState_t;

// ====================================================================
// 메인 클래스
// ====================================================================
class CL_B10_BLEScanner : public NimBLEScanCallbacks { 		// NimBLEAdvertisedDeviceCallbacks{
   public:
	// ===== 수명주기 =====
	void begin() {
		_loadConfig();
		

		_initNimBLE();

		_resetRuntime();

		CL_D10_Logger::log(EN_L10_LOG_INFO,
						   "[BLE] begin (trusted=%u, on=%d, off=%d, avg=%u, persist=%u, exit_delay=%us, scan_int=%us)",
						   _trustedCount, _rssiCfg.on, _rssiCfg.off, _rssiCfg.avg_count,
						   _rssiCfg.persist_count, _rssiCfg.exit_delay_sec, _scanIntervalSec);
	}

	void tick() {
		_maybeRestartStack();

		_tickScanScheduler();

		_tickPresenceFSM();
	}

	// ===== 외부 스캐너/디버그 입력(feed 모드) =====
	// 제조사 데이터 프리픽스(HEX 문자열, 예: "4C0002")가 있으면 같이 넘겨도 됨(없어도 동작)
	void feedAdv(const char* p_name, const char* p_mac, int p_rssi, const char* p_manufHex = nullptr) {
		_consumeAdv(p_name, p_mac, p_rssi, p_manufHex, (unsigned long)millis());
	}

	// ===== 결과 조회 & 직렬화 =====
	bool isAnyPresent() const {
		return _global.anyPresent;
	}
	EN_B10_presence_state_t getPresenceState() const {
		return _global.presenceState;
	}

	void toJson(JsonDocument& p_doc) const {
		JsonObject v_jsonObj_ble		 = p_doc["ble"].to<JsonObject>();
		v_jsonObj_ble["any_present"]	 = _global.anyPresent;
		v_jsonObj_ble["state"]			 = (_global.presenceState == EN_B10_PRESENCE_IDLE ? "IDLE" : _global.presenceState == EN_B10_PRESENCE_DETECT ? "DETECT"
																																		 : "EXIT_DELAY");
		v_jsonObj_ble["since_ms"]		 = _global.stateSinceMs;
		v_jsonObj_ble["on"]				 = _rssiCfg.on;
		v_jsonObj_ble["off"]			 = _rssiCfg.off;
		v_jsonObj_ble["avg_count"]		 = _rssiCfg.avg_count;
		v_jsonObj_ble["persist_count"]	 = _rssiCfg.persist_count;
		v_jsonObj_ble["exit_delay_s"]	 = _rssiCfg.exit_delay_sec;
		v_jsonObj_ble["scan_interval_s"] = _scanIntervalSec;

		JsonArray v_jsonArr_ble_trustedDevices = v_jsonObj_ble["trusted_devices"].to<JsonArray>();
		for (uint8_t v_i = 0; v_i < _trustedCount; ++v_i) {
			JsonObject v_jsonObj_ble_trustedDevice	 	= v_jsonArr_ble_trustedDevices.add<JsonObject>();
			v_jsonObj_ble_trustedDevice["alias"]		= _trusted[v_i].alias;
			v_jsonObj_ble_trustedDevice["name"]		  	= _trusted[v_i].name;
			v_jsonObj_ble_trustedDevice["mac"]		  	= _trusted[v_i].mac;
			v_jsonObj_ble_trustedDevice["manuf_prefix"] = _trusted[v_i].manuf_prefix;
			v_jsonObj_ble_trustedDevice["prefix_len"]	= _trusted[v_i].prefix_len;
			v_jsonObj_ble_trustedDevice["enabled"]	  	= _trusted[v_i].enabled;
		}

		JsonArray v_jsonArr_ble_Devices = v_jsonObj_ble["devices"].to<JsonArray>();
		for (uint8_t v_i = 0; v_i < _trustedCount; ++v_i) {

			const ST_B10_RuntimeDev_t& v_runtimeDev = _rt[v_i];

			JsonObject	v_jsonObj_ble_Device = v_jsonArr_ble_Devices.add<JsonObject>();

			v_jsonObj_ble_Device["alias"]				= _trusted[v_i].alias;
			v_jsonObj_ble_Device["present"]				= v_runtimeDev.present;
			v_jsonObj_ble_Device["seen"]				= v_runtimeDev.seenRecently;
			v_jsonObj_ble_Device["last_ms"]				= v_runtimeDev.lastSeenMs;
			v_jsonObj_ble_Device["rssi"]				= v_runtimeDev.lastRssi;
			v_jsonObj_ble_Device["samp_count"]			= v_runtimeDev.sampCount;
			v_jsonObj_ble_Device["persist_on"]			= v_runtimeDev.persistOn;
			v_jsonObj_ble_Device["persist_off"]			= v_runtimeDev.persistOff;
		}
	}

	// ===== 런타임 설정(CT10에서 동적 조정) =====
	void setOnThreshold(int p_val) {
		_rssiCfg.on = p_val;
	}
	void setOffThreshold(int p_val) {
		_rssiCfg.off = p_val;
	}
	void setAvgCount(uint8_t p_n) {
		_rssiCfg.avg_count = (p_n > 0 && p_n <= G_B10_MAX_SAMPLES_PER_DEV) ? p_n : _rssiCfg.avg_count;
	}
	void setPersistCount(uint8_t p_n) {
		_rssiCfg.persist_count = (p_n > 0) ? p_n : _rssiCfg.persist_count;
	}
	void setExitDelay(uint16_t p_s) {
		_rssiCfg.exit_delay_sec = (p_s > 0) ? p_s : _rssiCfg.exit_delay_sec;
	}
	void setScanInterval(uint16_t p_s) {
		_scanIntervalSec = (p_s > 0) ? p_s : _scanIntervalSec;
	}

	// 설정 재적용(파일 변경 후)
	void reloadConfig() {
		_loadConfig();
		_resetRuntime();
	}

	// 스캐너 강제 재기동
	void restartStackNow() {
		_stopScan();
		NimBLEDevice::deinit(true);
		delay(50);
		_initNimBLE();
		_global.lastStackRestartMs = millis();
		CL_D10_Logger::log(EN_L10_LOG_INFO, "[BLE] stack restarted (manual)");
	}


   private:
	// ===== 설정 로드 =====
	void _loadConfig() {
		// 기본값
		_trustedCount = 0;
		memset(_trusted, 0, sizeof(_trusted));
		_rssiCfg.on				= G_B10_DEFAULT_ON_TH;
		_rssiCfg.off			= G_B10_DEFAULT_OFF_TH;
		_rssiCfg.avg_count		= G_B10_DEFAULT_AVG_COUNT;
		_rssiCfg.persist_count	= G_B10_DEFAULT_PERSIST_COUNT;
		_rssiCfg.exit_delay_sec = G_B10_DEFAULT_EXIT_DELAY_S;

		// system.hw.ble.scan_interval
		_scanIntervalSec = (g_A10_config_root.system.hw.ble.enabled && g_A10_config_root.system.hw.ble.scan_interval > 0)
							   ? g_A10_config_root.system.hw.ble.scan_interval
							   : G_B10_DEFAULT_SCAN_ITVL_S;

		// motion.trusted_devices[], motion.rssi{...}
		if (g_A10_config_root.motion) {
			// trusted_devices
			for (uint8_t v_i = 0; v_i < g_A10_config_root.motion->ble.device_count && _trustedCount < G_B10_MAX_TRUSTED_DEV; ++v_i) {
				ST_B10_TrustedDev_t& t = _trusted[_trustedCount++];
				memset(&t, 0, sizeof(t));
				strlcpy(t.alias, g_A10_config_root.motion->ble.devices[v_i].alias, sizeof(t.alias));
				// 확장 키(name/mac/manuf_prefix/prefix_len)는 cfg_motion_022 확장안 기준(없어도 ok)
				if (g_A10_config_root.motion->ble.devices[v_i].name[0]) {
					strlcpy(t.name, g_A10_config_root.motion->ble.devices[v_i].name, sizeof(t.name));
				}
				if (g_A10_config_root.motion->ble.devices[v_i].mac[0]) {
					_toUpperHexMac(g_A10_config_root.motion->ble.devices[v_i].mac);
					strlcpy(t.mac, g_A10_config_root.motion->ble.devices[v_i].mac, sizeof(t.mac));
				}
				if (g_A10_config_root.motion->ble.devices[v_i].manuf_prefix[0]) {
					_toUpperHex(g_A10_config_root.motion->ble.devices[v_i].manuf_prefix);
					strlcpy(t.manuf_prefix, g_A10_config_root.motion->ble.devices[v_i].manuf_prefix, sizeof(t.manuf_prefix));
				}
				t.prefix_len = g_A10_config_root.motion->ble.devices[v_i].prefix_len;
				t.enabled	 = g_A10_config_root.motion->ble.devices[v_i].enabled;
			}

			// rssi 설정 블록(신규 JSON 스펙)
			if (g_A10_config_root.motion->ble.rssi_on)
				_rssiCfg.on = g_A10_config_root.motion->ble.rssi_on;
			if (g_A10_config_root.motion->ble.rssi_off)
				_rssiCfg.off = g_A10_config_root.motion->ble.rssi_off;
			if (g_A10_config_root.motion->ble.avg_count)
				_rssiCfg.avg_count = g_A10_config_root.motion->ble.avg_count;
			if (g_A10_config_root.motion->ble.persist_count)
				_rssiCfg.persist_count = g_A10_config_root.motion->ble.persist_count;
			if (g_A10_config_root.motion->ble.exit_delay_sec)
				_rssiCfg.exit_delay_sec = g_A10_config_root.motion->ble.exit_delay_sec;
		}

		// 히스테리시스 보정: on > off 되도록 강제
		if (_rssiCfg.on <= _rssiCfg.off)
			_rssiCfg.on = _rssiCfg.off + 5;
	}

	void _resetRuntime() {
		memset(_rt, 0, sizeof(_rt));
		for (uint8_t v_i = 0; v_i < _trustedCount; ++v_i) {
			_rt[v_i].lastRssi = -127;
		}
		_global.presenceState	   = EN_B10_PRESENCE_IDLE;
		_global.anyPresent		   = false;
		_global.stateSinceMs	   = millis();
		_global.lastStackRestartMs = millis();


		_lastScanKickMs = 0;

	}

	// ===== NimBLE 영역 =====

	void _initNimBLE() {
		NimBLEDevice::init("NW_Scanner");
		NimBLEDevice::setPower(ESP_PWR_LVL_P9);	 // 최대 TX 파워(스캔 감도 향상)
		NimBLEDevice::setMTU(69);
		_startPassiveScan();
	}

	void _startPassiveScan() {
		NimBLEScan* v_s = NimBLEDevice::getScan();
	
		v_s->setScanCallbacks(this);
		// v_s->setScanCallbacks(&scanCallbacks);

		//v_s->setCallbacks(this);
		
		// NimBLE 라이브러리에서 setDuplicateFilter(true)는 필터를 는 것(중복 배제)을 의미합니다.
        v_s->setDuplicateFilter(true); // 중복 필터 켜기 (대부분의 경우 권장)
        // 만약 'false /*dup*/'가 중복 필터를 끈다(중복 허용)는 의미였다면 아래 코드를 사용합니다.
        // v_s->setDuplicateFilter(false); // 중복 필터 끄기 (장치별 보고 횟수 제한 없음)

		// v_s->setAdvertisedDeviceCallbacks(this, false /*dup*/);

		v_s->setActiveScan(false);			  // 패시브 스캔
		v_s->setInterval(45);				  // 28.125~10.24s 범위, 45*0.625ms≈28ms
		v_s->setWindow(45);					  // Window=Interval → 연속 패시브
		v_s->setMaxResults(0);				  // 내부 버퍼 최소화
		v_s->start(_scanIntervalSec, false);  // 블로킹 false
		CL_D10_Logger::log(EN_L10_LOG_INFO, "[BLE] passive scan start (%us)", _scanIntervalSec);
	}

	void _stopScan() {
		NimBLEScan* v_s = NimBLEDevice::getScan();
		if (v_s->isScanning())
			v_s->stop();
	}

	void _tickScanScheduler() {
		// 일정 주기로 재조회(패시브+연속이나, 일부 보드에서 루프 재시작이 안정적)
		unsigned long v_now = millis();
		if (v_now - _lastScanKickMs >= (unsigned long)_scanIntervalSec * 1000UL) {
			_lastScanKickMs = v_now;
			NimBLEScan* v_s = NimBLEDevice::getScan();
			if (!v_s->isScanning()) {
				v_s->start(_scanIntervalSec, false);
			}
		}
	}

	void onResult(const NimBLEAdvertisedDevice* p_adv) override {
	// void onResult(NimBLEAdvertisedDevice* p_adv) override {
		// 이름/MAC/제조사 데이터 추출
		const char* v_name		   = p_adv->haveName() ? p_adv->getName().c_str() : "";
		const char* v_mac		   = p_adv->getAddress().toString().c_str();
		int			v_rssi		   = p_adv->getRSSI();
		char		v_manufHex[33] = {0};

		if (p_adv->haveManufacturerData()) {
			std::string v_m = p_adv->getManufacturerData();
			_binToHex((const uint8_t*)v_m.data(), (uint8_t)min((size_t)16, v_m.size()), v_manufHex, sizeof(v_manufHex));
		}
		_consumeAdv(v_name, v_mac, v_rssi, v_manufHex, (unsigned long)millis());
	}


	// ===== 공통: 광고 소비 → 화이트리스트 매칭/평균/히스테리시스/지속카운트 =====
	void _consumeAdv(const char* p_name, const char* p_mac, int p_rssi, const char* p_manufHex, unsigned long p_nowMs) {
		// 화이트리스트 매칭
		int v_idx = _matchTrusted(p_name, p_mac, p_manufHex);
		if (v_idx < 0)
			return;

		ST_B10_RuntimeDev_t& v_r = _rt[v_idx];

		v_r.seenRecently = true;
		v_r.lastSeenMs	 = p_nowMs;
		v_r.lastRssi	 = p_rssi;

		// 이동 평균 버퍼
		if (v_r.sampCount < _rssiCfg.avg_count) {
			v_r.samples[v_r.idx++] = p_rssi;
			v_r.sampCount++;
		} else {
			if (v_r.idx >= _rssiCfg.avg_count)
				v_r.idx = 0;
			v_r.samples[v_r.idx++] = p_rssi;
		}

		int v_avg = _avgRssi(v_r);

		// 히스테리시스 + 지속 카운트
		if (v_avg >= _rssiCfg.on) {
			if (v_r.persistOn < 255)
				v_r.persistOn++;
			v_r.persistOff = 0;
			if (!v_r.present && v_r.persistOn >= _rssiCfg.persist_count) {
				v_r.present = true;
				CL_D10_Logger::log(EN_L10_LOG_INFO, "[BLE] PRESENT (%s) avg=%d", _trusted[v_idx].alias, v_avg);
			}
		} else if (v_avg <= _rssiCfg.off) {
			if (v_r.persistOff < 255)
				v_r.persistOff++;
			v_r.persistOn = 0;
			if (v_r.present && v_r.persistOff >= _rssiCfg.persist_count) {
				v_r.present = false;
				CL_D10_Logger::log(EN_L10_LOG_INFO, "[BLE] AWAY (%s) avg=%d", _trusted[v_idx].alias, v_avg);
			}
		} else {
			// 히스테리시스 밴드 내: 카운트 유지(자연 감쇠)
			if (v_r.persistOn > 0)
				v_r.persistOn--;
			if (v_r.persistOff > 0)
				v_r.persistOff--;
		}
	}

	// ===== 전역 Presence FSM =====
	void _tickPresenceFSM() {
		// 디바이스별 'recent' 플래그 갱신(Exit Delay 기준)
		unsigned long v_now	   = millis();
		unsigned long v_expire = (unsigned long)_rssiCfg.exit_delay_sec * 1000UL;

		bool v_anyPresent = false;
		for (uint8_t v_i = 0; v_i < _trustedCount; ++v_i) {
			ST_B10_RuntimeDev_t& v_r = _rt[v_i];
			// 최근 관측 플래그
			v_r.seenRecently = (v_now - v_r.lastSeenMs) <= v_expire;
			// 전역 현재 근접?
			if (v_r.present)
				v_anyPresent = true;
		}

		// FSM
		switch (_global.presenceState) {
			case EN_B10_PRESENCE_IDLE:
				if (v_anyPresent) {
					_global.presenceState = EN_B10_PRESENCE_DETECT;
					_global.stateSinceMs  = v_now;
				}
				break;
			case EN_B10_PRESENCE_DETECT:
				if (!v_anyPresent) {
					_global.presenceState = EN_B10_PRESENCE_EXIT_DELAY;
					_global.stateSinceMs  = v_now;	// 지연 시작
				}
				break;
			case EN_B10_PRESENCE_EXIT_DELAY:
				if (v_anyPresent) {
					_global.presenceState = EN_B10_PRESENCE_DETECT;	 // 복귀
					_global.stateSinceMs  = v_now;
				} else if (v_now - _global.stateSinceMs >= v_expire) {
					_global.presenceState = EN_B10_PRESENCE_IDLE;  // 완전 이탈
					_global.stateSinceMs  = v_now;
				}
				break;
		}

		// Fail-safe/UX용 anyPresent: EXIT_DELAY 동안엔 유지
		_global.anyPresent = (_global.presenceState != EN_B10_PRESENCE_IDLE);
	}

	// ===== 스택 리스타트(장시간 안정) =====
	void _maybeRestartStack() {
		unsigned long v_now	 = millis();
		unsigned long v_span = (unsigned long)G_B10_STACK_RESTART_MIN * 60UL * 1000UL;
		if (v_now - _global.lastStackRestartMs >= v_span) {
			restartStackNow();
		}
	}

	// ===== 매칭/유틸 =====
	int _matchTrusted(const char* p_name, const char* p_mac, const char* p_manufHex) {
		// 표준화
		char v_mac[18] = {0};
		if (p_mac && p_mac[0]) {
			strlcpy(v_mac, p_mac, sizeof(v_mac));
			_toUpperHexMac(v_mac);
		}

		for (uint8_t v_i = 0; v_i < _trustedCount; ++v_i) {
			const ST_B10_TrustedDev_t& t = _trusted[v_i];
			if (!t.enabled)
				continue;

			bool v_match = false;

			// 1) MAC 일치
			if (t.mac[0] && v_mac[0] && _macEqual(t.mac, v_mac))
				v_match = true;

			// 2) 이름 매칭(정확히 같음)
			if (!v_match && t.name[0] && p_name && p_name[0] && strcmp(t.name, p_name) == 0)
				v_match = true;

			// 3) Manufacturer Prefix(HEX) 매칭
			if (!v_match && t.manuf_prefix[0] && p_manufHex && p_manufHex[0]) {
				if (_hexPrefixMatch(t.manuf_prefix, t.prefix_len, p_manufHex))
					v_match = true;
			}

			if (v_match)
				return (int)v_i;
		}
		return -1;
	}

	static bool _hexPrefixMatch(const char* p_needHex, uint8_t p_needBytes, const char* p_haveHex) {
		// p_needBytes: 필요 바이트 수. hex 문자열 길이의 반보다 클 수 없음.
		// 비교 길이(문자 수)
		uint8_t v_needChars = p_needBytes ? (uint8_t)(p_needBytes * 2) : (uint8_t)strlen(p_needHex);
		for (uint8_t v_i = 0; v_i < v_needChars; ++v_i) {
			char a = toupper((unsigned char)p_needHex[v_i]);
			char b = toupper((unsigned char)p_haveHex[v_i]);
			if (a == 0 || b == 0)
				return false;
			if (a != b)
				return false;
		}
		return true;
	}

	static int _avgRssi(const ST_B10_RuntimeDev_t& r) {
		if (r.sampCount == 0)
			return r.lastRssi;
		long v_sum = 0;
		for (uint8_t v_i = 0; v_i < r.sampCount; ++v_i) v_sum += r.samples[v_i];
		return (int)(v_sum / (long)r.sampCount);
	}

	static void _toUpperHexMac(char* p_mac) {
		for (size_t v_i = 0; p_mac[v_i]; ++v_i) {
			char c = p_mac[v_i];
			if (c >= 'a' && c <= 'f')
				p_mac[v_i] = (char)(c - 32);
			else
				p_mac[v_i] = (char)toupper((unsigned char)c);
		}
	}
	static void _toUpperHex(char* p_hex) {
		for (size_t v_i = 0; p_hex[v_i]; ++v_i) p_hex[v_i] = (char)toupper((unsigned char)p_hex[v_i]);
	}
	static bool _macEqual(const char* a, const char* b) {
		return strncmp(a, b, 17) == 0;
	}

	static void _binToHex(const uint8_t* p_bin, uint8_t p_len, char* p_out, size_t p_outLen) {
        
        size_t   v_pos = 0;
        for (uint8_t v_i = 0; v_i < p_len && v_pos + 2 < p_outLen; ++v_i) {
            // ✅ 수정: 전역 상수 G_B10_HEX_CHARS 사용
            p_out[v_pos++] = G_B10_HEX_CHARS[(p_bin[v_i] >> 4) & 0xF];
            p_out[v_pos++] = G_B10_HEX_CHARS[p_bin[v_i] & 0xF];
        }
        if (v_pos < p_outLen)
            p_out[v_pos] = 0;
    }

	// static void _binToHex(const uint8_t* p_bin, uint8_t p_len, char* p_out, size_t p_outLen) {
	// 	static const char* HEX	 = "0123456789ABCDEF";
	// 	size_t			   v_pos = 0;
	// 	for (uint8_t v_i = 0; v_i < p_len && v_pos + 2 < p_outLen; ++v_i) {
	// 		p_out[v_pos++] = HEX[(p_bin[v_i] >> 4) & 0xF];
	// 		p_out[v_pos++] = HEX[p_bin[v_i] & 0xF];
	// 	}
	// 	if (v_pos < p_outLen)
	// 		p_out[v_pos] = 0;
	// }

   private:
		// 설정/상태
		ST_B10_TrustedDev_t	 	_trusted[G_B10_MAX_TRUSTED_DEV];
		uint8_t				 	_trustedCount = 0;
		ST_B10_RssiCfg_t	 	_rssiCfg;
		ST_B10_RuntimeDev_t	 	_rt[G_B10_MAX_TRUSTED_DEV];
		ST_B10_GlobalState_t 	_global;
		uint16_t			 	_scanIntervalSec = G_B10_DEFAULT_SCAN_ITVL_S;

		unsigned long 			_lastScanKickMs = 0;
};
