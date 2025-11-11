#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : WF10_WiFiManager_023.h
 * 모듈약어 : WF10
 * 모듈명 : Smart Nature Wind Wi-Fi Manager + NTP Sync (v023)
 * ------------------------------------------------------
 * 기능 요약:
 *  - Wi-Fi AP/STA/AP+STA 자동 연결 및 폴백 제어
 *  - WiFiMulti 기반 STA 우선 연결 + SoftAP 백업
 *  - Disconnect 이벤트 기반 자동 복구 (제한/지연 포함)
 *  - DHCP 재할당 지연 대응 및 SoftAP 고정 IP 설정
 *  - DNS 연결 검증 및 NTP 주기 동기화(6시간)
 *  - SoftAP DHCP 서버 활성/비활성 옵션 지원
 *  - Wi-Fi 상태 및 스캔 JSON 출력 (hostname 포함)
 * ------------------------------------------------------
 * [구현 규칙]
 *  - ArduinoJson v7.x.x 사용 (v6 이하 금지)
 *  - JsonDocument 단일 타입만 사용
 *  - createNestedArray/Object/containsKey 사용 금지
 *  - memset + strlcpy 기반 안전 초기화
 *  - 주석/필드명은 JSON 구조와 동일하게 유지
 * ------------------------------------------------------
 * [코드 네이밍 규칙]
 *   - 전역 함수      : WF10_ 접두사
 *   - 로컬 변수      : v_ 접두사
 *   - 함수 인자      : p_ 접두사
 *   - 정적 멤버      : s_ 접두사
 *   - 타입/enum/struct: T_, EN_, ST_ 접두사
 * ------------------------------------------------------
 */

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <time.h>
#include <lwip/dns.h>

#include "A10_Const_015.h"
#include "D10_Logger_016.h"

class CL_WF10_WiFiManager {
public:
	static bool		s_staConnected;
	static wl_status_t s_lastStaStatus;
	static bool		s_timeSynced;
	static uint32_t	s_lastSyncMs;
	static uint8_t	s_reconnectAttempts;

public:
	// --------------------------------------------------
	// 이벤트 등록
	// --------------------------------------------------
	static void WF10_attachWiFiEvents() {
		static bool v_attached = false;
		if (v_attached) return;

		WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t) {
			CL_D10_Logger::log(EN_L10_LOG_INFO, "[WiFi] STA start");
		}, ARDUINO_EVENT_WIFI_STA_START);

		WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t) {
			CL_D10_Logger::log(EN_L10_LOG_INFO, "[WiFi] STA got IP: %s",
							   WiFi.localIP().toString().c_str());
			s_staConnected = true;
			s_lastStaStatus = WL_CONNECTED;
			s_reconnectAttempts = 0;
		}, ARDUINO_EVENT_WIFI_STA_GOT_IP);

		WiFi.onEvent([](arduino_event_id_t event, arduino_event_info_t info) {
			if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
				CL_D10_Logger::log(EN_L10_LOG_WARN,
								   "[WiFi] STA disconnected (reason=%d)",
								   info.wifi_sta_disconnected.reason);
				s_staConnected = false;
				s_lastStaStatus = WL_DISCONNECTED;
				s_timeSynced = false;

				// DHCP 지연 대응 + 재연결 시도 제한
				delay(500);
				if (s_reconnectAttempts < 5) {
					s_reconnectAttempts++;
					CL_D10_Logger::log(EN_L10_LOG_INFO,
									   "[WiFi] Reconnect attempt %d/5...",
									   s_reconnectAttempts);
					WiFi.reconnect();
				} else {
					CL_D10_Logger::log(EN_L10_LOG_ERROR,
									   "[WiFi] Reconnect limit exceeded");
				}
			}
		}, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

		v_attached = true;
	}

	// --------------------------------------------------
	// 초기화
	// --------------------------------------------------
	static bool init(const ST_A10_WifiConfig& p_cfg_wifi,
					 const ST_A10_SystemConfig& p_cfg_system,
					 WiFiMulti& p_multi,
					 uint8_t p_apChannel = 1,
					 uint8_t p_staMaxTries = 15,
					 bool p_enableApDhcp = true) {
		WF10_attachWiFiEvents();

		WiFi.persistent(false);
		WiFi.setAutoReconnect(true);
		WiFi.setSleep(false);

		char v_hostname[32];
		snprintf(v_hostname, sizeof(v_hostname), "NatureWind-%04X",
				 (uint16_t)(esp_random() & 0xFFFF));
		WiFi.setHostname(v_hostname);

		switch (p_cfg_wifi.wifiMode) {
			case 0: // AP
				WiFi.mode(WIFI_AP);
				return WF10_startAP(p_cfg_wifi, p_apChannel, p_enableApDhcp);

			case 1: { // STA
				WiFi.mode(WIFI_STA);
				bool v_ok = WF10_startSTA(p_cfg_wifi, p_multi, p_staMaxTries);
				if (!v_ok) {
					CL_D10_Logger::log(EN_L10_LOG_WARN, "[WiFi] STA fail → SoftAP fallback");
					WiFi.mode(WIFI_AP_STA);
					WF10_startAP(p_cfg_wifi, p_apChannel, p_enableApDhcp);
				}
				if (s_staConnected) WF10_syncTimeIfNeeded(p_cfg_wifi, p_cfg_system);
				return true;
			}

			default: { // AP+STA
				WiFi.mode(WIFI_AP_STA);
				WF10_startAP(p_cfg_wifi, p_apChannel, p_enableApDhcp);
				bool v_ok = WF10_startSTA(p_cfg_wifi, p_multi, p_staMaxTries);
				if (v_ok && s_staConnected)
					WF10_syncTimeIfNeeded(p_cfg_wifi, p_cfg_system);
				return true;
			}
		}
	}

	// --------------------------------------------------
	// AP 시작 (고정 IP + DHCP On/Off)
	// --------------------------------------------------
	static bool WF10_startAP(const ST_A10_WifiConfig& p_cfg_wifi,
							 uint8_t p_channel,
							 bool p_enableDhcp) {
		char v_pass[A10_Const::LEN_PASS + 1];
		strlcpy(v_pass, p_cfg_wifi.ap.password, sizeof(v_pass));

		if (strlen(v_pass) < 8) v_pass[0] = '\0';

		WiFi.softAPdisconnect(true);
		WiFi.disconnect(true, true);

		WiFi.softAPConfig(IPAddress(192, 168, 4, 1),
						  IPAddress(192, 168, 4, 1),
						  IPAddress(255, 255, 255, 0));

		bool v_ok = WiFi.softAP(p_cfg_wifi.ap.ssid, v_pass, p_channel, false, 4);

		if (!p_enableDhcp) {
			// DHCP 서버 중지 (필요 시)
			tcpip_adapter_dhcps_stop(TCPIP_ADAPTER_IF_AP);
			CL_D10_Logger::log(EN_L10_LOG_INFO, "[WiFi] AP DHCP disabled");
		}

		CL_D10_Logger::log(v_ok ? EN_L10_LOG_INFO : EN_L10_LOG_ERROR,
						   v_ok ? "[WiFi] AP started (%s)" : "[WiFi] AP start ERR",
						   WiFi.softAPIP().toString().c_str());
		return v_ok;
	}

	// --------------------------------------------------
	// STA 시작
	// --------------------------------------------------
	static bool WF10_startSTA(const ST_A10_WifiConfig& p_cfg_wifi,
							  WiFiMulti& p_multi,
							  uint8_t p_maxTries) {
		s_staConnected = false;
		s_lastStaStatus = WL_IDLE_STATUS;
		s_reconnectAttempts = 0;

		if (p_cfg_wifi.sta_count == 0) {
			CL_D10_Logger::log(EN_L10_LOG_WARN, "[WiFi] No STA entries");
			return false;
		}

		for (uint8_t v_i = 0; v_i < p_cfg_wifi.sta_count; v_i++) {
			const char* v_ssid = p_cfg_wifi.sta[v_i].ssid;
			const char* v_pass = p_cfg_wifi.sta[v_i].pass;
			if (v_ssid[0] == '\0') continue;
			p_multi.addAP(v_ssid, v_pass);
			CL_D10_Logger::log(EN_L10_LOG_INFO, "[WiFi] STA candidate: %s", v_ssid);
		}

		uint8_t v_try = 0;
		uint32_t v_wait = 500;
		while (WiFi.status() != WL_CONNECTED && v_try < p_maxTries) {
			if (p_multi.run(2000) == WL_CONNECTED) break;
			v_try++;
			delay(v_wait);
			v_wait = (v_wait < 4000) ? (v_wait * 2) : 4000;
		}

		if (WiFi.status() == WL_CONNECTED) {
			// DNS 확인
			ip_addr_t v_dns;
			dns_getserver(0, &v_dns);
			CL_D10_Logger::log(EN_L10_LOG_INFO, "[WiFi] DNS: %s",
							   ipaddr_ntoa(&v_dns));
			s_staConnected = true;
			s_lastStaStatus = WL_CONNECTED;
			return true;
		}

		CL_D10_Logger::log(EN_L10_LOG_WARN, "[WiFi] STA connect fail");
		return false;
	}

	// --------------------------------------------------
	// NTP 동기화 (6시간 주기)
	// --------------------------------------------------
	static void WF10_syncTimeIfNeeded(const ST_A10_WifiConfig&,
									  const ST_A10_SystemConfig& p_cfg_system,
									  uint32_t p_interval_ms = 21600000) {
		if (!s_staConnected) return;
		uint32_t v_now = millis();
		if (s_timeSynced && (v_now - s_lastSyncMs < p_interval_ms)) return;

		setenv("TZ", p_cfg_system.time.timezone, 1);
		tzset();
		configTime(0, 0, p_cfg_system.time.ntp_server);

		uint32_t v_start = millis();
		while (millis() - v_start < 10000) {
			time_t v_nowTime = time(nullptr);
			if (v_nowTime > 1700000000) {
				s_timeSynced = true;
				s_lastSyncMs = millis();
				CL_D10_Logger::log(EN_L10_LOG_INFO, "[NTP] Sync OK");
				return;
			}
			delay(250);
		}
		CL_D10_Logger::log(EN_L10_LOG_WARN, "[NTP] Timeout");
	}

	// --------------------------------------------------
	// 상태 JSON
	// --------------------------------------------------
	static void WF10_getWifiStateJson(JsonDocument& p_doc) {
		JsonObject v = p_doc["wifi"]["state"].to<JsonObject>();
		v["mode"] = (int)WiFi.getMode();
		v["mode_name"] = (WiFi.getMode() == WIFI_STA ? "STA" :
						  WiFi.getMode() == WIFI_AP ? "AP" : "AP+STA");
		v["status"] = WF10_getStaStatusString();
		v["ssid"] = WiFi.SSID();
		v["ip"] = WiFi.localIP().toString();
		v["mac"] = WiFi.macAddress();
		v["rssi"] = WiFi.RSSI();
		v["hostname"] = WiFi.getHostname();
		v["connected"] = WF10_isStaConnected();
		v["timeSynced"] = s_timeSynced;
		v["reconnectAttempts"] = s_reconnectAttempts;
	}

	// --------------------------------------------------
	// 스캔 JSON
	// --------------------------------------------------
	static void WF10_scanNetworksToJson(JsonDocument& p_doc) {
		int v_found = WiFi.scanNetworks(false, true);
		JsonArray arr = p_doc["wifi"]["scan"].to<JsonArray>();
		for (int i = 0; i < v_found; i++) {
			JsonObject o = arr.add<JsonObject>();
			o["ssid"] = WiFi.SSID(i);
			o["rssi"] = WiFi.RSSI(i);
			o["chan"] = WiFi.channel(i);
			o["bssid"] = WiFi.BSSIDstr(i);
			o["enc"] = _encTypeToString(WiFi.encryptionType(i));
		}
		WiFi.scanDelete();
	}

	static bool WF10_isStaConnected() {
		return s_staConnected && WiFi.status() == WL_CONNECTED;
	}

	static const char* WF10_getStaStatusString() {
		switch (WiFi.status()) {
			case WL_CONNECTED: return "CONNECTED";
			case WL_NO_SSID_AVAIL: return "NO_SSID";
			case WL_CONNECT_FAILED: return "FAILED";
			case WL_IDLE_STATUS: return "IDLE";
			case WL_DISCONNECTED: return "DISCONNECTED";
			default: return "UNKNOWN";
		}
	}

private:
	static const char* _encTypeToString(wifi_auth_mode_t p_mode) {
		switch (p_mode) {
			case WIFI_AUTH_OPEN: return "OPEN";
			case WIFI_AUTH_WEP: return "WEP";
			case WIFI_AUTH_WPA_PSK: return "WPA_PSK";
			case WIFI_AUTH_WPA2_PSK: return "WPA2_PSK";
			case WIFI_AUTH_WPA_WPA2_PSK: return "WPA_WPA2_PSK";
			case WIFI_AUTH_WPA3_PSK: return "WPA3_PSK";
			case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2_WPA3_PSK";
			default: return "UNKNOWN";
		}
	}
};

// --------------------------------------------------
// Static Members
// --------------------------------------------------
bool		CL_WF10_WiFiManager::s_staConnected = false;
wl_status_t CL_WF10_WiFiManager::s_lastStaStatus = WL_IDLE_STATUS;
bool		CL_WF10_WiFiManager::s_timeSynced = false;
uint32_t	CL_WF10_WiFiManager::s_lastSyncMs = 0;
uint8_t		CL_WF10_WiFiManager::s_reconnectAttempts = 0;
