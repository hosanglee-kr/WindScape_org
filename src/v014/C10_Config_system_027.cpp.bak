

/*
 * ------------------------------------------------------
 * 소스명 : C10_Config_system_026.cpp
 * 모듈 약어 : C10
 * 모듈명 : Smart Nature Wind Configuration Manager
 * ------------------------------------------------------
 * 기능 요약:
 *  - Smart Nature Wind 전체 설정(JSON 기반) 관리 매니저
 *  - 설정 파일 단위 분리 관리 (system / wifi / motion / schedules / userProfiles / windProfile)
 *  - 구조체 ↔ JSON 직렬화 및 역직렬화 (ArduinoJson v7 전용)
 *  - 파일 백업(.bak) / 복구 / 공장초기화(factoryResetFromDefault) 지원
 *  - PATCH 기반 부분 업데이트(patchConfigFromJson) 지원
 *  - Lazy-Load 하이브리드 구성 (필요 섹션만 동적 로드)
 *  - Wi-Fi 등 재초기화 판단 로직 확장 가능
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


#include "C10_Config_027.h"

// g_A10_config_root 전역 변수 사용을 위해 extern 선언
extern ST_A10_ConfigRoot_t g_A10_config_root; 
extern bool ioSaveJson(const char* p_path, const char* p_bak, const JsonDocument& p_doc);
extern bool ioLoadJson(const char* p_path, const char* p_bak, JsonDocument& p_doc);

// =====================================================
// 4. JSON Patch (System, Wifi, Motion) 구현
// (로직은 요청에 따라 이전 응답과 동일하게 유지)
// =====================================================

bool CL_C10_ConfigManager::patchSystemFromJson(ST_A10_SystemConfig& p_config,
								    const JsonDocument&	 p_patch) {
	    bool v_changed = false;

		// 💡 Mutex를 사용하여 쓰기 작업 보호
        if (xSemaphoreTake(s_configMutex, G_C10_MUTEX_TIMEOUT) != pdTRUE) {
            CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] patchSystem() Mutex timeout!");
            return false; // Mutex 획득 실패 시 실패 처리
        }
		
	    // JSON 패치 데이터의 최상위 "system" 객체를 찾음
	    JsonObjectConst j_sys = p_patch["system"];
	    // JSON 패치 데이터의 최상위 "security" 객체를 찾음
	    JsonObjectConst j_sec_root = p_patch["security"];
    
	    if (j_sys.isNull() && j_sec_root.isNull()) {
			xSemaphoreGive(s_configMutex);
		    return false; 
	    }
    
	    // 1. system.logging 객체 처리
	    if (!j_sys.isNull()) {
		    JsonObjectConst j_log = j_sys["logging"];
		    if (!j_log.isNull()) {
			    const char* v_lv = j_log["level"] | "";
			    // 0이 기본값인 경우 | 0을 사용하지 않고, is<int>()로 존재 여부 확인
			    if (j_log["max_entries"].is<uint16_t>()) {
					uint16_t v_max = j_log["max_entries"];
					if (v_max != p_config.system.logging.max_entries) {
						p_config.system.logging.max_entries = v_max;
						v_changed = true;
					}
				}
    
			    // level 필드 패치
			    if (strlen(v_lv) > 0 && strcmp(v_lv, p_config.system.logging.level) != 0) { 
				    strlcpy(p_config.system.logging.level, v_lv, sizeof(p_config.system.logging.level));
				    v_changed = true;
			    }
		    }
	    }
    
	    // 2. security 객체 처리
	    if (!j_sec_root.isNull()) {
		    const char* v_key = j_sec_root["api_key"] | "";
		    if (strlen(v_key) > 0 && strcmp(v_key, p_config.security.api_key) != 0) {
			    strlcpy(p_config.security.api_key, v_key, sizeof(p_config.security.api_key));
			    v_changed = true;
		    }
	    }
	    
	    // 3. 변경 사항이 있을 경우에만 Dirty Flag 설정
		if (v_changed) {
            _dirty_system = true;
            CL_D10_Logger::log(EN_L10_LOG_INFO, "[C10] System config patched (Memory Only). Dirty=true");
        }

		xSemaphoreGive(s_configMutex); 
		
        return v_changed;
}


bool CL_C10_ConfigManager::patchWifiFromJson(ST_A10_WifiConfig& p_config,
							      const JsonDocument&	 p_patch) {
	    bool v_changed = false;

		// 💡 Mutex를 사용하여 쓰기 작업 보호
        if (xSemaphoreTake(s_configMutex, G_C10_MUTEX_TIMEOUT) != pdTRUE) {
            CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] patchWifiFromJson() Mutex timeout!");
            return false; 
        }
	    
	    // 1. wifi 객체 접근
	    JsonObjectConst j_wifi = p_patch["wifi"];
	    if (j_wifi.isNull()) {
			xSemaphoreGive(s_configMutex);
		    return false;
	    }

		// wifiMode 처리
		if (j_wifi["wifiMode"].is<uint8_t>()) {
			uint8_t v_mode = j_wifi["wifiMode"];
			if (v_mode != p_config.wifiMode) {
				// 유효한 모드 범위(0, 1, 2) 확인
				if (v_mode >= EN_A10_WIFI_MODE_AP && v_mode <= EN_A10_WIFI_MODE_AP_STA) { 
					p_config.wifiMode = (EN_A10_WIFI_MODE_t)v_mode;
					v_changed = true;
				} else {
					CL_D10_Logger::log(EN_L10_LOG_WARN, "[C10] Invalid wifiMode value: %d", v_mode);
				}
			}
		}
    
	    // 2. ap 객체 처리
	    JsonObjectConst j_ap = j_wifi["ap"];
	    if (!j_ap.isNull()) {
		    // SSID 처리
		    const char* v_ssid = j_ap["ssid"] | "";
		    if (strlen(v_ssid) > 0 && strcmp(v_ssid, p_config.ap.ssid) != 0) {
			    strlcpy(p_config.ap.ssid, v_ssid, sizeof(p_config.ap.ssid));
			    v_changed = true;
		    }
		    
		    // Password 처리
		    const char* v_pwd = j_ap["password"] | "";
		    // 길이가 0이 아니거나, 기존 패스워드와 다른 경우에만 변경 (길이가 0이면 패스워드 미변경)
		    if (strlen(v_pwd) > 0 && strcmp(v_pwd, p_config.ap.password) != 0) {
			    strlcpy(p_config.ap.password, v_pwd, sizeof(p_config.ap.password));
			    v_changed = true;
		    }
	    }
	    
	    // 3. sta 배열 전체 덮어쓰기 (PUT 방식)
	    JsonArrayConst j_sta = j_wifi["sta"].as<JsonArrayConst>();
	    if (!j_sta.isNull()) {
			// 기존 목록 초기화
			p_config.sta_count = 0; 
			// memset(&p_config.sta, 0, sizeof(p_config.sta)); // strlcpy가 덮어쓰므로 불필요

		    for (JsonObjectConst v_js : j_sta) {
			    if (p_config.sta_count >= A10_Const::MAX_STA_NETWORKS)
				    break;
			    
				ST_A10_STANetwork_t& v_net = p_config.sta[p_config.sta_count];

			    strlcpy(v_net.ssid,
					    v_js["ssid"] | "",
					    sizeof(v_net.ssid));
			    strlcpy(v_net.pass,
					    v_js["pass"] | "",
					    sizeof(v_net.pass));
			    p_config.sta_count++;
		    }
		    v_changed = true;
		    CL_D10_Logger::log(EN_L10_LOG_DEBUG, "[C10] WiFi STA array fully replaced.");
	    }


	    // 4. 변경 사항이 있을 경우에만 Dirty Flag 설정
		if (v_changed) {
            _dirty_wifi = true;
            CL_D10_Logger::log(EN_L10_LOG_INFO, "[C10] WiFi config patched (Memory Only). Dirty=true");
        }

		xSemaphoreGive(s_configMutex); 
		
        return v_changed;
}


bool CL_C10_ConfigManager::patchMotionFromJson(ST_A10_MotionConfig& p_config,
								    const JsonDocument&	 p_patch) {
		// 💡 Mutex를 사용하여 쓰기 작업 보호
        if (xSemaphoreTake(s_configMutex, G_C10_MUTEX_TIMEOUT) != pdTRUE) {
            CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] patchMotionFromJson() Mutex timeout!");
            return false; 
        }
		
	    bool v_changed = false;
	    
	    JsonObjectConst j_motion = p_patch["motion"];
    
	    if (j_motion.isNull()) {
			xSemaphoreGive(s_configMutex);
		    return false; 
	    }
    
	    // 1. 최상위 enabled 필드
	    if (j_motion["enabled"].is<bool>() && 
		    j_motion["enabled"].as<bool>() != p_config.enabled) {
		    p_config.enabled = j_motion["enabled"];
		    v_changed = true;
	    }
    
	    // 2. pir 객체 처리
	    JsonObjectConst j_pir = j_motion["pir"];
	    if (!j_pir.isNull()) {
		    if (j_pir["enabled"].is<bool>() &&
			    j_pir["enabled"].as<bool>() != p_config.pir.enabled) {
			    p_config.pir.enabled = j_pir["enabled"];
			    v_changed = true;
		    }
		    if (j_pir["hold_sec"].is<uint16_t>()) { // 0이 유효할 수 있으므로 is<uint16_t>()로 존재 여부 확인
			    if (j_pir["hold_sec"].as<uint16_t>() != p_config.pir.hold_sec) {
					p_config.pir.hold_sec = j_pir["hold_sec"];
					v_changed = true;
				}
		    }
	    }
	    
	    // 3. ble 객체 및 중첩된 rssi 객체 처리
	    JsonObjectConst j_ble = j_motion["ble"];
	    if (!j_ble.isNull()) {
		    if (j_ble["enabled"].is<bool>() &&
			    j_ble["enabled"].as<bool>() != p_config.ble.enabled) {
			    p_config.ble.enabled = j_ble["enabled"];
			    v_changed = true;
		    }
		    
		    // rssi 객체
		    JsonObjectConst j_rssi = j_ble["rssi"];
		    if (!j_rssi.isNull()) {
			    if (j_rssi["on"].is<int8_t>() &&
				    j_rssi["on"].as<int8_t>() != p_config.ble.rssi.on) {
				    p_config.ble.rssi.on = j_rssi["on"];
				    v_changed = true;
			    }
			    if (j_rssi["off"].is<int8_t>() &&
				    j_rssi["off"].as<int8_t>() != p_config.ble.rssi.off) {
				    p_config.ble.rssi.off = j_rssi["off"];
				    v_changed = true;
			    }
			    if (j_rssi["avg_count"].is<uint8_t>() &&
				    j_rssi["avg_count"].as<uint8_t>() != p_config.ble.rssi.avg_count) {
				    p_config.ble.rssi.avg_count = j_rssi["avg_count"];
				    v_changed = true;
			    }
			    if (j_rssi["persist_count"].is<uint8_t>() &&
				    j_rssi["persist_count"].as<uint8_t>() != p_config.ble.rssi.persist_count) {
				    p_config.ble.rssi.persist_count = j_rssi["persist_count"];
				    v_changed = true;
			    }
			    if (j_rssi["exit_delay_sec"].is<uint16_t>()) { // 0이 유효할 수 있으므로 is<uint16_t>()로 존재 여부 확인
					if (j_rssi["exit_delay_sec"].as<uint16_t>() != p_config.ble.rssi.exit_delay_sec) {
						p_config.ble.rssi.exit_delay_sec = j_rssi["exit_delay_sec"];
						v_changed = true;
					}
			    }
		    }
    
		    // trusted_devices 배열 전체 덮어쓰기(PUT) 방식으로 처리합니다.
		    JsonArrayConst j_devices = j_ble["trusted_devices"].as<JsonArrayConst>();
		    if (!j_devices.isNull()) {
			    p_config.ble.trusted_count = 0; // 기존 목록 초기화
			    // memset(&p_config.ble.trusted_devices, 0, sizeof(p_config.ble.trusted_devices)); // strlcpy가 덮어쓰므로 불필요
			    for (JsonObjectConst j_dev : j_devices) {
				    if (p_config.ble.trusted_count >= A10_Const::MAX_BLE_DEVICES)
					    break;
				    
				    ST_A10_BLETrustedDevice& v_d = p_config.ble.trusted_devices[p_config.ble.trusted_count];
    
				    strlcpy(v_d.alias, j_dev["alias"] | "", sizeof(v_d.alias));
				    strlcpy(v_d.name, j_dev["name"] | "", sizeof(v_d.name));
				    strlcpy(v_d.mac, j_dev["mac"] | "", sizeof(v_d.mac));
				    strlcpy(v_d.manuf_prefix, j_dev["manuf_prefix"] | "", sizeof(v_d.manuf_prefix));
				    v_d.prefix_len = j_dev["prefix_len"] | 0;
				    v_d.enabled	   = j_dev["enabled"] | true;
					
					p_config.ble.trusted_count++;
			    }
			    v_changed = true;
			    CL_D10_Logger::log(EN_L10_LOG_DEBUG, "[C10] Motion Trusted Devices array fully replaced.");
		    }
	    }


	    // 4. 변경 사항이 있을 경우에만 Dirty Flag 설정
		if (v_changed) {
            _dirty_motion = true;
            CL_D10_Logger::log(EN_L10_LOG_INFO, "[C10] motion config patched (Memory Only). Dirty=true");
        }

		xSemaphoreGive(s_configMutex); 
		
        return v_changed;
}

// =====================================================
// 3-1. 목적물별 JSON Export 구현 (Wifi/Motion)
// =====================================================
void CL_C10_ConfigManager::toJson_Wifi(const ST_A10_WifiConfig& p,
							JsonDocument&			 d) {
		d["wifi"]["wifiMode"]		= p.wifiMode;
		d["wifi"]["wifiModeDesc"]	= p.wifiModeDesc;
		d["wifi"]["ap"]["ssid"]		= p.ap.ssid;
		d["wifi"]["ap"]["password"] = p.ap.password;

		for (uint8_t i = 0; i < p.sta_count; i++) {
			d["wifi"]["sta"][i]["ssid"] = p.sta[i].ssid;
			d["wifi"]["sta"][i]["pass"] = p.sta[i].pass;
		}
}

void CL_C10_ConfigManager::toJson_Motion(const ST_A10_MotionConfig& p,
							  JsonDocument&				 d) {
		d["motion"]["enabled"]		   = p.enabled;
		d["motion"]["pir"]["enabled"]  = p.pir.enabled;
		d["motion"]["pir"]["hold_sec"] = p.pir.hold_sec;

		d["motion"]["ble"]["enabled"]				 = p.ble.enabled;
		d["motion"]["ble"]["rssi"]["on"]			 = p.ble.rssi.on;
		d["motion"]["ble"]["rssi"]["off"]			 = p.ble.rssi.off;
		d["motion"]["ble"]["rssi"]["avg_count"]		 = p.ble.rssi.avg_count;
		d["motion"]["ble"]["rssi"]["persist_count"]	 = p.ble.rssi.persist_count;
		d["motion"]["ble"]["rssi"]["exit_delay_sec"] = p.ble.rssi.exit_delay_sec;

		for (uint8_t i = 0; i < p.ble.trusted_count; i++) {
			const ST_A10_BLETrustedDevice& v_d =
				p.ble.trusted_devices[i];
			JsonObject v_td =
				d["motion"]["ble"]["trusted_devices"][i];

			v_td["alias"]		 = v_d.alias;
			v_td["name"]		 = v_d.name;
			v_td["mac"]			 = v_d.mac;
			v_td["manuf_prefix"] = v_d.manuf_prefix;
			v_td["prefix_len"]	 = v_d.prefix_len;
			v_td["enabled"]		 = v_d.enabled;
		}
}

