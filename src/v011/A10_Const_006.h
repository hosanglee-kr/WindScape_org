

// A10_Const_006.h

#pragma once
/*
 * SC10_Const_006.h
 * - WindScape 프로젝트에서 공통적으로 사용되는 상수, 타입, 기본 구조 정의
 * - 네이밍 규칙:
 *    - 네이밍 규칙의 모듈약어 : A10
 *    - 전역 상수/매크로: G_모듈약어_ 접두사
 *    - 전역 변수: g_A10_ 접두사
 *    - 로컬 변수 :v_ 접두사
 *    - 함수 인자 : p_ 접두사
 *    - type은 T_모듈약어_ 접두사
 *    - enum 상수 : EN_모듈약어_ 접두사
 *    - 구조체 : ST_모듈약어_ 접두사
 *    - 클래스 : CL_모듈약어_ 접두사
 *    - 전역함수 : 모듈약어_ 접두사
 */

#include <Arduino.h>
#include <ArduinoJson.h>


#define G_A10_API_KEY_USE 1
// ====================================================================================
// 전역 파일 경로 상수
// ====================================================================================

// 전역 상수/경로/버전
namespace A10_Const {
    constexpr char FW_VERSION[]				= "SC10_FW_1.0.0";

    constexpr char CONFIG_JSON_FILE[]			= "/json/config_013.json";
    constexpr char CONFIG_JSON_FILE_BACKUP[]	= "/json/config_013.json.bak";

    constexpr char HTML_FILE[]					= "/html/SC10_main_015.html";
    constexpr char CSS_FILE[]					= "/html/SC10_main_015.css";
    constexpr char JS_FILE[]					= "/html/SC10_main_015.js";

    constexpr char HTML_URI[]					= "/main.html";
    constexpr char CSS_URI[]					= "/SC10_main_015.css";
    constexpr char JS_URI[]		    			= "/SC10_main_015.js";

    constexpr int  MAX_STA_NETWORKS = 5;   /// 저장 가능한 STA 네트워크 최대 개수
}  // namespace A10_Const


// ====================================================================================
// Wi-Fi 설정 관련 상수
// ====================================================================================

// 기존 매크로 대신 enum 사용
typedef enum : uint8_t {
    EN_A10_WIFI_MODE_AP      = 0,  // AP only
    EN_A10_WIFI_MODE_STA     = 1,  // STA only
    EN_A10_WIFI_MODE_AP_STA  = 2,  // AP + STA 동시
} T_A10_WifiMode_t;

// (가능하면 기존 매크로 제거 또는 아래처럼 유지 호환)
#define G_A10_WIFI_MODE_AP     EN_A10_WIFI_MODE_AP
#define G_A10_WIFI_MODE_STA    EN_A10_WIFI_MODE_STA
#define G_A10_WIFI_MODE_AP_STA EN_A10_WIFI_MODE_AP_STA

/*
/// Wi-Fi 모드 정의: AP 전용 모드
#define G_A10_WIFI_MODE_AP		0
/// Wi-Fi 모드 정의: STA 전용 모드
#define G_A10_WIFI_MODE_STA		1
*/

// ====================================================================================
// 풍속 시뮬레이션 단계 (Phase) 정의
// ====================================================================================

/**
 * @brief 시뮬레이션 기상 단계 (바람 세기 구간)
 */
typedef enum {
	EN_A10_WEATHER_PHASE_CALM	  = 0,	///< 잔잔한 바람 (낮은 평균 풍속)
	EN_A10_WEATHER_PHASE_NORMAL = 1,	///< 일반적인 바람 (중간 풍속)
	EN_A10_WEATHER_PHASE_STRONG = 2,	///< 강한 바람 (높은 평균 풍속)
	EN_A10_WEATHER_PHASE_COUNT		///< Phase 개수 (배열 크기용)
} SC10_WindWeatherPhase_t;

/// Phase 이름 문자열 배열
static const char* g_A10_WEATHER_PHASE_NAMES_Arr[] = {
	"Calm-잔잔한",
	"Normal-보통",
	"Strong-강한"
};

// ====================================================================================
// 프리셋 모드 정의
// ====================================================================================

/**
 * @brief 바람 환경 프리셋 모드
 * - 특정 지형/환경에 맞는 기본 파라미터 세트 제공
 */
typedef enum {
	EN_A10_PRESET_OFF			  	= 0,	///< 프리셋 사용 안 함
	EN_A10_PRESET_COUNTRY		  	= 1,	///< 시골/들판 바람
	EN_A10_PRESET_MEDITERRANEAN		= 2,	///< 지중해성 바람
	EN_A10_PRESET_OCEAN		  		= 3,	///< 해양성 바람
	EN_A10_PRESET_MOUNTAIN	  		= 4,	///< 산악 지역 바람
	EN_A10_PRESET_PLAINS		  	= 5,	///< 대평원 바람
    // ===== 신규 프리셋 5종 =====
    EN_A10_PRESET_HARBOR_BREEZE		= 6,	// 항구 바람
    EN_A10_PRESET_FOREST_CANOPY		= 7,    // 숲 그늘 바람
    EN_A10_PRESET_URBAN_SUNSET		= 8,    // 도시 석양 바람
    EN_A10_PRESET_TROPICAL_RAIN		= 9,    // 열대 소나기 바람
    EN_A10_PRESET_DESERT_NIGHT		= 10,   // 사막의 밤 바람
  // ==========================
	EN_A10_PRESET_COUNT
} T_A10_PresetMode_t;

/// 프리셋 모드 이름 문자열 배열
static const char* g_A10_PRESET_MODE_NAMES_Arr[] = {
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

#define G_A10_WIFI_SSID_LEN		32
#define G_A10_WIFI_PWD_LEN		64


// ====================================================================================
// Wi-Fi STA Credential 구조체 정의
// ====================================================================================

/**
 * @brief Wi-Fi STA 네트워크 인증 정보
 * - SSID: 최대 32바이트
 * - Password: 최대 64바이트
 */
struct ST_A10_StaCredential {
	char ssid[G_A10_WIFI_SSID_LEN];
	char password[G_A10_WIFI_PWD_LEN];
};

// ====================================================================================
// ST_A10_WindConfig 구조체 (최적화 버전)
// ====================================================================================
#define G_A10_API_KEY_MAX_LEN 64

typedef struct __attribute__((packed)) {
    // [security]
    char api_key[G_A10_API_KEY_MAX_LEN + 1];

    // ------------------------------------------------------
    // [sim] 시뮬레이션 파라미터 (float 우선 배치 → alignment 최소화)
    // ------------------------------------------------------
    float wind_intensity;              // 바람 강도 (0.0 ~ 1.0)
    float gust_frequency;              // 돌풍 발생 빈도 (Hz)
    float wind_variability;            // 바람 변동성
    float fan_speed_limit;             // 최대 팬 속도 제한 (%)
    float minimum_fan_speed;           // 최소 팬 속도 (%)
    float turbulence_length_scale;     // 난류 길이 스케일
    float turbulence_intensity_sigma;  // 난류 강도 시그마
    float thermal_bubble_strength;     // 열기포 강도
    float thermal_bubble_radius;       // 열기포 반경

    // ------------------------------------------------------
    // [hw] 하드웨어 설정
    // ------------------------------------------------------
    int16_t fan_pwm_pin;               // PWM 핀 (ESP32: -1 ~ 48)
    uint8_t pwm_channel;               // PWM 채널 (0~7)
    uint8_t pwm_resolution;            // PWM 해상도 (8~16)
    uint32_t pwm_frequency;            // PWM 주파수 (Hz)

    // ------------------------------------------------------
    // [sim/timing]
    // ------------------------------------------------------
    uint16_t wind_sim_interval_ms;     // 시뮬레이션 주기
    uint16_t gust_check_interval_ms;   // 돌풍 검사 주기
    uint16_t thermal_check_interval_ms;// 열기포 검사 주기
    uint8_t  preset_mode_index;        // 프리셋 인덱스 (0~10)

    // ------------------------------------------------------
    // [wifi]
    // ------------------------------------------------------
    uint8_t wifi_mode;                 // 0=AP, 1=STA
    uint8_t sta_network_count;         // 저장된 STA 네트워크 수
    char    ap_ssid[G_A10_WIFI_SSID_LEN];
    char    ap_password[G_A10_WIFI_PWD_LEN];
    ST_A10_StaCredential sta_networks[A10_Const::MAX_STA_NETWORKS];
} ST_A10_WindConfig;


// 전역 설정 인스턴스 (헤더 온리: inline로 ODR 방지)
inline ST_A10_WindConfig 	g_A10_config;

// --- 기본값 초기화 함수 ---
void initDefaultConfig(ST_A10_WindConfig &p_cfg) {
    // [security]
    p_cfg.api_key[0] 					= '\0'; // API 키 리셋


	// module			, ESP32				, ESP32-S3			, ESP32-C3
	// PWM_channel_No	,  0 ~ 15			, 0 ~ 7				, 0 ~ 5
	// Resolution		,    16bit			,     16bit			,     14bit
	// 					, 65,536			, 65,536			, 16,384
	// Freq(8bit)		, 312.50kHz			, 312.50kHz			, 312.50kHz
	// Freq(10bit)		,  78.10kHz			,  78.10kHz			,  78.10kHz
	// Freq(12 Bit)		,  19.50kHz			,  19.50kHz			,  19.50kHz
	// Freq(14 Bit)		,   4.88kHz			,   4.88kHz			,   4.88kHz
	// Freq(16 Bit)		,   1.22kHz			,   1.22kHz			, 		N/A


    // [hw] 기본 하드웨어 설정 (reset() 함수에서 사용되던 값 포함)
    p_cfg.fan_pwm_pin     				= 6;
    p_cfg.pwm_channel     				= 0;			//	Range : 0 ~ 5
    p_cfg.pwm_frequency   				= 25000;		//  Range : 25,000 ~ 40,000
    p_cfg.pwm_resolution  				= 10;			// 	Range : 8 ~ 10

    // [sim] 기본 시뮬레이션 설정
    p_cfg.preset_mode_index        		= EN_A10_PRESET_COUNTRY;
    p_cfg.wind_intensity           		= 0.5f;
    p_cfg.gust_frequency           		= 0.1f;
    p_cfg.wind_variability         		= 0.2f;
    p_cfg.fan_speed_limit          		= 1.0f;  // 100%
    p_cfg.minimum_fan_speed        		= 0.1f;  // 10%
    p_cfg.turbulence_length_scale  		= 1.0f;
    p_cfg.turbulence_intensity_sigma 	= 0.05f;
    p_cfg.thermal_bubble_strength  		= 0.0f;
    p_cfg.thermal_bubble_radius    		= 10.0f;

    // [timing] 기본 타이밍 설정
    p_cfg.wind_sim_interval_ms     		= 50;
    p_cfg.gust_check_interval_ms   		= 1000;
    p_cfg.thermal_check_interval_ms 	= 5000;

    // [wifi] 기본 Wi-Fi 설정
    p_cfg.wifi_mode 					= EN_A10_WIFI_MODE_AP_STA;
    strlcpy(p_cfg.ap_ssid, "WindSim_AP", sizeof(p_cfg.ap_ssid));
    strlcpy(p_cfg.ap_password, "password", sizeof(p_cfg.ap_password));
    p_cfg.sta_network_count 			= 0;
    // sta_networks 배열 초기화는 count가 0이므로 생략
}

// 난수 유틸 (0~1)
inline float A10_getRandom01() {
	return (float)esp_random() / (float)UINT32_MAX;
}
// 난수 유틸 (범위)
inline float A10_randRange(float a, float b) {
	return a + A10_getRandom01() * (b - a);
}
