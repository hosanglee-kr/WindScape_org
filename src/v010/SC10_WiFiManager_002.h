
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

			int				v_tries 	= 0;
			int 			v_maxTries 	= 15;
			unsigned long 	v_lastTick 	= millis();

			while (p_multi.run() != WL_CONNECTED && v_tries < v_maxTries) {
				if (millis() - v_lastTick >= 1000) {
					v_tries++;
					v_lastTick = millis();
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
		int			 v_scanedNetworks_count = WiFi.scanNetworks();
		JsonDocument v_doc;
		JsonArray	 v_jsonArr_Nets = v_doc.to<JsonArray>();
		for (int i = 0; i < v_scanedNetworks_count; i++) {
			JsonObject v_jsonObj_net = v_jsonArr_Nets.add<JsonObject>();
			v_jsonObj_net["ssid"]	 = WiFi.SSID(i);
			v_jsonObj_net["rssi"]	 = WiFi.RSSI(i);
			v_jsonObj_net["enc"]	 = (int)WiFi.encryptionType(i);
			v_jsonObj_net["bssid"]	 = WiFi.BSSIDstr(i);
			v_jsonObj_net["chan"]	 = WiFi.channel(i);
		}
		String v_scan_networks;
		serializeJson(v_doc, v_scan_networks);
		return v_scan_networks;
	}
};
