#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : M10_WiFiManager_010.h
 * 모듈약어 : M10
 * 모듈명 : Smart Nature Wind Wi-Fi Manager
 * ------------------------------------------------------
 * 기능 요약:
 *  - Wi-Fi AP/STA/AP+STA 모드 제어
 *  - STA 우선 연결 / 실패 시 AP 폴백
 *  - 네트워크 스캔(JSON), 이벤트 로그, 연결 상태 조회
 *  - cfg_wifi_021.json의 ST_A10_WifiConfig 기반
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
#include <ArduinoJson.h>        // v7: JsonDocument만 사용
#include <WiFi.h>
#include <WiFiMulti.h>

#include <freertos/FreeRTOS.h>
#include <esp_task_wdt.h>

#include "A10_Const_012.h"
#include "D10_Logger_011.h"

class CL_M10_WiFiManager {
public:
	// --------------------------------------------------
	// 정적 상태
	// --------------------------------------------------
	static bool        s_staConnected;       ///< STA 연결 여부 캐시
	static wl_status_t s_lastStaStatus;      ///< 마지막 STA 상태 코드

public:
	// ==================================================
	// 이벤트 핸들러 등록(1회)
	// ==================================================
	/**
	 * @brief Wi-Fi 이벤트 핸들러를 1회만 연결
	 */
	static void M10_attachWiFiEvents() {
		static bool v_attached = false;
		if (v_attached) return;

		WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t) {
			CL_D10_Logger::log(EN_L10_LOG_INFO, "WiFi STA start");
		}, ARDUINO_EVENT_WIFI_STA_START);

		WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t) {
			CL_D10_Logger::log(EN_L10_LOG_INFO, "WiFi STA got IP: %s", WiFi.localIP().toString().c_str());
			s_staConnected  = true;
			s_lastStaStatus = WL_CONNECTED;
		}, ARDUINO_EVENT_WIFI_STA_GOT_IP);

		WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t) {
			CL_D10_Logger::log(EN_L10_LOG_WARN, "WiFi STA disconnected");
			s_staConnected  = false;
			s_lastStaStatus = WL_DISCONNECTED;
		}, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

		v_attached = true;
	}

	// ==================================================
	// 초기화/모드 적용 (AP/STA/AP+STA)
	// ==================================================
	/**
	 * @brief Wi-Fi 초기화. ST_A10_WifiConfig에 맞춰 모드 적용.
	 * @param p_cfg       Wi-Fi 설정 구조체
	 * @param p_multi     WiFiMulti 인스턴스 (STA 후보 등록용)
	 * @param p_apChannel AP 채널 (기본 1)
	 * @param p_staMaxTries STA 접속 최대 시도 횟수 (기본 15)
	 * @return true: 성공(최소 한 모드 기동), false: 실패
	 */
	static bool M10_init(const ST_A10_WifiConfig& p_cfg,
	                     WiFiMulti& p_multi,
	                     uint8_t p_apChannel = 1,
	                     uint8_t p_staMaxTries = 15) {
		M10_attachWiFiEvents();

		WiFi.persistent(false);
		WiFi.setAutoReconnect(true);
		WiFi.setSleep(false);

		// 호스트네임 자동 생성
		char v_hostname[32];
		snprintf(v_hostname, sizeof(v_hostname), "NatureWind-%04X", (uint16_t)(esp_random() & 0xFFFF));
		WiFi.setHostname(v_hostname);

		switch (p_cfg.mode) {
			case EN_A10_WIFI_MODE_AP: {
				WiFi.mode(WIFI_AP);
				return M10_startAP(p_cfg, p_apChannel);
			}
			case EN_A10_WIFI_MODE_STA: {
				WiFi.mode(WIFI_STA);
				bool v_ok = M10_startSTA(p_cfg, p_multi, p_staMaxTries);
				if (!v_ok) {
					CL_D10_Logger::log(EN_L10_LOG_WARN, "STA failed → fallback AP");
					WiFi.mode(WIFI_AP);
					return M10_startAP(p_cfg, p_apChannel);
				}
				return true;
			}
			case EN_A10_WIFI_MODE_AP_STA:
			default: {
				WiFi.mode(WIFI_AP_STA);
				M10_startAP(p_cfg, p_apChannel);
				return M10_startSTA(p_cfg, p_multi, p_staMaxTries);
			}
		}
	}

	// ==================================================
	// AP 기동
	// ==================================================
	/**
	 * @brief AP 시작
	 * @param p_cfg       Wi-Fi 설정
	 * @param p_channel   AP 채널
	 */
	static bool M10_startAP(const ST_A10_WifiConfig& p_cfg, uint8_t p_channel) {
		char v_pass[G_A10_WIFI_PWD_LEN + 1];
		strlcpy(v_pass, p_cfg.ap.password, sizeof(v_pass));

		// 8자 미만 비번 방어: 임시 비번 생성
		if (strlen(v_pass) < 8) {
			uint32_t v_r = esp_random();
			snprintf(v_pass, sizeof(v_pass), "ap_%08X", (unsigned int)v_r);
			CL_D10_Logger::log(EN_L10_LOG_WARN, "AP password too short (<8). Using temp: %s", v_pass);
		}

		WiFi.softAPdisconnect(true);
		WiFi.disconnect(true, true);

		bool v_ok = WiFi.softAP(p_cfg.ap.ssid, v_pass, p_channel, false, 4 /*maxconn*/);
		if (v_ok) {
			CL_D10_Logger::log(EN_L10_LOG_INFO, "AP started: %s (%s)",
			                   p_cfg.ap.ssid, WiFi.softAPIP().toString().c_str());
		} else {
			CL_D10_Logger::log(EN_L10_LOG_ERROR, "AP start failed!");
		}
		return v_ok;
	}

	// ==================================================
	// STA 기동 (WiFiMulti)
	// ==================================================
	/**
	 * @brief STA 시작 (WiFiMulti 기반 후보 등록)
	 * @param p_cfg        Wi-Fi 설정
	 * @param p_multi      WiFiMulti
	 * @param p_maxTries   최대 시도 횟수
	 */
	static bool M10_startSTA(const ST_A10_WifiConfig& p_cfg, WiFiMulti& p_multi, uint8_t p_maxTries) {
		s_staConnected  = false;
		s_lastStaStatus = WL_IDLE_STATUS;

		if (p_cfg.sta_count == 0) {
			CL_D10_Logger::log(EN_L10_LOG_WARN, "No STA network configured");
			return false;
		}

		for (uint8_t v_i = 0; v_i < p_cfg.sta_count; v_i++) {
			p_multi.addAP(p_cfg.sta[v_i].ssid, p_cfg.sta[v_i].pass);
			CL_D10_Logger::log(EN_L10_LOG_INFO, "STA candidate: %s", p_cfg.sta[v_i].ssid);
		}

		CL_D10_Logger::log(EN_L10_LOG_INFO, "Connecting STA...");
		uint8_t  v_try  = 0;
		uint32_t v_wait = 500;

		while (WiFi.status() != WL_CONNECTED && v_try < p_maxTries) {
			if (p_multi.run(2500) == WL_CONNECTED) break;
			v_try++;
			delay(v_wait);
			v_wait = (v_wait < 4000) ? (v_wait * 2) : 4000;
			Serial.print(".");
		}

		if (WiFi.status() == WL_CONNECTED) {
			s_staConnected  = true;
			s_lastStaStatus = WL_CONNECTED;
			CL_D10_Logger::log(EN_L10_LOG_INFO, "STA connected: %s (%s)",
			                   WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
			return true;
		}
		CL_D10_Logger::log(EN_L10_LOG_WARN, "STA connect failed");
		return false;
	}

	// ==================================================
	// 네트워크 스캔(JSON)
	// ==================================================
	/**
	 * @brief 주변 네트워크 스캔 결과를 JSON 문자열로 반환
	 * @param p_async true면 비동기 스캔 트리거만 하고 "[]" 반환
	 */
	static String M10_scanNetworksJson(bool p_async = false) {
		// Task WDT 임시 해제/재등록(ESP32-S3 일부 보드의 스캔 중 WDT 이슈 회피)
		TaskHandle_t v_task = xTaskGetCurrentTaskHandle();
		esp_task_wdt_delete(v_task);

		int v_found = WiFi.scanNetworks(p_async, true /*show hidden*/);

		esp_task_wdt_add(v_task);

		if (p_async) return F("[]");

		JsonDocument v_doc;
		JsonArray v_arr = v_doc.to<JsonArray>();

		for (int v_i = 0; v_i < v_found; v_i++) {
			JsonObject v_o = v_arr.add<JsonObject>();
			v_o["ssid"]  = WiFi.SSID(v_i);
			v_o["rssi"]  = WiFi.RSSI(v_i);
			v_o["chan"]  = WiFi.channel(v_i);
			v_o["bssid"] = WiFi.BSSIDstr(v_i);
			v_o["enc"]   = _encTypeToString(WiFi.encryptionType(v_i));
		}

		String v_out;
		serializeJson(v_doc, v_out);
		WiFi.scanDelete();
		return v_out;
	}

	// ==================================================
	// 상태/정보
	// ==================================================
	/** @brief STA 연결 여부 */
	static bool M10_isStaConnected() {
		return s_staConnected && WiFi.status() == WL_CONNECTED;
	}

	/** @brief STA 상태 문자열 */
	static const char* M10_getStaStatusString() {
		wl_status_t v_s = (wl_status_t)WiFi.status();
		switch (v_s) {
			case WL_CONNECTED:       return "CONNECTED";
			case WL_NO_SSID_AVAIL:   return "NO_SSID";
			case WL_CONNECT_FAILED:  return "FAILED";
			case WL_IDLE_STATUS:     return "IDLE";
			case WL_DISCONNECTED:    return "DISCONNECTED";
			default:                 return "UNKNOWN";
		}
	}

	/** @brief AP 정보(JSON) */
	static String M10_getApInfoJson() {
		JsonDocument v_doc;
		v_doc["ssid"] = WiFi.softAPSSID();
		v_doc["ip"]   = WiFi.softAPIP().toString();
		v_doc["mac"]  = WiFi.softAPmacAddress();
		String v_out;
		serializeJson(v_doc, v_out);
		return v_out;
	}

	// ==================================================
	// 제어 유틸
	// ==================================================
	/** @brief AP/STA 모두 끊기 */
	static void M10_disconnectAll() {
		WiFi.softAPdisconnect(true);
		WiFi.disconnect(true, true);
		s_staConnected  = false;
		s_lastStaStatus = WL_DISCONNECTED;
	}

	/** @brief AP 중지 */
	static void M10_stopAP() {
		WiFi.softAPdisconnect(true);
	}

	/** @brief STA 중지 */
	static void M10_stopSTA() {
		WiFi.disconnect(true, true);
		s_staConnected  = false;
		s_lastStaStatus = WL_DISCONNECTED;
	}

private:
	// --------------------------------------------------
	// 헬퍼
	// --------------------------------------------------
	static const char* _encTypeToString(wifi_auth_mode_t p_mode) {
		switch (p_mode) {
			case WIFI_AUTH_OPEN:           return "OPEN";
			case WIFI_AUTH_WEP:            return "WEP";
			case WIFI_AUTH_WPA_PSK:        return "WPA_PSK";
			case WIFI_AUTH_WPA2_PSK:       return "WPA2_PSK";
			case WIFI_AUTH_WPA_WPA2_PSK:   return "WPA_WPA2_PSK";
			case WIFI_AUTH_WPA2_ENTERPRISE:return "WPA2_ENT";
			case WIFI_AUTH_WPA3_PSK:       return "WPA3_PSK";
			case WIFI_AUTH_WPA2_WPA3_PSK:  return "WPA2_WPA3_PSK";
			case WIFI_AUTH_WAPI_PSK:       return "WAPI_PSK";
			default:                       return "UNKNOWN";
		}
	}
};

// ------------------------
// 정적 멤버 정의
// ------------------------
bool        CL_M10_WiFiManager::s_staConnected  = false;
wl_status_t CL_M10_WiFiManager::s_lastStaStatus = WL_IDLE_STATUS;
