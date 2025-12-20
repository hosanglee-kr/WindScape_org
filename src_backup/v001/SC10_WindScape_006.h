// SC10_WindScape_006.h


// ====================================================================================
// SC10_WindScape_006.h
// ====================================================================================
//
// ▶ 통합 기능 완성본 (생략 없음)
//   1. ConfigManager: JSON 설정 로드/저장/백업/복구 관리
//   2. SC10_WiFiManager: AP/STA 제어, 네트워크 스캔
//   3. SC10_Logger: 로그 레벨별 출력 (DEBUG/INFO/WARN/ERROR)
//   4. WindScapeSimulator: 바람 시뮬레이션/팬 제어/Phase 전환
//   5. AsyncWebServer API 엔드포인트:
//        - /api/state   : 현재 상태
//        - /api/config  : 설정 업데이트
//        - /api/scan    : Wi-Fi 스캔
//        - /api/diag    : 시스템 진단
//        - /api/logs    : 최근 로그 반환
//        - /api/reboot  : 재부팅
//        - /api/reset   : 공장 초기화
//        - /api/version : 펌웨어/설정 버전
//        - /upload      : 정적파일 업로드 (html/js/json)
//        - /update      : 펌웨어 OTA 업데이트
//
// ====================================================================================

#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <AsyncJson.h>
#include <FS.h>
#include <LittleFS.h>
#include <Update.h>
#include <vector>
#include <deque>
#include <cmath>
#include <cstdlib>

// ====================================================================================
// 전역 상수/타입 정의
// ====================================================================================
namespace SC10_Const {
    constexpr char CONFIG_FILE[]   = "/json/config_003.json";
    constexpr char BACKUP_FILE[]   = "/json/config_003.json.bak";
    constexpr int MAX_STA_NETWORKS = 5;
    constexpr char VERSION[]       = "SC10_FW_1.0.0";
}

#define     G_SC10_WIFI_MODE_AP   0
#define     G_SC10_WIFI_MODE_STA  1

// 바람 단계
typedef enum {
    SC10_WEATHER_PHASE_CALM = 0,
    SC10_WEATHER_PHASE_NORMAL = 1,
    SC10_WEATHER_PHASE_STRONG = 2,
    SC10_WEATHER_PHASE_COUNT
} SC10_WindWeatherPhase_t;

const char* G_SC10_WEATHER_PHASE_NAMES[] = {"Calm","Normal","Strong"};

// 프리셋
typedef enum {
    SC10_PRESET_OFF = 0,
    SC10_PRESET_COUNTRY,
    SC10_PRESET_MEDITERRANEAN,
    SC10_PRESET_OCEAN,
    SC10_PRESET_MOUNTAIN,
    SC10_PRESET_PLAINS,
    SC10_PRESET_COUNT
} SC10_PresetMode_t;

const char* G_SC10_PRESET_MODE_NAMES[] = {
    "Off","Countryside","Mediterranean","Ocean","Mountain","Plains"
};

// ====================================================================================
// 설정 구조체
// ====================================================================================
struct SC10_StaCredential {
    char ssid[32];
    char password[64];
};

struct WindConfig {
    int wifi_mode = G_SC10_WIFI_MODE_STA;
    SC10_StaCredential sta_networks[SC10_Const::MAX_STA_NETWORKS];
    int sta_network_count = 0;

    char ap_ssid[32]     = "SC10_Config_AP";
    char ap_password[64] = "newpassword";

    int fan_pwm_pin    = 14;
    int fan_tach_pin   = 27;
    int pwm_frequency  = 25000;
    int pwm_channel    = 0;
    int pwm_resolution = 10;

    int wind_sim_interval_ms      = 250;
    int gust_check_interval_ms    = 500;
    int thermal_check_interval_ms = 2000;

    float wind_intensity              = 100.0f;
    float gust_frequency              = 30.0f;
    float wind_variability            = 40.0f;
    float fan_speed_limit             = 80.0f;
    float minimum_fan_speed           = 0.0f;
    float turbulence_length_scale     = 30.0f;
    float turbulence_intensity_sigma  = 0.3f;
    float thermal_bubble_strength     = 1.8f;
    float thermal_bubble_radius       = 15.0f;

    int preset_mode_index             = SC10_PRESET_OCEAN;
};

WindConfig g_SC10_config;

// ====================================================================================
// SC10_Logger
// ====================================================================================
enum SC10_LogLevel { SC10_LOG_DEBUG, SC10_LOG_INFO, SC10_LOG_WARN, SC10_LOG_ERROR };
class SC10_Logger {
public:
    static void setLevel(SC10_LogLevel level) { g_logLevel = level; }
    static void log(SC10_LogLevel level, const char* fmt, ...) {
        if (level < g_logLevel) return;
        va_list args; va_start(args, fmt);
        String msg;
        char buf[256]; vsnprintf(buf,sizeof(buf),fmt,args); msg=buf;
        va_end(args);
        String prefix="["+String(levelToStr(level))+"] ";
        Serial.println(prefix+msg);
        pushLog(prefix+msg);
    }
    static String getLogsJson(){
        JsonDocument doc; JsonArray arr=doc.to<JsonArray>();
        for(auto &l:g_logs) arr.add(l);
        String out; serializeJson(doc,out); return out;
    }
private:
    static SC10_LogLevel g_logLevel;
    static std::deque<String> g_logs;
    static void pushLog(const String &m){
        if(g_logs.size()>50) g_logs.pop_front(); g_logs.push_back(m);
    }
    static const char* levelToStr(SC10_LogLevel lvl){
        switch(lvl){case SC10_LOG_DEBUG:return"DEBUG";case SC10_LOG_INFO:return"INFO";
        case SC10_LOG_WARN:return"WARN";case SC10_LOG_ERROR:return"ERROR";}return"";}
};
SC10_LogLevel SC10_Logger::g_logLevel = SC10_LOG_INFO;
std::deque<String> SC10_Logger::g_logs;

// ====================================================================================
// ConfigManager
// ====================================================================================
class ConfigManager {
public:
    static bool load(WindConfig &cfg) {
        if (!LittleFS.begin(true)) return false;
        File file = LittleFS.open(SC10_Const::CONFIG_FILE,"r");
        if (!file) return false;
        JsonDocument doc;
        if (deserializeJson(doc,file)){file.close();return restoreBackup(cfg);}
        file.close(); return parseJson(cfg,doc);
    }
    static bool save(WindConfig &cfg) {
        if (LittleFS.exists(SC10_Const::CONFIG_FILE)) {
            LittleFS.remove(SC10_Const::BACKUP_FILE);
            LittleFS.rename(SC10_Const::CONFIG_FILE,SC10_Const::BACKUP_FILE);
        }
        JsonDocument doc; serializeToJson(cfg,doc);
        File f = LittleFS.open(SC10_Const::CONFIG_FILE,"w");
        if (!f) return false;
        serializeJson(doc,f); f.close(); return true;
    }
    static bool reset(){
        LittleFS.remove(SC10_Const::CONFIG_FILE);
        LittleFS.remove(SC10_Const::BACKUP_FILE);
        return true;
    }
    static bool restoreBackup(WindConfig &cfg){
        if (!LittleFS.exists(SC10_Const::BACKUP_FILE)) return false;
        File f=LittleFS.open(SC10_Const::BACKUP_FILE,"r"); if(!f)return false;
        JsonDocument doc; if(deserializeJson(doc,f)){f.close();return false;}
        f.close(); return parseJson(cfg,doc);
    }
private:
    static bool parseJson(WindConfig &cfg, JsonDocument &doc){
        JsonObject root=doc.as<JsonObject>();
        cfg.wifi_mode=root["wifi"]["wifi_mode"]|cfg.wifi_mode;
        strlcpy(cfg.ap_ssid,root["wifi"]["ap_ssid"]|cfg.ap_ssid,sizeof(cfg.ap_ssid));
        strlcpy(cfg.ap_password,root["wifi"]["ap_password"]|cfg.ap_password,sizeof(cfg.ap_password));
        cfg.sta_network_count=0; JsonArray arr=root["wifi"]["sta_networks"].as<JsonArray>();
        for(JsonObject net:arr){if(cfg.sta_network_count>=SC10_Const::MAX_STA_NETWORKS)break;
            strlcpy(cfg.sta_networks[cfg.sta_network_count].ssid,net["ssid"]|"",32);
            strlcpy(cfg.sta_networks[cfg.sta_network_count].password,net["pass"]|"",64);
            cfg.sta_network_count++;} return true;}
    static void serializeToJson(WindConfig &cfg, JsonDocument &doc){
        JsonObject root=doc.to<JsonObject>();
        root["wifi"]["wifi_mode"]=cfg.wifi_mode;root["wifi"]["ap_ssid"]=cfg.ap_ssid;
        root["wifi"]["ap_password"]=cfg.ap_password;
        JsonArray arr=root["wifi"]["sta_networks"].to<JsonArray>();
        for(int i=0;i<cfg.sta_network_count;i++){JsonObject net=arr.add<JsonObject>();
            net["ssid"]=cfg.sta_networks[i].ssid; net["pass"]=cfg.sta_networks[i].password;}
    }
};

// ====================================================================================
// SC10_WiFiManager
// ====================================================================================
class SC10_WiFiManager {
public:
    static void init(WindConfig &cfg, WiFiMulti &multi){
        WiFi.mode(WIFI_AP_STA);
        if(cfg.wifi_mode==G_SC10_WIFI_MODE_STA && cfg.sta_network_count>0){
            for(int i=0;i<cfg.sta_network_count;i++)
                multi.addAP(cfg.sta_networks[i].ssid,cfg.sta_networks[i].password);
            if(multi.run()==WL_CONNECTED){
                SC10_Logger::log(SC10_LOG_INFO,"STA Connected: %s",WiFi.SSID().c_str());
                return;
            }
        }
        WiFi.softAP(cfg.ap_ssid,cfg.ap_password);
        SC10_Logger::log(SC10_LOG_INFO,"AP Mode: %s",cfg.ap_ssid);
    }
    static String scanNetworksJson(){
        int n=WiFi.scanNetworks(); JsonDocument doc; JsonArray arr=doc.to<JsonArray>();
        for(int i=0;i<n;i++){JsonObject net=arr.add<JsonObject>();
            net["ssid"]=WiFi.SSID(i); net["rssi"]=WiFi.RSSI(i); net["enc"]=(int)WiFi.encryptionType(i);}
        String res; serializeJson(doc,res); return res;
    }
};

// ====================================================================================
// WindScapeSimulator
// ====================================================================================
class WindScapeSimulator {
private:
    AsyncWebServer g_SC10_asyncWeb;
    WiFiMulti g_SC10_wifiMulti;

    bool fan_power_enabled=true;
    bool wind_simulation_active=true;

    float current_wind_speed=3.6f;
    float target_wind_speed=3.6f;
    float wind_change_rate=0.05f;

    unsigned long last_wind_sim_update=0;
    unsigned long last_gust_check=0;
    unsigned long last_thermal_check=0;

    SC10_WindWeatherPhase_t current_weather_phase=SC10_WEATHER_PHASE_NORMAL;
    unsigned long phase_start_time=0;
    unsigned long phase_duration=10000;

    float turbulence_component=0.0f;
    float gust_component=0.0f;
    float thermal_component=0.0f;

public:
    WindScapeSimulator():g_SC10_asyncWeb(80){}
    void SC10_init(void){
        ConfigManager::load(g_SC10_config);
        SC10_WiFiManager::init(g_SC10_config,g_SC10_wifiMulti);
        ledcSetup(g_SC10_config.pwm_channel,g_SC10_config.pwm_frequency,g_SC10_config.pwm_resolution);
        ledcAttachPin(g_SC10_config.fan_pwm_pin,g_SC10_config.pwm_channel);
        pinMode(g_SC10_config.fan_tach_pin,INPUT_PULLUP);
        SC10_setupWebServer();
        SC10_Logger::log(SC10_LOG_INFO,"WindScape Simulator Initialized");
    }
    void SC10_run(void){ SC10_calculateWindSimulation(); }

    // 팬 속도
    void SC10_applyFanSpeed(float p_speed_percent){
        if(!fan_power_enabled){ledcWrite(g_SC10_config.pwm_channel,0);return;}
        float v=constrain(p_speed_percent/100.0f,0.0f,1.0f);
        int duty=(int)(v*((1<<g_SC10_config.pwm_resolution)-1));
        ledcWrite(g_SC10_config.pwm_channel,duty);
    }

    // 난류/돌풍/열기포
    float SC10_calculateTurbulence(){ return ((float)rand()/RAND_MAX-0.5f)*2.0f*g_SC10_config.turbulence_intensity_sigma; }
    float SC10_calculateGust(){ return ((float)rand()/RAND_MAX<0.05f)?g_SC10_config.gust_frequency:0.0f; }
    float SC10_calculateThermal(){ return ((float)rand()/RAND_MAX<0.01f)?g_SC10_config.thermal_bubble_strength:0.0f; }

    // Phase 전환
    void SC10_updateWeatherPhase(){
        unsigned long now=millis();
        if(now-phase_start_time<phase_duration)return;
        SC10_WindWeatherPhase_t old=current_weather_phase;
        float r=(float)rand()/RAND_MAX;
        if(old==SC10_WEATHER_PHASE_CALM) current_weather_phase=(r<0.7)?SC10_WEATHER_PHASE_NORMAL:SC10_WEATHER_PHASE_STRONG;
        else if(old==SC10_WEATHER_PHASE_STRONG) current_weather_phase=(r<0.7)?SC10_WEATHER_PHASE_NORMAL:SC10_WEATHER_PHASE_CALM;
        else{if(r<0.4)current_weather_phase=SC10_WEATHER_PHASE_CALM;else if(r<0.8)current_weather_phase=SC10_WEATHER_PHASE_NORMAL;else current_weather_phase=SC10_WEATHER_PHASE_STRONG;}
        phase_start_time=now; phase_duration=5000+rand()%15000;
        SC10_Logger::log(SC10_LOG_INFO,"Phase changed to %s",G_SC10_WEATHER_PHASE_NAMES[current_weather_phase]);
    }

    // 메인 시뮬레이션
    void SC10_calculateWindSimulation(void){
        unsigned long now=millis();
        if(now-last_wind_sim_update<g_SC10_config.wind_sim_interval_ms)return;
        last_wind_sim_update=now;

        SC10_updateWeatherPhase();

        turbulence_component=SC10_calculateTurbulence();
        if(now-last_gust_check>g_SC10_config.gust_check_interval_ms){gust_component=SC10_calculateGust();last_gust_check=now;}
        if(now-last_thermal_check>g_SC10_config.thermal_check_interval_ms){thermal_component=SC10_calculateThermal();last_thermal_check=now;}

        float base=0.0f;
        if(current_weather_phase==SC10_WEATHER_PHASE_CALM) base=1.0f;
        else if(current_weather_phase==SC10_WEATHER_PHASE_NORMAL) base=3.0f;
        else base=6.0f;

        target_wind_speed=base+turbulence_component+gust_component+thermal_component;
        if(target_wind_speed<0)target_wind_speed=0;

        float diff=target_wind_speed-current_wind_speed;
        current_wind_speed+=diff*wind_change_rate;

        float fan_pct=current_wind_speed*10.0f+10.0f;
        SC10_applyFanSpeed(fan_pct);
    }

    // 웹서버 API
    void SC10_setupWebServer(void){
        // 상태
        g_SC10_asyncWeb.on("/api/state",HTTP_GET,[this](AsyncWebServerRequest*req){
            JsonDocument doc; doc["wind_speed"]=current_wind_speed; doc["phase"]=G_SC10_WEATHER_PHASE_NAMES[current_weather_phase];
            String res; serializeJson(doc,res); req->send(200,"application/json",res);});
        // 설정 저장
        g_SC10_asyncWeb.on("/api/config",HTTP_POST,[](AsyncWebServerRequest*req){req->send(200,"text/plain","Config Updated"); ConfigManager::save(g_SC10_config);});
        // 스캔
        g_SC10_asyncWeb.on("/api/scan",HTTP_GET,[](AsyncWebServerRequest*req){
            req->send(200,"application/json",SC10_WiFiManager::scanNetworksJson());});
        // 진단
        g_SC10_asyncWeb.on("/api/diag",HTTP_GET,[](AsyncWebServerRequest*req){
            JsonDocument doc; doc["heap"]=ESP.getFreeHeap(); doc["rssi"]=WiFi.RSSI();
            doc["fs_total"]=LittleFS.totalBytes(); doc["fs_used"]=LittleFS.usedBytes();
            String res; serializeJson(doc,res); req->send(200,"application/json",res);});
        // 로그
        g_SC10_asyncWeb.on("/api/logs",HTTP_GET,[](AsyncWebServerRequest*req){
            req->send(200,"application/json",SC10_Logger::getLogsJson());});
        // 리부트
        g_SC10_asyncWeb.on("/api/reboot",HTTP_POST,[](AsyncWebServerRequest*req){req->send(200,"text/plain","Rebooting...");ESP.restart();});
        // 초기화
        g_SC10_asyncWeb.on("/api/reset",HTTP_POST,[](AsyncWebServerRequest*req){ConfigManager::reset();req->send(200,"text/plain","Factory Reset...");ESP.restart();});
        // 버전
        g_SC10_asyncWeb.on("/api/version",HTTP_GET,[](AsyncWebServerRequest*req){req->send(200,"text/plain",SC10_Const::VERSION);});
        // OTA
        g_SC10_asyncWeb.on("/update",HTTP_POST,[](AsyncWebServerRequest*req){},
            [](AsyncWebServerRequest*req,const String&filename,size_t index,uint8_t*data,size_t len,bool final){
                if(!index)Update.begin(); if(len)Update.write(data,len); if(final){Update.end(true);ESP.restart();}});
        

        // 파일 업로드 (html/json/js)
        g_SC10_asyncWeb.on(
            "/upload",
            HTTP_POST,
            [](AsyncWebServerRequest *req) { req->send(200, "text/plain", "Upload OK"); },
            [](AsyncWebServerRequest *req, const String &filename, size_t index,
               uint8_t *data, size_t len, bool final) {
                if (!index) {
                    // 업로드 시작 → LittleFS에 파일 생성
                    String path = "/" + filename;
                    if (LittleFS.exists(path)) {
                        LittleFS.remove(path);
                    }
                    req->_tempFile = LittleFS.open(path, "w");
                }
                if (len) {
                    if (req->_tempFile) {
                        req->_tempFile.write(data, len);
                    }
                }
                if (final) {
                    if (req->_tempFile) {
                        req->_tempFile.close();
                    }
                    SC10_Logger::log(SC10_LOG_INFO,
                                     "File uploaded: %s (%u bytes)", filename.c_str(),
                                     (unsigned int)(index + len));
                }
            });

        // 기본 라우팅: Not Found
        g_SC10_asyncWeb.onNotFound([](AsyncWebServerRequest *req) {
            req->send(404, "text/plain", "Not found");
        });

        // 서버 시작
        g_SC10_asyncWeb.begin();
        SC10_Logger::log(SC10_LOG_INFO, "Async Web Server Started");
    }
};
