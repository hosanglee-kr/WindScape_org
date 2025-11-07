#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : W10_WebAPI_020.h
 * 모듈 약어 : W10
 * 모듈명 : Smart Nature Wind Web API (v020)
 * ------------------------------------------------------
 * 기능 요약:
 *  - Web UI / REST API 엔드포인트 집약 모듈
 *  - System / WiFi / Motion / WindProfile / Schedules / UserProfiles 조회 및 제어
 *  - CT10(Control), S10(Simulation), C10(Config), N10(NVS), M10(Motion), P10(PWM) 연동
 *  - Preset × Style × Adjust 기반 바람 프로파일 제어
 *  - Manual Override / User Profile 선택 / Schedule 기반 운전 제어
 * ------------------------------------------------------
 * [구현 규칙]
 *  - 항상 소스 시작 주석 부분 체계 유지 및 내용 업데이트
 *  - 소스 시작 주석 부분 구현규칙, 코드네이밍규칙 내용 그대로 유지, 수정금지
 *  - ArduinoJson v7.x.x 사용 (v6 이하 사용 금지)
 *  - JsonDocument 단일 타입만 사용
 *  - createNestedArray/Object/containsKey 사용 금지
 *  - memset + strlcpy 기반 안전 초기화
 *  - 주석/필드명은 JSON 구조와 동일하게 유지
 *  - 모듈별 단일 헤더(h)파일로만 구성 (cpp 없음)
 * ------------------------------------------------------
 * [코드 네이밍 규칙]
 *   - 전역 상수,매크로      : G_모듈약어_ 접두사
 *   - 전역 변수             : g_모듈약어_ 접두사
 *   - 전역 함수             : 모듈약어_ 접두사
 *   - type                  : T_모듈약어_ 접두사
 *   - typedef               : _t  접미사
 *   - enum 상수             : EN_모듈약어_ 접두사
 *   - 구조체                : ST_모듈약어_ 접두사
 *   - 클래스명              : CL_모듈약어_ 접두사
 *   - 클래스 private 멤버   : _ 접두사
 *   - 클래스 멤버(함수/변수) : 모듈약어 접두사 미사용
 *   - 클래스 정적 멤버      : s_ 접두사
 * ------------------------------------------------------
 */

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <ESPAsyncWebServer.h>

#include "A10_Const_014.h"
#include "C10_ConfigManager_020.h"
#include "CT10_ControlManager_018.h"
#include "S10_Simulation_017.h"
#include "M10_MotionLogic_014.h"
#include "P10_PWM_ctrl_014.h"
#include "N10_NvsManager_016.h"
#include "D10_Logger_014.h"

class CL_W10_WebAPI {
public:
    // ==================================================
    // 초기화
    // ==================================================
    static void begin(AsyncWebServer& p_server) {
        s_server = &p_server;

        routeVersion();
        routeState();
        routeWindProfile();
        routeSchedules();
        routeUserProfiles();
        routeControl();
        routeSimulation();
        routeLogs();
        routeReload();

        CL_D10_Logger::log(EN_L10_LOG_INFO, "[W10] WebAPI v020 initialized");
    }

private:
    // ==================================================
    // 정적 멤버
    // ==================================================
    static AsyncWebServer* s_server;

    // ==================================================
    // 공통 유틸
    // ==================================================
    static void sendJson(AsyncWebServerRequest* p_req, JsonDocument& p_doc, int p_code = 200) {
        String v_out;
        serializeJson(p_doc, v_out);
        p_req->send(p_code, "application/json", v_out);
    }

    static bool parseJsonBody(AsyncWebServerRequest* p_req, JsonDocument& p_doc) {
        if (!p_req->hasParam("plain", true)) {
            return false;
        }
        AsyncWebParameter* v_p = p_req->getParam("plain", true);
        auto v_err = deserializeJson(p_doc, v_p->value());
        if (v_err) {
            CL_D10_Logger::log(EN_L10_LOG_WARN, "[W10] JSON parse error: %s", v_err.c_str());
            return false;
        }
        return true;
    }

    // ==================================================
    // 1. /api/version
    // ==================================================
    static void routeVersion() {
        s_server->on("/api/version", HTTP_GET, [](AsyncWebServerRequest* p_req) {
            JsonDocument v_doc;
            v_doc["module"] = "SmartNatureWind";
            v_doc["api"]    = "W10_WebAPI_020";
            v_doc["ct10"]   = "CT10_ControlManager_018";
            v_doc["s10"]    = "S10_Simulation_017";
            v_doc["c10"]    = "C10_ConfigManager_020";
            sendJson(p_req, v_doc);
        });
    }

    // ==================================================
    // 2. /api/state
    //  - Control / Simulation / Motion / AutoOff / Source 상태 집계
    // ==================================================
    static void routeState() {
        s_server->on("/api/state", HTTP_GET, [](AsyncWebServerRequest* p_req) {
            JsonDocument v_doc;

            // CT10: 제어 상태(JSON 내부에서 "control" 섹션 등 채운다고 가정)
            CL_CT10_ControlManager::CT10_toJson(v_doc);

            // S10: 시뮬레이션 상태 ("sim" 섹션)
            CL_S10_Simulation::S10_toJson(v_doc);

            // Motion: 현 존재 여부 상태
            ST_M10_MotionState_t v_ms = CL_M10_MotionLogic::M10_getState();
            v_doc["motion"]["present"]    = v_ms.present;
            v_doc["motion"]["pir"]        = v_ms.pirActive;
            v_doc["motion"]["ble"]        = v_ms.bleActive;
            v_doc["motion"]["lastChange"] = v_ms.lastChangeSec;

            sendJson(p_req, v_doc);
        });
    }

    // ==================================================
    // 3. /api/windProfile
    //  - windProfile 사전 조회 (Preset / Style)
    // ==================================================
    static void routeWindProfile() {
        s_server->on("/api/windProfile", HTTP_GET, [](AsyncWebServerRequest* p_req) {
            JsonDocument v_doc;
            ST_A10_WindProfileDict_t v_dict;
            memset(&v_dict, 0, sizeof(v_dict));

            if (!CL_C10_ConfigManager::C10_loadWindProfileDict(v_dict)) {
                p_req->send(500, "application/json", "{\"error\":\"load windProfile failed\"}");
                return;
            }

            v_doc["windProfile"]["version"] = v_dict.version;

            // presets
            for (uint8_t v_i = 0; v_i < v_dict.presetCount; v_i++) {
                const ST_A10_WindPresetDef_t& v_p = v_dict.presets[v_i];
                JsonObject v_jp = v_doc["windProfile"]["presets"][v_i];
                v_jp["name"] = v_p.name;
                v_jp["code"] = v_p.code;

                v_jp["base"]["wind_intensity"]             = v_p.base.wind_intensity;
                v_jp["base"]["gust_frequency"]             = v_p.base.gust_frequency;
                v_jp["base"]["wind_variability"]           = v_p.base.wind_variability;
                v_jp["base"]["fan_limit"]                  = v_p.base.fan_limit;
                v_jp["base"]["min_fan"]                    = v_p.base.min_fan;
                v_jp["base"]["turbulence_length_scale"]    = v_p.base.turbulence_length_scale;
                v_jp["base"]["turbulence_intensity_sigma"] = v_p.base.turbulence_intensity_sigma;
                v_jp["base"]["thermal_bubble_strength"]    = v_p.base.thermal_bubble_strength;
                v_jp["base"]["thermal_bubble_radius"]      = v_p.base.thermal_bubble_radius;
            }

            // styles
            for (uint8_t v_i = 0; v_i < v_dict.styleCount; v_i++) {
                const ST_A10_WindStyleDef_t& v_s = v_dict.styles[v_i];
                JsonObject v_js = v_doc["windProfile"]["styles"][v_i];
                v_js["name"] = v_s.name;
                v_js["code"] = v_s.code;
                v_js["factors"]["intensity_factor"]   = v_s.factor.intensity_factor;
                v_js["factors"]["variability_factor"] = v_s.factor.variability_factor;
                v_js["factors"]["gust_factor"]        = v_s.factor.gust_factor;
                v_js["factors"]["thermal_factor"]     = v_s.factor.thermal_factor;
            }

            sendJson(p_req, v_doc);
        });
    }

    // ==================================================
    // 4. /api/schedules
    //  - GET  : cfg_schedules_xxx.json 조회
    //  - POST : 전체 스케줄 교체 저장
    // ==================================================
    static void routeSchedules() {
        // GET
        s_server->on("/api/schedules", HTTP_GET, [](AsyncWebServerRequest* p_req) {
            JsonDocument v_doc;
            ST_A10_ScheduleConfig v_cfg;
            memset(&v_cfg, 0, sizeof(v_cfg));

            if (!CL_C10_ConfigManager::C10_loadSchedules(v_cfg)) {
                p_req->send(500, "application/json", "{\"error\":\"load schedules failed\"}");
                return;
            }

            CL_C10_ConfigManager::C10_toJson_Schedules(v_cfg, v_doc);
            sendJson(p_req, v_doc);
        });

        // POST
        s_server->on(
            "/api/schedules",
            HTTP_POST,
            [](AsyncWebServerRequest* p_req) {},
            nullptr,
            [](AsyncWebServerRequest* p_req,
               uint8_t* p_data,
               size_t p_len,
               size_t p_index,
               size_t p_total) {
                if (p_index + p_len != p_total) {
                    return;
                }

                JsonDocument v_doc;
                auto v_err = deserializeJson(v_doc, (const char*)p_data, p_len);
                if (v_err) {
                    p_req->send(400, "application/json", "{\"error\":\"json parse\"}");
                    return;
                }

                ST_A10_ScheduleConfig v_cfg;
                memset(&v_cfg, 0, sizeof(v_cfg));
                if (!fromJson_Schedules(v_doc, v_cfg)) {
                    p_req->send(400, "application/json", "{\"error\":\"invalid schedules\"}");
                    return;
                }

                if (!CL_C10_ConfigManager::C10_saveSchedules(v_cfg)) {
                    p_req->send(500, "application/json", "{\"error\":\"save failed\"}");
                    return;
                }

                CL_N10_NvsManager::N10_markDirty("schedules", true);
                p_req->send(200, "application/json", "{\"result\":\"ok\"}");
            }
        );
    }

    // JSON → ST_A10_ScheduleConfig 변환 (로컬 유틸)
    static bool fromJson_Schedules(const JsonDocument& p_doc, ST_A10_ScheduleConfig& p_cfg) {
        if (!p_doc["schedules"].is<JsonArrayConst>() &&
            !p_doc["schedules"][0].is<JsonObjectConst>()) {
            // 허용: {"schedules":[...]} 형식만 가정
        }

        JsonArrayConst v_arr = p_doc["schedules"].as<JsonArrayConst>();
        p_cfg.count = 0;

        for (JsonObjectConst v_js : v_arr) {
            if (p_cfg.count >= A10_Const::MAX_SCHEDULES) break;
            ST_A10_ScheduleItem_t& v_s = p_cfg.items[p_cfg.count++];

            v_s.schNo   = v_js["schNo"]   | 0;
            strlcpy(v_s.name, v_js["name"] | "", sizeof(v_s.name));
            v_s.enabled = v_js["enabled"] | true;

            v_s.period.enabled = v_js["period"]["enabled"] | false;
            for (uint8_t v_d = 0; v_d < 7; v_d++) {
                v_s.period.days[v_d] = v_js["period"]["days"][v_d] | 1;
            }
            strlcpy(v_s.period.start_time,
                    v_js["period"]["start_time"] | "00:00",
                    sizeof(v_s.period.start_time));
            strlcpy(v_s.period.end_time,
                    v_js["period"]["end_time"] | "23:59",
                    sizeof(v_s.period.end_time));

            v_s.segCount = 0;
            if (v_js["segments"].is<JsonArrayConst>()) {
                JsonArrayConst v_segArr = v_js["segments"].as<JsonArrayConst>();
                for (JsonObjectConst v_jseg : v_segArr) {
                    if (v_s.segCount >= A10_Const::MAX_SEGMENTS_PER_SCHEDULE) break;
                    ST_A10_OpSegment_t& v_sg = v_s.segments[v_s.segCount++];

                    v_sg.segNo       = v_jseg["segNo"]       | 0;
                    v_sg.on_minutes  = v_jseg["on_minutes"]  | 10;
                    v_sg.off_minutes = v_jseg["off_minutes"] | 0;
                    strlcpy(v_sg.mode, v_jseg["mode"] | "PRESET", sizeof(v_sg.mode));
                    strlcpy(v_sg.presetCode,
                            v_jseg["presetCode"] | "",
                            sizeof(v_sg.presetCode));
                    strlcpy(v_sg.styleCode,
                            v_jseg["styleCode"] | "",
                            sizeof(v_sg.styleCode));

                    memset(&v_sg.adjust, 0, sizeof(v_sg.adjust));
                    if (v_jseg["adjust"].is<JsonObjectConst>()) {
                        JsonObjectConst v_aj = v_jseg["adjust"];
                        v_sg.adjust.wind_intensity   = v_aj["wind_intensity"]   | 0.0f;
                        v_sg.adjust.wind_variability = v_aj["wind_variability"] | 0.0f;
                        v_sg.adjust.gust_frequency   = v_aj["gust_frequency"]   | 0.0f;
                        v_sg.adjust.fan_limit        = v_aj["fan_limit"]        | 0.0f;
                        v_sg.adjust.min_fan          = v_aj["min_fan"]          | 0.0f;
                    }

                    v_sg.fixed_speed = v_jseg["fixed_speed"] | 0.0f;
                }
            }

            memset(&v_s.autoOff, 0, sizeof(v_s.autoOff));
            if (v_js["autoOff"].is<JsonObjectConst>()) {
                JsonObjectConst v_ao = v_js["autoOff"];
                v_s.autoOff.timer.enabled   = v_ao["timer"]["enabled"]   | false;
                v_s.autoOff.timer.minutes   = v_ao["timer"]["minutes"]   | 0;
                v_s.autoOff.offTime.enabled = v_ao["offTime"]["enabled"] | false;
                strlcpy(v_s.autoOff.offTime.time,
                        v_ao["offTime"]["time"] | "",
                        sizeof(v_s.autoOff.offTime.time));
                v_s.autoOff.offTemp.enabled = v_ao["offTemp"]["enabled"] | false;
                v_s.autoOff.offTemp.temp    = v_ao["offTemp"]["temp"]    | 0.0f;
            }

            v_s.motion.pir.enabled        = v_js["motion"]["pir"]["enabled"]        | false;
            v_s.motion.pir.hold_sec       = v_js["motion"]["pir"]["hold_sec"]       | 0;
            v_s.motion.ble.enabled        = v_js["motion"]["ble"]["enabled"]        | false;
            v_s.motion.ble.rssi_threshold = v_js["motion"]["ble"]["rssi_threshold"] | -70;
            v_s.motion.ble.hold_sec       = v_js["motion"]["ble"]["hold_sec"]       | 0;
        }

        return true;
    }

    // ==================================================
    // 5. /api/userProfiles
    //  - GET  : cfg_uzOpProfile_xxx.json 조회
    //  - POST : 전체 userProfiles 교체 저장
    //  - POST : /api/userProfiles/select?id=n
    // ==================================================
    static void routeUserProfiles() {
        // GET
        s_server->on("/api/userProfiles", HTTP_GET, [](AsyncWebServerRequest* p_req) {
            JsonDocument v_doc;
            ST_A10_UserProfileConfig_t v_cfg;
            memset(&v_cfg, 0, sizeof(v_cfg));

            if (!CL_C10_ConfigManager::C10_loadUserProfiles(v_cfg)) {
                p_req->send(500, "application/json", "{\"error\":\"load userProfiles failed\"}");
                return;
            }

            CL_C10_ConfigManager::C10_toJson_UserProfiles(v_cfg, v_doc);
            sendJson(p_req, v_doc);
        });

        // POST: 전체 교체
        s_server->on(
            "/api/userProfiles",
            HTTP_POST,
            [](AsyncWebServerRequest* p_req) {},
            nullptr,
            [](AsyncWebServerRequest* p_req,
               uint8_t* p_data,
               size_t p_len,
               size_t p_index,
               size_t p_total) {
                if (p_index + p_len != p_total) {
                    return;
                }

                JsonDocument v_doc;
                auto v_err = deserializeJson(v_doc, (const char*)p_data, p_len);
                if (v_err) {
                    p_req->send(400, "application/json", "{\"error\":\"json parse\"}");
                    return;
                }

                ST_A10_UserProfileConfig_t v_cfg;
                memset(&v_cfg, 0, sizeof(v_cfg));
                if (!fromJson_UserProfiles(v_doc, v_cfg)) {
                    p_req->send(400, "application/json", "{\"error\":\"invalid userProfiles\"}");
                    return;
                }

                if (!CL_C10_ConfigManager::C10_saveUserProfiles(v_cfg)) {
                    p_req->send(500, "application/json", "{\"error\":\"save failed\"}");
                    return;
                }

                CL_N10_NvsManager::N10_markDirty("userProfiles", true);
                p_req->send(200, "application/json", "{\"result\":\"ok\"}");
            }
        );

        // POST: select
        s_server->on("/api/userProfiles/select", HTTP_POST, [](AsyncWebServerRequest* p_req) {
            if (!p_req->hasParam("id", true)) {
                p_req->send(400, "application/json", "{\"error\":\"missing id\"}");
                return;
            }
            int v_id = p_req->getParam("id", true)->value().toInt();
            if (!CL_CT10_ControlManager::CT10_setActiveUserProfile(v_id)) {
                p_req->send(400, "application/json", "{\"error\":\"invalid profile\"}");
                return;
            }
            p_req->send(200, "application/json", "{\"result\":\"ok\"}");
        });
    }

    // JSON → ST_A10_UserProfileConfig_t 변환 (로컬 유틸)
    static bool fromJson_UserProfiles(const JsonDocument& p_doc, ST_A10_UserProfileConfig_t& p_cfg) {
        JsonArrayConst v_arr = p_doc["userProfiles"]["profiles"].as<JsonArrayConst>();
        p_cfg.count = 0;

        for (JsonObjectConst v_jp : v_arr) {
            if (p_cfg.count >= A10_Const::MAX_USER_PROFILES) break;
            ST_A10_UserProfile_t& v_up = p_cfg.items[p_cfg.count++];

            v_up.profileNo = v_jp["profileNo"] | 0;
            strlcpy(v_up.name, v_jp["name"] | "", sizeof(v_up.name));
            v_up.enabled        = v_jp["enabled"]        | true;
            v_up.repeatSegments = v_jp["repeatSegments"] | true;

            v_up.segCount = 0;
            if (v_jp["segments"].is<JsonArrayConst>()) {
                JsonArrayConst v_sArr = v_jp["segments"].as<JsonArrayConst>();
                for (JsonObjectConst v_js : v_sArr) {
                    if (v_up.segCount >= A10_Const::MAX_SEGMENTS_PER_PROFILE) break;
                    ST_A10_OpSegment_t& v_sg = v_up.segments[v_up.segCount++];

                    v_sg.segNo       = v_js["segNo"]       | 0;
                    v_sg.on_minutes  = v_js["on_minutes"]  | 10;
                    v_sg.off_minutes = v_js["off_minutes"] | 0;
                    strlcpy(v_sg.mode, v_js["mode"] | "PRESET", sizeof(v_sg.mode));
                    strlcpy(v_sg.presetCode,
                            v_js["presetCode"] | "",
                            sizeof(v_sg.presetCode));
                    strlcpy(v_sg.styleCode,
                            v_js["styleCode"] | "",
                            sizeof(v_sg.styleCode));

                    memset(&v_sg.adjust, 0, sizeof(v_sg.adjust));
                    if (v_js["adjust"].is<JsonObjectConst>()) {
                        JsonObjectConst v_aj = v_js["adjust"];
                        v_sg.adjust.wind_intensity   = v_aj["wind_intensity"]   | 0.0f;
                        v_sg.adjust.wind_variability = v_aj["wind_variability"] | 0.0f;
                        v_sg.adjust.gust_frequency   = v_aj["gust_frequency"]   | 0.0f;
                        v_sg.adjust.fan_limit        = v_aj["fan_limit"]        | 0.0f;
                        v_sg.adjust.min_fan          = v_aj["min_fan"]          | 0.0f;
                    }

                    v_sg.fixed_speed = v_js["fixed_speed"] | 0.0f;
                }
            }

            memset(&v_up.autoOff, 0, sizeof(v_up.autoOff));
            if (v_jp["autoOff"].is<JsonObjectConst>()) {
                JsonObjectConst v_ao = v_jp["autoOff"];
                v_up.autoOff.timer.enabled   = v_ao["timer"]["enabled"]   | false;
                v_up.autoOff.timer.minutes   = v_ao["timer"]["minutes"]   | 0;
                v_up.autoOff.offTime.enabled = v_ao["offTime"]["enabled"] | false;
                strlcpy(v_up.autoOff.offTime.time,
                        v_ao["offTime"]["time"] | "",
                        sizeof(v_up.autoOff.offTime.time));
                v_up.autoOff.offTemp.enabled = v_ao["offTemp"]["enabled"] | false;
                v_up.autoOff.offTemp.temp    = v_ao["offTemp"]["temp"]    | 0.0f;
            }

            v_up.motion.pir.enabled        = v_jp["motion"]["pir"]["enabled"]        | false;
            v_up.motion.pir.hold_sec       = v_jp["motion"]["pir"]["hold_sec"]       | 0;
            v_up.motion.ble.enabled        = v_jp["motion"]["ble"]["enabled"]        | false;
            v_up.motion.ble.rssi_threshold = v_jp["motion"]["ble"]["rssi_threshold"] | -70;
            v_up.motion.ble.hold_sec       = v_jp["motion"]["ble"]["hold_sec"]       | 0;
        }

        return true;
    }

    // ==================================================
    // 6. /api/control
    //  - mode 전환 (schedule / profile)
    //  - manual override (fixed / preset+style)
    //  - override clear
    // ==================================================
    static void routeControl() {
        // 모드 전환
        s_server->on("/api/control/mode", HTTP_POST, [](AsyncWebServerRequest* p_req) {
            if (!p_req->hasParam("mode", true)) {
                p_req->send(400, "application/json", "{\"error\":\"missing mode\"}");
                return;
            }
            String v_m = p_req->getParam("mode", true)->value();
            if (v_m == "schedule") {
                CL_CT10_ControlManager::CT10_setMode(false);
            } else if (v_m == "profile") {
                CL_CT10_ControlManager::CT10_setMode(true);
            } else {
                p_req->send(400, "application/json", "{\"error\":\"invalid mode\"}");
                return;
            }
            p_req->send(200, "application/json", "{\"result\":\"ok\"}");
        });

        // manual fixed
        s_server->on("/api/control/override/fixed", HTTP_POST, [](AsyncWebServerRequest* p_req) {
            if (!p_req->hasParam("speed", true)) {
                p_req->send(400, "application/json", "{\"error\":\"missing speed\"}");
                return;
            }
            float v_sp = p_req->getParam("speed", true)->value().toFloat();
            ST_A10_ResolvedWind_t v_w;
            memset(&v_w, 0, sizeof(v_w));
            v_w.valid      = true;
            v_w.fixedMode  = true;
            v_w.fixedSpeed = v_sp;
            CL_CT10_ControlManager::CT10_applyManual(v_w);
            p_req->send(200, "application/json", "{\"result\":\"ok\"}");
        });

        // manual preset+style
        s_server->on(
            "/api/control/override/preset",
            HTTP_POST,
            [](AsyncWebServerRequest* p_req) {},
            nullptr,
            [](AsyncWebServerRequest* p_req,
               uint8_t* p_data,
               size_t p_len,
               size_t p_index,
               size_t p_total) {
                if (p_index + p_len != p_total) {
                    return;
                }

                JsonDocument v_doc;
                auto v_err = deserializeJson(v_doc, (const char*)p_data, p_len);
                if (v_err) {
                    p_req->send(400, "application/json", "{\"error\":\"json parse\"}");
                    return;
                }

                const char* v_preset = v_doc["presetCode"] | "";
                const char* v_style  = v_doc["styleCode"]  | "BALANCE";

                ST_A10_AdjustDelta_t v_adj;
                memset(&v_adj, 0, sizeof(v_adj));
                bool v_adjValid = false;
                if (v_doc["adjust"].is<JsonObjectConst>()) {
                    JsonObjectConst v_aj = v_doc["adjust"];
                    v_adj.valid            = true;
                    v_adj.wind_intensity   = v_aj["wind_intensity"]   | 0.0f;
                    v_adj.wind_variability = v_aj["wind_variability"] | 0.0f;
                    v_adj.gust_frequency   = v_aj["gust_frequency"]   | 0.0f;
                    v_adj.fan_limit        = v_aj["fan_limit"]        | 0.0f;
                    v_adj.min_fan          = v_aj["min_fan"]          | 0.0f;
                    v_adjValid = true;
                }

                ST_A10_WindProfileDict_t v_dict;
                memset(&v_dict, 0, sizeof(v_dict));
                if (!CL_C10_ConfigManager::C10_loadWindProfileDict(v_dict)) {
                    p_req->send(500, "application/json", "{\"error\":\"load dict failed\"}");
                    return;
                }

                ST_A10_ResolvedWind_t v_w;
                memset(&v_w, 0, sizeof(v_w));
                if (!CL_C10_ConfigManager::C10_resolveWindParams(
                        v_dict,
                        v_preset,
                        v_style,
                        v_adjValid ? &v_adj : nullptr,
                        v_w)) {
                    p_req->send(400, "application/json", "{\"error\":\"resolve failed\"}");
                    return;
                }

                CL_CT10_ControlManager::CT10_applyManual(v_w);
                p_req->send(200, "application/json", "{\"result\":\"ok\"}");
            }
        );

        // override clear
        s_server->on("/api/control/override/clear", HTTP_POST, [](AsyncWebServerRequest* p_req) {
            CL_CT10_ControlManager::CT10_clearManual();
            p_req->send(200, "application/json", "{\"result\":\"ok\"}");
        });
    }

    // ==================================================
    // 7. /api/sim
    //  - 시뮬레이션 Chart 조회
    // ==================================================
    static void routeSimulation() {
        s_server->on("/api/sim/chart", HTTP_GET, [](AsyncWebServerRequest* p_req) {
            JsonDocument v_doc;
            CL_S10_Simulation::S10_toChartJson(v_doc);
            sendJson(p_req, v_doc);
        });
    }

    // ==================================================
    // 8. /api/logs
    // ==================================================
    static void routeLogs() {
        s_server->on("/api/logs", HTTP_GET, [](AsyncWebServerRequest* p_req) {
            String v_logs = CL_D10_Logger::L10_getLogsJson();
            p_req->send(200, "application/json", v_logs);
        });
    }

    // ==================================================
    // 9. /api/reload
    //  - 설정 전체 재로드 (개발/복구용)
    // ==================================================
    static void routeReload() {
        s_server->on("/api/reload", HTTP_POST, [](AsyncWebServerRequest* p_req) {
            ST_A10_ConfigRoot v_root;
            bool v_ok = CL_C10_ConfigManager::C10_loadAll(v_root);
            if (!v_ok) {
                p_req->send(500, "application/json", "{\"error\":\"reload failed\"}");
                return;
            }
            // Control / Simulation 쪽에서 C10 재로드를 반영하는 훅이 있다면 여기서 호출
            CL_CT10_ControlManager::CT10_onConfigReload(v_root);
            p_req->send(200, "application/json", "{\"result\":\"ok\"}");
        });
    }
};

// ------------------------------------------------------
// 정적 멤버 정의
// ------------------------------------------------------
AsyncWebServer* CL_W10_WebAPI::s_server = nullptr;
