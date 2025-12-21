#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : WF10_WiFiManager_030.h
 * 모듈약어 : WF10
 * 모듈명 : Smart Nature Wind Wi-Fi Manager + NTP Sync (v024)
 * ------------------------------------------------------
 * 기능 요약:
 * - Wi-Fi AP/STA/AP+STA 모드 자동 연결 및 폴백 지원
 * - STA 우선 연결 / 실패 시 Soft AP 백업 모드 전환
 * - Disconnect 이벤트 기반 자동 복구 (delay 제거 및 횟수 제한 적용)
 * - DHCP 재할당 지연 대응 및 SoftAP 고정 IP 설정
 * - WiFiMulti 기반 다중 네트워크 연결 및 재시도 로직
 * - DNS 서버 확인 및 NTP 시간 주기 동기화(구성값 sync_interval_min 활용)
 * - SoftAP DHCP 서버 활성/비활성 제어 옵션
 * - Wi-Fi 상태 및 스캔 JSON 출력 (hostname, reconnect 횟수 포함)
 * - 공유 자원 보호를 위한 Mutex 적용
 * - system.time 설정 적용 유틸리티: WF10_applyTimeConfigFromSystem()
 * ------------------------------------------------------
 * [구현 규칙]
 * - ArduinoJson v7.x.x 사용 (v6 이하 금지)
 * - JsonDocument 단일 타입만 사용
 * - createNestedArray/Object/containsKey 사용 금지
 * - memset + strlcpy 기반 안전 초기화
 * - 주석/필드명은 JSON 구조와 동일하게 유지
 * ------------------------------------------------------
 * [코드 네이밍 규칙]
 * - 전역 함수      : 모듈약어 제거 (※ 예외: 호환성을 위해 WF10_ 접두 함수 1개 존재)
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
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <lwip/dns.h>
#include <time.h>

#include "A20_Const_020.h"
#include "C10_Config_030.h"	 // ST_A20_WifiConfig, ST_A20_SystemConfig, ST_A20_ConfigRoot_t
#include "D10_Logger_020.h"

// Mutex 보호 매크로 정의
#define WF10_MUTEX_ACQUIRE() xSemaphoreTake(CL_WF10_WiFiManager::s_wifiMutex, portMAX_DELAY)
#define WF10_MUTEX_RELEASE() xSemaphoreGive(CL_WF10_WiFiManager::s_wifiMutex)

// --------------------------------------------------
// Time 설정 적용 전역 함수 선언 (Web API / Config에서 호출)
// --------------------------------------------------
/**
 * @brief system.time 설정을 기준으로 TZ/NTP/주기 설정을 런타임에 반영
 * @param p_cfg 시스템 설정 구조체 (ST_A20_SystemConfig)
 */
void WF10_applyTimeConfigFromSystem(const ST_A20_SystemConfig& p_cfg);

class CL_WF10_WiFiManager {
  public:
	static bool				 s_staConnected;
	static wl_status_t		 s_lastStaStatus;
	static bool				 s_timeSynced;
	static uint32_t			 s_lastSyncMs;
	static uint8_t			 s_reconnectAttempts;
	static SemaphoreHandle_t s_wifiMutex;  // Mutex 포인터 (init()에서 생성)

  public:
	// --------------------------------------------------
	// Wi-Fi 설정 적용 함수 (Web API에서 호출)
	// --------------------------------------------------
	/**
	 * @brief 새로운 Wi-Fi 설정을 모듈에 적용하고 재초기화를 수행합니다.
	 * @param p_cfg 적용할 Wi-Fi 설정 구조체 참조
	 * @return 성공 여부
	 */
	static bool applyConfig(const ST_A20_WifiConfig& p_cfg);

	// --------------------------------------------------
	// 이벤트 등록
	// --------------------------------------------------
	static void attachWiFiEvents();

	// --------------------------------------------------
	// 초기화
	// --------------------------------------------------
	static bool init(const ST_A20_WifiConfig& p_cfg_wifi, const ST_A20_SystemConfig& p_cfg_system, WiFiMulti& p_multi, uint8_t p_apChannel = 1, uint8_t p_staMaxTries = 15, bool p_enableApDhcp = true);

	// --------------------------------------------------
	// AP 시작 (고정 IP + DHCP On/Off)
	// --------------------------------------------------
	static bool startAP(const ST_A20_WifiConfig& p_cfg_wifi, uint8_t p_channel, bool p_enableDhcp);

	// --------------------------------------------------
	// STA 시작
	// --------------------------------------------------
	static bool startSTA(const ST_A20_WifiConfig& p_cfg_wifi, WiFiMulti& p_multi, uint8_t p_maxTries);

	// --------------------------------------------------
	// NTP 동기화 (구성값 기반 주기)
	// --------------------------------------------------
	static void syncTimeIfNeeded(const ST_A20_WifiConfig& p_cfg_wifi, const ST_A20_SystemConfig& p_cfg_system, uint32_t p_interval_ms = 21600000);

	// --------------------------------------------------
	// 상태 JSON
	// --------------------------------------------------
	static void getWifiStateJson(JsonDocument& p_doc);

	// --------------------------------------------------
	// 스캔 JSON
	// --------------------------------------------------
	static void scanNetworksToJson(JsonDocument& p_doc);

	static bool isStaConnected();
	static const char* getStaStatusString();

  private:
	static const char* _encTypeToString(wifi_auth_mode_t p_mode);
};
