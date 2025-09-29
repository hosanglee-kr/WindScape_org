// SC10_WindScape_001.h

#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h> // V7.4.x 사용
#include <FS.h>
#include <LittleFS.h>    // <--- LittleFS 헤더로 변경
#include <cmath>
#include <cstdlib>
#include <string>
#include <map>

// 사용자 정의 상수 및 설정
#define FAN_PWM_PIN       14  // 팬 PWM 출력 핀 (GPIO14)
#define FAN_TACH_PIN      27  // 팬 RPM 입력 핀 (GPIO27) - 현재 코드에서는 사용되지 않음 (향후 PID 제어용)
#define PWM_FREQUENCY     25000 // 25kHz
#define PWM_CHANNEL       0
#define PWM_RESOLUTION    10  // 0-1023 (10비트)

// Wi-Fi 설정 (AP 모드로 독립 실행)
#define WIFI_SSID         "WindScape_AP"
#define WIFI_PASSWORD     "wind1234"
#define CONFIG_FILE_PATH  "/config.json"

// Von Kármán 및 Thermal Convection 기본값 (파일 없을 시 초기값)
#define DEFAULT_WIND_INTENSITY_PCT 100.0f
#define DEFAULT_GUST_FREQUENCY_PCT 30.0f
#define DEFAULT_WIND_VARIABILITY_PCT 40.0f
#define DEFAULT_MAXIMUM_FAN_SPEED_PCT 80.0f
#define DEFAULT_MINIMUM_FAN_SPEED_PCT 0.0f

#define DEFAULT_TURBULENCE_LENGTH_SCALE 30.0f
#define DEFAULT_TURBULENCE_INTENSITY 0.3f
#define DEFAULT_THERMAL_BUBBLE_STRENGTH 1.8f
#define DEFAULT_THERMAL_BUBBLE_RADIUS 15.0f // <--- 보완 사항: Radius 기본값 추가

// 시뮬레이션 업데이트 간격 (ms)
#define WIND_SIM_INTERVAL_MS 250
#define GUST_CHECK_INTERVAL_MS 500
#define THERMAL_CHECK_INTERVAL_MS 2000


// 난수 생성 함수 (0.0f ~ 1.0f)
float getRandomFloat() {
    return (float)esp_random() / 4294967295.0f; // UINT32_MAX
}

float getRandomFloat(float min, float max) {
    return min + (getRandomFloat() * (max - min));
}

// Web Server 인스턴스
AsyncWebServer server(80);

// ====================================================================================
// WindScape Configuration Structure
// ====================================================================================
struct WindConfig {
    float wind_intensity = DEFAULT_WIND_INTENSITY_PCT;
    float gust_frequency = DEFAULT_GUST_FREQUENCY_PCT;
    float wind_variability = DEFAULT_WIND_VARIABILITY_PCT;
    float fan_speed_limit = DEFAULT_MAXIMUM_FAN_SPEED_PCT;
    float minimum_fan_speed = DEFAULT_MINIMUM_FAN_SPEED_PCT;
    float turbulence_length_scale = DEFAULT_TURBULENCE_LENGTH_SCALE;
    float turbulence_intensity_sigma = DEFAULT_TURBULENCE_INTENSITY;
    float thermal_bubble_strength = DEFAULT_THERMAL_BUBBLE_STRENGTH;
    float thermal_bubble_radius = DEFAULT_THERMAL_BUBBLE_RADIUS; // <--- 보완 사항: Radius 추가
    char preset_mode[20] = "Ocean";
};

WindConfig config;


// ====================================================================================
// WindScape Simulator Class
// ====================================================================================

class WindScapeSimulator {
public:
    // --- 1. 상태 변수 ---
    bool fan_power_enabled = true;
    bool enable_wind_simulation = true;
    bool wind_simulation_active = false;

    // Wind Dynamics
    float current_wind_speed = 3.6f; // m/s
    float target_wind_speed = 3.6f; // m/s
    float wind_change_rate = 0.1f;
    float wind_momentum = 0.0f;
    
    // Gust System
    bool gust_active = false;
    float gust_start_time = 0.0f;
    float gust_duration = 3.0f;
    float gust_intensity = 1.0f;
    unsigned long last_gust_check = 0;
    
    // Thermal Convection
    bool thermal_bubble_active = false;
    float thermal_bubble_start_time = 0.0f;
    float thermal_bubble_duration = 8.0f;
    unsigned long last_thermal_check = 0;
    float current_thermal_contribution = 0.0f;
    
    // Turbulence Modeling
    float spectral_energy_buffer = 0.0f;
    float spectral_phase_accumulator = 0.0f;
    float turbulence_time_scale = 5.0f;
    
    // Dynamic Weather Phases
    int current_weather_phase = 1;
    float phase_start_time = 0.0f;
    float phase_duration = 120.0f;
    float phase_wind_min = 5.0f;
    float phase_wind_max = 15.0f;

    // Preset Configuration (내부 시뮬레이션 로직용)
    float base_wind_min = 2.2f;
    float base_wind_max = 6.7f;
    float gust_probability_base = 0.02f;
    float location_gust_strength = 1.8f;
    float thermal_bubble_frequency = 0.025f;

    // 타이머 변수
    unsigned long last_wind_sim_update = 0;
    
    // --- 2. 설정 파일 관리 ---

    bool loadConfig() {
        // [LittleFS 변경]: SPIFFS.begin(true) 대신 LittleFS.begin(true) 사용
        if (!LittleFS.begin(true)) {
            Serial.println("LittleFS Mount Failed! Using default config.");
            return false;
        }

        File configFile = LittleFS.open(CONFIG_FILE_PATH, "r");
        if (!configFile) {
            Serial.println("Config file not found. Using default.");
            configFile.close();
            return false;
        }

        size_t size = configFile.size();
        if (size > 512) { // 크기 조정
            Serial.println("Config file size is too large.");
            configFile.close();
            return false;
        }

        StaticJsonDocument<512> doc; // JSON 크기 확장
        DeserializationError error = deserializeJson(doc, configFile);
        configFile.close();

        if (error) {
            Serial.printf("Failed to deserialize JSON: %s\n", error.c_str());
            return false;
        }

        // 설정 로드
        config.wind_intensity = doc["intensity"] | DEFAULT_WIND_INTENSITY_PCT;
        config.gust_frequency = doc["gust_freq"] | DEFAULT_GUST_FREQUENCY_PCT;
        config.wind_variability = doc["variability"] | DEFAULT_WIND_VARIABILITY_PCT;
        config.fan_speed_limit = doc["fan_limit"] | DEFAULT_MAXIMUM_FAN_SPEED_PCT;
        config.minimum_fan_speed = doc["min_fan"] | DEFAULT_MINIMUM_FAN_SPEED_PCT;
        
        // turb_len은 float으로 로드
        if (doc.containsKey("turb_len")) {
            config.turbulence_length_scale = doc["turb_len"].as<float>();
        } else {
            config.turbulence_length_scale = DEFAULT_TURBULENCE_LENGTH_SCALE;
        }

        config.turbulence_intensity_sigma = doc["turb_sig"] | DEFAULT_TURBULENCE_INTENSITY;
        config.thermal_bubble_strength = doc["therm_str"] | DEFAULT_THERMAL_BUBBLE_STRENGTH;
        config.thermal_bubble_radius = doc["therm_rad"] | DEFAULT_THERMAL_BUBBLE_RADIUS; // <--- 보완: Radius 로드
        
        const char* preset = doc["preset"] | "Ocean";
        strncpy(config.preset_mode, preset, sizeof(config.preset_mode) - 1);
        config.preset_mode[sizeof(config.preset_mode) - 1] = '\0';
        
        Serial.println("Config loaded successfully.");
        return true;
    }

    bool saveConfig() {
        StaticJsonDocument<512> doc;

        // 설정 저장
        doc["intensity"] = config.wind_intensity;
        doc["gust_freq"] = config.gust_frequency;
        doc["variability"] = config.wind_variability;
        doc["fan_limit"] = config.fan_speed_limit;
        doc["min_fan"] = config.minimum_fan_speed;
        doc["turb_len"] = config.turbulence_length_scale;
        doc["turb_sig"] = config.turbulence_intensity_sigma;
        doc["therm_str"] = config.thermal_bubble_strength;
        doc["therm_rad"] = config.thermal_bubble_radius; // <--- 보완: Radius 저장
        doc["preset"] = config.preset_mode;

        // [LittleFS 변경]: SPIFFS.open 대신 LittleFS.open 사용
        File configFile = LittleFS.open(CONFIG_FILE_PATH, "w");
        if (!configFile) {
            Serial.println("Failed to open config file for writing");
            return false;
        }

        if (serializeJson(doc, configFile) == 0) {
            Serial.println("Failed to write to file");
            configFile.close();
            return false;
        }
        
        configFile.close();
        Serial.println("Config saved successfully.");
        return true;
    }

    // --- 3. 초기화 및 설정 ---

    void setup() {
        Serial.begin(115200);
        Serial.println("\nWindScape Simulator Starting...");

        // 1. 파일 시스템 및 설정 로드
        loadConfig();
        
        // 2. 팬 PWM 설정
        ledcSetup(PWM_CHANNEL, PWM_FREQUENCY, PWM_RESOLUTION);
        ledcAttachPin(FAN_PWM_PIN, PWM_CHANNEL);
        pinMode(FAN_TACH_PIN, INPUT_PULLUP);
        
        if (fan_power_enabled) {
            applyFanSpeed(config.minimum_fan_speed + 12.0f); // 초기 팬 속도 (최소 + 12%)
        } else {
            ledcWrite(PWM_CHANNEL, 0);
        }

        // 3. Wi-Fi AP 모드 설정
        Serial.printf("Setting up Access Point: %s\n", WIFI_SSID);
        WiFi.softAP(WIFI_SSID, WIFI_PASSWORD);
        Serial.printf("AP IP address: %s\n", WiFi.softAPIP().toString().c_str());

        // 4. Web Server 초기화
        setupWebServer();

        // 5. 시뮬레이션 시작
        srand(esp_random()); 
        applyCurrentPreset(true); 
    }

    void setupWebServer() {
        String ap_ip = WiFi.softAPIP().toString(); // AP IP 주소 가져오기
        
        // 루트 페이지 (설정 UI) - 단순화된 HTML
        server.on("/", HTTP_GET, [this, ap_ip](AsyncWebServerRequest *request){
            String html = R"raw(
                <!DOCTYPE html><html><head><title>WindScape Config</title>
                <meta name='viewport' content='width=device-width, initial-scale=1'>
                <style>body{font-family:sans-serif;} input[type=submit]{padding:10px 20px;}</style>
                </head><body>
                <h2>WindScape Config (V7)</h2>
                <p>Status: Wind Sim: %s | Phase: %d | Wind: %.1f m/s | Fan: %d PWM</p>
                <p><strong>AP IP: %s</strong></p> <form method='POST' action='/set_config'>
                <h3>User Control</h3>
                <label>Intensity (%%): <input type='number' name='intensity' value='%.1f' step='1' min='0' max='100'></label><br>
                <label>Gust Freq (%%): <input type='number' name='gust_freq' value='%.1f' step='1' min='0' max='100'></label><br>
                <label>Variability (%%): <input type='number' name='variability' value='%.1f' step='1' min='0' max='100'></label><br>
                <h3>Fan Limits</h3>
                <label>Fan Limit (%%): <input type='number' name='fan_limit' value='%.1f' step='1' min='0' max='100'></label><br>
                <label>Min Fan (%%): <input type='number' name='min_fan' value='%.1f' step='1' min='0' max='100'></label><br>
                <h3>Physics Control</h3>
                <label>Thermal Str (x): <input type='number' name='therm_str' value='%.1f' step='0.1' min='1.0' max='5.0'></label><br>
                <label>Thermal Radius (m): <input type='number' name='therm_rad' value='%.1f' step='1.0' min='1.0' max='100.0'></label><br>
                <label>Turb Length (m): <input type='number' name='turb_len' value='%.1f' step='1.0' min='1.0' max='100.0'></label><br>
                <label>Turb Sigma (x): <input type='number' name='turb_sig' value='%.2f' step='0.01' min='0.01' max='1.0'></label><br>
                <h3>Preset</h3>
                <label>Preset: <select name='preset'>
                    <option value='Ocean' %s>Ocean</option>
                    <option value='Plains' %s>Plains</option>
                    <option value='Mountain' %s>Mountain</option>
                    <option value='Countryside' %s>Countryside</option>
                    <option value='Mediterranean' %s>Mediterranean</option>
                    <option value='Off' %s>Off (Steady)</option>
                </select></label><br><br>
                <input type='submit' value='Save & Apply'>
                </form></body></html>
            )raw";

            String state = wind_simulation_active ? "ON" : "OFF (Steady)";
            
            // snprintf를 사용하여 최종 HTML 생성 (float 값 형식 지정)
            char buffer[2048]; 
            snprintf(buffer, sizeof(buffer), html.c_str(), 
                     state.c_str(), current_weather_phase, current_wind_speed, ledcRead(PWM_CHANNEL),
                     ap_ip.c_str(),
                     config.wind_intensity, config.gust_frequency, config.wind_variability, 
                     config.fan_speed_limit, config.minimum_fan_speed,
                     config.thermal_bubble_strength, config.thermal_bubble_radius, config.turbulence_length_scale, config.turbulence_intensity_sigma, // <--- Physics Params
                     strcmp(config.preset_mode, "Ocean") == 0 ? "selected" : "",
                     strcmp(config.preset_mode, "Plains") == 0 ? "selected" : "",
                     strcmp(config.preset_mode, "Mountain") == 0 ? "selected" : "",
                     strcmp(config.preset_mode, "Countryside") == 0 ? "selected" : "",
                     strcmp(config.preset_mode, "Mediterranean") == 0 ? "selected" : "",
                     strcmp(config.preset_mode, "Off") == 0 ? "selected" : ""
                     );

            request->send(200, "text/html", buffer);
        });

        // 설정 저장 API (POST)
        server.on("/set_config", HTTP_POST, [this](AsyncWebServerRequest *request){
            bool changesMade = false;
            
            // 모든 파라미터를 순회하며 설정 업데이트
            for(int i=0; i<request->args(); i++){
                String name = request->argName(i);
                String value = request->arg(i);

                if (name == "intensity") {
                    config.wind_intensity = value.toFloat(); changesMade = true;
                } else if (name == "gust_freq") {
                    config.gust_frequency = value.toFloat(); changesMade = true;
                } else if (name == "variability") {
                    config.wind_variability = value.toFloat(); changesMade = true;
                } else if (name == "fan_limit") {
                    config.fan_speed_limit = value.toFloat(); changesMade = true;
                } else if (name == "min_fan") {
                    config.minimum_fan_speed = value.toFloat(); changesMade = true;
                } else if (name == "turb_len") {
                    config.turbulence_length_scale = value.toFloat(); changesMade = true;
                } else if (name == "turb_sig") {
                    config.turbulence_intensity_sigma = value.toFloat(); changesMade = true;
                } else if (name == "therm_str") {
                    config.thermal_bubble_strength = value.toFloat(); changesMade = true;
                } else if (name == "therm_rad") { // <--- 보완: Radius 파싱 추가
                    config.thermal_bubble_radius = value.toFloat(); changesMade = true;
                } else if (name == "preset") {
                    strncpy(config.preset_mode, value.c_str(), sizeof(config.preset_mode) - 1);
                    config.preset_mode[sizeof(config.preset_mode) - 1] = '\0';
                    changesMade = true;
                }
            }

            if (changesMade) {
                // 변경된 설정 저장 및 적용
                saveConfig();
                applyCurrentPreset(true);
            }
            
            // 사용자에게 다시 루트 페이지로 리다이렉트 (설정 적용 확인)
            request->redirect("/");
        });

        // JSON API (현재 상태 및 설정)
        server.on("/api/state", HTTP_GET, [this](AsyncWebServerRequest *request){
            StaticJsonDocument<512> doc;
            doc["wind_speed"] = current_wind_speed;
            doc["phase"] = current_weather_phase;
            doc["sim_active"] = wind_simulation_active;
            doc["fan_pwm"] = ledcRead(PWM_CHANNEL);
            doc["intensity"] = config.wind_intensity;
            doc["preset"] = config.preset_mode;

            String response;
            serializeJson(doc, response);
            request->send(200, "application/json", response);
        });

        server.begin();
    }
    
    // --- 4. 시뮬레이션 엔진 로직 (보완 사항 적용) ---

    void applyFanSpeed(float speed_percent) {
        if (!fan_power_enabled) {
            ledcWrite(PWM_CHANNEL, 0);
            return;
        }

        float requested_speed = speed_percent / 100.0f;
        float speed_limit = config.fan_speed_limit / 100.0f;
        float min_speed = config.minimum_fan_speed / 100.0f;
        float intensity_multiplier = config.wind_intensity / 100.0f;

        // [보완] Intensity가 0%일 경우 강제 종료 (팬 정지)
        if (intensity_multiplier <= 0.01f) {
            ledcWrite(PWM_CHANNEL, 0);
            return;
        }
        
        if (wind_simulation_active) {
            requested_speed *= intensity_multiplier;
        }

        requested_speed = fmax(min_speed, fmin(speed_limit, requested_speed));
        
        int pwm_level = (int)(requested_speed * (pow(2, PWM_RESOLUTION) - 1));

        if (requested_speed <= 0.01f) {
            ledcWrite(PWM_CHANNEL, 0);
        } else {
            ledcWrite(PWM_CHANNEL, pwm_level);
        }
    }

    void applyCurrentPreset(bool force_apply) {
        wind_simulation_active = false;
        gust_active = false;
        thermal_bubble_active = false;
        gust_intensity = 1.0f;
        
        String current_preset_mode = config.preset_mode;
        
        if (current_preset_mode == "Off") {
            enable_wind_simulation = false;
            float steady_fan_pct = (config.minimum_fan_speed > 0.0f) ? config.minimum_fan_speed : 12.0f;
            applyFanSpeed(steady_fan_pct);
            current_wind_speed = 0.5f;
            target_wind_speed = 0.5f;

        } else {
            enable_wind_simulation = true;
            // 프리셋에 따른 물리적 값 설정
            if (current_preset_mode == "Countryside") {
              base_wind_min = 0.7f; base_wind_max = 3.4f;
              gust_probability_base = 0.006f; location_gust_strength = 1.35f; thermal_bubble_frequency = 0.015f;
            } else if (current_preset_mode == "Mediterranean") {
              base_wind_min = 1.6f; base_wind_max = 3.8f;
              gust_probability_base = 0.012f; location_gust_strength = 1.55f; thermal_bubble_frequency = 0.035f;
            } else if (current_preset_mode == "Ocean") {
              base_wind_min = 1.8f; base_wind_max = 5.5f;
              gust_probability_base = 0.040f; location_gust_strength = 2.1f; thermal_bubble_frequency = 0.022f;
            } else if (current_preset_mode == "Mountain") {
              base_wind_min = 2.2f; base_wind_max = 7.5f;
              gust_probability_base = 0.045f; location_gust_strength = 2.2f; thermal_bubble_frequency = 0.028f;
            } else if (current_preset_mode == "Plains") {
              base_wind_min = 4.0f; base_wind_max = 8.8f;
              gust_probability_base = 0.070f; location_gust_strength = 2.4f; thermal_bubble_frequency = 0.018f;
            }
            
            startWindSimulation();
        }
    }
    
    void startWindSimulation() {
        // ... (로직 동일) ...
        float mid_range = (base_wind_min + base_wind_max) * 0.5f;
        current_wind_speed = mid_range;
        target_wind_speed = mid_range;
        
        wind_momentum = 0.0f;
        spectral_energy_buffer = 0.0f;
        spectral_phase_accumulator = 0.0f;

        current_weather_phase = 1;
        phase_start_time = millis() / 1000.0f;
        phase_duration = 120.0f;
        
        float range_span = base_wind_max - base_wind_min;
        phase_wind_min = base_wind_min + (range_span * 0.15f);
        phase_wind_max = base_wind_min + (range_span * 0.85f);
        
        wind_simulation_active = true;
        generateWindTarget();
    }

    void calculateVonKarmanTurbulence(float dt) {
        if (!wind_simulation_active) return;
        
        // [config 값 사용]
        float L = config.turbulence_length_scale;
        float sigma = config.turbulence_intensity_sigma;
        float U = current_wind_speed;
        
        if (U < 0.1f) U = 0.1f;
        
        float turbulence_sum = 0.0f;
        
        for (int i = 1; i <= 12; i++) {
            float n = i * 0.1f; 
            float f = n * U / L; 
            
            float fL_over_U = f * L / U;
            float term = 70.8f * fL_over_U * fL_over_U;
            float numerator = 4.0f * sigma * sigma * (L / U) * (1.0f + term);
            float denominator = pow(1.0f + term, 5.0f/6.0f);
            float spectral_density = numerator / denominator;
            
            float phase_rate = 2.0f * M_PI * f;
            float phase_increment = phase_rate * dt;
            float phase_noise = getRandomFloat(-0.1f, 0.1f);
            float current_phase = spectral_phase_accumulator * i + phase_increment + phase_noise;
            
            float amplitude = sqrt(2.0f * spectral_density * 0.083f);
            float component = amplitude * sin(current_phase);
            
            turbulence_sum += component;
        }
        
        spectral_phase_accumulator += dt * 0.5f;
        if (spectral_phase_accumulator > 2.0f * M_PI) {
            spectral_phase_accumulator -= 2.0f * M_PI;
        }
        
        float correlation_factor = exp(-dt / turbulence_time_scale);
        spectral_energy_buffer = spectral_energy_buffer * correlation_factor + 
                                       turbulence_sum * (1.0f - correlation_factor);
    }
    
    void calculateThermalBubble() {
        if (!wind_simulation_active) return;
        current_thermal_contribution = 0.0f;
        
        if (thermal_bubble_active) {
            float current_time = millis() / 1000.0f;
            float bubble_age = current_time - thermal_bubble_start_time;
            
            if (bubble_age >= thermal_bubble_duration) {
                thermal_bubble_active = false;
                return;
            }
            
            float progress = bubble_age / thermal_bubble_duration;
            float envelope;
            
            if (progress < 0.2f) { envelope = 1.0f - pow(1.0f - progress / 0.2f, 2.0f);
            } else if (progress < 0.6f) { envelope = 1.0f; float osc_freq = 0.8f + (current_weather_phase * 0.2f);
              float variation = sin(bubble_age * osc_freq * 2.0f * M_PI) * 0.15f; envelope += variation;
            } else { float decay_progress = (progress - 0.6f) / 0.4f; envelope = 1.0f - pow(decay_progress, 1.3f); }
            
            float thermal_strength = config.thermal_bubble_strength * envelope; // [config 값 사용]
            current_thermal_contribution = thermal_strength - 1.0f;
            
            // NOTE: config.thermal_bubble_radius는 현재 로직에 반영되지 않았습니다.
        }
    }

    void updateGustState() {
        if (!wind_simulation_active) return;
        float current_time = millis() / 1000.0f;
        if (gust_active) {
            float gust_age = current_time - gust_start_time;
            if (gust_age >= gust_duration) {
                gust_active = false; gust_intensity = 1.0f;
            } else {
                float progress = gust_age / gust_duration;
                float envelope;
                if (progress < 0.25f) { envelope = 1.0f - pow(1.0f - progress / 0.25f, 1.8f);
                } else if (progress < 0.65f) { envelope = 1.0f; float osc_freq = 1.5f + (current_weather_phase * 0.5f);
                  envelope += sin(gust_age * osc_freq) * 0.08f;
                } else { float decay_progress = (progress - 0.65f) / 0.35f; envelope = 1.0f - pow(decay_progress, 1.5f); }
                gust_intensity = 1.0f + (location_gust_strength - 1.0f) * envelope;
            }
            return;
        }
        if (current_time - last_gust_check * 0.001f >= 1.5f) {
            last_gust_check = millis();
            float base_prob = gust_probability_base;
            float user_freq = config.gust_frequency / 100.0f; // [config 값 사용]
            float wind_speed_factor = 1.0f + (current_wind_speed / 8.9f) * 0.5f;
            float phase_multiplier;
            if (current_weather_phase == 0) phase_multiplier = 0.3f * wind_speed_factor;
            else if (current_weather_phase == 2) phase_multiplier = 2.2f * wind_speed_factor;
            else phase_multiplier = 0.9f * wind_speed_factor;
            float final_prob = base_prob * user_freq * phase_multiplier;
            if (getRandomFloat() < final_prob) {
                gust_active = true; gust_start_time = current_time;
                float duration_base, intensity_base; float speed_factor = current_wind_speed / 6.7f;
                if (current_weather_phase == 0) { duration_base = getRandomFloat(3.0f, 8.0f);
                  intensity_base = getRandomFloat(1.08f, 1.33f);
                } else if (current_weather_phase == 2) { duration_base = getRandomFloat(0.8f, 3.3f);
                  intensity_base = getRandomFloat(1.3f, 1.3f + 0.9f * (1.0f + speed_factor * 0.3f));
                } else { duration_base = getRandomFloat(1.8f, 5.8f);
                  intensity_base = getRandomFloat(1.15f, 1.15f + 0.5f * (1.0f + speed_factor * 0.2f));
                }
                gust_duration = duration_base;
                gust_intensity = fmin(intensity_base, location_gust_strength);
            }
        }
    }

    void updateThermalBubbleCheck() {
        if (!wind_simulation_active) return;
        if (thermal_bubble_active) return;
        float current_time = millis() / 1000.0f;
        if (current_time - last_thermal_check * 0.001f < 2.0f) return;
        last_thermal_check = millis();
        float base_prob = thermal_bubble_frequency;
        float wind_speed_factor = 1.0f + (current_wind_speed / 8.0f) * 0.3f;
        float phase_multiplier;
        if (current_weather_phase == 0) phase_multiplier = 1.2f;
        else if (current_weather_phase == 2) phase_multiplier = 0.7f;
        else phase_multiplier = 1.0f;
        float final_prob = base_prob * wind_speed_factor * phase_multiplier;
        if (getRandomFloat() < final_prob) {
            thermal_bubble_active = true; thermal_bubble_start_time = current_time;
            float duration_base = getRandomFloat(8.0f, 14.0f);
            if (current_weather_phase == 0) duration_base *= 1.3f;
            else if (current_weather_phase == 2) duration_base *= 0.8f;
            thermal_bubble_duration = duration_base;
        }
    }

    void generateWindTarget() {
        if (!wind_simulation_active) return;
        float range = phase_wind_max - phase_wind_min;
        float new_target = phase_wind_min + (getRandomFloat() * range);
        float mid_point = (phase_wind_min + phase_wind_max) * 0.5f;
        float bias_factor = getRandomFloat(0.0f, 1.0f);
        new_target = (new_target + mid_point * bias_factor) / (1.0f + bias_factor);
        target_wind_speed = new_target;
        
        float variability = config.wind_variability / 100.0f; // [config 값 사용]
        float base_rate;
        if (current_weather_phase == 0) base_rate = 0.08f + (variability * 0.12f);
        else if (current_weather_phase == 2) base_rate = 0.25f + (variability * 0.35f);
        else base_rate = 0.15f + (variability * 0.25f);
        
        float U = current_wind_speed;
        if (U < 0.1f) U = 0.1f;
        float time_scale_factor = config.turbulence_length_scale / U; // [config 값 사용]
        base_rate *= (1.0f + time_scale_factor * 0.1f);
        wind_change_rate = base_rate * getRandomFloat(0.7f, 1.7f); 
    }
    
    void updateWeatherPhase() {
        if (!wind_simulation_active) return;
        float current_time = millis() / 1000.0f;
        if (current_time - phase_start_time >= phase_duration) {
            int old_phase = current_weather_phase;
            float random_val = getRandomFloat();
            if (old_phase == 0) { if (random_val < 0.6f) current_weather_phase = 1; else current_weather_phase = 2;
            } else if (old_phase == 1) { if (random_val < 0.3f) current_weather_phase = 0;
            else if (random_val < 0.7f) current_weather_phase = 1; else current_weather_phase = 2;
            } else { if (random_val < 0.4f) current_weather_phase = 1; else if (random_val < 0.7f) current_weather_phase = 0;
            else current_weather_phase = 2; }
            if (current_weather_phase == 0) phase_duration = getRandomFloat(90.0f, 210.0f);
            else if (current_weather_phase == 1) phase_duration = getRandomFloat(120.0f, 300.0f);
            else phase_duration = getRandomFloat(60.0f, 150.0f);
            phase_start_time = current_time;
            float range_span = base_wind_max - base_wind_min;
            if (current_weather_phase == 0) { phase_wind_min = base_wind_min; phase_wind_max = base_wind_min + (range_span * 0.6f);
            } else if (current_weather_phase == 1) { phase_wind_min = base_wind_min + (range_span * 0.15f);
            phase_wind_max = base_wind_min + (range_span * 0.85f);
            } else { phase_wind_min = base_wind_min + (range_span * 0.4f); phase_wind_max = base_wind_max; }
            phase_wind_min = fmax(0.2f, phase_wind_min);
            phase_wind_max = fmin(11.0f, phase_wind_max);
            generateWindTarget();
        }
    }

    void calculateWindSimulation() {
        if (!wind_simulation_active) return;
        unsigned long now = millis();
        if (now - last_wind_sim_update < WIND_SIM_INTERVAL_MS + (esp_random() % 300)) return;
        float dt = (float)(now - last_wind_sim_update) / 1000.0f;
        last_wind_sim_update = now;
        
        updateWeatherPhase();
        
        float target = target_wind_speed;
        float current = current_wind_speed;
        float change_rate = wind_change_rate;
        
        float difference = target - current;
        float change = difference * change_rate * dt; 
        
        wind_momentum = wind_momentum * 0.85f + change * 0.15f;
        wind_momentum = fmax(-0.5f, fmin(0.5f, wind_momentum));
        change = wind_momentum;
        
        calculateVonKarmanTurbulence(dt);
        calculateThermalBubble();
        
        float von_karman_turb = spectral_energy_buffer;
        float thermal_contribution = current_thermal_contribution;
        
        float new_speed = current + change + von_karman_turb;
        new_speed = fmax(0.2f, fmin(11.0f, new_speed));
        current_wind_speed = new_speed;
        
        float change_threshold = 0.5f + (current / 20.0f);
        float close_change_chance = 30.0f; 
        float far_change_chance = 6.0f; 
        
        if (config.wind_variability > 70.0f) { // [config 값 사용]
             close_change_chance *= 1.5f; far_change_chance *= 1.5f;
        }

        if (abs(difference) < change_threshold) {
            if (getRandomFloat() * 100.0f < close_change_chance) {
                generateWindTarget();
            }
        } else {
            if (getRandomFloat() * 100.0f < far_change_chance) {
                generateWindTarget();
            }
        }
        
        float final_wind = current_wind_speed;
        if (gust_active) {
            final_wind *= gust_intensity;
        }
        final_wind += thermal_contribution;
        
        static float continuity_phase = 0.0f;
        continuity_phase += 0.1f;
        if (continuity_phase > 2.0f * M_PI) continuity_phase -= 2.0f * M_PI;
        final_wind += sin(continuity_phase) * 0.05f;

        final_wind = fmax(0.3f, fmin(11.0f, final_wind));
        
        float wind_ratio = final_wind / 11.0f;
        float curved_ratio = sqrt(wind_ratio);
        float fan_percent = 12.0f + (curved_ratio * 78.0f);
        fan_percent = fmax(12.0f, fmin(90.0f, fan_percent));

        applyFanSpeed(fan_percent);
    }

    // --- 5. 메인 루프 실행 ---

    void loop() {
        if (!fan_power_enabled) return;
        if (enable_wind_simulation) {
            calculateWindSimulation();
            updateGustState();
            updateThermalBubbleCheck();
        }
    }
};


// ====================================================================================
// Arduino Setup & Loop
// ====================================================================================

WindScapeSimulator simulator;

void setup() {
    simulator.setup();
}

void loop() {
    simulator.loop();
}

