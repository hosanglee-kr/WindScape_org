// M10_WiFiManager_006.h

/* 사용법
1. 연결과 분기

bool sta = CL_M10_WiFiManager::init(g_config, wifiMulti);
if (sta) {
  // STA 경로
} else {
  // AP 경로(설정 페이지/캡티브 포털 등)
}

2.wifi scan 비동기 사용
CL_M10_WiFiManager::scanNetworksJson(true);   // 1차: 트리거
delay(1500);                                // 스캔 대기
String nets = CL_M10_WiFiManager::scanNetworksJson(false); // 2차: 수집

*/

#pragma once
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WiFiMulti.h>

#include "A10_Const_006.h"
#include "D10_Logger_004.h"

class CL_M10_WiFiManager {
   public:
	// 연결 상태 캐시
	static bool s_staConnected;

	// 이벤트 핸들러 등록(한 번만)
	static void attachWiFiEvents() {
		static bool v_attached = false;
		if (v_attached)
			return;
		WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t) {
			CL_D10_Logger::log(EN_L10_LOG_INFO, "WiFi event: STA started");
		},
					 ARDUINO_EVENT_WIFI_STA_START);

		WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t) {
			CL_D10_Logger::log(EN_L10_LOG_INFO, "WiFi event: STA got IP: %s",
							 WiFi.localIP().toString().c_str());
			s_staConnected = true;
		},
					 ARDUINO_EVENT_WIFI_STA_GOT_IP);

		WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t) {
			CL_D10_Logger::log(EN_L10_LOG_WARN, "WiFi event: STA disconnected");
			s_staConnected = false;
		},
					 ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

		v_attached = true;
	}

	// AP/STA 초기화 (STA 실패 시 AP로 폴백, 성공 시 AP 끄기)
	// 반환: true = STA 연결됨, false = AP 모드로 폴백
	static bool init(ST_A10_WindConfig &p_cfg, WiFiMulti &p_multi, uint8_t p_apChannel = 1,
					 uint8_t p_staMaxTries = 15) {
		attachWiFiEvents();

		// 기본 옵션
		WiFi.persistent(false);	 // NVS 쓰기 최소화
		WiFi.setAutoReconnect(true);
		WiFi.setSleep(false);  // 필요 시 true로 절전

		/*
		if (strlen(p_cfg.hostname) > 0) {
		  WiFi.setHostname(p_cfg.hostname);
		}
		*/

		// (선택) 국가코드 설정 - 규제 채널/출력 준수
		// esp_wifi_set_country를 직접 쓰려면 esp_wifi.h include 필요
		// WiFi.setCountry("KR"); // 최신 IDF 래퍼가 있는 경우

		WiFi.mode(WIFI_AP_STA);

		// ---------- STA 우선 시도 ----------
		if (p_cfg.wifi_mode == G_A10_WIFI_MODE_STA && p_cfg.sta_network_count > 0) {
			for (int i = 0; i < p_cfg.sta_network_count; i++) {
				// 비밀번호는 로그 금지
				p_multi.addAP(p_cfg.sta_networks[i].ssid, p_cfg.sta_networks[i].password);
				CL_D10_Logger::log(EN_L10_LOG_INFO, "WiFi STA added: %s", p_cfg.sta_networks[i].ssid);
			}

			const uint32_t v_tryStart = millis();
			uint8_t		   v_tries	  = 0;
			uint32_t	   v_waitMs	  = 500;  // 지수 백오프 시작
			while (WiFi.status() != WL_CONNECTED && v_tries < p_staMaxTries) {
				wl_status_t s = (wl_status_t)WiFi.status();
				(void)s;  // 필요하면 상태별 로깅 추가
				if (p_multi.run(2500) == WL_CONNECTED)
					break;	// 각 시도 2.5초
				v_tries++;
				delay(v_waitMs);
				// 지수 백오프: 최대 4초까지
				v_waitMs = (v_waitMs < 4000) ? v_waitMs * 2 : 4000;
				Serial.print(".");
			}

			if (WiFi.status() == WL_CONNECTED) {
				CL_D10_Logger::log(EN_L10_LOG_INFO, "\nSTA Connected: %s, IP: %s",
								 WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
				// STA 붙었으면 AP는 끔
				WiFi.softAPdisconnect(true);
				return true;
			}
			CL_D10_Logger::log(EN_L10_LOG_WARN, "\nSTA connect failed. Fallback to AP");
		}

		// ---------- AP 기동 ----------

		char v_apPass[65] = {0};
		strlcpy(v_apPass, p_cfg.ap_password, sizeof(v_apPass));
		/*
		// AP 비번 안전장치(8자 미만이면 임시 난수 비번 생성)
		char v_apPass[65] = {0};
		if (strlen(p_cfg.ap_password) >= 8) {
			strlcpy(v_apPass, p_cfg.ap_password, sizeof(v_apPass));
		} else {
			// 간단 난수 비번 생성(개발 편의) — 제품에선 고정/UI 입력 권장
			uint32_t r = (uint32_t)esp_random();
			snprintf(v_apPass, sizeof(v_apPass), "ap_%08X", (unsigned)r);
			CL_D10_Logger::log(EN_L10_LOG_WARN,
							 "AP password too short(<8). Using temporary password: %s", v_apPass);
		}
		*/

		// 이전 연결 상태 정리 후 AP 시작
		WiFi.disconnect(true, true);
		// (선택) 고정 AP IP 설정이 필요하다면:
		// WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));

		bool v_apOk = WiFi.softAP(p_cfg.ap_ssid, v_apPass, p_apChannel, false /*hidden*/, 4 /*max conn*/);
		if (!v_apOk) {
			CL_D10_Logger::log(EN_L10_LOG_ERROR, "AP start failed");
		} else {
			CL_D10_Logger::log(EN_L10_LOG_INFO, "AP started: %s, IP: %s",
							 p_cfg.ap_ssid, WiFi.softAPIP().toString().c_str());
		}
		s_staConnected = false;
		return false;
	}

	// 주변 네트워크 스캔 → JSON(String)
	// 비동기 스캔을 원하면 p_async=true로 두 번 호출(1차 트리거, 2차 수집)
	static String scanNetworksJson(bool p_async = false) {
		int v_found = WiFi.scanNetworks(p_async /*async*/, true /*hidden*/);
		if (p_async) {
			// 트리거만 하고 빈 배열 반환(다음 호출에서 결과 수집)
			return F("[]");
		}

		//
		JsonDocument v_doc;
		JsonArray	 v_arr = v_doc.to<JsonArray>();

		for (int i = 0; i < v_found; i++) {
			JsonObject o = v_arr.add<JsonObject>();
			o["ssid"]	 = WiFi.SSID(i);
			o["rssi"]	 = WiFi.RSSI(i);
			o["bssid"]	 = WiFi.BSSIDstr(i);
			o["chan"]	 = WiFi.channel(i);
			o["enc"]	 = encTypeToString(WiFi.encryptionType(i));	 // 사람이 읽기 쉬운 문자열
		}

		String out;
		serializeJson(v_doc, out);
		// 스캔 버퍼 정리(메모리 회수)
		WiFi.scanDelete();
		return out;
	}

	// 현재 STA 연결 여부
	static bool isStaConnected() {
		return s_staConnected && (WiFi.status() == WL_CONNECTED);
	}

   private:
	// 암호화 타입 문자열 변환
	static const char *encTypeToString(wifi_auth_mode_t m) {
		switch (m) {
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
			case WIFI_AUTH_WPA2_ENTERPRISE:
				return "WPA2_ENT";
			case WIFI_AUTH_WPA3_PSK:
				return "WPA3_PSK";
			case WIFI_AUTH_WPA2_WPA3_PSK:
				return "WPA2_WPA3_PSK";
			case WIFI_AUTH_WAPI_PSK:
				return "WAPI_PSK";
			default:
				return "UNKNOWN";
		}
	}
};

// 정적 멤버 정의
bool CL_M10_WiFiManager::s_staConnected = false;
