// WF10_WiFiManager_024.cpp

#include "WF10_WiFiManager_024.h"

// --------------------------------------------------
// Static Members Definition (단 하나의 .cpp 파일에만 정의)
// --------------------------------------------------
bool				CL_WF10_WiFiManager::s_staConnected		 = false;
wl_status_t			CL_WF10_WiFiManager::s_lastStaStatus	 = WL_IDLE_STATUS;
bool				CL_WF10_WiFiManager::s_timeSynced		 = false;
uint32_t			CL_WF10_WiFiManager::s_lastSyncMs		 = 0;
uint8_t				CL_WF10_WiFiManager::s_reconnectAttempts = 0;
SemaphoreHandle_t	CL_WF10_WiFiManager::s_wifiMutex		 = xSemaphoreCreateMutex(); // 🚨 Mutex 생성

// --------------------------------------------------
// [오류 해결] applyConfig 함수 구현
// --------------------------------------------------
/**
 * @brief 새로운 Wi-Fi 설정을 모듈에 적용하고 재시작을 처리합니다.
 * * @param p_cfg 적용할 Wi-Fi 설정 구조체 참조
 * @return 성공 여부 (항상 true를 반환하지만, 실제 로직에 따라 변경될 수 있음)
 */
bool CL_WF10_WiFiManager::applyConfig(const ST_A10_WifiConfig& p_cfg) {
    CL_D10_Logger::log(EN_L10_LOG_INFO, "[WiFi] Applying new configuration...");
    
    // 1. 현재 Wi-Fi 연결/AP를 모두 끊습니다.
    WiFi.disconnect(true);
    WiFi.softAPdisconnect(true);
    
    // 2. 모드에 따라 Wi-Fi를 재설정합니다.
    WiFiMulti v_multi;
    
    // 이전에 전역 변수로 관리되던 g_A10_config_root.system도 필요하다고 가정
    // 하지만 applyConfig는 설정 구조체만 받으므로, init()을 다시 호출합니다.
    // NOTE: W10_Web_Routes_029.cpp에서 이 함수를 호출하기 때문에, 
    //       여기서는 단순히 Wi-Fi를 재시작하고, 
    //       시스템 Config가 필요하다면 그쪽에서 init()을 다시 호출해야 합니다. 
    //       (여기서는 Config만 적용하는 로직을 가정합니다.)

    WiFiMode_t v_mode;
    switch (p_cfg.wifiMode) {
        case 0: v_mode = WIFI_AP; break;
        case 1: v_mode = WIFI_STA; break;
        default: v_mode = WIFI_AP_STA; break;
    }

    WiFi.mode(v_mode);

    // 3. 재시작 로직 (ST_A10_SystemConfig가 없으므로 임시로 기본값 사용)
    // 일반적으로 설정 변경 후에는 전체 재초기화(init)를 수행해야 합니다.
    // 여기서는 init 함수를 호출하기 위한 ST_A10_SystemConfig가 없으므로 
    // 전체 시스템 리셋을 유도하거나, 호출부에서 init()을 다시 호출하도록 유도하는 것이 안전합니다.
    
    // 임시로, startAP/startSTA 로직을 직접 실행합니다. (ST_A10_SystemConfig 누락 문제)
    
    if (v_mode == WIFI_AP || v_mode == WIFI_AP_STA) {
        // [TODO] p_apChannel, p_enableApDhcp 값은 이 함수만으로는 알 수 없으므로 
        //        기본값(채널 1, DHCP 활성)을 사용하거나 저장된 설정에서 가져와야 합니다.
        startAP(p_cfg, 1, true); 
    }

    if (v_mode == WIFI_STA || v_mode == WIFI_AP_STA) {
        // [TODO] p_staMaxTries 값은 이 함수만으로는 알 수 없습니다.
        startSTA(p_cfg, v_multi, 15);
        
        // NTP 동기화는 ST_A10_SystemConfig가 필요하므로 여기서는 생략
    }


    CL_D10_Logger::log(EN_L10_LOG_INFO, "[WiFi] Configuration applied. Mode: %d", (int)v_mode);

    // [개선 제안] 실제 제품에서는 설정 변경 후 ESP.restart()를 호출하여
    //            시스템 전체를 깨끗하게 다시 시작하는 것이 가장 안전합니다.
    // ESP.restart(); 
    
    return true; 
}



