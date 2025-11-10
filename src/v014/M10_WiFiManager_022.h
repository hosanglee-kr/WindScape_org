#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : M10_WiFiManager_022.h
 * 모듈약어 : M10
 * 모듈명 : Smart Nature Wind Wi-Fi Manager + NTP Sync (v022)
 * ------------------------------------------------------
 * 기능 요약:
 *  - Wi-Fi AP/STA/AP+STA 모드 제어
 *  - STA 우선 연결 / 실패 시 Soft AP 폴백
 *  - WiFiMulti 기반 재시도 로직
 *  - NTP 시간 동기화 (cfg_system.time 기반)
 *  - 네트워크 스캔 및 상태 JSON 출력 지원
 *  - cfg_wifi_022.json 완전 대응
 * ------------------------------------------------------
 * [구현 규칙]
 *  - 항상 소스 시작 주석 체계 유지
 *  - ArduinoJson v7.x.x 사용 (v6 이하 금지)
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
#include <WiFi.h>
#include <WiFiMulti.h>
#include <esp_task_wdt.h>
#include <time.h>

#include "A10_Const_014.h"
#include "D10_Logger_016.h"

class CL_M10_WiFiManager {
   public:
	// --------------------------------------------------
	// Static 상태 변수
	// --------------------------------------------------
	static bool		   s_staConnected;
	static wl_status_t s_lastStaStatus;
	static bool		   s_timeSynced;

   public:
	// --------------------------------------------------
	// Wi-Fi 이벤트 등록
	// --------------------------------------------------
	static void M10_attachWiFiEvents() {
		static bool v_attached = false;
		if (v_attached)
			return;

		WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t) {
			CL_D10_Logger::log(EN_L10_LOG_INFO, "[WiFi] STA start");
		},
					 ARDUINO_EVENT_WIFI_STA_START);

		WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t) {
			CL_D10_Logger::log(EN_L10_LOG_INFO, "[WiFi] STA got IP: %s",
							   WiFi.localIP().toString().c_str());
			s_staConnected	= true;
			s_lastStaStatus = WL_CONNECTED;
		},
					 ARDUINO_EVENT_WIFI_STA_GOT_IP);

		WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t) {
			CL_D10_Logger::log(EN_L10_LOG_WARN, "[WiFi] STA disconnected");
			s_staConnected	= false;
			s_lastStaStatus = WL_DISCONNECTED;
		},
					 ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

		v_attached = true;
	}

	// --------------------------------------------------
	// Wi-Fi 초기화
	// --------------------------------------------------
	static bool init(const ST_A10_WifiConfig&	p_cfg_wifi,
					 const ST_A10_SystemConfig& p_cfg_system,
					 WiFiMulti&					p_multi,
					 uint8_t					p_apChannel	  = 1,
					 uint8_t					p_staMaxTries = 15) {
		M10_attachWiFiEvents();

		WiFi.persistent(false);
		WiFi.setAutoReconnect(true);
		WiFi.setSleep(false);

		char v_hostname[32];
		snprintf(v_hostname, sizeof(v_hostname), "NatureWind-%04X",
				 (uint16_t)(esp_random() & 0xFFFF));
		WiFi.setHostname(v_hostname);

		switch (p_cfg_wifi.wifiMode) {
			case 0:	 // AP
				WiFi.mode(WIFI_AP);
				return M10_startAP(p_cfg_wifi, p_apChannel);

			case 1: {  // STA
				WiFi.mode(WIFI_STA);
				bool v_ok = M10_startSTA(p_cfg_wifi, p_multi, p_staMaxTries);
				if (!v_ok) {
					CL_D10_Logger::log(EN_L10_LOG_WARN, "[WiFi] STA fail → SoftAP fallback");
					WiFi.mode(WIFI_AP_STA);
					M10_startAP(p_cfg_wifi, p_apChannel);
				}
				if (s_staConnected) {
					M10_syncTimeIfNeeded(p_cfg_wifi, p_cfg_system);
				}
				return true;
			}

			case 2:
			default: {	// AP+STA
				WiFi.mode(WIFI_AP_STA);
				M10_startAP(p_cfg_wifi, p_apChannel);
				bool v_ok = M10_startSTA(p_cfg_wifi, p_multi, p_staMaxTries);
				if (v_ok && s_staConnected) {
					M10_syncTimeIfNeeded(p_cfg_wifi, p_cfg_system);
				}
				return true;
			}
		}
	}

	// --------------------------------------------------
	// AP 모드 시작
	// --------------------------------------------------
	static bool M10_startAP(const ST_A10_WifiConfig& p_cfg_wifi, uint8_t p_channel) {
		char v_pass[A10_Const::LEN_PASS + 1];
		strlcpy(v_pass, p_cfg_wifi.ap.password, sizeof(v_pass));

		if (strlen(v_pass) < 8) {
			uint32_t v_r = esp_random();
			snprintf(v_pass, sizeof(v_pass), "ap_%08X", (unsigned int)v_r);
			CL_D10_Logger::log(EN_L10_LOG_WARN, "[WiFi] AP passwd <8 → temp: %s", v_pass);
		}

		WiFi.softAPdisconnect(true);
		WiFi.disconnect(true, true);

		bool v_ok = WiFi.softAP(p_cfg_wifi.ap.ssid, v_pass, p_channel, false, 4);
		CL_D10_Logger::log(v_ok ? EN_L10_LOG_INFO : EN_L10_LOG_ERROR,
						   v_ok ? "[WiFi] AP started (%s)" : "[WiFi] AP start ERR",
						   WiFi.softAPIP().toString().c_str());
		return v_ok;
	}

	// --------------------------------------------------
	// STA 모드 시작
	// --------------------------------------------------
	static bool M10_startSTA(const ST_A10_WifiConfig& p_cfg_wifi,
							 WiFiMulti&				  p_multi,
							 uint8_t				  p_maxTries) {
		s_staConnected	= false;
		s_lastStaStatus = WL_IDLE_STATUS;

		if (p_cfg_wifi.sta_count == 0) {
			CL_D10_Logger::log(EN_L10_LOG_WARN, "[WiFi] No STA entries");
			return false;
		}

		for (uint8_t v_i = 0; v_i < p_cfg_wifi.sta_count; v_i++) {
			const char* v_ssid = p_cfg_wifi.sta[v_i].ssid;
			const char* v_pass = p_cfg_wifi.sta[v_i].pass;
			if (v_ssid[0] == '\0')
				continue;

			p_multi.addAP(v_ssid, v_pass);
			CL_D10_Logger::log(EN_L10_LOG_INFO, "[WiFi] STA candidate: %s", v_ssid);
		}

		CL_D10_Logger::log(EN_L10_LOG_INFO, "[WiFi] STA connecting...");
		uint8_t	 v_try	= 0;
		uint32_t v_wait = 500;

		while (WiFi.status() != WL_CONNECTED && v_try < p_maxTries) {
			if (p_multi.run(2000) == WL_CONNECTED)
				break;
			v_try++;
			delay(v_wait);
			v_wait = (v_wait < 4000) ? (v_wait * 2) : 4000;
		}

		if (WiFi.status() == WL_CONNECTED) {
			s_staConnected	= true;
			s_lastStaStatus = WL_CONNECTED;
			CL_D10_Logger::log(EN_L10_LOG_INFO,
							   "[WiFi] STA ok: %s (%s)",
							   WiFi.SSID().c_str(),
							   WiFi.localIP().toString().c_str());
			return true;
		}

		CL_D10_Logger::log(EN_L10_LOG_WARN, "[WiFi] STA connect fail");
		return false;
	}

	// --------------------------------------------------
	// NTP 동기화
	// --------------------------------------------------
	static void M10_syncTimeIfNeeded(const ST_A10_WifiConfig&	p_cfg_wifi,
									 const ST_A10_SystemConfig& p_cfg_system) {
		if (s_timeSynced || !s_staConnected)
			return;

		const char* v_ntp = p_cfg_system.time.ntp_server;
		const char* v_tz  = p_cfg_system.time.timezone;

		CL_D10_Logger::log(EN_L10_LOG_INFO, "[NTP] Sync → %s (%s)", v_ntp, v_tz);

		configTime(0, 0, v_ntp);
		setenv("TZ", v_tz, 1);
		tzset();

		uint32_t v_start = millis();
		while (millis() - v_start < 10000) {
			time_t v_now = time(nullptr);
			if (v_now > 1700000000) {
				s_timeSynced = true;
				CL_D10_Logger::log(EN_L10_LOG_INFO, "[NTP] OK: %lu", v_now);
				return;
			}
			delay(250);
		}
		CL_D10_Logger::log(EN_L10_LOG_WARN, "[NTP] Timeout");
	}

	// --------------------------------------------------
	// Wi-Fi 상태 JSON
	// --------------------------------------------------
	static void M10_getWifiStateJson(JsonDocument& p_doc) {
		JsonObject v_o	  = p_doc["wifi"]["state"].to<JsonObject>();
		v_o["mode"]		  = (int)WiFi.getMode();
		v_o["status"]	  = M10_getStaStatusString();
		v_o["ssid"]		  = WiFi.SSID();
		v_o["ip"]		  = WiFi.localIP().toString();
		v_o["mac"]		  = WiFi.macAddress();
		v_o["rssi"]		  = WiFi.RSSI();
		v_o["connected"]  = M10_isStaConnected();
		v_o["timeSynced"] = s_timeSynced;
	}

	// --------------------------------------------------
	// 네트워크 스캔 JSON
	// --------------------------------------------------
	static void M10_scanNetworksToJson(JsonDocument& p_doc) {
		int		  v_found = WiFi.scanNetworks(false, true);
		JsonArray v_arr	  = p_doc["wifi"]["scan"].to<JsonArray>();

		for (int v_i = 0; v_i < v_found; v_i++) {
			JsonObject v_o = v_arr.add<JsonObject>();
			v_o["ssid"]	   = WiFi.SSID(v_i);
			v_o["rssi"]	   = WiFi.RSSI(v_i);
			v_o["chan"]	   = WiFi.channel(v_i);
			v_o["bssid"]   = WiFi.BSSIDstr(v_i);
			v_o["enc"]	   = _encTypeToString(WiFi.encryptionType(v_i));
		}
		WiFi.scanDelete();
	}

	// --------------------------------------------------
	// 유틸 / 상태 확인
	// --------------------------------------------------
	static bool M10_isStaConnected() {
		return s_staConnected && WiFi.status() == WL_CONNECTED;
	}

	static const char* M10_getStaStatusString() {
		switch (WiFi.status()) {
			case WL_CONNECTED:
				return "CONNECTED";
			case WL_NO_SSID_AVAIL:
				return "NO_SSID";
			case WL_CONNECT_FAILED:
				return "FAILED";
			case WL_IDLE_STATUS:
				return "IDLE";
			case WL_DISCONNECTED:
				return "DISCONNECTED";
			default:
				return "UNKNOWN";
		}
	}

   private:
	static const char* _encTypeToString(wifi_auth_mode_t p_mode) {
		switch (p_mode) {
			case WIFI_AUTH_OPEN:
				return "OPEN";
			case WIFI_AUTH_WEP:
				return "WEP";
			case WIFI_AUTH_WPA_PSK:
				return "WPA_PSK";
			case WIFI_AUTH_WPA2_PSK:
				return "WPA2_PSK";
			case WIFI_AUTH_WPA_WPA2_PSK:
				return "WPA_WPA2_PSK";
			case WIFI_AUTH_WPA3_PSK:
				return "WPA3_PSK";
			case WIFI_AUTH_WPA2_WPA3_PSK:
				return "WPA2_WPA3_PSK";
			default:
				return "UNKNOWN";
		}
	}
};

// --------------------------------------------------
// Static Member 정의
// --------------------------------------------------
bool		CL_M10_WiFiManager::s_staConnected	= false;
wl_status_t CL_M10_WiFiManager::s_lastStaStatus = WL_IDLE_STATUS;
bool		CL_M10_WiFiManager::s_timeSynced	= false;
