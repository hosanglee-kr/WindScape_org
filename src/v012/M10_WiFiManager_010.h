#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : M10_WiFiManager_010.h
 * 모듈명 : Smart Nature Wind Wi-Fi Manager
 * ------------------------------------------------------
 * 기능 요약:
 *  - Wi-Fi AP/STA/AP+STA 모드 제어
 *  - STA 우선 연결 / 실패 시 AP 폴백
 *  - 네트워크 스캔, 이벤트 로그, 연결 상태 확인
 *  - FreeRTOS WDT 안전성 확보
 * ------------------------------------------------------
 * [코드 네이밍 규칙]
 * 		- 현재 파일 모듈약어    : M10
 * 		- 전역 상수,매크로      : G_모듈약어_ 접두사
 * 		- 전역 변수             : g_모듈약어_ 접두사
 * 		- 전역 함수             : 모듈약어_ 접두사
 * 		- type                  : T_모듈약어_ 접두사
 * 		- enum 상수             : EN_모듈약어_ 접두사
 * 		- 구조체                : ST_모듈약어_ 접두사
 * 		- 클래스명              : CL_모듈약어_ 접두사
 * 		- 클래스 private 멤버   : _ 접두사,
 * 		- 클래스 정적 멤버      : s_접두사
 * 		- 로컬 변수             : v_접두사
 * 		- 함수 인자             : p_접두사
 */

#include <WiFi.h>
#include <WiFiMulti.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <esp_task_wdt.h>

#include "A10_Const_010.h"
#include "D10_Logger_010.h"

class CL_M10_WiFiManager {
public:
    // ------------------------------------------------------
    // 정적 멤버
    // ------------------------------------------------------
    static bool s_staConnected;

    // ======================================================
    // Wi-Fi 이벤트 핸들러 등록
    // ======================================================
    static void attachWiFiEvents() {
        static bool v_attached = false;
        if (v_attached) return;

        WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t) {
            CL_D10_Logger::log(EN_L10_LOG_INFO, "WiFi STA start");
        }, ARDUINO_EVENT_WIFI_STA_START);

        WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t) {
            CL_D10_Logger::log(EN_L10_LOG_INFO, "WiFi STA got IP: %s", WiFi.localIP().toString().c_str());
            s_staConnected = true;
        }, ARDUINO_EVENT_WIFI_STA_GOT_IP);

        WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t) {
            CL_D10_Logger::log(EN_L10_LOG_WARN, "WiFi STA disconnected");
            s_staConnected = false;
        }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

        WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t) {
            CL_D10_Logger::log(EN_L10_LOG_INFO, "WiFi AP started: %s", WiFi.softAPIP().toString().c_str());
        }, ARDUINO_EVENT_WIFI_AP_START);

        v_attached = true;
    }

    // ======================================================
    // Wi-Fi 초기화 (AP/STA/AP+STA 모드 자동 선택)
    // ======================================================
    static bool init(const ST_A10_WifiConfig& p_cfg, WiFiMulti& p_multi, uint8_t p_apChannel = 1, uint8_t p_staMaxTries = 15) {
        attachWiFiEvents();

        WiFi.persistent(false);
        WiFi.setAutoReconnect(true);
        WiFi.setSleep(false);

        // 호스트네임 자동 생성
        char v_hostname[32];
        snprintf(v_hostname, sizeof(v_hostname), "NatureWind-%04X", (uint16_t)(esp_random() & 0xFFFF));
        WiFi.setHostname(v_hostname);

        bool v_ok = false;
        switch (p_cfg.mode) {
            case EN_A10_WIFI_MODE_AP:
                WiFi.mode(WIFI_AP);
                v_ok = startAP(p_cfg, p_apChannel);
                break;

            case EN_A10_WIFI_MODE_STA:
                WiFi.mode(WIFI_STA);
                v_ok = startSTA(p_cfg, p_multi, p_staMaxTries);
                if (!v_ok) {
                    CL_D10_Logger::log(EN_L10_LOG_WARN, "STA connect failed → Fallback to AP");
                    WiFi.mode(WIFI_AP);
                    v_ok = startAP(p_cfg, p_apChannel);
                }
                break;

            case EN_A10_WIFI_MODE_AP_STA:
            default:
                WiFi.mode(WIFI_AP_STA);
                startAP(p_cfg, p_apChannel);
                v_ok = startSTA(p_cfg, p_multi, p_staMaxTries);
                break;
        }
        return v_ok;
    }

    // ======================================================
    // AP 기동
    // ======================================================
    static bool startAP(const ST_A10_WifiConfig& p_cfg, uint8_t p_channel) {
        char v_pass[65];
        strlcpy(v_pass, p_cfg.ap.password, sizeof(v_pass));

        // 비밀번호 8자 미만 시 임시 비번 생성
        if (strlen(v_pass) < 8) {
            uint32_t v_r = esp_random();
            snprintf(v_pass, sizeof(v_pass), "ap_%08X", v_r);
            CL_D10_Logger::log(EN_L10_LOG_WARN, "AP password too short → temp: %s", v_pass);
        }

        WiFi.softAPdisconnect(true);
        WiFi.disconnect(true, true);

        bool v_ok = WiFi.softAP(p_cfg.ap.ssid, v_pass, p_channel, false, 4);
        if (v_ok) {
            CL_D10_Logger::log(EN_L10_LOG_INFO, "AP started: %s (%s)", p_cfg.ap.ssid, WiFi.softAPIP().toString().c_str());
        } else {
            CL_D10_Logger::log(EN_L10_LOG_ERROR, "AP start failed!");
        }
        return v_ok;
    }

    // ======================================================
    // STA 기동 (WiFiMulti 이용)
    // ======================================================
    static bool startSTA(const ST_A10_WifiConfig& p_cfg, WiFiMulti& p_multi, uint8_t p_maxTries) {
        s_staConnected = false;

        if (p_cfg.sta_count == 0) {
            CL_D10_Logger::log(EN_L10_LOG_WARN, "No STA networks configured");
            return false;
        }

        for (uint8_t i = 0; i < p_cfg.sta_count; i++) {
            p_multi.addAP(p_cfg.sta[i].ssid, p_cfg.sta[i].pass);
            CL_D10_Logger::log(EN_L10_LOG_INFO, "STA candidate: %s", p_cfg.sta[i].ssid);
        }

        CL_D10_Logger::log(EN_L10_LOG_INFO, "Connecting STA...");
        uint8_t v_try = 0;
        uint32_t v_wait = 500;
        while (WiFi.status() != WL_CONNECTED && v_try < p_maxTries) {
            if (p_multi.run(2000) == WL_CONNECTED) break;
            v_try++;
            delay(v_wait);
            v_wait = (v_wait < 4000) ? v_wait * 2 : 4000;
            Serial.print(".");
        }

        if (WiFi.status() == WL_CONNECTED) {
            s_staConnected = true;
            CL_D10_Logger::log(EN_L10_LOG_INFO, "STA connected: %s (%s)", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
            return true;
        } else {
            CL_D10_Logger::log(EN_L10_LOG_WARN, "STA connect failed");
            return false;
        }
    }

    // ======================================================
    // 네트워크 스캔 (JSON 반환)
    // ======================================================
    static String scanNetworksJson(bool p_async = false) {
        // Task Watchdog 임시 비활성화 (ESP32-S3 안정성)
        TaskHandle_t v_task = xTaskGetCurrentTaskHandle();
        esp_task_wdt_delete(v_task);

        int v_found = WiFi.scanNetworks(p_async, true);
        esp_task_wdt_add(v_task);

        if (p_async) return F("[]");

        JsonDocument v_doc;
        JsonArray v_arr = v_doc.to<JsonArray>();

        for (int i = 0; i < v_found; i++) {
            JsonObject o = v_arr.add<JsonObject>();
            o["ssid"]  = WiFi.SSID(i);
            o["rssi"]  = WiFi.RSSI(i);
            o["chan"]  = WiFi.channel(i);
            o["bssid"] = WiFi.BSSIDstr(i);
            o["enc"]   = encTypeToString(WiFi.encryptionType(i));
        }

        String v_out;
        serializeJson(v_doc, v_out);
        WiFi.scanDelete();
        return v_out;
    }

    // ======================================================
    // 연결 상태 / 정보
    // ======================================================
    static bool isStaConnected() {
        return s_staConnected && WiFi.status() == WL_CONNECTED;
    }

    static const char* getStaStatusString() {
        wl_status_t s = (wl_status_t)WiFi.status();
        switch (s) {
            case WL_CONNECTED:       return "CONNECTED";
            case WL_NO_SSID_AVAIL:   return "NO_SSID";
            case WL_CONNECT_FAILED:  return "FAILED";
            case WL_IDLE_STATUS:     return "IDLE";
            case WL_DISCONNECTED:    return "DISCONNECTED";
            default:                 return "UNKNOWN";
        }
    }

    static String getApInfoJson() {
        JsonDocument v_doc;
        v_doc["ssid"] = WiFi.softAPSSID();
        v_doc["ip"]   = WiFi.softAPIP().toString();
        v_doc["mac"]  = WiFi.softAPmacAddress();
        String v_out;
        serializeJson(v_doc, v_out);
        return v_out;
    }

    static String getStaInfoJson() {
        JsonDocument v_doc;
        v_doc["ssid"] = WiFi.SSID();
        v_doc["ip"]   = WiFi.localIP().toString();
        v_doc["mac"]  = WiFi.macAddress();
        v_doc["rssi"] = WiFi.RSSI();
        String v_out;
        serializeJson(v_doc, v_out);
        return v_out;
    }

private:
    // ======================================================
    // 암호화 타입 → 문자열 변환
    // ======================================================
    static const char* encTypeToString(wifi_auth_mode_t p_mode) {
        switch (p_mode) {
            case WIFI_AUTH_OPEN:          return "OPEN";
            case WIFI_AUTH_WEP:           return "WEP";
            case WIFI_AUTH_WPA_PSK:       return "WPA_PSK";
            case WIFI_AUTH_WPA2_PSK:      return "WPA2_PSK";
            case WIFI_AUTH_WPA_WPA2_PSK:  return "WPA_WPA2_PSK";
            case WIFI_AUTH_WPA2_ENTERPRISE:return "WPA2_ENT";
            case WIFI_AUTH_WPA3_PSK:      return "WPA3_PSK";
            case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2_WPA3_PSK";
            case WIFI_AUTH_WAPI_PSK:      return "WAPI_PSK";
            default:                      return "UNKNOWN";
        }
    }
};

// ---------------------------------------------------------
// 정적 멤버 정의
// ---------------------------------------------------------
bool CL_M10_WiFiManager::s_staConnected = false;
