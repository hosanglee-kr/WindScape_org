#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : CT10_ControlManager_018.h
 * 모듈약어 : CT10
 * 모듈명 : Smart Nature Wind 제어 통합 Manager (v018)
 * ------------------------------------------------------
 * 기능 요약:
 *  - Schedule / UserProfile / Override 기반 풍속 제어
 *  - WindDict 기반 해석 (C10_resolveWindParams) 후 S10.applyResolvedWind 연동
 *  - PWM(P10) / Simulation(S10) 통합 제어
 *  - Motion (PIR / BLE) 및 AutoOff 조건 훅 제공
 *  - Web UI / 버튼에서 Profile 선택, Override 즉시 반영
 *  - JSON 상태 Export (control / override / autoOff / sim)
 * ------------------------------------------------------
 * [구현 규칙]
 *  - 항상 소스 시작 주석 부분 체계 유지 및 내용 업데이트
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
 *   - 함수 로컬 변수        : v_ 접두사
 *   - 함수 인자             : p_ 접두사
 * ------------------------------------------------------
 */

#include <Arduino.h>
#include <ArduinoJson.h>
#include <time.h>

#include "A10_Const_014.h"
#include "C10_ConfigManager_020.h"
#include "S10_Simulation_017.h"
#include "P10_PWM_ctrl_014.h"
#include "D10_Logger_014.h"

// Motion 모듈 (예: M10) 인터페이스 가정
class CL_M10_MotionLogic {
public:
    bool isPresenceActive() { return true; } // 기본값: 항상 활동 감지됨 (차단 안 함)
};

// ------------------------------------------------------
// 런타임 상태 구조체
// ------------------------------------------------------
typedef enum : uint8_t {
    EN_CT10_RUN_NONE         = 0,
    EN_CT10_RUN_SCHEDULE     = 1,
    EN_CT10_RUN_USER_PROFILE = 2
} EN_CT10_run_source_t;

// Override 제어 상태
typedef struct {
    bool                    active;
    unsigned long           endMs;
    bool                    useFixed;
    float                   fixedPercent;
    char                    presetCode[24];
    char                    styleCode[24];
    ST_A10_AdjustDelta_t    adjust;
} ST_CT10_Override_t;

// 세그먼트 실행 상태
typedef struct {
    int8_t          index;
    bool            onPhase;
    unsigned long   phaseStartMs;
} ST_CT10_SegmentRuntime_t;

// AutoOff 런타임 상태
typedef struct {
    bool        timerArmed;
    unsigned long timerStartMs;
    uint32_t    timerMinutes;

    bool        offTimeEnabled;
    uint16_t    offTimeMinutes;

    bool        offTempEnabled;
    float       offTemp;
} ST_CT10_AutoOffRuntime_t;

// ======================================================
// CL_CT10_ControlManager 클래스
// ======================================================
class CL_CT10_ControlManager {
public:
    bool                        active = false;
    EN_CT10_run_source_t        runSource = EN_CT10_RUN_NONE;

    int8_t                      curScheduleIndex = -1;
    int8_t                      curProfileIndex  = -1;

    ST_CT10_SegmentRuntime_t    scheduleSegRt;
    ST_CT10_SegmentRuntime_t    profileSegRt;

    ST_CT10_Override_t          overrideState;
    ST_CT10_AutoOffRuntime_t    autoOffRt;

    CL_P10_PWM*                 pwm = nullptr;
    CL_M10_MotionLogic*         motion = nullptr;

    CL_S10_Simulation           sim;

    unsigned long               lastTickMs = 0;

public:
    // --------------------------------------------------
    // 초기화
    // --------------------------------------------------
    void begin(CL_P10_PWM& p_pwm) {
        pwm = &p_pwm;
        memset(&overrideState, 0, sizeof(overrideState));
        memset(&scheduleSegRt, 0, sizeof(scheduleSegRt));
        memset(&profileSegRt, 0, sizeof(profileSegRt));
        memset(&autoOffRt, 0, sizeof(autoOffRt));

        scheduleSegRt.index = -1;
        profileSegRt.index  = -1;

        sim.begin(p_pwm);
        active = true;
        CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] begin()");
    }

    void setMotion(CL_M10_MotionLogic* p_motion) { motion = p_motion; }

    // --------------------------------------------------
    // UserProfile 작동 제어
    // --------------------------------------------------
    bool startUserProfileByNo(uint8_t p_profileNo) {
        if (!g_A10_config_root.userProfiles) return false;

        auto& v_cfg = *g_A10_config_root.userProfiles;
        for (uint8_t v_i = 0; v_i < v_cfg.count; v_i++) {
            const ST_A10_UserProfile_t& v_p = v_cfg.items[v_i];
            if (!v_p.enabled) continue;
            if (v_p.profileNo == p_profileNo) {
                runSource = EN_CT10_RUN_USER_PROFILE;
                curProfileIndex = v_i;
                profileSegRt.index = -1;
                profileSegRt.onPhase = true;
                _initAutoOffFromUserProfile(v_p);
                CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] Start UserProfile #%u (%s)", p_profileNo, v_p.name);
                return true;
            }
        }
        return false;
    }

    void stopUserProfile() {
        if (runSource == EN_CT10_RUN_USER_PROFILE) {
            runSource = EN_CT10_RUN_NONE;
            curProfileIndex = -1;
            sim.stop();
            CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] UserProfile stopped");
        }
    }

    // --------------------------------------------------
    // Override (수동 제어)
    // --------------------------------------------------
    void startOverrideFixed(float p_percent, uint32_t p_seconds) {
        if (p_seconds == 0) return;
        memset(&overrideState, 0, sizeof(overrideState));
        overrideState.active = true;
        overrideState.useFixed = true;
        overrideState.fixedPercent = constrain(p_percent, 0.0f, 100.0f);
        overrideState.endMs = millis() + (p_seconds * 1000UL);
        CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] Override FIXED %.1f%% for %lu sec", p_percent, (unsigned long)p_seconds);
    }

    void startOverridePreset(const char* p_presetCode,
                             const char* p_styleCode,
                             const ST_A10_AdjustDelta_t* p_adj,
                             uint32_t p_seconds) {
        if (p_seconds == 0) return;
        memset(&overrideState, 0, sizeof(overrideState));
        overrideState.active = true;
        overrideState.useFixed = false;
        overrideState.endMs = millis() + (p_seconds * 1000UL);
        strlcpy(overrideState.presetCode, p_presetCode, sizeof(overrideState.presetCode));
        strlcpy(overrideState.styleCode,  p_styleCode,  sizeof(overrideState.styleCode));
        if (p_adj) overrideState.adjust = *p_adj;
        CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] Override PRESET (%s,%s) for %lu sec", p_presetCode, p_styleCode, (unsigned long)p_seconds);
    }

    void stopOverride() {
        if (!overrideState.active) return;
        memset(&overrideState, 0, sizeof(overrideState));
        CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] Override cleared");
    }

    // --------------------------------------------------
    // Tick 루프
    // --------------------------------------------------
    void tick() {
        if (!active || !pwm) return;
        unsigned long v_now = millis();
        if (v_now - lastTickMs < 40UL) return;
        lastTickMs = v_now;

        // 1) Override 최우선
        if (_tickOverride()) { sim.tick(); return; }

        // 2) UserProfile
        if (runSource == EN_CT10_RUN_USER_PROFILE && _tickUserProfile()) { sim.tick(); return; }

        // 3) Schedule
        if (_tickSchedule()) { sim.tick(); return; }

        // 4) 아무 것도 없으면 정지
        if (sim.active) sim.stop();
    }

    // --------------------------------------------------
    // JSON 상태 Export
    // --------------------------------------------------
    void toJson(JsonDocument& p_doc) {
        JsonObject o = p_doc["control"].to<JsonObject>();
        o["active"] = active;
        o["runSource"] = (int)runSource;
        o["scheduleIdx"] = curScheduleIndex;
        o["profileIdx"] = curProfileIndex;

        JsonObject ov = o["override"].to<JsonObject>();
        ov["active"] = overrideState.active;
        if (overrideState.active) {
            unsigned long v_now = millis();
            uint32_t remain = (overrideState.endMs > v_now) ? (overrideState.endMs - v_now) / 1000UL : 0;
            ov["remainSec"] = remain;
            ov["useFixed"]  = overrideState.useFixed;
            if (overrideState.useFixed) {
                ov["fixedPercent"] = overrideState.fixedPercent;
            } else {
                ov["presetCode"] = overrideState.presetCode;
                ov["styleCode"]  = overrideState.styleCode;
            }
        }

        JsonObject ao = o["autoOff"].to<JsonObject>();
        ao["timerArmed"] = autoOffRt.timerArmed;
        ao["timerMinutes"] = autoOffRt.timerMinutes;
        ao["offTimeEnabled"] = autoOffRt.offTimeEnabled;
        ao["offTimeMinutes"] = autoOffRt.offTimeMinutes;
        ao["offTempEnabled"] = autoOffRt.offTempEnabled;
        ao["offTemp"] = autoOffRt.offTemp;

        if (pwm) o["pwmDuty"] = pwm->P10_getDutyPercent();
        sim.toJson(p_doc);
    }

private:
    // ==================================================
    // Override 처리
    // ==================================================
    bool _tickOverride() {
        if (!overrideState.active) return false;
        unsigned long now = millis();
        if (now >= overrideState.endMs) { memset(&overrideState, 0, sizeof(overrideState)); return false; }

        if (overrideState.useFixed) {
            sim.stop();
            pwm->P10_setDutyPercent(overrideState.fixedPercent);
            return true;
        }

        if (!g_A10_config_root.windDict) return false;
        ST_A10_ResolvedWind_t v_resolved;
        bool ok = CL_C10_ConfigManager::C10_resolveWindParams(*g_A10_config_root.windDict,
                                                              overrideState.presetCode,
                                                              overrideState.styleCode,
                                                              &overrideState.adjust,
                                                              v_resolved);
        if (ok) sim.applyResolvedWind(v_resolved);
        return true;
    }

    // ==================================================
    // UserProfile Tick
    // ==================================================
    bool _tickUserProfile() {
        if (!g_A10_config_root.userProfiles) return false;
        if (curProfileIndex < 0) return false;

        auto& v_cfg = *g_A10_config_root.userProfiles;
        if ((uint8_t)curProfileIndex >= v_cfg.count) return false;
        ST_A10_UserProfile_t& v_p = v_cfg.items[curProfileIndex];
        if (!v_p.enabled || v_p.segCount == 0) return false;

        if (_checkAutoOff()) { sim.stop(); runSource = EN_CT10_RUN_NONE; curProfileIndex = -1; return true; }

        return _tickSegmentSequence(v_p.repeatSegments, v_p.segments, v_p.segCount, profileSegRt);
    }

    // ==================================================
    // Schedule Tick
    // ==================================================
    bool _tickSchedule() {
        if (!g_A10_config_root.schedules) return false;
        auto& cfg = *g_A10_config_root.schedules;
        int idx = _findActiveScheduleIndex(cfg);
        if (idx < 0) { curScheduleIndex = -1; return false; }

        if (curScheduleIndex != idx) {
            curScheduleIndex = idx;
            _initAutoOffFromSchedule(cfg.items[idx]);
            runSource = EN_CT10_RUN_SCHEDULE;
            CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] Active Schedule idx=%d", idx);
        }

        auto& s = cfg.items[curScheduleIndex];
        if (!s.enabled || s.segCount == 0) return false;
        if (_checkAutoOff()) { sim.stop(); runSource = EN_CT10_RUN_NONE; curScheduleIndex = -1; return true; }
        if (_isMotionBlocked(s.motion)) { sim.stop(); return true; }

        return _tickSegmentSequence(true, s.segments, s.segCount, scheduleSegRt);
    }

    // ==================================================
    // Segment 처리
    // ==================================================
    bool _tickSegmentSequence(bool repeat, ST_A10_OpSegment_t* segs, uint8_t count, ST_CT10_SegmentRuntime_t& rt) {
        unsigned long now = millis();
        if (rt.index < 0) { rt.index = 0; rt.onPhase = true; rt.phaseStartMs = now; _applySegmentOn(segs[0]); return true; }

        if (rt.index >= count) {
            if (!repeat) { sim.stop(); return true; }
            rt.index = 0; rt.onPhase = true; rt.phaseStartMs = now; _applySegmentOn(segs[0]); return true;
        }

        ST_A10_OpSegment_t& seg = segs[rt.index];
        uint32_t onMs  = seg.on_minutes * 60000UL;
        uint32_t offMs = seg.off_minutes * 60000UL;

        if (rt.onPhase && onMs > 0 && now - rt.phaseStartMs >= onMs) { rt.onPhase = false; rt.phaseStartMs = now; _applySegmentOff(); }
        else if (!rt.onPhase && offMs > 0 && now - rt.phaseStartMs >= offMs) {
            rt.index++; if (rt.index >= count && repeat) rt.index = 0;
            rt.onPhase = true; rt.phaseStartMs = now;
            if (rt.index < count) _applySegmentOn(segs[rt.index]); else sim.stop();
        }
        return true;
    }

    void _applySegmentOn(const ST_A10_OpSegment_t& seg) {
        if (!g_A10_config_root.windDict) return;
        if (strcasecmp(seg.mode, "FIXED") == 0) { sim.stop(); pwm->P10_setDutyPercent(seg.fixed_speed); return; }
        ST_A10_ResolvedWind_t res;
        if (CL_C10_ConfigManager::C10_resolveWindParams(*g_A10_config_root.windDict, seg.presetCode, seg.styleCode, &seg.adjust, res))
            sim.applyResolvedWind(res);
    }
    void _applySegmentOff() { sim.stop(); }

    // ==================================================
    // AutoOff
    // ==================================================
    void _initAutoOffFromUserProfile(const ST_A10_UserProfile_t& up) {
        memset(&autoOffRt, 0, sizeof(autoOffRt));
        if (up.autoOff.timer.enabled) { autoOffRt.timerArmed = true; autoOffRt.timerStartMs = millis(); autoOffRt.timerMinutes = up.autoOff.timer.minutes; }
        if (up.autoOff.offTime.enabled) { autoOffRt.offTimeEnabled = true; autoOffRt.offTimeMinutes = _parseHHMMtoMin(up.autoOff.offTime.time); }
        if (up.autoOff.offTemp.enabled) { autoOffRt.offTempEnabled = true; autoOffRt.offTemp = up.autoOff.offTemp.temp; }
    }
    void _initAutoOffFromSchedule(const ST_A10_ScheduleItem_t& s) {
        memset(&autoOffRt, 0, sizeof(autoOffRt));

        if (s.autoOff.timer.enabled) {
            autoOffRt.timerArmed = true;
            autoOffRt.timerStartMs = millis();
            autoOffRt.timerMinutes = s.autoOff.timer.minutes;
        }
        if (s.autoOff.offTime.enabled) {
            autoOffRt.offTimeEnabled = true;
            autoOffRt.offTimeMinutes = _parseHHMMtoMin(s.autoOff.offTime.time);
        }
        if (s.autoOff.offTemp.enabled) {
            autoOffRt.offTempEnabled = true;
            autoOffRt.offTemp = s.autoOff.offTemp.temp;
        }
    }

    // --------------------------------------------------
    // AutoOff 조건 점검
    // --------------------------------------------------
    bool _checkAutoOff() {
        if (!autoOffRt.timerArmed && !autoOffRt.offTimeEnabled && !autoOffRt.offTempEnabled)
            return false;

        unsigned long v_now = millis();

        // 1️⃣ 타이머 만료 확인
        if (autoOffRt.timerArmed && autoOffRt.timerMinutes > 0) {
            uint32_t elapsedMin = (v_now - autoOffRt.timerStartMs) / 60000UL;
            if (elapsedMin >= autoOffRt.timerMinutes) {
                CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] AutoOff(timer) triggered");
                return true;
            }
        }

        // 2️⃣ 지정시간 오프
        if (autoOffRt.offTimeEnabled) {
            time_t t = time(nullptr);
            struct tm* lt = localtime(&t);
            uint16_t curMin = lt->tm_hour * 60 + lt->tm_min;
            if (curMin >= autoOffRt.offTimeMinutes) {
                CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] AutoOff(time %u) triggered", autoOffRt.offTimeMinutes);
                return true;
            }
        }

        // 3️⃣ 온도 기반 (온도값은 추후 외부 센서 입력으로 연동)
        if (autoOffRt.offTempEnabled) {
            float currentTemp = _getCurrentTemperature();
            if (currentTemp >= autoOffRt.offTemp) {
                CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] AutoOff(temp %.1f°C) triggered", currentTemp);
                return true;
            }
        }
        return false;
    }

    // --------------------------------------------------
    // 유틸리티 함수
    // --------------------------------------------------
    uint16_t _parseHHMMtoMin(const char* p_time) {
        if (!p_time || strlen(p_time) < 4) return 0;
        int hh = atoi(p_time);
        const char* colon = strchr(p_time, ':');
        int mm = (colon) ? atoi(colon + 1) : 0;
        return (hh * 60 + mm);
    }

    float _getCurrentTemperature() {
        // 실제 온도 센서 연동 시 교체.
        // 기본값: 24.0°C 반환 (테스트용)
        return 24.0f;
    }

    int _findActiveScheduleIndex(const ST_A10_Schedules_t& cfg) {
        if (cfg.count == 0) return -1;
        time_t now = time(nullptr);
        struct tm* lt = localtime(&now);
        uint8_t wday = (lt->tm_wday == 0) ? 6 : lt->tm_wday - 1; // 일요일 보정
        uint16_t curMin = lt->tm_hour * 60 + lt->tm_min;

        for (int i = 0; i < cfg.count; i++) {
            const ST_A10_ScheduleItem_t& s = cfg.items[i];
            if (!s.enabled || !s.period.enabled) continue;
            if (!s.period.days[wday]) continue;

            uint16_t st = _parseHHMMtoMin(s.period.start_time);
            uint16_t en = _parseHHMMtoMin(s.period.end_time);
            if (st <= en) {
                if (curMin >= st && curMin < en) return i;
            } else {
                // 자정 넘어가는 스케줄
                if (curMin >= st || curMin < en) return i;
            }
        }
        return -1;
    }

    bool _isMotionBlocked(const ST_A10_MotionBinding_t& p_motionCfg) {
        // M10 모듈과 연동 전: 항상 통과시키는 기본 구현.
        if (!motion) return false;
        bool v_needPir = p_motionCfg.pir.enabled;
        bool v_needBle = p_motionCfg.ble.enabled;
        if (!v_needPir && !v_needBle) return false;

        // CL_M10_MotionLogic 구현 시 isPresenceActive()로 판정
        if (!motion->isPresenceActive()) {
            CL_D10_Logger::log(EN_L10_LOG_DEBUG, "[CT10] Motion blocked (no presence)");
            return true;
        }
        return false;
    }
};
