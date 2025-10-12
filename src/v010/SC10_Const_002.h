// SC10_Const_002.h

#pragma once
/*
 * SC10_Const.h
 * - WindScape 프로젝트에서 공통적으로 사용되는 상수, 타입, 기본 구조 정의
 * - 다른 모듈(SC10_ConfigManager, SC10_WindScape, SC10_WebAPI 등)에서 참조
 * - 네이밍 규칙:
 *    - 전역 상수/매크로: G_SC10_ 로 시작
 *    - enum 타입: SC10_ 접두사
 *    - 구조체, typedef: SC10_ 접두사
 *    - 전역 변수: g_SC10_
 */

#include <Arduino.h>
#include <ArduinoJson.h>

// ====================================================================================
// 전역 파일 경로 상수
// ====================================================================================

// 전역 상수/경로/버전
namespace SC10_Const {
constexpr char FW_VERSION[]		= "SC10_FW_1.0.0";
constexpr char CONFIG_FILE[]	= "/json/config_010.json";
constexpr char BACKUP_FILE[]	= "/json/config_010.json.bak";
constexpr char HTML_FILE[]		= "/html/SC10_main_012.html";
constexpr char CSS_FILE[]		= "/html/SC10_main_012.css";
constexpr char JS_FILE[]		= "/html/SC10_main_012.js";
constexpr int  MAX_STA_NETWORKS = 5;
}  // namespace SC10_Const

/*
/// 기본 설정 JSON 파일 경로
#define G_SC10_CONFIG_FILE_PATH   "/json/config_003.json"
/// 설정 저장 시 기존 파일 백업본 경로 (.bak)
#define G_SC10_CONFIG_FILE_BAK    "/json/config_003.json.bak"
/// 웹 UI HTML (메인 페이지) 파일 경로
#define G_SC10_CONFIG_HTML_PATH   "/html/SC10_main_004.html"
/// 웹 UI JS (클라이언트 로직) 파일 경로
#define G_SC10_CONFIG_JS_PATH     "/html/SC10_main_003.js"
*/

// ====================================================================================
// Wi-Fi 설정 관련 상수
// ====================================================================================

/// 저장 가능한 STA 네트워크 최대 개수
#define G_SC10_MAX_STA_NETWORKS 5

/// Wi-Fi 모드 정의: AP 전용 모드
#define G_SC10_WIFI_MODE_AP		0
/// Wi-Fi 모드 정의: STA 전용 모드
#define G_SC10_WIFI_MODE_STA	1

// ====================================================================================
// 풍속 시뮬레이션 단계 (Phase) 정의
// ====================================================================================

/**
 * @brief 시뮬레이션 기상 단계 (바람 세기 구간)
 */
typedef enum {
	SC10_WEATHER_PHASE_CALM	  = 0,	///< 잔잔한 바람 (낮은 평균 풍속)
	SC10_WEATHER_PHASE_NORMAL = 1,	///< 일반적인 바람 (중간 풍속)
	SC10_WEATHER_PHASE_STRONG = 2,	///< 강한 바람 (높은 평균 풍속)
	SC10_WEATHER_PHASE_COUNT		///< Phase 개수 (배열 크기용)
} SC10_WindWeatherPhase_t;

/// Phase 이름 문자열 배열
static const char* G_SC10_WEATHER_PHASE_NAMES[] = {
	"Calm-잔잔한",
	"Normal-보통",
	"Strong-강한"};

// ====================================================================================
// 프리셋 모드 정의
// ====================================================================================

/**
 * @brief 바람 환경 프리셋 모드
 * - 특정 지형/환경에 맞는 기본 파라미터 세트 제공
 */
typedef enum {
	SC10_PRESET_OFF			  = 0,	///< 프리셋 사용 안 함
	SC10_PRESET_COUNTRY		  = 1,	///< 시골/들판 바람
	SC10_PRESET_MEDITERRANEAN = 2,	///< 지중해성 바람
	SC10_PRESET_OCEAN		  = 3,	///< 해양성 바람
	SC10_PRESET_MOUNTAIN	  = 4,	///< 산악 지역 바람
	SC10_PRESET_PLAINS		  = 5,	///< 대평원 바람
    // ===== 신규 프리셋 5종 =====
    SC10_PRESET_HARBOR_BREEZE,     // 항구 바람
    SC10_PRESET_FOREST_CANOPY,     // 숲 그늘 바람
    SC10_PRESET_URBAN_SUNSET,      // 도시 석양 바람
    SC10_PRESET_TROPICAL_RAIN,     // 열대 소나기 바람
    SC10_PRESET_DESERT_NIGHT,      // 사막의 밤 바람
  // ==========================
	SC10_PRESET_COUNT
} SC10_PresetMode_t;

/// 프리셋 모드 이름 문자열 배열
static const char* G_SC10_PRESET_MODE_NAMES[] = {
	"Off",
	"Countryside-들판",
	"Mediterranean-지중해",
	"Ocean-해양성",
	"Mountain-산악",
	"Plains-대평원",
    // 신규
    "Harbor Breeze-항구",
    "Forest Canopy-숲그늘",
    "Urban Sunset-도시석양",
    "Tropical Rain-열대소나기",
    "Desert Night-사막밤"
};

// ====================================================================================
// Wi-Fi STA Credential 구조체 정의
// ====================================================================================

/**
 * @brief Wi-Fi STA 네트워크 인증 정보
 * - SSID: 최대 32바이트
 * - Password: 최대 64바이트
 */
struct SC10_StaCredential {
	char ssid[32];
	char password[64];
};

// ====================================================================================
// WindConfig 구조체
// ====================================================================================

/**
 * @brief WindScape의 전체 구성(설정) 데이터 구조체
 * - JSON 직렬화/역직렬화 대상
 * - Wi-Fi, 하드웨어, 시뮬레이션 관련 설정 포함
 */
struct WindConfig {
	// --- Wi-Fi 설정 ---
	int				   wifi_mode = G_SC10_WIFI_MODE_STA;			///< 현재 Wi-Fi 모드
	SC10_StaCredential sta_networks[SC10_Const::MAX_STA_NETWORKS];	///< STA 네트워크 목록
	int				   sta_network_count = 0;						///< 실제 저장된 STA 네트워크 수
	char			   ap_ssid[32]		 = "SC10_Config_AP";		///< AP 모드 SSID
	char			   ap_password[64]	 = "newpassword";			///< AP 모드 Password

	// --- 하드웨어/시스템 상수 ---
	int fan_pwm_pin	   = 6; // 14;	 ///< 팬 PWM 출력 핀
	int fan_tach_pin   = 5;	 ///< 팬 회전 센서 입력 핀

	int pwm_frequency  = 25000;	 ///< PWM 주파수
	int pwm_channel	   = 0;		 ///< PWM 채널
	int pwm_resolution = 10;	 ///< PWM 해상도 (비트)

	int wind_sim_interval_ms	  = 250;   ///< 풍속 시뮬레이션 업데이트 주기 (ms)
	int gust_check_interval_ms	  = 500;   ///< 돌풍 체크 주기 (ms)
	int thermal_check_interval_ms = 2000;  ///< 열기포 체크 주기 (ms)

	// --- 시뮬레이션 설정 ---
	float wind_intensity			 = 100.0f;	///< 전체 풍속 강도 (퍼센트)
	float gust_frequency			 = 30.0f;	///< 돌풍 발생 빈도 (퍼센트)
	float wind_variability			 = 40.0f;	///< 풍속 변동성 (퍼센트)
	float fan_speed_limit			 = 80.0f;	///< 팬 속도 제한 (퍼센트)
	float minimum_fan_speed			 = 0.0f;	///< 최소 팬 속도 (퍼센트)
	float turbulence_length_scale	 = 30.0f;	///< 난류 길이 척도
	float turbulence_intensity_sigma = 0.3f;	///< 난류 강도 분산 값
	float thermal_bubble_strength	 = 1.8f;	///< 열기포 강도
	float thermal_bubble_radius		 = 15.0f;	///< 열기포 반경

	int preset_mode_index = SC10_PRESET_OCEAN;	///< 현재 선택된 프리셋 모드 인덱스
};

// 전역 설정 인스턴스 (헤더 온리: inline로 ODR 방지)
inline WindConfig g_SC10_config;

// 난수 유틸 (0~1)
inline float SC10_getRandom01() {
	return (float)esp_random() / (float)UINT32_MAX;
}
// 난수 유틸 (범위)
inline float SC10_randRange(float a, float b) {
	return a + SC10_getRandom01() * (b - a);
}
