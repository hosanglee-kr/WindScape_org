// ------------------------------------------------------
// WF10_WiFiManager_024.cpp
// ------------------------------------------------------

#include "WF10_WiFiManager_024.h"

// g_A10_config_root extern (ConfigManager에서 정의)
extern ST_A10_ConfigRoot_t g_A10_config_root;

// --------------------------------------------------
// Static Members Definition (단 하나의 .cpp 파일에만 정의)
// --------------------------------------------------
bool				CL_WF10_WiFiManager::s_staConnected		 = false;
wl_status_t			CL_WF10_WiFiManager::s_lastStaStatus	 = WL_IDLE_STATUS;
bool				CL_WF10_WiFiManager::s_timeSynced		 = false;
uint32_t			CL_WF10_WiFiManager::s_lastSyncMs		 = 0;
uint8_t				CL_WF10_WiFiManager::s_reconnectAttempts = 0;
SemaphoreHandle_t	CL_WF10_WiFiManager::s_wifiMutex		 = nullptr; // Mutex는 init()에서 생성

// --------------------------------------------------
// applyConfig 함수 구현 (init 재사용)
// --------------------------------------------------
bool CL_WF10_WiFiManager::applyConfig(const ST_A10_WifiConfig& p_cfg) {
	CL_D10_Logger::log(EN_L10_LOG_INFO, "[WiFi] Applying new configuration...");

	// 1. 현재 Wi-Fi 연결/AP를 모두 끊습니다.
	WiFi.disconnect(true);
	WiFi.softAPdisconnect(true);

	// 2. WiFiMulti 준비
	WiFiMulti v_multi;

	// 3. system config 존재 여부 확인
	if (!g_A10_config_root.system) {
		CL_D10_Logger::log(
			EN_L10_LOG_ERROR,
			"[WiFi] applyConfig: system config is null. "
			"Using default time interval (6h) without full system integration."
		);

		// 임시 기본 system time 구성 (TZ/ntp는 빈 값)
		ST_A10_SystemConfig v_sys;
		memset(&v_sys, 0, sizeof(v_sys));
		v_sys.time.sync_interval_min = 360; // 6시간

		bool v_ok = init(p_cfg, v_sys, v_multi, 1, 15, true);
		return v_ok;
	}

	// 4. 기존 init() 로직 재사용 (AP/STA + NTP 동기화까지 포함)
	bool v_ok = init(p_cfg, *g_A10_config_root.system, v_multi, 1, 15, true);

	CL_D10_Logger::log(EN_L10_LOG_INFO,
					   "[WiFi] Configuration applied (ok=%d, mode=%d)",
					   (int)v_ok,
					   (int)p_cfg.wifiMode);
	return v_ok;
}

// --------------------------------------------------
// WF10_applyTimeConfigFromSystem 구현
//  - /api/system/time/set 등에서 호출
//  - system.time 설정(TZ, NTP 서버, sync_interval_min)을 런타임에 반영
// --------------------------------------------------
void WF10_applyTimeConfigFromSystem(const ST_A10_SystemConfig& p_cfg) {
	// 1. 로그: 적용할 설정 요약
	CL_D10_Logger::log(
		EN_L10_LOG_INFO,
		"[T10] Apply time config: ntp=%s, tz=%s, interval=%u min",
		p_cfg.time.ntp_server,
		p_cfg.time.timezone,
		(unsigned int)p_cfg.time.sync_interval_min
	);

	// 2. TZ 및 NTP 서버 환경 설정
	setenv("TZ", p_cfg.time.timezone, 1);
	tzset();
	configTime(0, 0, p_cfg.time.ntp_server);

	// 3. (선택적) 즉시 동기화 시도
	uint32_t v_start = millis();
	bool	 v_ok	   = false;
	while (millis() - v_start < 5000) { // 최대 5초 대기
		time_t v_nowTime = time(nullptr);
		if (v_nowTime > 1700000000) { // 2023년 11월 15일 이후
			v_ok = true;
			break;
		}
		delay(250);
	}

	if (v_ok) {
		CL_D10_Logger::log(EN_L10_LOG_INFO, "[T10] Time sync OK after config apply");
	} else {
		CL_D10_Logger::log(
			EN_L10_LOG_WARN,
			"[T10] Time sync not yet complete after config apply (non-fatal)"
		);
	}

	// 4. Wi-Fi가 연결되어 있고, sync_interval_min이 설정되어 있다면
	//    WiFiManager의 주기적 동기화 로직도 업데이트 (옵션)
	if (g_A10_config_root.wifi) {
		uint32_t v_interval_ms =
			(uint32_t)p_cfg.time.sync_interval_min * 60000UL;
		if (v_interval_ms == 0) {
			v_interval_ms = 21600000UL; // 6시간 기본값
		}

		// STA 연결 상태에서 주기 동기화 트리거
		CL_WF10_WiFiManager::syncTimeIfNeeded(
			*g_A10_config_root.wifi,
			p_cfg,
			v_interval_ms
		);
	}
}
