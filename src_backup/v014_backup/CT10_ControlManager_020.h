#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : CT10_ControlManager_020.h
 * 모듈약어 : CT10
 * 모듈명 : Smart Nature Wind 제어 통합 Manager (v020)
 * ------------------------------------------------------
 * 기능 요약:
 *  - Schedule / UserProfile / Manual Override 기반 풍속 제어
 *  - WindDict 기반 해석 (C10_resolveWindParams) 후 S10.applyResolvedWind 연동
 *  - PWM(P10) / Simulation(S10) 통합 제어
 *  - Motion (PIR / BLE) 및 AutoOff 조건 훅 제공
 *  - Web UI / 버튼에서 Profile 선택, Override 즉시 반영
 *  - JSON 상태 Export (control / override / autoOff / sim)
 *  - 정적 싱글톤 인터페이스 제공 (W10_WebAPI에서 직접 사용)
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
 *   - 함수 로컬 변수        : v_ 접두사
 *   - 함수 인자             : p_ 접두사
 * ------------------------------------------------------
 */

#include <Arduino.h>
#include <ArduinoJson.h>
#include <time.h>
#include <string.h>

#include "A10_Const_014.h"
#include "C10_ConfigManager_020.h"
#include "S10_Simulation_018.h"
#include "P10_PWM_ctrl_014.h"
#include "D10_Logger_014.h"
#include "M10_MotionLogic_015.h"


// forward declaration으로 순환참조 방지
class CL_W10_WebAPI;

// ------------------------------------------------------
// 런타임 상태 구조체
// ------------------------------------------------------
typedef enum : uint8_t {
    EN_CT10_RUN_NONE         = 0,
    EN_CT10_RUN_SCHEDULE     = 1,
    EN_CT10_RUN_USER_PROFILE = 2
} EN_CT10_run_source_t;

// Override 제어 상태 (ResolvedWind 기반)
typedef struct {
    bool                    active;             // Override 활성 여부
    bool                    useFixed;           // true: 고정 PWM, false: ResolvedWind 사용
    bool                    resolvedApplied;    // ResolvedWind를 S10에 1회 적용했는지 여부
    unsigned long           endMs;              // 0이면 타이머 없음(무한)
    float                   fixedPercent;       // 0~100, useFixed==true 일 때만 사용
    ST_A10_ResolvedWind_t   resolved;           // 수동 바람 설정 (useFixed==false 일 때)
} ST_CT10_Override_t;

// 세그먼트 실행 상태
typedef struct {
    int8_t          index;          // 현재 seg index (-1이면 아직 시작 전)
    bool            onPhase;        // true: On 구간, false: Off 구간
    unsigned long   phaseStartMs;   // 현재 phase 시작 시간
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
//  - 인스턴스 기반 + 정적 싱글톤 인터페이스 제공
// ======================================================
class CL_CT10_ControlManager {
public:
    // --------------------------------------------------
    // 싱글톤 정적 인터페이스 (W10 등 외부 모듈용)
    // --------------------------------------------------
    static CL_CT10_ControlManager& instance() {
        static CL_CT10_ControlManager v_inst;
        return v_inst;
    }

    // 전역 PWM 인스턴스(g_P10_pwm)를 이용한 초기화
    static bool begin() {
        instance().begin(g_P10_pwm);
        return true;
    }

    // 주기 호출
    static void tick() {
        instance().tick();
    }

    // 상태 JSON Export
    static void toJson(JsonDocument& p_doc) {
        instance().toJson(p_doc);
    }

    // 모드 설정: false=schedule 모드, true=profile 전용 모드
    static void setMode(bool p_profileMode) {
        instance().setMode(p_profileMode);
    }

    // UserProfile 선택/시작 (profileNo 기준)
    static bool setActiveUserProfile(uint8_t p_profileNo) {
        return instance().startUserProfileByNo(p_profileNo);
    }

    // 수동 override 적용 (ResolvedWind 기반)
    static void applyManual(const ST_A10_ResolvedWind_t& p_wind) {
        instance().applyManual(p_wind, 0); // 0 => 타임아웃 없이 유지, clearManual()까지
    }

    // 수동 override 해제
    static void clearManual() {
        instance().stopOverride();
    }

    // Config 전체 재로드 (C10_loadAll 사용)
    static bool reloadAll() {
        bool v_ok = CL_C10_ConfigManager::C10_loadAll(g_A10_config_root);
        if (!v_ok) return false;

        CL_CT10_ControlManager& v_inst = instance();
        v_inst.runSource        = EN_CT10_RUN_NONE;
        v_inst.curScheduleIndex = -1;
        v_inst.curProfileIndex  = -1;
        v_inst.useProfileMode   = false;

        memset(&v_inst.overrideState, 0, sizeof(v_inst.overrideState));
        memset(&v_inst.autoOffRt, 0, sizeof(v_inst.autoOffRt));
        memset(&v_inst.scheduleSegRt, 0, sizeof(v_inst.scheduleSegRt));
        memset(&v_inst.profileSegRt, 0, sizeof(v_inst.profileSegRt));
        v_inst.scheduleSegRt.index = -1;
        v_inst.profileSegRt.index  = -1;

        v_inst.sim.stop();
        CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] reloadAll done");
        return true;
    }

public:
    // --------------------------------------------------
    // 인스턴스 멤버 변수
    // --------------------------------------------------
    bool                        active          = false;
    bool                        useProfileMode  = false; // true면 schedule 무시, profile만 운전

    EN_CT10_run_source_t        runSource       = EN_CT10_RUN_NONE;

    int8_t                      curScheduleIndex = -1;
    int8_t                      curProfileIndex  = -1;

    ST_CT10_SegmentRuntime_t    scheduleSegRt;
    ST_CT10_SegmentRuntime_t    profileSegRt;

    ST_CT10_Override_t          overrideState;
    ST_CT10_AutoOffRuntime_t    autoOffRt;

    CL_P10_PWM*                 pwm     = nullptr;
    CL_M10_MotionLogic*         motion  = nullptr;

    CL_S10_Simulation           sim;

    unsigned long               lastTickMs      = 0;
    unsigned long               lastMetricsPushMs = 0;   // ✅ 메트릭 푸시 주기 관리

public:
    // --------------------------------------------------
    // 초기화 (PWM 인스턴스 주입)
    // --------------------------------------------------
    void begin(CL_P10_PWM& p_pwm) {
        pwm = &p_pwm;

        memset(&overrideState, 0, sizeof(overrideState));
        memset(&scheduleSegRt, 0, sizeof(scheduleSegRt));
        memset(&profileSegRt, 0, sizeof(profileSegRt));
        memset(&autoOffRt, 0, sizeof(autoOffRt));

        scheduleSegRt.index = -1;
        profileSegRt.index  = -1;

        useProfileMode      = false;
        runSource           = EN_CT10_RUN_NONE;
        lastTickMs          = 0;
        lastMetricsPushMs   = 0;

        sim.begin(p_pwm);
        active = true;

        CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] begin()");
    }

    void setMotion(CL_M10_MotionLogic* p_motion) {
        motion = p_motion;
    }

    // 모드 설정: false = Schedule 기반, true = UserProfile 전용 모드
    void setMode(bool p_profileMode) {
        useProfileMode = p_profileMode;
        if (!p_profileMode) {
            // schedule 모드로 전환 시 프로파일은 정지
            stopUserProfile();
        } else {
            // profile 모드로 전환 시 schedule index 초기화
            curScheduleIndex = -1;
            if (runSource == EN_CT10_RUN_SCHEDULE) {
                runSource = EN_CT10_RUN_NONE;
            }
        }
        CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] setMode(profileMode=%d)", p_profileMode ? 1 : 0);
    }

    // --------------------------------------------------
    // UserProfile 작동 제어
    // --------------------------------------------------
    bool startUserProfileByNo(uint8_t p_profileNo) {
        if (!g_A10_config_root.userProfiles) return false;

        ST_A10_UserProfileConfig_t& v_cfg = *g_A10_config_root.userProfiles;
        for (uint8_t v_i = 0; v_i < v_cfg.count; v_i++) {
            const ST_A10_UserProfile_t& v_p = v_cfg.items[v_i];
            if (!v_p.enabled) continue;
            if (v_p.profileNo == p_profileNo) {
                runSource        = EN_CT10_RUN_USER_PROFILE;
                curProfileIndex  = v_i;
                profileSegRt.index      = -1;
                profileSegRt.onPhase    = true;
                profileSegRt.phaseStartMs = millis();

                _initAutoOffFromUserProfile(v_p);

                CL_D10_Logger::log(EN_L10_LOG_INFO,
                                   "[CT10] Start UserProfile #%u (%s)",
                                   (unsigned)p_profileNo, v_p.name);
				_broadcastState();
                return true;
            }
        }
        return false;
    }

    void stopUserProfile() {
        if (runSource == EN_CT10_RUN_USER_PROFILE) {
            runSource        = EN_CT10_RUN_NONE;
            curProfileIndex  = -1;
            profileSegRt.index = -1;
            sim.stop();
			_broadcastState();
            CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] UserProfile stopped");
        }
    }

    // --------------------------------------------------
    // Manual Override (수동 제어)
    // --------------------------------------------------
    // 1) 고정 PWM 비율로 일정 시간 또는 무한 Override
    void startOverrideFixed(float p_percent, uint32_t p_seconds) {
    memset(&overrideState, 0, sizeof(overrideState));
    overrideState.active       = true;
    overrideState.useFixed     = true;
    overrideState.fixedPercent = constrain(p_percent, 0.0f, 100.0f);

    if (p_seconds > 0)
        overrideState.endMs = millis() + (p_seconds * 1000UL);
    else
        overrideState.endMs = 0;

    _broadcastState(true);
    _maybeBroadcastMetrics();

    CL_D10_Logger::log(EN_L10_LOG_INFO,
                       "[CT10] Override FIXED %.1f%% (sec=%lu)",
                       p_percent, (unsigned long)p_seconds);
}

    // 2) preset+style+adjust → ResolvedWind 해석 후 Override 시작
    void startOverridePreset(const char* p_presetCode,
                             const char* p_styleCode,
                             const ST_A10_AdjustDelta_t* p_adj,
                             uint32_t p_seconds) {
        if (!g_A10_config_root.windDict) return;

        ST_A10_ResolvedWind_t v_resolved;
        memset(&v_resolved, 0, sizeof(v_resolved));
        bool v_ok = CL_C10_ConfigManager::C10_resolveWindParams(
                        *g_A10_config_root.windDict,
                        p_presetCode,
                        p_styleCode,
                        p_adj,
                        v_resolved);
        if (!v_ok || !v_resolved.valid) {
            CL_D10_Logger::log(EN_L10_LOG_WARN,
                               "[CT10] startOverridePreset resolve failed (%s,%s)",
                               p_presetCode ? p_presetCode : "",
                               p_styleCode  ? p_styleCode  : "");
            return;
        }
        applyManual(v_resolved, p_seconds);
        _broadcastState(true);
        _maybeBroadcastMetrics();  // ✅ override 시작 직후 metrics 즉시 반영
    }

    // 3) ResolvedWind 직접 수동 적용 (W10 API와 연동)
    void applyManual(const ST_A10_ResolvedWind_t& p_wind, uint32_t p_seconds) {
        if (!p_wind.valid) {
            CL_D10_Logger::log(EN_L10_LOG_WARN, "[CT10] applyManual: invalid ResolvedWind");
            return;
        }

        // Fixed 모드라면 PWM Override로 처리
        if (p_wind.fixedMode) {
            startOverrideFixed(p_wind.fixedSpeed, p_seconds);
            return;
        }

        memset(&overrideState, 0, sizeof(overrideState));
        overrideState.active          = true;
        overrideState.useFixed        = false;
        overrideState.resolvedApplied = false;
        overrideState.fixedPercent    = 0.0f;
        overrideState.resolved        = p_wind;

        if (p_seconds > 0) {
            overrideState.endMs = millis() + (p_seconds * 1000UL);
        } else {
            overrideState.endMs = 0; // 타임아웃 없음
        }

        CL_D10_Logger::log(EN_L10_LOG_INFO,
                           "[CT10] applyManual: preset=%s style=%s (sec=%lu)",
                           p_wind.presetCode,
                           p_wind.styleCode,
                           (unsigned long)p_seconds);
    }

    void stopOverride() {
        if (!overrideState.active) return;
        memset(&overrideState, 0, sizeof(overrideState));
        CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] Override cleared");
		_broadcastState();
    }

    // --------------------------------------------------
    // Tick 루프
    // --------------------------------------------------
    void tick() {
    if (!active || !pwm) return;

    unsigned long v_now = millis();
    if (v_now - lastTickMs < 40UL) return;
    lastTickMs = v_now;

    if (_tickOverride()) {
        sim.tick();
        _maybeBroadcastMetrics();
        return;
    }

    if (useProfileMode) {
        if (runSource == EN_CT10_RUN_USER_PROFILE && _tickUserProfile()) {
            sim.tick();
            _maybeBroadcastMetrics();
            return;
        }
        if (sim.active) {
            sim.stop();
            _maybeBroadcastMetrics();
        }
        return;
    }

    if (runSource == EN_CT10_RUN_USER_PROFILE && _tickUserProfile()) {
        sim.tick();
        _maybeBroadcastMetrics();
        return;
    }

    if (_tickSchedule()) {
        sim.tick();

        JsonDocument v_doc;
        toChartJson(v_doc);
        CL_W10_WebAPI::broadcastChart(v_doc, true);  // diffOnly 적용
        _maybeBroadcastMetrics();
        return;
    }

    if (sim.active) {
        sim.stop();
        _maybeBroadcastMetrics();
    }
}

    // JSON 전체 상태 (control + override + autoOff + sim)
    void toJson(JsonDocument& p_doc) {
        JsonObject v_o = p_doc["control"].to<JsonObject>();
        v_o["active"]         = active;
        v_o["useProfileMode"] = useProfileMode;
        v_o["runSource"]      = (int)runSource;
        v_o["scheduleIdx"]    = curScheduleIndex;
        v_o["profileIdx"]     = curProfileIndex;

        // override
        JsonObject v_ov = v_o["override"].to<JsonObject>();
        v_ov["active"]      = overrideState.active;
        v_ov["useFixed"]    = overrideState.useFixed;
        v_ov["resolved"]    = (!overrideState.useFixed && overrideState.active);
        v_ov["remainSec"]   = _calcOverrideRemainSec();
        if (overrideState.useFixed && overrideState.active) {
            v_ov["fixedPercent"] = overrideState.fixedPercent;
        } else if (overrideState.active && overrideState.resolved.valid) {
            v_ov["presetCode"] = overrideState.resolved.presetCode;
            v_ov["styleCode"]  = overrideState.resolved.styleCode;
        }

        // autoOff
        JsonObject v_ao = v_o["autoOff"].to<JsonObject>();
        v_ao["timerArmed"]     = autoOffRt.timerArmed;
        v_ao["timerMinutes"]   = autoOffRt.timerMinutes;
        v_ao["offTimeEnabled"] = autoOffRt.offTimeEnabled;
        v_ao["offTimeMinutes"] = autoOffRt.offTimeMinutes;
        v_ao["offTempEnabled"] = autoOffRt.offTempEnabled;
        v_ao["offTemp"]        = autoOffRt.offTemp;

        // pwm
        v_o["pwmDuty"] = pwm ? pwm->P10_getDutyPercent() : 0.0f;

        // sim 상태 포함
        sim.toJson(p_doc);

        // 상태 직렬화 이후 dirty 플래그는 외부에서 consume
    }

    // 요약 상태: 가벼운 폴링/심플 UI용
    void toSummaryJson(JsonDocument& p_doc) {
        JsonObject v_s = p_doc["summary"].to<JsonObject>();
        v_s["active"]         = active;
        v_s["runSource"]      = (int)runSource;
        v_s["useProfileMode"] = useProfileMode;
        v_s["scheduleIdx"]    = curScheduleIndex;
        v_s["profileIdx"]     = curProfileIndex;
        v_s["overrideActive"] = overrideState.active;
        v_s["pwmDuty"]        = pwm ? pwm->P10_getDutyPercent() : 0.0f;
        v_s["phase"]          = g_A10_WEATHER_PHASE_NAMES_Arr[(uint8_t)sim.phase];
    }

    // 메트릭 전용: /api/metrics, /ws/metrics 용
    void toMetricsJson(JsonDocument& p_doc) {
        JsonObject v_m = p_doc["metrics"].to<JsonObject>();

        v_m["active"]         = active;
        v_m["runSource"]      = (int)runSource;
        v_m["useProfileMode"] = useProfileMode;
        v_m["scheduleIdx"]    = curScheduleIndex;
        v_m["profileIdx"]     = curProfileIndex;

        v_m["overrideActive"] = overrideState.active;
        v_m["overrideFixed"]  = overrideState.useFixed;
        v_m["overrideRemain"] = _calcOverrideRemainSec();

        v_m["pwmDuty"]        = pwm ? pwm->P10_getDutyPercent() : 0.0f;

        // sim 메트릭
        v_m["simActive"]      = sim.active;
        v_m["simPhase"]       = g_A10_WEATHER_PHASE_NAMES_Arr[(uint8_t)sim.phase];
        v_m["simWind"]        = sim.currentWindSpeed;
        v_m["simTarget"]      = sim.targetWindSpeed;
        v_m["simGust"]        = sim.gustActive;
        v_m["simThermal"]     = sim.thermalActive;

        // AutoOff 메트릭
        v_m["autoOffTimerArmed"]   = autoOffRt.timerArmed;
        v_m["autoOffTimerMinutes"] = autoOffRt.timerMinutes;
        v_m["autoOffOffTime"]      = autoOffRt.offTimeEnabled ? autoOffRt.offTimeMinutes : 0;
        v_m["autoOffOffTemp"]      = autoOffRt.offTempEnabled ? autoOffRt.offTemp : 0.0f;
    }

    // --------------------------------------------------
    // Dirty 플래그 관리 (W10 diffOnly 연동용)
    // --------------------------------------------------
    void markDirty(const char* p_section = nullptr) {
        // section 분기 필요 시 확장 가능
        (void)p_section;
        _dirtyState   = true;
        _dirtyMetrics = true;
        _dirtyChart   = true;
    }

    bool consumeDirtyState() {
        bool v = _dirtyState;
        _dirtyState = false;
        return v;
    }

    bool consumeDirtyMetrics() {
        bool v = _dirtyMetrics;
        _dirtyMetrics = false;
        return v;
    }

    bool consumeDirtyChart() {
        bool v = _dirtyChart;
        _dirtyChart = false;
        return v;
    }




// --------------------------------------------------
// W10 연동용 : 시뮬레이션 차트 데이터 Export
// --------------------------------------------------
void toChartJson(JsonDocument& p_doc) {
    // 1️⃣ S10 모듈이 보유한 차트 상태 직렬화
    sim.toChartJson(p_doc);

    // 2️⃣ 제어 매니저 레벨 정보 추가
    JsonObject v_chart = p_doc["chart"].to<JsonObject>();
    v_chart["pwmDuty"]   = pwm ? pwm->P10_getDutyPercent() : 0.0f;
    v_chart["active"]    = active;
    v_chart["runSource"] = (int)runSource;
    v_chart["phase"]     = g_A10_WEATHER_PHASE_NAMES_Arr[(uint8_t)sim.phase];

    // 3️⃣ override 상태 포함
    v_chart["override"]  = overrideState.active ? (overrideState.useFixed ? "fixed" : "resolved") : "none";
}



private:

    bool _dirtyState  = false;
    bool _dirtyMetrics = false;
    bool _dirtyChart   = false;

    uint32_t _calcOverrideRemainSec() const {
        if (!overrideState.active || overrideState.endMs == 0) return 0;
        unsigned long v_now = millis();
        if (overrideState.endMs <= v_now) return 0;
        return (uint32_t)((overrideState.endMs - v_now) / 1000UL);
    }

    // ==================================================
    // Override 처리
    // ==================================================
    bool _tickOverride() {
        if (!overrideState.active) return false;

        unsigned long v_now = millis();

        // 타임아웃 처리 (endMs == 0이면 무한)
        if (overrideState.endMs != 0 && v_now >= overrideState.endMs) {
            CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] Override timeout");
            memset(&overrideState, 0, sizeof(overrideState));
            return false;
        }

        // 1) 고정 PWM Override
        if (overrideState.useFixed) {
            sim.stop();
            if (pwm) {
                pwm->P10_setDutyPercent(overrideState.fixedPercent);
            }
            return true;
        }

        // 2) ResolvedWind 기반 Override
        if (!overrideState.resolved.valid) {
            CL_D10_Logger::log(EN_L10_LOG_WARN, "[CT10] Override resolved invalid, clear");
            memset(&overrideState, 0, sizeof(overrideState));
            return false;
        }

        // 첫 적용 시에만 S10에 파라미터 전달 (applyResolvedWind는 상태 리셋하기 때문)
        if (!overrideState.resolvedApplied) {
            sim.applyResolvedWind(overrideState.resolved);
            overrideState.resolvedApplied = true;

			// ✅ override 적용 직후 차트 갱신
            JsonDocument v_doc;
            toChartJson(v_doc);
            CL_W10_WebAPI::broadcastChart(v_doc);	
        }

        return true;
    }

    // ==================================================
    // UserProfile Tick
    // ==================================================
    bool _tickUserProfile() {
        if (!g_A10_config_root.userProfiles) return false;
        if (curProfileIndex < 0) return false;

        ST_A10_UserProfileConfig_t& v_cfg = *g_A10_config_root.userProfiles;
        if ((uint8_t)curProfileIndex >= v_cfg.count) return false;

        ST_A10_UserProfile_t& v_p = v_cfg.items[curProfileIndex];
        if (!v_p.enabled || v_p.segCount == 0) return false;

        // AutoOff 조건 체크
        if (_checkAutoOff()) {
            sim.stop();
            runSource        = EN_CT10_RUN_NONE;
            curProfileIndex  = -1;
            CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] UserProfile AutoOff stop");
            return true;
        }

        // Motion 조건 체크 (UserProfile에도 적용)
        if (_isMotionBlocked(v_p.motion)) {
            sim.stop();
            return true;
        }

        return _tickSegmentSequence(
                    v_p.repeatSegments,
                    v_p.segments,
                    v_p.segCount,
                    profileSegRt);
    }

    // ==================================================
    // Schedule Tick
    // ==================================================
    bool _tickSchedule() {
        if (!g_A10_config_root.schedules) return false;

        ST_A10_ScheduleConfig& v_cfg = *g_A10_config_root.schedules;
        int v_idx = _findActiveScheduleIndex(v_cfg);
        if (v_idx < 0) {
            curScheduleIndex = -1;
            return false;
        }

        if (curScheduleIndex != v_idx) {
            curScheduleIndex = v_idx;
            _initAutoOffFromSchedule(v_cfg.items[v_idx]);
            runSource = EN_CT10_RUN_SCHEDULE;
            scheduleSegRt.index = -1;
            scheduleSegRt.onPhase = true;
            scheduleSegRt.phaseStartMs = millis();
            CL_D10_Logger::log(EN_L10_LOG_INFO,
                               "[CT10] Active Schedule idx=%d",
                               v_idx);
        }

        ST_A10_ScheduleItem_t& v_s = v_cfg.items[curScheduleIndex];
        if (!v_s.enabled || v_s.segCount == 0) return false;

        if (_checkAutoOff()) {
            sim.stop();
            runSource        = EN_CT10_RUN_NONE;
            curScheduleIndex = -1;
            CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] Schedule AutoOff stop");
            return true;
        }

        if (_isMotionBlocked(v_s.motion)) {
            sim.stop();
            return true;
        }

        return _tickSegmentSequence(
                    true,
                    v_s.segments,
                    v_s.segCount,
                    scheduleSegRt);
    }

    // ==================================================
    // Segment 시퀀스 처리
    // ==================================================
    bool _tickSegmentSequence(bool p_repeat,
                              ST_A10_OpSegment_t* p_segs,
                              uint8_t p_count,
                              ST_CT10_SegmentRuntime_t& p_rt) {
        unsigned long v_now = millis();

        if (p_rt.index < 0) {
            p_rt.index        = 0;
            p_rt.onPhase      = true;
            p_rt.phaseStartMs = v_now;
            _applySegmentOn(p_segs[0]);
            return true;
        }

        if ((uint8_t)p_rt.index >= p_count) {
            if (!p_repeat) {
                sim.stop();
                return true;
            }
            p_rt.index        = 0;
            p_rt.onPhase      = true;
            p_rt.phaseStartMs = v_now;
            _applySegmentOn(p_segs[0]);
            return true;
        }

        ST_A10_OpSegment_t& v_seg = p_segs[p_rt.index];
        uint32_t v_onMs  = v_seg.on_minutes  * 60000UL;
        uint32_t v_offMs = v_seg.off_minutes * 60000UL;

        if (p_rt.onPhase && v_onMs > 0 && v_now - p_rt.phaseStartMs >= v_onMs) {
            // On → Off 전환
            p_rt.onPhase      = false;
            p_rt.phaseStartMs = v_now;
            _applySegmentOff();
        } else if (!p_rt.onPhase && v_offMs > 0 && v_now - p_rt.phaseStartMs >= v_offMs) {
            // Off → 다음 Segment On
            p_rt.index++;
            if ((uint8_t)p_rt.index >= p_count && p_repeat) {
                p_rt.index = 0;
            }
            p_rt.onPhase      = true;
            p_rt.phaseStartMs = v_now;

            if ((uint8_t)p_rt.index < p_count) {
                _applySegmentOn(p_segs[p_rt.index]);
            } else {
                sim.stop();
            }
        }
        return true;
    }

    // Segment On
    void _applySegmentOn(const ST_A10_OpSegment_t& p_seg) {
        if (!g_A10_config_root.windDict) return;

        if (strcasecmp(p_seg.mode, "FIXED") == 0) {
            sim.stop();
            if (pwm) {
                pwm->P10_setDutyPercent(p_seg.fixed_speed);
            }
            return;
        }

        // PRESET 모드: presetCode+styleCode+adjust → ResolvedWind → sim.applyResolvedWind
        ST_A10_ResolvedWind_t v_res;
        memset(&v_res, 0, sizeof(v_res));

        bool v_ok = CL_C10_ConfigManager::C10_resolveWindParams(
                        *g_A10_config_root.windDict,
                        p_seg.presetCode,
                        p_seg.styleCode,
                        &p_seg.adjust,
                        v_res);
		if (v_ok && v_res.valid) {
            sim.applyResolvedWind(v_res);

            // ✅ Segment 변경 시 상태 브로드캐스트 (Web UI 즉시 반영)
            JsonDocument v_doc;
            toJson(v_doc);
            CL_W10_WebAPI::broadcastState(v_doc, true);
        } else {
            CL_D10_Logger::log(EN_L10_LOG_WARN,
                               "[CT10] Segment resolve failed (mode=%s,preset=%s,style=%s)",
                               p_seg.mode,
                               p_seg.presetCode,
                               p_seg.styleCode);
        }
    }

    // Segment Off
    void _applySegmentOff() {
        sim.stop();
    }

    // ==================================================
    // AutoOff 초기화
    // ==================================================
    void _initAutoOffFromUserProfile(const ST_A10_UserProfile_t& p_up) {
        memset(&autoOffRt, 0, sizeof(autoOffRt));

        if (p_up.autoOff.timer.enabled) {
            autoOffRt.timerArmed   = true;
            autoOffRt.timerStartMs = millis();
            autoOffRt.timerMinutes = p_up.autoOff.timer.minutes;
        }
        if (p_up.autoOff.offTime.enabled) {
            autoOffRt.offTimeEnabled = true;
            autoOffRt.offTimeMinutes = _parseHHMMtoMin(p_up.autoOff.offTime.time);
        }
        if (p_up.autoOff.offTemp.enabled) {
            autoOffRt.offTempEnabled = true;
            autoOffRt.offTemp        = p_up.autoOff.offTemp.temp;
        }
    }

    void _initAutoOffFromSchedule(const ST_A10_ScheduleItem_t& p_s) {
        memset(&autoOffRt, 0, sizeof(autoOffRt));

        if (p_s.autoOff.timer.enabled) {
            autoOffRt.timerArmed   = true;
            autoOffRt.timerStartMs = millis();
            autoOffRt.timerMinutes = p_s.autoOff.timer.minutes;
        }
        if (p_s.autoOff.offTime.enabled) {
            autoOffRt.offTimeEnabled = true;
            autoOffRt.offTimeMinutes = _parseHHMMtoMin(p_s.autoOff.offTime.time);
        }
        if (p_s.autoOff.offTemp.enabled) {
            autoOffRt.offTempEnabled = true;
            autoOffRt.offTemp        = p_s.autoOff.offTemp.temp;
        }
    }

    // --------------------------------------------------
    // AutoOff 조건 점검
    // --------------------------------------------------
    bool _checkAutoOff() {
        if (!autoOffRt.timerArmed &&
            !autoOffRt.offTimeEnabled &&
            !autoOffRt.offTempEnabled) {
            return false;
        }

        unsigned long v_now = millis();

        // 1) 타이머 만료
        if (autoOffRt.timerArmed && autoOffRt.timerMinutes > 0) {
            uint32_t v_elapsedMin = (v_now - autoOffRt.timerStartMs) / 60000UL;
            if (v_elapsedMin >= autoOffRt.timerMinutes) {
                CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] AutoOff(timer %lu min) triggered",
                                   (unsigned long)autoOffRt.timerMinutes);
                return true;
            }
        }

        // 2) 지정시간 오프
        if (autoOffRt.offTimeEnabled) {
            time_t v_t = time(nullptr);
            struct tm* v_lt = localtime(&v_t);
            uint16_t v_curMin = (uint16_t)v_lt->tm_hour * 60 + (uint16_t)v_lt->tm_min;

            if (v_curMin >= autoOffRt.offTimeMinutes) {
                CL_D10_Logger::log(EN_L10_LOG_INFO,
                                   "[CT10] AutoOff(time %u) triggered",
                                   (unsigned)autoOffRt.offTimeMinutes);
                return true;
            }
        }

        // 3) 온도 기반
        if (autoOffRt.offTempEnabled) {
            float v_curTemp = _getCurrentTemperature();
            if (v_curTemp >= autoOffRt.offTemp) {
                CL_D10_Logger::log(EN_L10_LOG_INFO,
                                   "[CT10] AutoOff(temp %.1f°C >= %.1f°C) triggered",
                                   v_curTemp, autoOffRt.offTemp);
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
        int v_hh = atoi(p_time);
        const char* v_colon = strchr(p_time, ':');
        int v_mm = (v_colon) ? atoi(v_colon + 1) : 0;
        return (uint16_t)(v_hh * 60 + v_mm);
    }

    float _getCurrentTemperature() {
        // 실제 온도 센서 연동 시 교체.
        // 기본값: 24.0°C 반환 (테스트용)
        return 24.0f;
    }

    int _findActiveScheduleIndex(const ST_A10_ScheduleConfig& p_cfg) {
        if (p_cfg.count == 0) return -1;

        time_t v_now = time(nullptr);
        struct tm* v_lt = localtime(&v_now);

        uint8_t v_wday = (v_lt->tm_wday == 0) ? 6 : (uint8_t)(v_lt->tm_wday - 1); // 일요일 보정
        uint16_t v_curMin = (uint16_t)v_lt->tm_hour * 60 + (uint16_t)v_lt->tm_min;

        for (int v_i = 0; v_i < p_cfg.count; v_i++) {
            const ST_A10_ScheduleItem_t& v_s = p_cfg.items[v_i];
            if (!v_s.enabled || !v_s.period.enabled) continue;
            if (!v_s.period.days[v_wday]) continue;

            uint16_t v_st = _parseHHMMtoMin(v_s.period.start_time);
            uint16_t v_en = _parseHHMMtoMin(v_s.period.end_time);

            if (v_st <= v_en) {
                // 일반 구간
                if (v_curMin >= v_st && v_curMin < v_en) return v_i;
            } else {
                // 자정을 넘기는 구간
                if (v_curMin >= v_st || v_curMin < v_en) return v_i;
            }
        }
        return -1;
    }

    bool _isMotionBlocked(const ST_A10_MotionBinding_t& p_motionCfg) {
  	    if (!motion) return false; // 연결 안됨
	    if (!p_motionCfg.pir.enabled && !p_motionCfg.ble.enabled) return false;

	    // M10에서 실시간 활성여부 판단
	    bool v_active = motion->isActive();
	    if (!v_active) {
		    CL_D10_Logger::log(EN_L10_LOG_DEBUG, "[CT10] Motion blocked (no presence)");
		    return true;
	    }
	    return false;
    }

     // --------------------------------------------------
// 상태 변경 시 WebSocket 브로드캐스트 헬퍼
// --------------------------------------------------
// --------------------------------------------------
// 상태 변경 시 WebSocket 브로드캐스트 헬퍼
// --------------------------------------------------
void _broadcastState(bool p_diffOnly) {
    JsonDocument v_doc;
    toJson(v_doc);
    CL_W10_WebAPI::broadcastState(v_doc, p_diffOnly);
}

// --------------------------------------------------
// Metrics WebSocket 브로드캐스트 (주기 + diffOnly)
// --------------------------------------------------
void _maybeBroadcastMetrics() {
    unsigned long v_now = millis();
    if (v_now - lastMetricsPushMs < 1500UL) return;
    lastMetricsPushMs = v_now;

    JsonDocument v_doc;
    toMetricsJson(v_doc);
    CL_W10_WebAPI::broadcastMetrics(v_doc, true);
}

};

