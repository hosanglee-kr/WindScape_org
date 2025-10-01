//
// SC10_WindScape_005.h

#pragma once 

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiMulti.h> 

#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h> // V7.4.x 사용
#include <AsyncJson.h> 
#include <FS.h>
#include <LittleFS.h>
#include <cmath>
#include <cstdlib>
#include <string>
#include <map>

// ====================================================================================
// 전역 상수 (파일 경로 등)
// ====================================================================================
// G_SC10_으로 시작하는 전역 상수명
const char* G_SC10_CONFIG_FILE_PATH     = "/json/config_003.json";
const char* G_SC10_CONFIG_HTML_PATH     = "/html/SC10_main_004.html"; 
const char* G_SC10_CONFIG_JS_PATH       = "/html/SC10_main_003.js"; 


// ====================================================================================
// 타입 정의 (typedef) 및 열거형 (enum) - SC10_ 접두사 적용
// ====================================================================================

/**
 * @brief 시뮬레이션의 동적 기상 단계 (Phase) 정의
 */
typedef enum {
    SC10_WEATHER_PHASE_CALM = 0,    // 잔잔한 바람 (낮은 평균 풍속)
    SC10_WEATHER_PHASE_NORMAL = 1,  // 일반적인 바람 (중간 평균 풍속)
    SC10_WEATHER_PHASE_STRONG = 2,  // 강한 바람 (높은 평균 풍속)
    SC10_WEATHER_PHASE_COUNT
} SC10_WindWeatherPhase_t;

/**
 * @brief 설정 프리셋 모드 정의
 */
typedef enum {
    SC10_PRESET_OFF = 0,
    SC10_PRESET_COUNTRY = 1,
    SC10_PRESET_MEDITERRANEAN = 2,
    SC10_PRESET_OCEAN = 3,
    SC10_PRESET_MOUNTAIN = 4,
    SC10_PRESET_PLAINS = 5,
    SC10_PRESET_COUNT
} SC10_PresetMode_t;

// SC10_WindWeatherPhase_t 값에 대응하는 문자열 상수 배열 (G_SC10_으로 시작)
const char* G_SC10_WEATHER_PHASE_NAMES[] = {
    "Calm",
    "Normal",
    "Strong"
};

// SC10_PresetMode_t 값에 대응하는 문자열 상수 배열 (G_SC10_으로 시작)
const char* G_SC10_PRESET_MODE_NAMES[] = {
    "Off",
    "Countryside",
    "Mediterranean",
    "Ocean",
    "Mountain",
    "Plains"
};

// 난수 생성 함수 (0.0f ~ 1.0f)
float SC10_getRandomFloat(void) {
    // 컴파일러 경고를 피하기 위해 명시적 캐스팅 적용
    return (float)esp_random() / (float)4294967295.0f; // UINT32_MAX
}

float SC10_getRandomFloat(float p_min, float p_max) { // p_로 시작하는 함수 파라미터
    return p_min + (SC10_getRandomFloat() * (p_max - p_min));
}

// ====================================================================================
// 전역 변수
// ====================================================================================

// g_SC10_으로 시작하는 전역 변수명
AsyncWebServer g_SC10_asyncWeb(80);



#define     G_SC10_MAX_STA_NETWORKS 5 // 최대 저장 가능한 STA 네트워크 수
WiFiMulti   g_SC10_wifiMulti;

// WiFi 모드 정의
#define     G_SC10_WIFI_MODE_AP   0
#define     G_SC10_WIFI_MODE_STA  1



// ====================================================================================
// WindScape Configuration Structure
// ====================================================================================
struct WindConfig {
    // --- WIFI 설정 추가 ---
    int     wifi_mode;                  // 0: G_SC10_WIFI_MODE_AP, 1: G_SC10_WIFI_MODE_STA
    
    // WiFiMulti를 위한 STA 네트워크 목록 (JSON 배열로 저장/로드됨)
    // 메모리 절약을 위해 SSID/Password는 32바이트/64바이트로 제한합니다.
    struct StaCredential {
        char ssid[32];
        char password[64];
    };
    StaCredential sta_networks[G_SC10_MAX_STA_NETWORKS];
    int     sta_network_count; // 실제 저장된 네트워크 수
    
    // AP 모드 고정 설정 (필요 시)
    char    ap_ssid[32];
    char    ap_password[64];


    // --- 1. 하드웨어/시스템 상수 (JSON 관리) ---
    int     fan_pwm_pin                 = 14;     
    int     fan_tach_pin                = 27;     
    int     pwm_frequency               = 25000;  
    int     pwm_channel                 = 0;
    int     pwm_resolution              = 10;     

    int     wind_sim_interval_ms        = 250;
    int     gust_check_interval_ms      = 500;
    int     thermal_check_interval_ms   = 2000;
    
    // --- 2. 시뮬레이션 설정 (JSON 관리) ---
    float   wind_intensity              = 100.0f;
    float   gust_frequency              = 30.0f;
    float   wind_variability            = 40.0f;
    float   fan_speed_limit             = 80.0f;
    float   minimum_fan_speed           = 0.0f;
    float   turbulence_length_scale     = 30.0f;
    float   turbulence_intensity_sigma  = 0.3f;
    float   thermal_bubble_strength     = 1.8f;
    float   thermal_bubble_radius       = 15.0f; 
    // JSON 로드/저장을 위해 enum의 int 인덱스를 저장
    int     preset_mode_index           = SC10_PRESET_OCEAN; 
};

WindConfig g_SC10_config;

// ====================================================================================
// WindScape Simulator Class
// ====================================================================================

class WindScapeSimulator {
private:
    // --- 유틸리티 함수 ---
    
    // Preset Name을 받아 해당 enum index를 반환 (SC10_으로 시작하는 함수)
    bool SC10_getPresetIndexByName(const char* p_name, int& p_index) { // p_로 시작하는 함수 파라미터
        for (int v_i = 0; v_i < SC10_PRESET_COUNT; ++v_i) {
            if (strcmp(p_name, G_SC10_PRESET_MODE_NAMES[v_i]) == 0) {
                p_index = v_i;
                return true;
            }
        }
        return false;
    }

public:
    // --- 1. 상태 변수 (클래스 멤버) ---
    bool fan_power_enabled              = true;
    bool enable_wind_simulation         = true;
    bool wind_simulation_active         = false;

    // Wind Dynamics
    float current_wind_speed            = 3.6f; 
    float target_wind_speed             = 3.6f; 
    float wind_change_rate              = 0.1f;
    float wind_momentum                 = 0.0f;

    // Gust System      
    bool gust_active                    = false;
    float gust_start_time               = 0.0f;
    float gust_duration                 = 3.0f;
    float gust_intensity                = 1.0f;
    unsigned long last_gust_check       = 0;
    
    // Thermal Convection
    bool thermal_bubble_active          = false;
    float thermal_bubble_start_time     = 0.0f;
    float thermal_bubble_duration       = 8.0f;
    unsigned long last_thermal_check    = 0;
    float current_thermal_contribution  = 0.0f;
    
    // Turbulence Modeling
    float spectral_energy_buffer        = 0.0f;
    float spectral_phase_accumulator    = 0.0f;
    float turbulence_time_scale         = 5.0f;
    
    // Dynamic Weather Phases
    SC10_WindWeatherPhase_t current_weather_phase = SC10_WEATHER_PHASE_NORMAL; // SC10_enum 사용
    float phase_start_time              = 0.0f;
    float phase_duration                = 120.0f;
    float phase_wind_min                = 5.0f;
    float phase_wind_max                = 15.0f;

    // Preset Configuration (내부 시뮬레이션 로직용)
    float base_wind_min                 = 2.2f;
    float base_wind_max                 = 6.7f;
    float gust_probability_base         = 0.02f;
    float location_gust_strength        = 1.8f;
    float thermal_bubble_frequency      = 0.025f;

    // 타이머 변수
    unsigned long last_wind_sim_update  = 0;
    
    // --- 2. 설정 파일 관리 ---

    bool SC10_loadConfig(void) {
        if (!LittleFS.begin(true)) {
            Serial.println("LittleFS Mount Failed! Using default config.");
            return false;
        }

        File v_configFile = LittleFS.open(G_SC10_CONFIG_FILE_PATH, "r");
        if (!v_configFile) {
            Serial.println("Config file not found. Using default.");
            v_configFile.close();
            return false;
        }

        JsonDocument v_doc;
        DeserializationError v_error = deserializeJson(v_doc, v_configFile);
        v_configFile.close();

        if (v_error) {
            Serial.printf("Failed to deserialize JSON: %s\n", v_error.c_str());
            return false;
        }
        
        // JSON Root Object (편의를 위해 v_root 변수 사용)
        JsonObject v_root = v_doc.as<JsonObject>();

        // --- 1. 하드웨어/시스템 상수 로드 ---
        g_SC10_config.fan_pwm_pin               = v_root["hw"]["pwm_pin"] | 14;
        g_SC10_config.pwm_frequency             = v_root["hw"]["pwm_freq"] | 25000;
        g_SC10_config.pwm_resolution            = v_root["hw"]["pwm_res"] | 10;
        // 이전 wifi_ssid/pass 로직은 제거됨

        g_SC10_config.wind_sim_interval_ms      = v_root["timing"]["sim_int"] | 250;
        g_SC10_config.gust_check_interval_ms    = v_root["timing"]["gust_int"] | 500;
        g_SC10_config.thermal_check_interval_ms = v_root["timing"]["thermal_int"] | 2000;

        // --- 2. 시뮬레이션 설정 로드 ---
        g_SC10_config.wind_intensity            = v_root["sim"]["intensity"] | g_SC10_config.wind_intensity;
        g_SC10_config.gust_frequency            = v_root["sim"]["gust_freq"] | g_SC10_config.gust_frequency;
        g_SC10_config.wind_variability          = v_root["sim"]["variability"] | g_SC10_config.wind_variability;
        g_SC10_config.fan_speed_limit           = v_root["sim"]["fan_limit"] | g_SC10_config.fan_speed_limit;
        g_SC10_config.minimum_fan_speed         = v_root["sim"]["min_fan"] | g_SC10_config.minimum_fan_speed;
        
        g_SC10_config.turbulence_length_scale   = v_root["sim"]["turb_len"] | g_SC10_config.turbulence_length_scale;
        g_SC10_config.turbulence_intensity_sigma= v_root["sim"]["turb_sig"] | g_SC10_config.turbulence_intensity_sigma;
        g_SC10_config.thermal_bubble_strength   = v_root["sim"]["therm_str"] | g_SC10_config.thermal_bubble_strength;
        g_SC10_config.thermal_bubble_radius     = v_root["sim"]["therm_rad"] | g_SC10_config.thermal_bubble_radius;
        
        const char* v_preset_name = v_root["sim"]["preset"] | G_SC10_PRESET_MODE_NAMES[SC10_PRESET_OCEAN];
        int v_index;
        if (SC10_getPresetIndexByName(v_preset_name, v_index)) {
            g_SC10_config.preset_mode_index = v_index;
        } else {
            g_SC10_config.preset_mode_index = SC10_PRESET_OCEAN;
        }

        // -------------------- WIFI 모드 및 네트워크 로드 (수정된 부분) --------------------
        g_SC10_config.wifi_mode         = v_root["wifi"]["wifi_mode"] | G_SC10_WIFI_MODE_STA; 

        // AP 설정 로드
        strncpy(g_SC10_config.ap_ssid,      v_root["wifi"]["ap_ssid"] | "SC10_Config_AP", sizeof(g_SC10_config.ap_ssid));
        strncpy(g_SC10_config.ap_password,  v_root["wifi"]["ap_password"] | "newpassword", sizeof(g_SC10_config.ap_password));

        // STA 네트워크 목록 로드 (JSON 배열)
        JsonArray v_sta_networks_json   = v_root["wifi"]["sta_networks"].as<JsonArray>();
        g_SC10_config.sta_network_count = 0;

        for (JsonObject v_network : v_sta_networks_json) {
            if (g_SC10_config.sta_network_count < G_SC10_MAX_STA_NETWORKS) {
                // JSON에서 ssid와 pass를 읽어 StaCredential 구조체에 복사
                strncpy(g_SC10_config.sta_networks[g_SC10_config.sta_network_count].ssid, 
                        v_network["ssid"] | "", sizeof(g_SC10_config.sta_networks[0].ssid) - 1);
                g_SC10_config.sta_networks[g_SC10_config.sta_network_count].ssid[sizeof(g_SC10_config.sta_networks[0].ssid) - 1] = '\0';
                        
                strncpy(g_SC10_config.sta_networks[g_SC10_config.sta_network_count].password, 
                        v_network["pass"] | "", sizeof(g_SC10_config.sta_networks[0].password) - 1);
                g_SC10_config.sta_networks[g_SC10_config.sta_network_count].password[sizeof(g_SC10_config.sta_networks[0].password) - 1] = '\0';
                
                // SSID가 비어있지 않은 경우에만 카운트 증가
                if (strlen(g_SC10_config.sta_networks[g_SC10_config.sta_network_count].ssid) > 0) {
                     g_SC10_config.sta_network_count++;
                }
            }
        }
        // ----------------------------------------------------------------------------------
        
        Serial.println("Config loaded successfully.");
        return true;
    }

    bool SC10_saveConfig(void) {
        JsonDocument v_doc;
        JsonObject v_root = v_doc.to<JsonObject>();

        // --- 1. 하드웨어/시스템 상수 저장 ---
        v_root["hw"]["pwm_pin"]      = g_SC10_config.fan_pwm_pin;
        v_root["hw"]["pwm_freq"]     = g_SC10_config.pwm_frequency;
        v_root["hw"]["pwm_res"]      = g_SC10_config.pwm_resolution;
        // 이전 wifi_ssid/pass 로직 제거
        v_root["timing"]["sim_int"]  = g_SC10_config.wind_sim_interval_ms;
        v_root["timing"]["gust_int"] = g_SC10_config.gust_check_interval_ms;
        v_root["timing"]["thermal_int"] = g_SC10_config.thermal_check_interval_ms;

        // --- 2. 시뮬레이션 설정 저장 ---
        v_root["sim"]["intensity"]   = g_SC10_config.wind_intensity;
        v_root["sim"]["gust_freq"]   = g_SC10_config.gust_frequency;
        v_root["sim"]["variability"] = g_SC10_config.wind_variability;
        v_root["sim"]["fan_limit"]   = g_SC10_config.fan_speed_limit;
        v_root["sim"]["min_fan"]     = g_SC10_config.minimum_fan_speed;
        v_root["sim"]["turb_len"]    = g_SC10_config.turbulence_length_scale;
        v_root["sim"]["turb_sig"]    = g_SC10_config.turbulence_intensity_sigma;
        v_root["sim"]["therm_str"]   = g_SC10_config.thermal_bubble_strength;
        v_root["sim"]["therm_rad"]   = g_SC10_config.thermal_bubble_radius;
        
        // JSON에는 문자열 이름으로 저장
        v_root["sim"]["preset"]      = G_SC10_PRESET_MODE_NAMES[g_SC10_config.preset_mode_index]; 
        

        // -------------------- WIFI 모드 및 네트워크 저장 (수정된 부분) --------------------
        
        // 1. "wifi" 객체 생성 및 기본 값 설정
        // v_root["wifi"] = JsonObject()를 통해 중첩 객체를 생성합니다.
        JsonObject v_wifi_obj = v_root["wifi"].to<JsonObject>(); // JsonObject 생성

        v_wifi_obj["wifi_mode"]     = g_SC10_config.wifi_mode;
        v_wifi_obj["ap_ssid"]       = g_SC10_config.ap_ssid;
        v_wifi_obj["ap_password"]   = g_SC10_config.ap_password; 

        // 2. STA 네트워크 목록 저장 (JSON 배열)
        JsonArray v_sta_networks_json = v_wifi_obj["sta_networks"].to<JsonArray>();

        // 3. STA 네트워크 배열에 객체 추가
        for (int i = 0; i < g_SC10_config.sta_network_count; i++) {
            
            // **v7.4.x 권장 방식 (add 사용):**
            JsonObject v_network = v_sta_networks_json.add<JsonObject>(); // JsonArray에 새 객체를 추가하며 참조를 얻음

            v_network["ssid"] = g_SC10_config.sta_networks[i].ssid;
            v_network["pass"] = g_SC10_config.sta_networks[i].password;
        }

        // ----------------------------------------------------------------------------------

        File v_configFile = LittleFS.open(G_SC10_CONFIG_FILE_PATH, "w");
        if (!v_configFile) {
            Serial.println("Failed to open config file for writing");
            return false;
        }

        if (serializeJson(v_doc, v_configFile) == 0) {
            Serial.println("Failed to write to file");
            v_configFile.close();
            return false;
        }
        
        v_configFile.close();
        Serial.println("Config saved successfully.");
        return true;
    }

    void SC10_initWiFi() {
        Serial.println("SC10_initWiFi: Initializing WiFi...");
        
        // 1. WiFiMulti에 저장된 STA 네트워크 목록 추가
        g_SC10_wifiMulti.run(); // 혹시 모를 이전 연결 해제
        WiFi.mode(WIFI_AP_STA); // AP/STA 동시 사용을 위해 기본 모드를 AP+STA로 설정
    
        for (int i = 0; i < g_SC10_config.sta_network_count; i++) {
            if (strlen(g_SC10_config.sta_networks[i].ssid) > 0) {
                g_SC10_wifiMulti.addAP(
                    g_SC10_config.sta_networks[i].ssid, 
                    g_SC10_config.sta_networks[i].password
                );
                Serial.printf("  Added STA: %s\n", g_SC10_config.sta_networks[i].ssid);
            }
        }
        
        // 2. STA 모드 접속 시도
        if (g_SC10_config.wifi_mode == G_SC10_WIFI_MODE_STA && g_SC10_config.sta_network_count > 0) {
            Serial.print("Trying to connect to STA network(s)...");
            
            int v_connect_attempts = 0;
            int v_max_attempts = 15; // 최대 시도 횟수를 늘려 연결 안정성 확보
            
            while (g_SC10_wifiMulti.run() != WL_CONNECTED && v_connect_attempts < v_max_attempts) {
                v_connect_attempts++;
                Serial.print(".");
                delay(1000); 
            }
            
            // 3. 접속 결과 처리
            if (WiFi.status() == WL_CONNECTED) {
                Serial.println("\nSTA Connected!");
                Serial.print("IP address: ");
                Serial.println(WiFi.localIP());
                // AP 모드는 비활성화
                WiFi.softAPdisconnect(true);
                return;
            } else {
                // 접속 실패 시 AP 모드로 전환 (실제 설정값 변경은 하지 않음, 임시 전환)
                Serial.println("\nSTA connection failed. Activating AP mode for configuration.");
                g_SC10_config.wifi_mode = G_SC10_WIFI_MODE_AP;
                // AP 모드 시작 로직으로 넘어감
            }
        }
    
        // 4. AP 모드 설정 및 시작 (초기 설정이 AP이거나, STA 모드 접속 실패 시)
        if (g_SC10_config.wifi_mode == G_SC10_WIFI_MODE_AP) {
            // 기존 STA 연결을 끊고 AP만 활성화
            WiFi.disconnect(true); 
            WiFi.softAP(g_SC10_config.ap_ssid, g_SC10_config.ap_password);
            Serial.printf("Starting AP mode: %s\n", g_SC10_config.ap_ssid);
            Serial.print("AP IP address: ");
            Serial.println(WiFi.softAPIP());
        }
    }

    // --- 3. 초기화 및 설정 ---

    void SC10_init(void) {
        Serial.println("\nWindScape Simulator Starting...");

        // 1. 파일 시스템 및 설정 로드
        SC10_loadConfig();

        // 2. Wi-Fi 초기화 (AP/STA/Multi 로직 포함)
        SC10_initWiFi();
        
        // 3. 팬 PWM 설정 
        ledcSetup(g_SC10_config.pwm_channel, g_SC10_config.pwm_frequency, g_SC10_config.pwm_resolution);
        ledcAttachPin(g_SC10_config.fan_pwm_pin, g_SC10_config.pwm_channel);
        pinMode(g_SC10_config.fan_tach_pin, INPUT_PULLUP);
        
        float v_initial_speed = g_SC10_config.minimum_fan_speed + 12.0f; // v_로 시작하는 지역 변수
        if (fan_power_enabled) {
            SC10_applyFanSpeed(v_initial_speed); 
        } else {
            ledcWrite(g_SC10_config.pwm_channel, 0);
        }

        // 4. Web Server 초기화
        SC10_setupWebServer();

        // 5. 시뮬레이션 시작
        srand(esp_random()); 
        SC10_applyCurrentPreset(true); 
    }


    /**
     * @brief 웹 서버 초기화 (SPA API 방식)
     * * 정적 파일(/, /script.js) 서빙 및 2개의 API 엔드포인트(/api/state, /api/config) 설정.
     */
    void SC10_setupWebServer(void) {
        // LittleFS 마운트 확인 (정적 파일 제공을 위해 필수)
        if (!LittleFS.begin()) {
            Serial.println("LittleFS Mount Failed! Web server starting without FS.");
        }
    
        // 1. 루트 페이지 및 정적 파일 제공 (index.html 및 script.js)
        g_SC10_asyncWeb.on("/", HTTP_GET, [](AsyncWebServerRequest *p_request){
            p_request->send(LittleFS, G_SC10_CONFIG_HTML_PATH, "text/html");
        });
        
        g_SC10_asyncWeb.on("/script.js", HTTP_GET, [](AsyncWebServerRequest *p_request){
            p_request->send(LittleFS, G_SC10_CONFIG_JS_PATH, "application/javascript");
        });
        
        g_SC10_asyncWeb.onNotFound([](AsyncWebServerRequest *p_request){
            p_request->send(404, "text/plain", "Not found");
        });
    
    
        // 2. 현재 상태 및 설정 API (GET: /api/state)
        g_SC10_asyncWeb.on("/api/state", HTTP_GET, [this](AsyncWebServerRequest *p_request){

            JsonDocument v_doc; 
            
            // A. 현재 동적 상태
            JsonObject v_status = v_doc["status"].to<JsonObject>();
            v_status["sim_active"] = wind_simulation_active;
            v_status["wind_speed"] = roundf(current_wind_speed * 100) / 100.0f;
            v_status["fan_pwm"] = ledcRead(g_SC10_config.pwm_channel);
            v_status["phase_name"] = G_SC10_WEATHER_PHASE_NAMES[current_weather_phase];
            
            // 접속 IP 정보
            if (g_SC10_config.wifi_mode == G_SC10_WIFI_MODE_STA && WiFi.status() == WL_CONNECTED) {
                v_status["wifi_mode"]   = "STA";
                v_status["ip_addr"]     = WiFi.localIP().toString();
                v_status["ssid"]        = WiFi.SSID(); // 현재 연결된 SSID
            } else {
                v_status["wifi_mode"]   = "AP";
                v_status["ip_addr"]     = WiFi.softAPIP().toString();
                v_status["ssid"]        = g_SC10_config.ap_ssid;
            }
    
            // B. 현재 설정값
            JsonObject v_config = v_doc["config"].to<JsonObject>();
            v_config["intensity"]   = g_SC10_config.wind_intensity;
            v_config["gust_freq"]   = g_SC10_config.gust_frequency;
            v_config["variability"] = g_SC10_config.wind_variability;
            v_config["fan_limit"]   = g_SC10_config.fan_speed_limit;
            v_config["min_fan"]     = g_SC10_config.minimum_fan_speed;
            v_config["turb_len"]    = g_SC10_config.turbulence_length_scale;
            v_config["turb_sig"]    = g_SC10_config.turbulence_intensity_sigma;
            v_config["therm_str"]   = g_SC10_config.thermal_bubble_strength;
            v_config["therm_rad"]   = g_SC10_config.thermal_bubble_radius;
            v_config["preset"]      = G_SC10_PRESET_MODE_NAMES[g_SC10_config.preset_mode_index];

            // C. Wi-Fi 설정값 (GET 시에는 구조체처럼 반환)
            JsonObject v_wifi_config = v_config["wifi"].to<JsonObject>();
            v_wifi_config["wifi_mode"]     = g_SC10_config.wifi_mode;
            v_wifi_config["ap_ssid"]       = g_SC10_config.ap_ssid;
            v_wifi_config["ap_password"]   = g_SC10_config.ap_password;
                        
            JsonArray v_sta_networks_json = v_wifi_config["sta_networks"].to<JsonArray>();

            // 배열에 객체를 추가합니다. createNestedObject() 대신 add<JsonObject>()를 사용합니다.
            for (int i = 0; i < g_SC10_config.sta_network_count; i++) {
                // v7.4.x에서 권장되는 방식으로, JsonArray에 새 객체를 추가하며 참조를 얻습니다.
                JsonObject v_network = v_sta_networks_json.add<JsonObject>(); 
                v_network["ssid"]      = g_SC10_config.sta_networks[i].ssid;
                v_network["pass"]      = g_SC10_config.sta_networks[i].password; // 보안상 문제 있으나, 임시 설정용
            }

            // // C. Wi-Fi 설정값 (GET 시에는 구조체처럼 반환)
            // JsonObject v_wifi_config        = v_config.createNestedObject("wifi");
            // v_wifi_config["wifi_mode"]      = g_SC10_config.wifi_mode;
            // v_wifi_config["ap_ssid"]        = g_SC10_config.ap_ssid;
            // v_wifi_config["ap_password"]    = g_SC10_config.ap_password;
            
            // JsonArray v_sta_networks_json = v_wifi_config.createNestedArray("sta_networks");
            // for (int i = 0; i < g_SC10_config.sta_network_count; i++) {
            //     JsonObject v_network    = v_sta_networks_json.createNestedObject();
            //     v_network["ssid"]       = g_SC10_config.sta_networks[i].ssid;
            //     v_network["pass"]       = g_SC10_config.sta_networks[i].password; // 보안상 문제 있으나, 임시 설정용
            // }
            
            // D. 프리셋 목록
            JsonArray v_presets = v_doc["presets"].to<JsonArray>();
            for(int v_i = 0; v_i < SC10_PRESET_COUNT; ++v_i) {
                v_presets.add(G_SC10_PRESET_MODE_NAMES[v_i]);
            }
            
            // JSON 응답 전송
            String v_response;
            serializeJson(v_doc, v_response);
            p_request->send(200, "application/json", v_response);
        });
    
        // 3. 설정 업데이트 API (POST: /api/config) - 대체 구현
        g_SC10_asyncWeb.on("/api/config", HTTP_POST, [this](AsyncWebServerRequest *p_request){}, NULL, 
            [this](AsyncWebServerRequest *p_request, uint8_t *p_data, size_t p_len, size_t p_index, size_t p_total){
                if (p_index == 0 && p_len == p_total) {
                    if (p_request->hasHeader("Content-Type") && 
                        p_request->header("Content-Type").indexOf("application/json") != -1) 
                    {
                        JsonDocument v_doc;
                        DeserializationError v_error = deserializeJson(v_doc, (const char*)p_data, p_len);
                        
                        if (v_error) {
                            Serial.printf("JSON Deserialization failed: %s\n", v_error.c_str());
                            p_request->send(400, "application/json", "{\"error\":\"Invalid JSON format\"}");
                            return;
                        }
                        
                        bool v_changesMade = false;
                        JsonObject v_root_config = v_doc.as<JsonObject>();
                        
                        // -------------------- 시뮬레이션 설정 값 업데이트 (이전 로직 유지) --------------------
                        
                        if (!v_root_config["preset"].isNull()) {
                            const char* v_preset_name = v_root_config["preset"];
                            int v_index;
                            if (SC10_getPresetIndexByName(v_preset_name, v_index)) {
                                g_SC10_config.preset_mode_index = v_index;
                                v_changesMade = true;
                            }
                        }
                        
                        if (!v_root_config["intensity"].isNull()) { 
                            g_SC10_config.wind_intensity = v_root_config["intensity"] | g_SC10_config.wind_intensity; 
                            v_changesMade = true; 
                        }
                        // 나머지 시뮬레이션 설정 값 업데이트 (생략하지 않고 모든 필드를 체크해야 합니다.)
                        if (!v_root_config["gust_freq"].isNull()) { 
                            g_SC10_config.gust_frequency = v_root_config["gust_freq"] | g_SC10_config.gust_frequency; 
                            v_changesMade = true; 
                        }
                        if (!v_root_config["variability"].isNull()) { 
                            g_SC10_config.wind_variability = v_root_config["variability"] | g_SC10_config.wind_variability; 
                            v_changesMade = true; 
                        }
                        if (!v_root_config["fan_limit"].isNull()) { 
                            g_SC10_config.fan_speed_limit = v_root_config["fan_limit"] | g_SC10_config.fan_speed_limit; 
                            v_changesMade = true; 
                        }
                        if (!v_root_config["min_fan"].isNull()) { 
                            g_SC10_config.minimum_fan_speed = v_root_config["min_fan"] | g_SC10_config.minimum_fan_speed; 
                            v_changesMade = true; 
                        }
                        if (!v_root_config["turb_len"].isNull()) { 
                            g_SC10_config.turbulence_length_scale = v_root_config["turb_len"] | g_SC10_config.turbulence_length_scale; 
                            v_changesMade = true; 
                        }
                        if (!v_root_config["turb_sig"].isNull()) { 
                            g_SC10_config.turbulence_intensity_sigma = v_root_config["turb_sig"] | g_SC10_config.turbulence_intensity_sigma; 
                            v_changesMade = true; 
                        }
                        if (!v_root_config["therm_str"].isNull()) { 
                            g_SC10_config.thermal_bubble_strength = v_root_config["therm_str"] | g_SC10_config.thermal_bubble_strength; 
                            v_changesMade = true; 
                        }
                        if (!v_root_config["therm_rad"].isNull()) { 
                            g_SC10_config.thermal_bubble_radius = v_root_config["therm_rad"] | g_SC10_config.thermal_bubble_radius; 
                            v_changesMade = true; 
                        }
    
                        // -------------------- WIFI 모드 및 네트워크 업데이트 (수정된 부분) --------------------
                        bool v_wifi_changes_made = false;
    
                        if (!v_root_config["wifi_mode"].isNull()) {
                            g_SC10_config.wifi_mode = v_root_config["wifi_mode"] | g_SC10_config.wifi_mode;
                            v_changesMade = true;
                            v_wifi_changes_made = true;
                        }
    
                        if (!v_root_config["ap_ssid"].isNull()) {
                            strncpy(g_SC10_config.ap_ssid, v_root_config["ap_ssid"], sizeof(g_SC10_config.ap_ssid) - 1);
                            g_SC10_config.ap_ssid[sizeof(g_SC10_config.ap_ssid) - 1] = '\0';
                            v_changesMade = true;
                            v_wifi_changes_made = true;
                        }
                        if (!v_root_config["ap_password"].isNull()) {
                            strncpy(g_SC10_config.ap_password, v_root_config["ap_password"], sizeof(g_SC10_config.ap_password) - 1);
                            g_SC10_config.ap_password[sizeof(g_SC10_config.ap_password) - 1] = '\0';
                            v_changesMade = true;
                            v_wifi_changes_made = true;
                        }
    
                        // STA 네트워크 목록 업데이트 (JSON 배열 처리)
                        if (v_root_config["sta_networks"].is<JsonArray>()) {
                            JsonArray v_sta_networks_json = v_root_config["sta_networks"].as<JsonArray>();
                            g_SC10_config.sta_network_count = 0;
                            
                            for (JsonObject v_network : v_sta_networks_json) {
                                if (g_SC10_config.sta_network_count < G_SC10_MAX_STA_NETWORKS) {
                                    // ssid와 pass 필드가 있는지 확인하고 복사
                                    if (v_network["ssid"].is<const char*>() && strlen(v_network["ssid"]) > 0) {
                                        strncpy(g_SC10_config.sta_networks[g_SC10_config.sta_network_count].ssid, 
                                                v_network["ssid"], sizeof(g_SC10_config.sta_networks[0].ssid) - 1);
                                        g_SC10_config.sta_networks[g_SC10_config.sta_network_count].ssid[sizeof(g_SC10_config.sta_networks[0].ssid) - 1] = '\0';
    
                                        strncpy(g_SC10_config.sta_networks[g_SC10_config.sta_network_count].password, 
                                                v_network["pass"] | "", sizeof(g_SC10_config.sta_networks[0].password) - 1);
                                        g_SC10_config.sta_networks[g_SC10_config.sta_network_count].password[sizeof(g_SC10_config.sta_networks[0].password) - 1] = '\0';
                                        
                                        g_SC10_config.sta_network_count++;
                                    }
                                }
                            }
                            v_changesMade = true;
                            v_wifi_changes_made = true;
                        }
                        
                        // -------------------------------------------------------------------------
    
                        if (v_changesMade) {
                            Serial.println("Saving configuration...");
                            SC10_saveConfig(); 
                            SC10_applyCurrentPreset(true); 
                            
                            // Wi-Fi 설정이 변경되었으면 재초기화
                            if (v_wifi_changes_made) {
                                Serial.println("WiFi configuration changed. Re-initializing WiFi...");
                                SC10_initWiFi(); // 변경된 설정으로 즉시 Wi-Fi 재시작 시도
                            }
                        }
                        
                        // 성공 응답
                        p_request->send(200, "application/json", "{\"message\":\"Config updated successfully\"}");
                        return;
    
                    } else {
                        p_request->send(400, "text/plain", "Bad Request: Expected application/json");
                        return;
                    }
                }
            }
        );
    
        Serial.println("Starting Async Web Server...");
        g_SC10_asyncWeb.begin();
    }
    
    // --- 4. 시뮬레이션 엔진 로직 ---

    void SC10_applyFanSpeed(float p_speed_percent) { // p_로 시작하는 함수 파라미터
        if (!fan_power_enabled) {
            ledcWrite(g_SC10_config.pwm_channel, 0);
            return;
        }

        float v_requested_speed = p_speed_percent / 100.0f; // v_로 시작하는 지역 변수
        float v_speed_limit = g_SC10_config.fan_speed_limit / 100.0f;
        float v_min_speed = g_SC10_config.minimum_fan_speed / 100.0f;
        float v_intensity_multiplier = g_SC10_config.wind_intensity / 100.0f;

        if (v_intensity_multiplier <= 0.01f) {
            ledcWrite(g_SC10_config.pwm_channel, 0);
            return;
        }
        
        if (wind_simulation_active) {
            v_requested_speed *= v_intensity_multiplier;
        }

        v_requested_speed = fmax(v_min_speed, fmin(v_speed_limit, v_requested_speed));
        
        int v_pwm_level = (int)(v_requested_speed * (pow(2, g_SC10_config.pwm_resolution) - 1)); // v_로 시작하는 지역 변수

        if (v_requested_speed <= 0.01f) {
            ledcWrite(g_SC10_config.pwm_channel, 0);
        } else {
            ledcWrite(g_SC10_config.pwm_channel, v_pwm_level);
        }
    }

    void SC10_applyCurrentPreset(bool p_force_apply) { // p_로 시작하는 함수 파라미터
        wind_simulation_active  = false;
        gust_active             = false;
        thermal_bubble_active   = false;
        gust_intensity          = 1.0f;
        
        SC10_PresetMode_t v_current_preset_mode = (SC10_PresetMode_t)g_SC10_config.preset_mode_index; // v_로 시작하는 지역 변수
        
        if (v_current_preset_mode == SC10_PRESET_OFF) {
            enable_wind_simulation = false;
            float v_steady_fan_pct = (g_SC10_config.minimum_fan_speed > 0.0f) ? g_SC10_config.minimum_fan_speed : 12.0f;

            SC10_applyFanSpeed(v_steady_fan_pct);
            
            current_wind_speed  = 0.5f;
            target_wind_speed   = 0.5f;

        } else {
            enable_wind_simulation = true;

            if (v_current_preset_mode == SC10_PRESET_COUNTRY){
                base_wind_min               = 0.7f;
                base_wind_max               = 3.4f;
                gust_probability_base       = 0.006f;
                location_gust_strength      = 1.35f;
                thermal_bubble_frequency    = 0.015f;
            } else if (v_current_preset_mode == SC10_PRESET_MEDITERRANEAN){
                base_wind_min               = 1.6f;
                base_wind_max               = 3.8f;
                gust_probability_base       = 0.012f;
                location_gust_strength      = 1.55f;
                thermal_bubble_frequency    = 0.035f;
            } else if (v_current_preset_mode == SC10_PRESET_OCEAN){
                base_wind_min               = 1.8f;
                base_wind_max               = 5.5f;
                gust_probability_base       = 0.040f;
                location_gust_strength      = 2.1f;
                thermal_bubble_frequency    = 0.022f;
            } else if (v_current_preset_mode == SC10_PRESET_MOUNTAIN){
                base_wind_min               = 2.2f;
                base_wind_max               = 7.5f;
                gust_probability_base       = 0.045f;
                location_gust_strength      = 2.2f;
                thermal_bubble_frequency    = 0.028f;
            } else if (v_current_preset_mode == SC10_PRESET_PLAINS){
                base_wind_min               = 4.0f;
                base_wind_max               = 8.8f;
                gust_probability_base       = 0.070f;
                location_gust_strength      = 2.4f;
                thermal_bubble_frequency    = 0.018f;
            }

            SC10_startWindSimulation();
        }
    }
    
    void SC10_startWindSimulation(void) {
        float v_mid_range = (base_wind_min + base_wind_max) * 0.5f; // v_로 시작하는 지역 변수
        current_wind_speed = v_mid_range;
        target_wind_speed = v_mid_range;
        
        wind_momentum = 0.0f;
        spectral_energy_buffer = 0.0f;
        spectral_phase_accumulator = 0.0f;

        current_weather_phase = SC10_WEATHER_PHASE_NORMAL;
        phase_start_time = millis() / 1000.0f;
        phase_duration = 120.0f;
        
        float v_range_span = base_wind_max - base_wind_min; // v_로 시작하는 지역 변수
        phase_wind_min = base_wind_min + (v_range_span * 0.15f);
        phase_wind_max = base_wind_min + (v_range_span * 0.85f);
        
        wind_simulation_active = true;
        SC10_generateWindTarget();
    }

    void SC10_calculateVonKarmanTurbulence(float p_dt) { // p_로 시작하는 함수 파라미터
        if (!wind_simulation_active) return;
        
        float v_L = g_SC10_config.turbulence_length_scale; // v_로 시작하는 지역 변수
        float v_sigma = g_SC10_config.turbulence_intensity_sigma;
        float v_U = current_wind_speed;
        
        if (v_U < 0.1f) v_U = 0.1f;
        
        float v_turbulence_sum = 0.0f;
        
        for (int v_i = 1; v_i <= 12; v_i++) {
            float v_n = v_i * 0.1f; 
            float v_f = v_n * v_U / v_L; 
            
            float v_fL_over_U = v_f * v_L / v_U;
            float v_term = 70.8f * v_fL_over_U * v_fL_over_U;
            float v_numerator = 4.0f * v_sigma * v_sigma * (v_L / v_U) * (1.0f + v_term);
            float v_denominator = pow(1.0f + v_term, 5.0f/6.0f);
            float v_spectral_density = v_numerator / v_denominator;
            
            float v_phase_rate = 2.0f * M_PI * v_f;
            float v_phase_increment = v_phase_rate * p_dt;
            float v_phase_noise = SC10_getRandomFloat(-0.1f, 0.1f);
            float v_current_phase = spectral_phase_accumulator * v_i + v_phase_increment + v_phase_noise;
            
            float v_amplitude = sqrt(2.0f * v_spectral_density * 0.083f);
            float v_component = v_amplitude * sin(v_current_phase);
            
            v_turbulence_sum += v_component;
        }
        
        spectral_phase_accumulator += p_dt * 0.5f;
        if (spectral_phase_accumulator > 2.0f * M_PI) {
            spectral_phase_accumulator -= 2.0f * M_PI;
        }
        
        float v_correlation_factor = exp(-p_dt / turbulence_time_scale);
        spectral_energy_buffer = spectral_energy_buffer * v_correlation_factor + 
                                       v_turbulence_sum * (1.0f - v_correlation_factor);
    }
    
    void SC10_calculateThermalBubble(void) {
        if (!wind_simulation_active) return;
        current_thermal_contribution = 0.0f;
        
        if (thermal_bubble_active) {
            float v_current_time = millis() / 1000.0f; // v_로 시작하는 지역 변수
            float v_bubble_age = v_current_time - thermal_bubble_start_time;
            
            if (v_bubble_age >= thermal_bubble_duration) {
                thermal_bubble_active = false;
                return;
            }
            
            float v_progress = v_bubble_age / thermal_bubble_duration;
            float v_envelope;
            
            if (v_progress < 0.2f) { v_envelope = 1.0f - pow(1.0f - v_progress / 0.2f, 2.0f);
            } else if (v_progress < 0.6f) { v_envelope = 1.0f; float v_osc_freq = 0.8f + (current_weather_phase * 0.2f);
              float v_variation = sin(v_bubble_age * v_osc_freq * 2.0f * M_PI) * 0.15f; v_envelope += v_variation;
            } else { float v_decay_progress = (v_progress - 0.6f) / 0.4f; v_envelope = 1.0f - pow(v_decay_progress, 1.3f); }
            
            float v_thermal_strength = g_SC10_config.thermal_bubble_strength * v_envelope;
            current_thermal_contribution = v_thermal_strength - 1.0f;
        }
    }

    void SC10_updateGustState(void) {
        if (!wind_simulation_active) return;
        float v_current_time = millis() / 1000.0f; // v_로 시작하는 지역 변수
        if (gust_active) {
            float v_gust_age = v_current_time - gust_start_time;
            if (v_gust_age >= gust_duration) {
                gust_active = false; gust_intensity = 1.0f;
            } else {
                float v_progress = v_gust_age / gust_duration;
                float v_envelope;
                if (v_progress < 0.25f) { v_envelope = 1.0f - pow(1.0f - v_progress / 0.25f, 1.8f);
                } else if (v_progress < 0.65f) { v_envelope = 1.0f; float v_osc_freq = 1.5f + (current_weather_phase * 0.5f);
                  v_envelope += sin(v_gust_age * v_osc_freq) * 0.08f;
                } else { float v_decay_progress = (v_progress - 0.65f) / 0.35f; v_envelope = 1.0f - pow(v_decay_progress, 1.5f); }
                gust_intensity = 1.0f + (location_gust_strength - 1.0f) * v_envelope;
            }
            return;
        }
        if (v_current_time - last_gust_check * 0.001f >= 1.5f) {
            last_gust_check = millis();
            float v_base_prob = gust_probability_base;
            float v_user_freq = g_SC10_config.gust_frequency / 100.0f;
            float v_wind_speed_factor = 1.0f + (current_wind_speed / 8.9f) * 0.5f;
            float v_phase_multiplier;
            if (current_weather_phase == SC10_WEATHER_PHASE_CALM) v_phase_multiplier = 0.3f * v_wind_speed_factor;
            else if (current_weather_phase == SC10_WEATHER_PHASE_STRONG) v_phase_multiplier = 2.2f * v_wind_speed_factor;
            else v_phase_multiplier = 0.9f * v_wind_speed_factor; // SC10_WEATHER_PHASE_NORMAL
            float v_final_prob = v_base_prob * v_user_freq * v_phase_multiplier;
            if (SC10_getRandomFloat() < v_final_prob) {
                gust_active = true; gust_start_time = v_current_time;
                float v_duration_base, v_intensity_base; float v_speed_factor = current_wind_speed / 6.7f;
                if (current_weather_phase == SC10_WEATHER_PHASE_CALM) { v_duration_base = SC10_getRandomFloat(3.0f, 8.0f);
                  v_intensity_base = SC10_getRandomFloat(1.08f, 1.33f);
                } else if (current_weather_phase == SC10_WEATHER_PHASE_STRONG) { v_duration_base = SC10_getRandomFloat(0.8f, 3.3f);
                  v_intensity_base = SC10_getRandomFloat(1.3f, 1.3f + 0.9f * (1.0f + v_speed_factor * 0.3f));
                } else { v_duration_base = SC10_getRandomFloat(1.8f, 5.8f);
                  v_intensity_base = SC10_getRandomFloat(1.15f, 1.15f + 0.5f * (1.0f + v_speed_factor * 0.2f));
                }
                gust_duration = v_duration_base;
                gust_intensity = fmin(v_intensity_base, location_gust_strength);
            }
        }
    }

    void SC10_updateThermalBubbleCheck(void) {
        if (!wind_simulation_active) return;
        if (thermal_bubble_active) return;
        float v_current_time = millis() / 1000.0f; // v_로 시작하는 지역 변수
        if (v_current_time - last_thermal_check * 0.001f < 2.0f) return;
        last_thermal_check = millis();
        float v_base_prob = thermal_bubble_frequency;
        float v_wind_speed_factor = 1.0f + (current_wind_speed / 8.0f) * 0.3f;
        float v_phase_multiplier;
        if (current_weather_phase == SC10_WEATHER_PHASE_CALM) v_phase_multiplier = 1.2f;
        else if (current_weather_phase == SC10_WEATHER_PHASE_STRONG) v_phase_multiplier = 0.7f;
        else v_phase_multiplier = 1.0f; // SC10_WEATHER_PHASE_NORMAL
        float v_final_prob = v_base_prob * v_wind_speed_factor * v_phase_multiplier;
        if (SC10_getRandomFloat() < v_final_prob) {
            thermal_bubble_active = true; thermal_bubble_start_time = v_current_time;
            float v_duration_base = SC10_getRandomFloat(8.0f, 14.0f);
            if (current_weather_phase == SC10_WEATHER_PHASE_CALM) v_duration_base *= 1.3f;
            else if (current_weather_phase == SC10_WEATHER_PHASE_STRONG) v_duration_base *= 0.8f;
            thermal_bubble_duration = v_duration_base;
        }
    }

    void SC10_generateWindTarget(void) {
        if (!wind_simulation_active) return;
        float v_range = phase_wind_max - phase_wind_min; // v_로 시작하는 지역 변수
        float v_new_target = phase_wind_min + (SC10_getRandomFloat() * v_range);
        float v_mid_point = (phase_wind_min + phase_wind_max) * 0.5f;
        float v_bias_factor = SC10_getRandomFloat(0.0f, 1.0f);
        v_new_target = (v_new_target + v_mid_point * v_bias_factor) / (1.0f + v_bias_factor);
        target_wind_speed = v_new_target;
        
        float v_variability = g_SC10_config.wind_variability / 100.0f;
        float v_base_rate;
        if (current_weather_phase == SC10_WEATHER_PHASE_CALM) v_base_rate = 0.08f + (v_variability * 0.12f);
        else if (current_weather_phase == SC10_WEATHER_PHASE_STRONG) v_base_rate = 0.25f + (v_variability * 0.35f);
        else v_base_rate = 0.15f + (v_variability * 0.25f); // SC10_WEATHER_PHASE_NORMAL
        
        float v_U = current_wind_speed;
        if (v_U < 0.1f) v_U = 0.1f;
        float v_time_scale_factor = g_SC10_config.turbulence_length_scale / v_U;
        v_base_rate *= (1.0f + v_time_scale_factor * 0.1f);
        wind_change_rate = v_base_rate * SC10_getRandomFloat(0.7f, 1.7f); 
    }
    
    void SC10_updateWeatherPhase(void) {
        if (!wind_simulation_active) return;
        float v_current_time = millis() / 1000.0f; // v_로 시작하는 지역 변수
        if (v_current_time - phase_start_time >= phase_duration) {
            SC10_WindWeatherPhase_t v_old_phase = current_weather_phase; // v_로 시작하는 지역 변수
            float v_random_val = SC10_getRandomFloat();
            
            // 다음 phase 결정 로직 
            if (v_old_phase == SC10_WEATHER_PHASE_CALM) { if (v_random_val < 0.6f) current_weather_phase = SC10_WEATHER_PHASE_NORMAL; else current_weather_phase = SC10_WEATHER_PHASE_STRONG;
            } else if (v_old_phase == SC10_WEATHER_PHASE_NORMAL) { if (v_random_val < 0.3f) current_weather_phase = SC10_WEATHER_PHASE_CALM;
            else if (v_random_val < 0.7f) current_weather_phase = SC10_WEATHER_PHASE_NORMAL; else current_weather_phase = SC10_WEATHER_PHASE_STRONG;
            } else { if (v_random_val < 0.4f) current_weather_phase = SC10_WEATHER_PHASE_NORMAL; else if (v_random_val < 0.7f) current_weather_phase = SC10_WEATHER_PHASE_CALM;
            else current_weather_phase = SC10_WEATHER_PHASE_STRONG; }
            
            if (current_weather_phase == SC10_WEATHER_PHASE_CALM) phase_duration = SC10_getRandomFloat(90.0f, 210.0f);
            else if (current_weather_phase == SC10_WEATHER_PHASE_NORMAL) phase_duration = SC10_getRandomFloat(120.0f, 300.0f);
            else phase_duration = SC10_getRandomFloat(60.0f, 150.0f); // SC10_WEATHER_PHASE_STRONG
            
            phase_start_time = v_current_time;
            float v_range_span = base_wind_max - base_wind_min;
            
            if (current_weather_phase == SC10_WEATHER_PHASE_CALM) { phase_wind_min = base_wind_min; phase_wind_max = base_wind_min + (v_range_span * 0.6f);
            } else if (current_weather_phase == SC10_WEATHER_PHASE_NORMAL) { phase_wind_min = base_wind_min + (v_range_span * 0.15f);
            phase_wind_max = base_wind_min + (v_range_span * 0.85f);
            } else { phase_wind_min = base_wind_min + (v_range_span * 0.4f); phase_wind_max = base_wind_max; } // SC10_WEATHER_PHASE_STRONG
            
            phase_wind_min = fmax(0.2f, phase_wind_min);
            phase_wind_max = fmin(11.0f, phase_wind_max);
            SC10_generateWindTarget();
        }
    }

    void SC10_calculateWindSimulation(void) {
        if (!wind_simulation_active) return;
        unsigned long v_now = millis(); // v_로 시작하는 지역 변수
        if (v_now - last_wind_sim_update < g_SC10_config.wind_sim_interval_ms + (esp_random() % 300)) return;
        float v_dt = (float)(v_now - last_wind_sim_update) / 1000.0f; // v_로 시작하는 지역 변수
        last_wind_sim_update = v_now;
        
        SC10_updateWeatherPhase();
        
        float v_target = target_wind_speed; // v_로 시작하는 지역 변수
        float v_current = current_wind_speed;
        float v_change_rate = wind_change_rate;
        
        float v_difference = v_target - v_current;
        float v_change = v_difference * v_change_rate * v_dt; 
        
        wind_momentum = wind_momentum * 0.85f + v_change * 0.15f;
        wind_momentum = fmax(-0.5f, fmin(0.5f, wind_momentum));
        v_change = wind_momentum;
        
        SC10_calculateVonKarmanTurbulence(v_dt);
        SC10_calculateThermalBubble();
        
        float v_von_karman_turb = spectral_energy_buffer;
        float v_thermal_contribution = current_thermal_contribution;
        
        float v_new_speed = v_current + v_change + v_von_karman_turb;
        v_new_speed = fmax(0.2f, fmin(11.0f, v_new_speed));
        current_wind_speed = v_new_speed;
        
        float v_change_threshold = 0.5f + (v_current / 20.0f);
        float v_close_change_chance = 30.0f; 
        float v_far_change_chance = 6.0f; 
        
        if (g_SC10_config.wind_variability > 70.0f) {
             v_close_change_chance *= 1.5f; v_far_change_chance *= 1.5f;
        }

        if (fabs(v_difference) < v_change_threshold) {
             if (SC10_getRandomFloat(0.0f, 100.0f) < v_close_change_chance) {
                 SC10_generateWindTarget();
             }
        } else if (SC10_getRandomFloat(0.0f, 100.0f) < v_far_change_chance) {
             SC10_generateWindTarget();
        }

        float v_fan_speed_percent = current_wind_speed * 10.0f + 10.0f; 
        v_fan_speed_percent *= gust_intensity; 
        v_fan_speed_percent += v_thermal_contribution * 5.0f; 
        
        SC10_applyFanSpeed(v_fan_speed_percent);
        SC10_updateGustState();
        SC10_updateThermalBubbleCheck();

        yield(); 
    }

    // --- 5. 메인 루프 실행 함수 (SC10_run() 함수 추가) ---
    void SC10_run(void) {
        SC10_calculateWindSimulation();

    }
};
