
// SC10_WiFiManager_002.h

#pragma once
#include <LittleFS.h>
#include <WiFi.h>
#include <WiFiMulti.h>

#include "SC10_Const_002.h"
#include "SC10_Logger_002.h"

class SC10_WiFiManager {
   public:
	// AP/STA 초기화 (STA 실패 시 AP로 폴백, 성공 시 AP 끄기)
	static void init(WindConfig &p_cfg, WiFiMulti &p_multi) {
		WiFi.mode(WIFI_AP_STA);
		// STA 우선 시도
		if (p_cfg.wifi_mode == G_SC10_WIFI_MODE_STA && p_cfg.sta_network_count > 0) {
			for (int i = 0; i < p_cfg.sta_network_count; i++) {
				p_multi.addAP(p_cfg.sta_networks[i].ssid, p_cfg.sta_networks[i].password);
				SC10_Logger::log(SC10_LOG_INFO, "WiFi STA added: %s", p_cfg.sta_networks[i].ssid);
			}
			int			  tries = 0, maxTries = 15;
			unsigned long lastTick = millis();
			while (p_multi.run() != WL_CONNECTED && tries < maxTries) {
				if (millis() - lastTick >= 1000) {
					tries++;
					lastTick = millis();
					Serial.print(".");
				}
				delay(1);  // yield
			}
			if (WiFi.status() == WL_CONNECTED) {
				SC10_Logger::log(SC10_LOG_INFO, "\nSTA Connected: %s, IP: %s", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
				WiFi.softAPdisconnect(true);  // AP 비활성화
				return;
			}
			SC10_Logger::log(SC10_LOG_WARN, "\nSTA connect failed. Fallback to AP");
		}

		// AP 기동
		WiFi.disconnect(true);
		WiFi.softAP(p_cfg.ap_ssid, p_cfg.ap_password);
		SC10_Logger::log(SC10_LOG_INFO, "AP started: %s, IP: %s", p_cfg.ap_ssid, WiFi.softAPIP().toString().c_str());
	}

	// 주변 네트워크 스캔
	static String scanNetworksJson() {
		int			 n = WiFi.scanNetworks();
		JsonDocument doc;
		JsonArray	 arr = doc.to<JsonArray>();
		for (int i = 0; i < n; i++) {
			JsonObject o = arr.add<JsonObject>();
			o["ssid"]	 = WiFi.SSID(i);
			o["rssi"]	 = WiFi.RSSI(i);
			o["enc"]	 = (int)WiFi.encryptionType(i);
			o["bssid"]	 = WiFi.BSSIDstr(i);
			o["chan"]	 = WiFi.channel(i);
		}
		String s;
		serializeJson(doc, s);
		return s;
	}
};
