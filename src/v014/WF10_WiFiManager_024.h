#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : WF10_WiFiManager_024.h
 * 모듈약어 : WF10
 * 모듈명 : Smart Nature Wind Wi-Fi Manager + NTP Sync (v023)
 * ------------------------------------------------------
 * 기능 요약:
 * - Wi-Fi AP/STA/AP+STA 모드 자동 연결 및 폴백 지원
 * - STA 우선 연결 / 실패 시 Soft AP 백업 모드 전환
 * - Disconnect 이벤트 기반 자동 복구 (delay 제거 및 횟수 제한 적용)
 * - DHCP 재할당 지연 대응 및 SoftAP 고정 IP 설정
 * - WiFiMulti 기반 다중 네트워크 연결 및 재시도 로직
 * - DNS 서버 확인 및 NTP 시간 주기 동기화(6시간)
 * - SoftAP DHCP 서버 활성/비활성 제어 옵션
 * - Wi-Fi 상태 및 스캔 JSON 출력 (hostname, reconnect 횟수 포함)
 * - **[개선] 공유 자원 보호를 위한 Mutex 적용**
 * ------------------------------------------------------
 * [구현 규칙]
 * - ArduinoJson v7.x.x 사용 (v6 이하 금지)
 * - JsonDocument 단일 타입만 사용
 * - createNestedArray/Object/containsKey 사용 금지
 * - memset + strlcpy 기반 안전 초기화
 * - 주석/필드명은 JSON 구조와 동일하게 유지
 * ------------------------------------------------------
 * [코드 네이밍 규칙]
 * - 전역 함수      : 모듈약어 제거
 * - 로컬 변수      : v_ 접두사
 * - 함수 인자      : p_ 접두사
 * - 정적 멤버      : s_ 접두사
 * - 타입/enum/struct: T_, EN_, ST_ 접두사
 * ------------------------------------------------------
 */

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <lwip/dns.h>
#include <time.h>
#include <freertos/FreeRTOS.h> // Mutex 사용을 위해 필요
#include <freertos/semphr.h>   // Mutex 사용을 위해 필요

#include "A10_Const_015.h"
#include "D10_Logger_016.h"
// g_A10_config_root.wifi가 ST_A10_WiFiConfig* 타입이라고 가정
#include "C10_Config_029.h" 

// Mutex 보호 매크로 정의
#define WF10_MUTEX_ACQUIRE() xSemaphoreTake(CL_WF10_WiFiManager::s_wifiMutex, portMAX_DELAY)
#define WF10_MUTEX_RELEASE() xSemaphoreGive(CL_WF10_WiFiManager::s_wifiMutex)

class CL_WF10_WiFiManager {
   public:
	static bool				s_staConnected;
	static wl_status_t		s_lastStaStatus;
	static bool				s_timeSynced;
	static uint32_t			s_lastSyncMs;
	static uint8_t			s_reconnectAttempts;
	static SemaphoreHandle_t s_wifiMutex; // 🚨 Mutex 선언 추가

   public:
	// --------------------------------------------------
	// [오류 해결] 설정 적용 함수 (W10_Web_Routes_029.cpp 호출)
	// --------------------------------------------------
	/**
	 * @brief 새로운 Wi-Fi 설정을 모듈에 적용하고 재시작을 처리합니다.
	 * @param p_cfg 적용할 Wi-Fi 설정 구조체 참조
	 * @return 성공 여부
	 */
	static bool applyConfig(const ST_A10_WifiConfig& p_cfg); // 🚨 누락된 함수 선언

	// --------------------------------------------------
	// 이벤트 등록
	// --------------------------------------------------
	static void attachWiFiEvents() {
		static bool v_attached = false;
		if (v_attached)
			return;

		WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t) {
			CL_D10_Logger::log(EN_L10_LOG_INFO, "[WiFi] STA start");
		},
					 ARDUINO_EVENT_WIFI_STA_START);

		WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t) {
			WF10_MUTEX_ACQUIRE(); // Mutex 시작
			CL_D10_Logger::log(EN_L10_LOG_INFO, "[WiFi] STA got IP: %s",
							   WiFi.localIP().toString().c_str());
			s_staConnected		= true;
			s_lastStaStatus		= WL_CONNECTED;
			s_reconnectAttempts = 0;
			WF10_MUTEX_RELEASE(); // Mutex 종료
		},
					 ARDUINO_EVENT_WIFI_STA_GOT_IP);

		WiFi.onEvent([](arduino_event_id_t event, arduino_event_info_t info) {
			if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
				WF10_MUTEX_ACQUIRE(); // Mutex 시작
				CL_D10_Logger::log(EN_L10_LOG_WARN,
								   "[WiFi] STA disconnected (reason=%d)",
								   info.wifi_sta_disconnected.reason);
				s_staConnected	= false;
				s_lastStaStatus = WL_DISCONNECTED;
				s_timeSynced	= false;

				// 🚨 개선: 이벤트 핸들러 내 delay(500) 제거
				// delay(500); 

				if (s_reconnectAttempts < 5) { // 재연결 횟수 제한
					s_reconnectAttempts++;
					CL_D10_Logger::log(EN_L10_LOG_INFO,
									   "[WiFi] Reconnect attempt %d/5...",
									   s_reconnectAttempts);
					WiFi.reconnect();
				} else {
					CL_D10_Logger::log(EN_L10_LOG_ERROR,
									   "[WiFi] Reconnect limit exceeded. Manual re-init/fallback required.");
				}
				WF10_MUTEX_RELEASE(); // Mutex 종료
			}
		},
					 ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

		v_attached = true;
	}

	// --------------------------------------------------
	// 초기화
	// --------------------------------------------------
	static bool init(const ST_A10_WifiConfig&	p_cfg_wifi,
					 const ST_A10_SystemConfig& p_cfg_system,
					 WiFiMulti&					p_multi,
					 uint8_t					p_apChannel	   = 1,
					 uint8_t					p_staMaxTries  = 15,
					 bool						p_enableApDhcp = true) {
		// Mutex가 생성되었는지 확인 후 attachWiFiEvents 호출
		if (s_wifiMutex == nullptr) {
			s_wifiMutex = xSemaphoreCreateMutex();
		}
		
		attachWiFiEvents();

		WiFi.persistent(false);
		WiFi.setAutoReconnect(true); 
		WiFi.setSleep(false);

		char v_hostname[32];
		snprintf(v_hostname, sizeof(v_hostname), "NatureWind-%04X",
				 (uint16_t)(esp_random() & 0xFFFF));
		WiFi.setHostname(v_hostname);

		bool v_ap_ok = false;
		bool v_sta_ok = false;
		
		switch (p_cfg_wifi.wifiMode) {
			case 0: // AP Only
				WiFi.mode(WIFI_AP);
				return startAP(p_cfg_wifi, p_apChannel, p_enableApDhcp);
			case 1: { // STA Only (Fallback to AP)
				WiFi.mode(WIFI_STA);
				v_sta_ok = startSTA(p_cfg_wifi, p_multi, p_staMaxTries);
				if (!v_sta_ok) {
					CL_D10_Logger::log(EN_L10_LOG_WARN,
									   "[WiFi] STA fail → SoftAP fallback");
					WiFi.mode(WIFI_AP_STA); // 폴백 시 AP+STA 모드
					startAP(p_cfg_wifi, p_apChannel, p_enableApDhcp);
				}
				if (isStaConnected())
					syncTimeIfNeeded(p_cfg_wifi, p_cfg_system);
				return true;
			}
			default: { // 2 이상, AP+STA
				WiFi.mode(WIFI_AP_STA);
				v_ap_ok = startAP(p_cfg_wifi, p_apChannel, p_enableApDhcp);
				v_sta_ok = startSTA(p_cfg_wifi, p_multi, p_staMaxTries);
				if (isStaConnected())
					syncTimeIfNeeded(p_cfg_wifi, p_cfg_system);
				return v_ap_ok || v_sta_ok; // 둘 중 하나라도 성공하면 true
			}
		}
	}

	// --------------------------------------------------
	// AP 시작 (고정 IP + DHCP On/Off)
	// --------------------------------------------------
	static bool startAP(const ST_A10_WifiConfig& p_cfg_wifi,
						uint8_t					 p_channel,
						bool					 p_enableDhcp) {
		char v_pass[A10_Const::LEN_PASS + 1];
		strlcpy(v_pass, p_cfg_wifi.ap.password, sizeof(v_pass));

		if (strlen(v_pass) < 8)
			v_pass[0] = '\0';

		WiFi.softAPdisconnect(true);
		WiFi.disconnect(true, true);

		WiFi.softAPConfig(IPAddress(192, 168, 4, 1),
						  IPAddress(192, 168, 4, 1),
						  IPAddress(255, 255, 255, 0));

		bool v_ok = WiFi.softAP(p_cfg_wifi.ap.ssid, v_pass, p_channel, false, 4);

		if (!p_enableDhcp) {
			// tcpip_adapter_dhcps_stop은 esp32 v2.0.x 이상에서 esp_netif_dhcps_stop으로 대체됨
			// 하지만 플랫폼아이오 아두이노 코어 호환성을 위해 기존 함수 유지
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
	static bool startSTA(const ST_A10_WifiConfig& p_cfg_wifi,
						 WiFiMulti&				  p_multi,
						 uint8_t				  p_maxTries) {
		WF10_MUTEX_ACQUIRE(); // Mutex 시작
		s_staConnected		= false;
		s_lastStaStatus		= WL_IDLE_STATUS;
		s_reconnectAttempts = 0;
		WF10_MUTEX_RELEASE(); // Mutex 종료

		if (p_cfg_wifi.sta_count == 0) {
			CL_D10_Logger::log(EN_L10_LOG_WARN, "[WiFi] No STA entries");
			return false;
		}

		p_multi.cleanAPlist(); // 기존 목록 정리
		for (uint8_t v_i = 0; v_i < p_cfg_wifi.sta_count; v_i++) {
			const char* v_ssid = p_cfg_wifi.sta[v_i].ssid;
			const char* v_pass = p_cfg_wifi.sta[v_i].pass;
			if (v_ssid[0] == '\0')
				continue;
			p_multi.addAP(v_ssid, v_pass);
			CL_D10_Logger::log(EN_L10_LOG_INFO, "[WiFi] STA candidate: %s", v_ssid);
		}

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
			const ip_addr_t* v_dns = dns_getserver(0);
            if (v_dns) {
                CL_D10_Logger::log(EN_L10_LOG_INFO, "[WiFi] DNS: %s", ipaddr_ntoa(v_dns));
            }
			WF10_MUTEX_ACQUIRE(); // Mutex 시작
			s_staConnected	= true;
			s_lastStaStatus = WL_CONNECTED;
			WF10_MUTEX_RELEASE(); // Mutex 종료
			return true;
		}

		CL_D10_Logger::log(EN_L10_LOG_WARN, "[WiFi] STA connect fail");
		return false;
	}

	// --------------------------------------------------
	// NTP 동기화 (6시간 주기)
	// --------------------------------------------------
	static void syncTimeIfNeeded(const ST_A10_WifiConfig&,
								 const ST_A10_SystemConfig& p_cfg_system,
								 uint32_t					p_interval_ms = 21600000) {
		WF10_MUTEX_ACQUIRE(); // Mutex 시작
		if (!s_staConnected) {
			WF10_MUTEX_RELEASE(); // Mutex 종료
			return;
		}
		uint32_t v_now = millis();
		if (s_timeSynced && (v_now - s_lastSyncMs < p_interval_ms)) {
			WF10_MUTEX_RELEASE(); // Mutex 종료
			return;
		}
		WF10_MUTEX_RELEASE(); // Mutex 종료 (configTime은 블로킹 함수가 아니므로 설정 후 해제)

		setenv("TZ", p_cfg_system.time.timezone, 1);
		tzset();
		configTime(0, 0, p_cfg_system.time.ntp_server);

		uint32_t v_start = millis();
		while (millis() - v_start < 10000) {
			time_t v_nowTime = time(nullptr);
			if (v_nowTime > 1700000000) { // 2023년 11월 15일 이후
				WF10_MUTEX_ACQUIRE(); // Mutex 시작
				s_timeSynced = true;
				s_lastSyncMs = millis();
				WF10_MUTEX_RELEASE(); // Mutex 종료
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
	static void getWifiStateJson(JsonDocument& p_doc) {
		WF10_MUTEX_ACQUIRE(); // Mutex 시작
		// JsonObject v = p_doc["wifi"]["state"].to<JsonObject>(); // createNestedObject/Array 금지 규칙 준수

		// to<>() 체이닝으로 객체 생성
		JsonObject v = p_doc.to<JsonObject>()
							.set("wifi", JsonObject())
							.as<JsonObject>()["wifi"]
							.set("state", JsonObject())
							.as<JsonObject>()["state"]
							.as<JsonObject>();
		
		v["mode"]			   = (int)WiFi.getMode();
		v["mode_name"]		   = (WiFi.getMode() == WIFI_STA ? "STA" : WiFi.getMode() == WIFI_AP ? "AP"
																								 : "AP+STA");
		v["status"]			   = getStaStatusString();
		v["ssid"]			   = WiFi.SSID();
		v["ip"]				   = WiFi.localIP().toString();
		v["mac"]			   = WiFi.macAddress();
		v["rssi"]			   = WiFi.RSSI();
		v["hostname"]		   = WiFi.getHostname();
		v["connected"]		   = isStaConnected();
		v["timeSynced"]		   = s_timeSynced;
		v["reconnectAttempts"] = s_reconnectAttempts;
		WF10_MUTEX_RELEASE(); // Mutex 종료
	}

	// --------------------------------------------------
	// 스캔 JSON
	// --------------------------------------------------
	static void scanNetworksToJson(JsonDocument& p_doc) {
		int		  v_found = WiFi.scanNetworks(false, true);
		
		// to<>() 체이닝으로 배열 생성
		JsonArray arr = p_doc.to<JsonObject>()
							.set("wifi", JsonObject())
							.as<JsonObject>()["wifi"]
							.set("scan", JsonArray())
							.as<JsonObject>()["scan"]
							.as<JsonArray>();

		for (int i = 0; i < v_found; i++) {
			JsonObject o = arr.add<JsonObject>();
			o["ssid"]	 = WiFi.SSID(i);
			o["rssi"]	 = WiFi.RSSI(i);
			o["chan"]	 = WiFi.channel(i);
			o["bssid"]	 = WiFi.BSSIDstr(i);
			o["enc"]	 = _encTypeToString(WiFi.encryptionType(i));
		}
		WiFi.scanDelete();
	}

	static bool isStaConnected() {
		// 상태 변수 접근 시 Mutex 사용
		WF10_MUTEX_ACQUIRE(); // Mutex 시작
		bool v_status = s_staConnected && WiFi.status() == WL_CONNECTED;
		WF10_MUTEX_RELEASE(); // Mutex 종료
		return v_status;
	}

	static const char* getStaStatusString() {
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


