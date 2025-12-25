/*
 * ------------------------------------------------------
 * 소스명 : WF10_WiFiManager_040.cpp
 * 모듈약어 : WF10
 * 모듈명 : Smart Nature Wind Wi-Fi Manager + NTP Sync (v024)
 * ------------------------------------------------------
 * 구현 파일 요약:
 * - Wi-Fi 초기화 / AP / STA / AP+STA 모드 처리
 * - 이벤트 콜백 및 재연결 로직
 * - NTP 시간 동기화 및 주기 제어
 * - Wi-Fi 상태/스캔 JSON 출력
 * - WF10_applyTimeConfigFromSystem 구현
 * ------------------------------------------------------
 */

#include "WF10_WiFiManager_040.h"

// Config 루트 (다른 모듈에서 정의)
extern ST_A20_ConfigRoot_t g_A20_config_root;

// --------------------------------------------------
// Static Members Definition (단 하나의 .cpp 파일에만 정의)
// --------------------------------------------------
bool					   CL_WF10_WiFiManager::s_staConnected		= false;
wl_status_t				   CL_WF10_WiFiManager::s_lastStaStatus		= WL_IDLE_STATUS;
bool					   CL_WF10_WiFiManager::s_timeSynced		= false;
uint32_t				   CL_WF10_WiFiManager::s_lastSyncMs		= 0;
uint8_t					   CL_WF10_WiFiManager::s_reconnectAttempts = 0;
SemaphoreHandle_t		   CL_WF10_WiFiManager::s_wifiMutex			= nullptr;	// Mutex는 init()에서 생성

// --------------------------------------------------
// 이벤트 등록
// --------------------------------------------------
void CL_WF10_WiFiManager::attachWiFiEvents() {
	static bool v_attached = false;
	if (v_attached)
		return;

	WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t) { CL_D10_Logger::log(EN_L10_LOG_INFO, "[WiFi] STA start"); }, ARDUINO_EVENT_WIFI_STA_START);

	WiFi.onEvent(
		[](arduino_event_id_t, arduino_event_info_t) {
			WF10_MUTEX_ACQUIRE();
			CL_D10_Logger::log(EN_L10_LOG_INFO, "[WiFi] STA got IP: %s", WiFi.localIP().toString().c_str());
			s_staConnected		= true;
			s_lastStaStatus		= WL_CONNECTED;
			s_reconnectAttempts = 0;
			WF10_MUTEX_RELEASE();
		},
		ARDUINO_EVENT_WIFI_STA_GOT_IP);

	WiFi.onEvent(
		[](arduino_event_id_t event, arduino_event_info_t info) {
			if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
				WF10_MUTEX_ACQUIRE();
				CL_D10_Logger::log(EN_L10_LOG_WARN, "[WiFi] STA disconnected (reason=%d)", info.wifi_sta_disconnected.reason);
				s_staConnected	= false;
				s_lastStaStatus = WL_DISCONNECTED;
				s_timeSynced	= false;

				if (s_reconnectAttempts < 5) {	// 재연결 횟수 제한
					s_reconnectAttempts++;
					CL_D10_Logger::log(EN_L10_LOG_INFO, "[WiFi] Reconnect attempt %d/5...", s_reconnectAttempts);
					WiFi.reconnect();
				} else {
					CL_D10_Logger::log(EN_L10_LOG_ERROR, "[WiFi] Reconnect limit exceeded. Manual re-init/fallback required.");
				}
				WF10_MUTEX_RELEASE();
			}
		},
		ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

	v_attached = true;
}

// --------------------------------------------------
// 초기화
// --------------------------------------------------
bool CL_WF10_WiFiManager::init(const ST_A20_WifiConfig& p_cfg_wifi, const ST_A20_SystemConfig& p_cfg_system, WiFiMulti& p_multi, uint8_t p_apChannel, uint8_t p_staMaxTries, bool p_enableApDhcp) {
	// Mutex 생성
	if (s_wifiMutex == nullptr) {
		s_wifiMutex = xSemaphoreCreateMutex();
		if (!s_wifiMutex) {
			CL_D10_Logger::log(EN_L10_LOG_ERROR, "[WiFi] Mutex create failed");
		}
	}

	attachWiFiEvents();

	WiFi.persistent(false);
	WiFi.setAutoReconnect(true);
	WiFi.setSleep(false);

	char v_hostname[32];
	snprintf(v_hostname, sizeof(v_hostname), "NatureWind-%04X", (uint16_t)(esp_random() & 0xFFFF));
	WiFi.setHostname(v_hostname);

	bool	 v_ap_ok	   = false;
	bool	 v_sta_ok	   = false;

	// system.time.sync_interval_min을 ms로 변환 (0이면 기본 6시간 사용)
	uint32_t v_interval_ms = (uint32_t)p_cfg_system.time.sync_interval_min * 60000UL;
	if (v_interval_ms == 0) {
		v_interval_ms = 21600000UL;	 // 6시간 기본값
	}

	switch (p_cfg_wifi.wifiMode) {
		case 0:	 // AP Only
			WiFi.mode(WIFI_AP);
			return startAP(p_cfg_wifi, p_apChannel, p_enableApDhcp);

		case 1: {  // STA Only (Fallback to AP)
			WiFi.mode(WIFI_STA);
			v_sta_ok = startSTA(p_cfg_wifi, p_multi, p_staMaxTries);
			if (!v_sta_ok) {
				CL_D10_Logger::log(EN_L10_LOG_WARN, "[WiFi] STA fail → SoftAP fallback");
				WiFi.mode(WIFI_AP_STA);	 // 폴백 시 AP+STA 모드
				startAP(p_cfg_wifi, p_apChannel, p_enableApDhcp);
			}
			if (isStaConnected())
				syncTimeIfNeeded(p_cfg_wifi, p_cfg_system, v_interval_ms);
			return true;
		}

		default: {	// 2 이상, AP+STA
			WiFi.mode(WIFI_AP_STA);
			v_ap_ok	 = startAP(p_cfg_wifi, p_apChannel, p_enableApDhcp);
			v_sta_ok = startSTA(p_cfg_wifi, p_multi, p_staMaxTries);
			if (isStaConnected())
				syncTimeIfNeeded(p_cfg_wifi, p_cfg_system, v_interval_ms);
			return v_ap_ok || v_sta_ok;	 // 둘 중 하나라도 성공하면 true
		}
	}
}

// --------------------------------------------------
// AP 시작 (고정 IP + DHCP On/Off)
// --------------------------------------------------
bool CL_WF10_WiFiManager::startAP(const ST_A20_WifiConfig& p_cfg_wifi, uint8_t p_channel, bool p_enableDhcp) {
	char v_pass[A20_Const::LEN_PASS + 1];
	memset(v_pass, 0, sizeof(v_pass));
	strlcpy(v_pass, p_cfg_wifi.ap.password, sizeof(v_pass));

	if (strlen(v_pass) < 8)
		v_pass[0] = '\0';

	WiFi.softAPdisconnect(true);
	WiFi.disconnect(true, true);

	WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));

	bool v_ok = WiFi.softAP(p_cfg_wifi.ap.ssid, v_pass, p_channel, false, 4);

	if (!p_enableDhcp) {
		tcpip_adapter_dhcps_stop(TCPIP_ADAPTER_IF_AP);
		CL_D10_Logger::log(EN_L10_LOG_INFO, "[WiFi] AP DHCP disabled");
	}

	CL_D10_Logger::log(v_ok ? EN_L10_LOG_INFO : EN_L10_LOG_ERROR, v_ok ? "[WiFi] AP started (%s)" : "[WiFi] AP start ERR", WiFi.softAPIP().toString().c_str());
	return v_ok;
}

// --------------------------------------------------
// STA 시작
// --------------------------------------------------
bool CL_WF10_WiFiManager::startSTA(const ST_A20_WifiConfig& p_cfg_wifi, WiFiMulti& p_multi, uint8_t p_maxTries) {
	WF10_MUTEX_ACQUIRE();
	s_staConnected		= false;
	s_lastStaStatus		= WL_IDLE_STATUS;
	s_reconnectAttempts = 0;
	WF10_MUTEX_RELEASE();

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
		WF10_MUTEX_ACQUIRE();
		s_staConnected	= true;
		s_lastStaStatus = WL_CONNECTED;
		WF10_MUTEX_RELEASE();
		return true;
	}

	CL_D10_Logger::log(EN_L10_LOG_WARN, "[WiFi] STA connect fail");
	return false;
}

// --------------------------------------------------
// NTP 동기화 (구성값 기반 주기)
// --------------------------------------------------
void CL_WF10_WiFiManager::syncTimeIfNeeded(const ST_A20_WifiConfig&, const ST_A20_SystemConfig& p_cfg_system, uint32_t p_interval_ms) {
	WF10_MUTEX_ACQUIRE();
	if (!s_staConnected) {
		WF10_MUTEX_RELEASE();
		return;
	}
	uint32_t v_now = millis();
	if (s_timeSynced && (v_now - s_lastSyncMs < p_interval_ms)) {
		WF10_MUTEX_RELEASE();
		return;
	}
	WF10_MUTEX_RELEASE();

	// TZ/NTP 설정 적용
	setenv("TZ", p_cfg_system.time.timezone, 1);
	tzset();
	configTime(0, 0, p_cfg_system.time.ntp_server);

	uint32_t v_start = millis();
	while (millis() - v_start < 10000) {
		time_t v_nowTime = time(nullptr);
		if (v_nowTime > 1700000000) {  // 2023-11-15 이후
			WF10_MUTEX_ACQUIRE();
			s_timeSynced = true;
			s_lastSyncMs = millis();
			WF10_MUTEX_RELEASE();
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
void CL_WF10_WiFiManager::getWifiStateJson(JsonDocument& p_doc) {
	WF10_MUTEX_ACQUIRE();

	JsonObject v_root	   = p_doc.to<JsonObject>();
	JsonObject v_wifi	   = v_root["wifi"].to<JsonObject>();
	JsonObject v		   = v_wifi["state"].to<JsonObject>();

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

	WF10_MUTEX_RELEASE();
}

// --------------------------------------------------
// 스캔 JSON
// --------------------------------------------------
void CL_WF10_WiFiManager::scanNetworksToJson(JsonDocument& p_doc) {
	int		   v_found = WiFi.scanNetworks(false, true);

	JsonObject v_root  = p_doc.to<JsonObject>();
	JsonObject v_wifi  = v_root["wifi"].to<JsonObject>();
	JsonArray  arr	   = v_wifi["scan"].to<JsonArray>();

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

// --------------------------------------------------
// 상태/헬퍼
// --------------------------------------------------
bool CL_WF10_WiFiManager::isStaConnected() {
	WF10_MUTEX_ACQUIRE();
	bool v_status = s_staConnected && WiFi.status() == WL_CONNECTED;
	WF10_MUTEX_RELEASE();
	return v_status;
}

const char* CL_WF10_WiFiManager::getStaStatusString() {
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

const char* CL_WF10_WiFiManager::_encTypeToString(wifi_auth_mode_t p_mode) {
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

// --------------------------------------------------
// Wi-Fi 설정 적용 (Web API → Config 변경 후 호출)
// --------------------------------------------------
bool CL_WF10_WiFiManager::applyConfig(const ST_A20_WifiConfig& p_cfg) {
	CL_D10_Logger::log(EN_L10_LOG_INFO, "[WiFi] Applying new configuration...");

	// 1. 현재 Wi-Fi 연결/AP를 모두 끊습니다.
	WiFi.disconnect(true);
	WiFi.softAPdisconnect(true);

	// 2. WiFiMulti 준비
	WiFiMulti v_multi;

	// 3. system config 존재 여부 확인
	if (!g_A20_config_root.system) {
		CL_D10_Logger::log(EN_L10_LOG_ERROR,
			"[WiFi] applyConfig: system config is null. "
			"Using default time interval (6h) without full system integration.");

		ST_A20_SystemConfig v_sys;
		memset(&v_sys, 0, sizeof(v_sys));
		v_sys.time.sync_interval_min = 360;	 // 6시간

		bool v_ok					 = init(p_cfg, v_sys, v_multi, 1, 15, true);
		return v_ok;
	}

	// 4. 기존 init() 로직 재사용 (AP/STA + NTP 동기화까지 포함)
	bool v_ok = init(p_cfg, *g_A20_config_root.system, v_multi, 1, 15, true);

	CL_D10_Logger::log(EN_L10_LOG_INFO, "[WiFi] Configuration applied (ok=%d, mode=%d)", (int)v_ok, (int)p_cfg.wifiMode);
	return v_ok;
}

// --------------------------------------------------
// WF10_applyTimeConfigFromSystem 구현
//  - /api/system/time/set 등에서 호출
//  - system.time 설정(TZ, NTP 서버, sync_interval_min)을 런타임에 반영
// --------------------------------------------------
void WF10_applyTimeConfigFromSystem(const ST_A20_SystemConfig& p_cfg) {
	// 1. 로그: 적용할 설정 요약
	CL_D10_Logger::log(EN_L10_LOG_INFO, "[WF10] Apply time config: ntp=%s, tz=%s, interval=%u min", p_cfg.time.ntp_server, p_cfg.time.timezone, (unsigned int)p_cfg.time.sync_interval_min);

	// 2. TZ 및 NTP 서버 환경 설정
	setenv("TZ", p_cfg.time.timezone, 1);
	tzset();
	configTime(0, 0, p_cfg.time.ntp_server);

	// 3. (옵션) 즉시 동기화 시도 (Wi-Fi 연결 여부와 무관하게 "최대한" 맞추기)
	uint32_t v_start = millis();
	bool	 v_ok	 = false;
	while (millis() - v_start < 5000) {	 // 최대 5초 대기
		time_t v_nowTime = time(nullptr);
		if (v_nowTime > 1700000000) {	 // 2023년 11월 15일 이후
			v_ok = true;
			break;
		}
		delay(250);
	}

	if (v_ok) {
		CL_D10_Logger::log(EN_L10_LOG_INFO, "[WF10] Time sync OK after config apply");
	} else {
		CL_D10_Logger::log(EN_L10_LOG_WARN, "[WF10] Time sync not yet complete after config apply (non-fatal)");
	}

	// 4. Wi-Fi가 존재하면, 새 sync_interval 기준으로 정규 동기화 루프도 갱신
	if (g_A20_config_root.wifi) {
		uint32_t v_interval_ms = (uint32_t)p_cfg.time.sync_interval_min * 60000UL;
		if (v_interval_ms == 0) {
			v_interval_ms = 21600000UL;	 // 6시간 기본값
		}

		CL_WF10_WiFiManager::syncTimeIfNeeded(*g_A20_config_root.wifi, p_cfg, v_interval_ms);
	}
}
