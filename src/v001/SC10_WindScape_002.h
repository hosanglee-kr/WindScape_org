// SC10_WindScape_002.h
#pragma once 


#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h> 
#include <FS.h>
#include <LittleFS.h>
#include <cmath>
#include <cstdlib>
#include <string>
#include <map>

// ====================================================================================
// 전역 상수 (파일 경로 등)
// ====================================================================================
const char* G_SC10_CONFIG_FILE_PATH     = "/json/config_002.json";
const char* G_SC10_CONFIG_PAGE_PATH     = "/html/SC10_main_001.html"; // 웹 설정 페이지 HTML 파일 경로

// 난수 생성 함수 (0.0f ~ 1.0f)
float SC10_getRandomFloat(void) {
    return (float)esp_random() / 4294967295.0f; // UINT32_MAX
}

float SC10_getRandomFloat(float p_min, float p_max) {
    return p_min + (SC10_getRandomFloat() * (p_max - p_min));
}

// ====================================================================================
// 전역 변수
// ====================================================================================

// Web Server 인스턴스 (포트 80)
AsyncWebServer g_SC10_asyncWeb(80);

// ====================================================================================
// WindScape Configuration Structure (상수 및 설정 통합)
// ====================================================================================
struct WindConfig {
    // --- 1. 하드웨어/시스템 상수 (JSON 관리) ---
    int fan_pwm_pin                 = 14;     // 팬 PWM 출력 핀 (GPIO14)
    int fan_tach_pin                = 27;     // 팬 RPM 입력 핀 (GPIO27)
    int pwm_frequency               = 25000;  // 25kHz
    int pwm_channel                 = 0;
    int pwm_resolution              = 10;     // 0-1023 (10비트)
    char wifi_ssid[32]              = "WindScape_AP";
    char wifi_password[32]          = "wind1234";
    int wind_sim_interval_ms        = 250;
    int gust_check_interval_ms      = 500;
    int thermal_check_interval_ms   = 2000;
    
    // --- 2. 시뮬레이션 설정 (JSON 관리) ---
    float wind_intensity            = 100.0f;
    float gust_frequency            = 30.0f;
    float wind_variability          = 40.0f;
    float fan_speed_limit           = 80.0f;
    float minimum_fan_speed         = 0.0f;
    float turbulence_length_scale   = 30.0f;
    float turbulence_intensity_sigma= 0.3f;
    float thermal_bubble_strength   = 1.8f;
    float thermal_bubble_radius     = 15.0f; 
    char preset_mode[20]            = "Ocean";
};

WindConfig g_SC10_config;

// ====================================================================================
// WindScape Simulator Class
// ====================================================================================

class WindScapeSimulator {
private:
    // --- 유틸리티 함수 ---
    // LittleFS에서 HTML 파일 읽기
    String SC10_readHtmlConfigPage(const char* p_path) {
        if (!LittleFS.begin()) {
            Serial.println("LittleFS not mounted to read HTML.");
            return "";
        }
        File v_file = LittleFS.open(p_path, "r");
        if (!v_file) {
            Serial.printf("Failed to open file: %s\n", p_path);
            return "";
        }
        String v_content = v_file.readString();
        v_file.close();
        return v_content;
    }

public:
    // --- 1. 상태 변수 (클래스 멤버) ---
    bool fan_power_enabled              = true;
    bool enable_wind_simulation         = true;
    bool wind_simulation_active         = false;

    // Wind Dynamics
    float current_wind_speed            = 3.6f; // m/s
    float target_wind_speed             = 3.6f; // m/s
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
    int current_weather_phase           = 1;
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

        // --- 1. 하드웨어/시스템 상수 로드 ---
        g_SC10_config.fan_pwm_pin       = v_doc["hw"]["pwm_pin"] | 14;
        g_SC10_config.pwm_frequency     = v_doc["hw"]["pwm_freq"] | 25000;
        g_SC10_config.pwm_resolution    = v_doc["hw"]["pwm_res"] | 10;
        const char* v_ssid = v_doc["wifi"]["ssid"] | "WindScape_AP";
        strncpy(g_SC10_config.wifi_ssid, v_ssid, sizeof(g_SC10_config.wifi_ssid) - 1);
        const char* v_pass = v_doc["wifi"]["pass"] | "wind1234";
        strncpy(g_SC10_config.wifi_password, v_pass, sizeof(g_SC10_config.wifi_password) - 1);
        
        g_SC10_config.wind_sim_interval_ms      = v_doc["timing"]["sim_int"] | 250;
        g_SC10_config.gust_check_interval_ms    = v_doc["timing"]["gust_int"] | 500;

        // --- 2. 시뮬레이션 설정 로드 ---
        g_SC10_config.wind_intensity            = v_doc["sim"]["intensity"] | g_SC10_config.wind_intensity;
        g_SC10_config.gust_frequency            = v_doc["sim"]["gust_freq"] | g_SC10_config.gust_frequency;
        g_SC10_config.wind_variability          = v_doc["sim"]["variability"] | g_SC10_config.wind_variability;
        g_SC10_config.fan_speed_limit           = v_doc["sim"]["fan_limit"] | g_SC10_config.fan_speed_limit;
        g_SC10_config.minimum_fan_speed         = v_doc["sim"]["min_fan"] | g_SC10_config.minimum_fan_speed;
        g_SC10_config.turbulence_length_scale   = v_doc["sim"]["turb_len"].as<float>() | g_SC10_config.turbulence_length_scale;
        g_SC10_config.turbulence_intensity_sigma= v_doc["sim"]["turb_sig"] | g_SC10_config.turbulence_intensity_sigma;
        g_SC10_config.thermal_bubble_strength   = v_doc["sim"]["therm_str"] | g_SC10_config.thermal_bubble_strength;
        g_SC10_config.thermal_bubble_radius     = v_doc["sim"]["therm_rad"] | g_SC10_config.thermal_bubble_radius;
        
        const char* v_preset = v_doc["sim"]["preset"] | "Ocean";
        strncpy(g_SC10_config.preset_mode, v_preset, sizeof(g_SC10_config.preset_mode) - 1);
        
        Serial.println("Config loaded successfully.");
        return true;
    }

    bool SC10_saveConfig(void) {
        JsonDocument v_doc;

        // --- 1. 하드웨어/시스템 상수 저장 ---
        v_doc["hw"]["pwm_pin"]      = g_SC10_config.fan_pwm_pin;
        v_doc["hw"]["pwm_freq"]     = g_SC10_config.pwm_frequency;
        v_doc["hw"]["pwm_res"]      = g_SC10_config.pwm_resolution;
        v_doc["wifi"]["ssid"]       = g_SC10_config.wifi_ssid;
        v_doc["wifi"]["pass"]       = g_SC10_config.wifi_password;
        v_doc["timing"]["sim_int"]  = g_SC10_config.wind_sim_interval_ms;
        v_doc["timing"]["gust_int"] = g_SC10_config.gust_check_interval_ms;

        // --- 2. 시뮬레이션 설정 저장 ---
        v_doc["sim"]["intensity"]   = g_SC10_config.wind_intensity;
        v_doc["sim"]["gust_freq"]   = g_SC10_config.gust_frequency;
        v_doc["sim"]["variability"] = g_SC10_config.wind_variability;
        v_doc["sim"]["fan_limit"]   = g_SC10_config.fan_speed_limit;
        v_doc["sim"]["min_fan"]     = g_SC10_config.minimum_fan_speed;
        v_doc["sim"]["turb_len"]    = g_SC10_config.turbulence_length_scale;
        v_doc["sim"]["turb_sig"]    = g_SC10_config.turbulence_intensity_sigma;
        v_doc["sim"]["therm_str"]   = g_SC10_config.thermal_bubble_strength;
        v_doc["sim"]["therm_rad"]   = g_SC10_config.thermal_bubble_radius;
        v_doc["sim"]["preset"]      = g_SC10_config.preset_mode;

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

    // --- 3. 초기화 및 설정 ---

    void SC10_init(void) {
        Serial.println("\nWindScape Simulator Starting...");

        // 1. 파일 시스템 및 설정 로드
        SC10_loadConfig();
        
        // 2. 팬 PWM 설정 (로드된 설정값 사용)
        ledcSetup(g_SC10_config.pwm_channel, g_SC10_config.pwm_frequency, g_SC10_config.pwm_resolution);
        ledcAttachPin(g_SC10_config.fan_pwm_pin, g_SC10_config.pwm_channel);
        pinMode(g_SC10_config.fan_tach_pin, INPUT_PULLUP);
        
        float v_initial_speed = g_SC10_config.minimum_fan_speed + 12.0f;
        if (fan_power_enabled) {
            SC10_applyFanSpeed(v_initial_speed); 
        } else {
            ledcWrite(g_SC10_config.pwm_channel, 0);
        }

        // 3. Wi-Fi AP 모드 설정 (로드된 설정값 사용)
        Serial.printf("Setting up Access Point: %s\n", g_SC10_config.wifi_ssid);
        WiFi.softAP(g_SC10_config.wifi_ssid, g_SC10_config.wifi_password);
        Serial.printf("AP IP address: %s\n", WiFi.softAPIP().toString().c_str());

        // 4. Web Server 초기화
        SC10_setupWebServer();

        // 5. 시뮬레이션 시작
        srand(esp_random()); 
        SC10_applyCurrentPreset(true); 
    }

    void SC10_setupWebServer(void) {
        String v_ap_ip = WiFi.softAPIP().toString();
        
        // 루트 페이지 (설정 UI) - HTML 파일을 LittleFS에서 읽어옴
        g_SC10_asyncWeb.on("/", HTTP_GET, [this, v_ap_ip](AsyncWebServerRequest *p_request){
            
            // HTML 템플릿 로드 (LittleFS 파일 사용)
            String v_html_template = SC10_readHtmlConfigPage(G_SC10_CONFIG_PAGE_PATH);
            if (v_html_template.length() == 0) {
                 p_request->send(500, "text/plain", "Error loading config_page.html");
                 return;
            }

            String v_state = wind_simulation_active ? "ON" : "OFF (Steady)";
            
            // snprintf를 사용하여 최종 HTML 생성 (float 값 형식 지정)
            char v_buffer[4096]; // 충분한 크기로 확장
            snprintf(v_buffer, sizeof(v_buffer), v_html_template.c_str(), 
                     v_state.c_str(), current_weather_phase, current_wind_speed, ledcRead(g_SC10_config.pwm_channel),
                     v_ap_ip.c_str(),
                     g_SC10_config.wind_intensity, g_SC10_config.gust_frequency, g_SC10_config.wind_variability, 
                     g_SC10_config.fan_speed_limit, g_SC10_config.minimum_fan_speed,
                     g_SC10_config.thermal_bubble_strength, g_SC10_config.thermal_bubble_radius, g_SC10_config.turbulence_length_scale, g_SC10_config.turbulence_intensity_sigma, 
                     strcmp(g_SC10_config.preset_mode, "Ocean") == 0 ? "selected" : "",
                     strcmp(g_SC10_config.preset_mode, "Plains") == 0 ? "selected" : "",
                     strcmp(g_SC10_config.preset_mode, "Mountain") == 0 ? "selected" : "",
                     strcmp(g_SC10_config.preset_mode, "Countryside") == 0 ? "selected" : "",
                     strcmp(g_SC10_config.preset_mode, "Mediterranean") == 0 ? "selected" : "",
                     strcmp(g_SC10_config.preset_mode, "Off") == 0 ? "selected" : ""
                     );

            p_request->send(200, "text/html", v_buffer);
        });

        // 설정 저장 API (POST)
        g_SC10_asyncWeb.on("/set_config", HTTP_POST, [this](AsyncWebServerRequest *p_request){
            bool v_changesMade = false;
            
            for(int v_i=0; v_i<p_request->args(); v_i++){
                String v_name = p_request->argName(v_i);
                String v_value = p_request->arg(v_i);

                // 명명 규칙 적용 및 파라미터 업데이트
                if (v_name == "intensity") {
                    g_SC10_config.wind_intensity = v_value.toFloat(); v_changesMade = true;
                } else if (v_name == "gust_freq") {
                    g_SC10_config.gust_frequency = v_value.toFloat(); v_changesMade = true;
                } else if (v_name == "variability") {
                    g_SC10_config.wind_variability = v_value.toFloat(); v_changesMade = true;
                } else if (v_name == "fan_limit") {
                    g_SC10_config.fan_speed_limit = v_value.toFloat(); v_changesMade = true;
                } else if (v_name == "min_fan") {
                    g_SC10_config.minimum_fan_speed = v_value.toFloat(); v_changesMade = true;
                } else if (v_name == "turb_len") {
                    g_SC10_config.turbulence_length_scale = v_value.toFloat(); v_changesMade = true;
                } else if (v_name == "turb_sig") {
                    g_SC10_config.turbulence_intensity_sigma = v_value.toFloat(); v_changesMade = true;
                } else if (v_name == "therm_str") {
                    g_SC10_config.thermal_bubble_strength = v_value.toFloat(); v_changesMade = true;
                } else if (v_name == "therm_rad") {
                    g_SC10_config.thermal_bubble_radius = v_value.toFloat(); v_changesMade = true;
                } else if (v_name == "preset") {
                    strncpy(g_SC10_config.preset_mode, v_value.c_str(), sizeof(g_SC10_config.preset_mode) - 1);
                    g_SC10_config.preset_mode[sizeof(g_SC10_config.preset_mode) - 1] = '\0';
                    v_changesMade = true;
                }
            }

            if (v_changesMade) {
                SC10_saveConfig();
                SC10_applyCurrentPreset(true);
            }
            
            p_request->redirect("/");
        });

        // JSON API (현재 상태 및 설정)
        g_SC10_asyncWeb.on("/api/state", HTTP_GET, [this](AsyncWebServerRequest *p_request){

            JsonDocument v_doc;
            v_doc["wind_speed"] = current_wind_speed;
            v_doc["phase"] = current_weather_phase;
            v_doc["sim_active"] = wind_simulation_active;
            v_doc["fan_pwm"] = ledcRead(g_SC10_config.pwm_channel);
            v_doc["intensity"] = g_SC10_config.wind_intensity;
            v_doc["preset"] = g_SC10_config.preset_mode;

            String v_response;
            serializeJson(v_doc, v_response);
            p_request->send(200, "application/json", v_response);
        });

        g_SC10_asyncWeb.begin();
    }
    
    // --- 4. 시뮬레이션 엔진 로직 (명명 규칙 적용) ---

    void SC10_applyFanSpeed(float p_speed_percent) {
        if (!fan_power_enabled) {
            ledcWrite(g_SC10_config.pwm_channel, 0);
            return;
        }

        float v_requested_speed = p_speed_percent / 100.0f;
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
        
        int v_pwm_level = (int)(v_requested_speed * (pow(2, g_SC10_config.pwm_resolution) - 1));

        if (v_requested_speed <= 0.01f) {
            ledcWrite(g_SC10_config.pwm_channel, 0);
        } else {
            ledcWrite(g_SC10_config.pwm_channel, v_pwm_level);
        }
    }

    void SC10_applyCurrentPreset(bool p_force_apply) {
        // ... (내부 로직은 명명 규칙만 적용하고, 구조 변경 없이 유지) ...
        wind_simulation_active = false;
        gust_active = false;
        thermal_bubble_active = false;
        gust_intensity = 1.0f;
        
        String v_current_preset_mode = g_SC10_config.preset_mode;
        
        if (v_current_preset_mode == "Off") {
            enable_wind_simulation = false;
            float v_steady_fan_pct = (g_SC10_config.minimum_fan_speed > 0.0f) ? g_SC10_config.minimum_fan_speed : 12.0f;
            SC10_applyFanSpeed(v_steady_fan_pct);
            current_wind_speed = 0.5f;
            target_wind_speed = 0.5f;

        } else {
            enable_wind_simulation = true;
            if (v_current_preset_mode == "Countryside") {
              base_wind_min = 0.7f; base_wind_max = 3.4f;
              gust_probability_base = 0.006f; location_gust_strength = 1.35f; thermal_bubble_frequency = 0.015f;
            } else if (v_current_preset_mode == "Mediterranean") {
              base_wind_min = 1.6f; base_wind_max = 3.8f;
              gust_probability_base = 0.012f; location_gust_strength = 1.55f; thermal_bubble_frequency = 0.035f;
            } else if (v_current_preset_mode == "Ocean") {
              base_wind_min = 1.8f; base_wind_max = 5.5f;
              gust_probability_base = 0.040f; location_gust_strength = 2.1f; thermal_bubble_frequency = 0.022f;
            } else if (v_current_preset_mode == "Mountain") {
              base_wind_min = 2.2f; base_wind_max = 7.5f;
              gust_probability_base = 0.045f; location_gust_strength = 2.2f; thermal_bubble_frequency = 0.028f;
            } else if (v_current_preset_mode == "Plains") {
              base_wind_min = 4.0f; base_wind_max = 8.8f;
              gust_probability_base = 0.070f; location_gust_strength = 2.4f; thermal_bubble_frequency = 0.018f;
            }
            
            SC10_startWindSimulation();
        }
    }
    
    void SC10_startWindSimulation(void) {
        // ... (내부 로직은 명명 규칙만 적용하고, 구조 변경 없이 유지) ...
        float v_mid_range = (base_wind_min + base_wind_max) * 0.5f;
        current_wind_speed = v_mid_range;
        target_wind_speed = v_mid_range;
        
        wind_momentum = 0.0f;
        spectral_energy_buffer = 0.0f;
        spectral_phase_accumulator = 0.0f;

        current_weather_phase = 1;
        phase_start_time = millis() / 1000.0f;
        phase_duration = 120.0f;
        
        float v_range_span = base_wind_max - base_wind_min;
        phase_wind_min = base_wind_min + (v_range_span * 0.15f);
        phase_wind_max = base_wind_min + (v_range_span * 0.85f);
        
        wind_simulation_active = true;
        SC10_generateWindTarget();
    }

    void SC10_calculateVonKarmanTurbulence(float p_dt) {
        if (!wind_simulation_active) return;
        
        float v_L = g_SC10_config.turbulence_length_scale;
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
            float v_current_time = millis() / 1000.0f;
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
        float v_current_time = millis() / 1000.0f;
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
            if (current_weather_phase == 0) v_phase_multiplier = 0.3f * v_wind_speed_factor;
            else if (current_weather_phase == 2) v_phase_multiplier = 2.2f * v_wind_speed_factor;
            else v_phase_multiplier = 0.9f * v_wind_speed_factor;
            float v_final_prob = v_base_prob * v_user_freq * v_phase_multiplier;
            if (SC10_getRandomFloat() < v_final_prob) {
                gust_active = true; gust_start_time = v_current_time;
                float v_duration_base, v_intensity_base; float v_speed_factor = current_wind_speed / 6.7f;
                if (current_weather_phase == 0) { v_duration_base = SC10_getRandomFloat(3.0f, 8.0f);
                  v_intensity_base = SC10_getRandomFloat(1.08f, 1.33f);
                } else if (current_weather_phase == 2) { v_duration_base = SC10_getRandomFloat(0.8f, 3.3f);
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
        float v_current_time = millis() / 1000.0f;
        if (v_current_time - last_thermal_check * 0.001f < 2.0f) return;
        last_thermal_check = millis();
        float v_base_prob = thermal_bubble_frequency;
        float v_wind_speed_factor = 1.0f + (current_wind_speed / 8.0f) * 0.3f;
        float v_phase_multiplier;
        if (current_weather_phase == 0) v_phase_multiplier = 1.2f;
        else if (current_weather_phase == 2) v_phase_multiplier = 0.7f;
        else v_phase_multiplier = 1.0f;
        float v_final_prob = v_base_prob * v_wind_speed_factor * v_phase_multiplier;
        if (SC10_getRandomFloat() < v_final_prob) {
            thermal_bubble_active = true; thermal_bubble_start_time = v_current_time;
            float v_duration_base = SC10_getRandomFloat(8.0f, 14.0f);
            if (current_weather_phase == 0) v_duration_base *= 1.3f;
            else if (current_weather_phase == 2) v_duration_base *= 0.8f;
            thermal_bubble_duration = v_duration_base;
        }
    }

    void SC10_generateWindTarget(void) {
        if (!wind_simulation_active) return;
        float v_range = phase_wind_max - phase_wind_min;
        float v_new_target = phase_wind_min + (SC10_getRandomFloat() * v_range);
        float v_mid_point = (phase_wind_min + phase_wind_max) * 0.5f;
        float v_bias_factor = SC10_getRandomFloat(0.0f, 1.0f);
        v_new_target = (v_new_target + v_mid_point * v_bias_factor) / (1.0f + v_bias_factor);
        target_wind_speed = v_new_target;
        
        float v_variability = g_SC10_config.wind_variability / 100.0f;
        float v_base_rate;
        if (current_weather_phase == 0) v_base_rate = 0.08f + (v_variability * 0.12f);
        else if (current_weather_phase == 2) v_base_rate = 0.25f + (v_variability * 0.35f);
        else v_base_rate = 0.15f + (v_variability * 0.25f);
        
        float v_U = current_wind_speed;
        if (v_U < 0.1f) v_U = 0.1f;
        float v_time_scale_factor = g_SC10_config.turbulence_length_scale / v_U;
        v_base_rate *= (1.0f + v_time_scale_factor * 0.1f);
        wind_change_rate = v_base_rate * SC10_getRandomFloat(0.7f, 1.7f); 
    }
    
    void SC10_updateWeatherPhase(void) {
        if (!wind_simulation_active) return;
        float v_current_time = millis() / 1000.0f;
        if (v_current_time - phase_start_time >= phase_duration) {
            int v_old_phase = current_weather_phase;
            float v_random_val = SC10_getRandomFloat();
            if (v_old_phase == 0) { if (v_random_val < 0.6f) current_weather_phase = 1; else current_weather_phase = 2;
            } else if (v_old_phase == 1) { if (v_random_val < 0.3f) current_weather_phase = 0;
            else if (v_random_val < 0.7f) current_weather_phase = 1; else current_weather_phase = 2;
            } else { if (v_random_val < 0.4f) current_weather_phase = 1; else if (v_random_val < 0.7f) current_weather_phase = 0;
            else current_weather_phase = 2; }
            if (current_weather_phase == 0) phase_duration = SC10_getRandomFloat(90.0f, 210.0f);
            else if (current_weather_phase == 1) phase_duration = SC10_getRandomFloat(120.0f, 300.0f);
            else phase_duration = SC10_getRandomFloat(60.0f, 150.0f);
            phase_start_time = v_current_time;
            float v_range_span = base_wind_max - base_wind_min;
            if (current_weather_phase == 0) { phase_wind_min = base_wind_min; phase_wind_max = base_wind_min + (v_range_span * 0.6f);
            } else if (current_weather_phase == 1) { phase_wind_min = base_wind_min + (v_range_span * 0.15f);
            phase_wind_max = base_wind_min + (v_range_span * 0.85f);
            } else { phase_wind_min = base_wind_min + (v_range_span * 0.4f); phase_wind_max = base_wind_max; }
            phase_wind_min = fmax(0.2f, phase_wind_min);
            phase_wind_max = fmin(11.0f, phase_wind_max);
            SC10_generateWindTarget();
        }
    }

    void SC10_calculateWindSimulation(void) {
        if (!wind_simulation_active) return;
        unsigned long v_now = millis();
        if (v_now - last_wind_sim_update < g_SC10_config.wind_sim_interval_ms + (esp_random() % 300)) return;
        float v_dt = (float)(v_now - last_wind_sim_update) / 1000.0f;
        last_wind_sim_update = v_now;
        
        SC10_updateWeatherPhase();
        
        float v_target = target_wind_speed;
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

        if (abs(v_difference) < v_change_threshold) {
            if (SC10_getRandomFloat() * 100.0f < v_close_change_chance) {
                SC10_generateWindTarget();
            }
        } else {
            if (SC10_getRandomFloat() * 100.0f < v_far_change_chance) {
                SC10_generateWindTarget();
            }
        }
        
        float v_final_wind = current_wind_speed;
        if (gust_active) {
            v_final_wind *= gust_intensity;
        }
        v_final_wind += v_thermal_contribution;
        
        static float v_continuity_phase = 0.0f;
        v_continuity_phase += 0.1f;
        if (v_continuity_phase > 2.0f * M_PI) v_continuity_phase -= 2.0f * M_PI;
        v_final_wind += sin(v_continuity_phase) * 0.05f;

        v_final_wind = fmax(0.3f, fmin(11.0f, v_final_wind));
        
        float v_wind_ratio = v_final_wind / 11.0f;
        float v_curved_ratio = sqrt(v_wind_ratio);
        float v_fan_percent = 12.0f + (v_curved_ratio * 78.0f);
        v_fan_percent = fmax(12.0f, fmin(90.0f, v_fan_percent));

        SC10_applyFanSpeed(v_fan_percent);
    }

    // --- 5. 메인 루프 실행 ---

    void SC10_run(void) {
        if (!fan_power_enabled) return;
        if (enable_wind_simulation) {
            SC10_calculateWindSimulation();
            
            unsigned long v_now = millis();
            if (v_now - last_gust_check >= g_SC10_config.gust_check_interval_ms) {
                SC10_updateGustState();
            }
            if (v_now - last_thermal_check >= g_SC10_config.thermal_check_interval_ms) {
                SC10_updateThermalBubbleCheck();
            }
        }
    }
};
