/*
 * ------------------------------------------------------
 * 소스명 : W10_Web_Const_050.h
 * 모듈 약어 : W10
 * 모듈명 : Smart Nature Wind Web API Manager (Constants)
 * ------------------------------------------------------
 * 기능 요약:
 * - REST API(v001) 및 WebSocket 엔드포인트 경로 정의
 * - 파일 시스템 경로 및 UI 설정 파일 경로 정의
 * ------------------------------------------------------
 * [구현 규칙]
 * - constexpr const char* 를 통한 컴파일 타임 문자열 결합 (Flash 메모리 절약)
 * - HTTP_API_ / WS_API_ 접두사를 통한 통신 프로토콜 구분
 * ------------------------------------------------------
 */

#pragma once

namespace W10_Const {

// --------------------------------------------------
// 0. REST API / WebSocket 기본 Prefix
// --------------------------------------------------
#define HTTP_API_BASE "/api/v001"
#define WS_API_BASE	  "/ws"

// --------------------------------------------------
// 1. 시스템 정보 및 상태 조회 (System & Status)
// --------------------------------------------------
constexpr const char* HTTP_API_VERSION			   = HTTP_API_BASE "/version";
constexpr const char* HTTP_API_STATE			   = HTTP_API_BASE "/state";
constexpr const char* HTTP_API_SYSTEM			   = HTTP_API_BASE "/system";

// constexpr const char* HTTP_API_WIFI           = HTTP_API_BASE "/wifi";
constexpr const char* HTTP_API_WIFI_SCAN		   = HTTP_API_BASE "/wifi/scan";
constexpr const char* HTTP_API_WIFI_CONFIG		   = HTTP_API_BASE "/wifi/config";

constexpr const char* HTTP_API_DIAG				   = HTTP_API_BASE "/diag";
constexpr const char* HTTP_API_AUTH_TEST		   = HTTP_API_BASE "/auth/test";

constexpr const char* HTTP_API_TIME_SET			   = HTTP_API_BASE "/system/time/set";
constexpr const char* HTTP_API_FW_CHECK			   = HTTP_API_BASE "/system/firmware/check";

// --------------------------------------------------
// 2. 설정 및 실시간 제어 (Control & Simulation)
// --------------------------------------------------
constexpr const char* HTTP_API_MOTION			   = HTTP_API_BASE "/motion";
constexpr const char* HTTP_API_SIMULATION		   = HTTP_API_BASE "/simulation";
constexpr const char* HTTP_API_SIM_STATE		   = HTTP_API_BASE "/sim/state";
constexpr const char* HTTP_API_CONTROL_SUMMARY	   = HTTP_API_BASE "/control/summary";

// --------------------------------------------------
// 3. 설정 관리 (Configuration Life Cycle)
// --------------------------------------------------
constexpr const char* HTTP_API_CONFIG_SAVE		   = HTTP_API_BASE "/config/save";
constexpr const char* HTTP_API_CONFIG_DIRTY		   = HTTP_API_BASE "/config/dirty";
constexpr const char* HTTP_API_CONFIG_INIT		   = HTTP_API_BASE "/config/init";
constexpr const char* HTTP_API_RELOAD			   = HTTP_API_BASE "/reload";

// --------------------------------------------------
// 4. 리소스 CRUD (Profile, Schedule, User)
// --------------------------------------------------
constexpr const char* HTTP_API_WIND_PROFILE		   = HTTP_API_BASE "/windProfile";
constexpr const char* HTTP_API_SCHEDULES		   = HTTP_API_BASE "/schedules";
constexpr const char* HTTP_API_USER_PROFILES	   = HTTP_API_BASE "/user_profiles";
constexpr const char* HTTP_API_USER_PROFILES_PATCH = HTTP_API_BASE "/user_profiles/patch";

// --------------------------------------------------
// 5. 하드웨어 직접 제어 (Direct HW Control)
// --------------------------------------------------
constexpr const char* HTTP_API_CTL_REBOOT		   = HTTP_API_BASE "/control/reboot";
constexpr const char* HTTP_API_CTL_FACTORY		   = HTTP_API_BASE "/control/factoryReset";
constexpr const char* HTTP_API_CTL_PROF_SEL		   = HTTP_API_BASE "/control/profile/select";
constexpr const char* HTTP_API_CTL_PROF_STOP	   = HTTP_API_BASE "/control/profile/stop";
constexpr const char* HTTP_API_CTL_OVR_FIXED	   = HTTP_API_BASE "/control/override/fixed";
constexpr const char* HTTP_API_CTL_OVR_PRESET	   = HTTP_API_BASE "/control/override/preset";
constexpr const char* HTTP_API_CTL_OVR_CLEAR	   = HTTP_API_BASE "/control/override/clear";

// --------------------------------------------------
// 6. 데이터 피드 및 메트릭스 (Feed & Metrics)
// --------------------------------------------------
constexpr const char* HTTP_API_FEED_PIR			   = HTTP_API_BASE "/motion/pir/feed";
constexpr const char* HTTP_API_FEED_BLE			   = HTTP_API_BASE "/motion/ble/feed";
constexpr const char* HTTP_API_METRICS			   = HTTP_API_BASE "/metrics";
constexpr const char* HTTP_API_LOGS				   = HTTP_API_BASE "/logs";

// --------------------------------------------------
// 7. 파일 및 업데이트 (Upload & OTA)
// --------------------------------------------------
constexpr const char* HTTP_API_FILE_UPLOAD		   = HTTP_API_BASE "/fileUpload";
constexpr const char* HTTP_API_FW_UPDATE		   = HTTP_API_BASE "/fwUpdate";

// --------------------------------------------------
// 8. 웹 인터페이스 및 메뉴 (UI)
// --------------------------------------------------
constexpr const char* HTTP_API_MENU				   = HTTP_API_BASE "/menu";

// --------------------------------------------------
// 9. WebSocket 엔드포인트
// --------------------------------------------------
constexpr const char* WS_API_LOG				   = WS_API_BASE "/log";
constexpr const char* WS_API_STATE				   = WS_API_BASE "/state";
constexpr const char* WS_API_CHART				   = WS_API_BASE "/chart";
constexpr const char* WS_API_METRICS			   = WS_API_BASE "/metrics";

// --------------------------------------------------
// 10. 파일 시스템 경로 (LittleFS Path)
// --------------------------------------------------
constexpr const char* PATH_STATIC_HTML			   = "/html_v2";
constexpr const char* PATH_STATIC_JSON			   = "/json";

// constexpr const char* FILE_PAGES_JSON         = "/json/cfg_pages_032.json";

}  // namespace W10_Const
