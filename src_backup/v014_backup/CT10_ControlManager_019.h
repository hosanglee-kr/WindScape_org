#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : CT10_ControlManager_019.h
 * 모듈약어 : CT10
 * 모듈명 : Smart Nature Wind 제어 통합 Manager (v019)
 * ------------------------------------------------------
 * 기능 요약:
 *  - Schedule / UserProfile / Manual Override 기반 풍속 제어
 *  - WindProfile Dict(C10) 기반 preset × style × adjust 해석
 *  - S10 Simulation 및 P10 PWM 제어기 연동
 *  - Motion (PIR / BLE) 및 AutoOff 조건 훅 제공
 *  - Web / 버튼에서 Profile 선택, Override 즉시 반영
 *  - JSON 상태 Export (control / override / autoOff / sim)
 *  - 런타임 상태는 RAM 전용 (NVS 불필요 갱신 최소화)
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
 *   - 함수 로컬 변수        : v_접두사
 *   - 함수 인자             : p_접두사
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
#include "M10_MotionLogic_014.h"

// ------------------------------------------------------
// 런타임 제어 모드
// ------------------------------------------------------
typedef enum : uint8_t {
    EN_CT10_RUN_NONE         = 0,
    EN_CT10_RUN_SCHEDULE     = 1,
    EN_CT10_RUN_USER_PROFILE = 2
} EN_CT10_run_source_t;

// ------------------------------------------------------
// Manual Override 상태
// ------------------------------------------------------
typedef struct {
    bool                 active;          // override 활성 여부
    unsigned long        endMs;           // 종료 시각 (millis)
    bool                 useFixed;        // true: fixedPercent 사용, false: preset/style
    float                fixedPercent;    // 0~100 (%)
    char                 presetCode[24];  // PRESET 코드
    char                 styleCode[24];   // STYLE 코드
    ST_A10_AdjustDelta_t adjust;          // 옵션 조정값
} ST_CT10_Override_t;

// ------------------------------------------------------
// 세그먼트 런타임 상태
// ------------------------------------------------------
typedef struct {
    int8_t        index;         // 현재 세그먼트 인덱스 (-1: 아직 시작 안함)
    bool          onPhase;       // true: on_minutes 구간, false: off_minutes 구간
    unsigned long phaseStartMs;  // 현재 phase 시작 시각
} ST_CT10_SegmentRuntime_t;

// ------------------------------------------------------
// AutoOff 런타임 상태 (RAM 전용)
// ------------------------------------------------------
typedef struct {
    bool          timerArmed;       // 타이머 사용 여부
    unsigned long timerStartMs;    // 타이머 시작 시각
    uint32_t      timerMinutes;    // 타이머 분

    bool          offTimeEnabled;  // 특정 시각 AutoOff
    uint16_t      offTimeMinutes;  // 분(0~1440)

    bool          offTempEnabled;  // 온도 기반 AutoOff
    float         offTemp;         // 임계 온도
} ST_CT10_AutoOffRuntime_t;

// ======================================================
// CL_CT10_ControlManager
// ======================================================
class CL_CT10_ControlManager {
public:
    // 상태 필드 (외부 직접 접근 가능하지만, 주로 API 사용 권장)
    bool                     active = false;
    EN_CT10_run_source_t     runSource = EN_CT10_RUN_NONE;

    int8_t                   curScheduleIndex = -1;
    int8_t                   curProfileIndex  = -1;

    ST_CT10_SegmentRuntime_t scheduleSegRt;
    ST_CT10_SegmentRuntime_t profileSegRt;

    ST_CT10_Override_t       overrideState;
    ST_CT10_AutoOffRuntime_t autoOffRt;

    CL_P10_PWM*              pwm = nullptr;
    CL_M10_MotionLogic*      motion = nullptr;
    CL_S10_Simulation        sim;

    unsigned long            lastTickMs = 0;

public:
    // --------------------------------------------------
    // 초기화
    // --------------------------------------------------
    void begin(CL_P10_PWM& p_pwm) {
        pwm = &p_pwm;

        memset(&scheduleSegRt, 0, sizeof(scheduleSegRt));
        memset(&profileSegRt,  0, sizeof(profileSegRt));
        memset(&overrideState, 0, sizeof(overrideState));
        memset(&autoOffRt,     0, sizeof(autoOffRt));

        scheduleSegRt.index = -1;
        profileSegRt.index  = -1;

        sim.begin(p_pwm);

        active      = true;
        runSource   = EN_CT10_RUN_NONE;
        curScheduleIndex = -1;
        curProfileIndex  = -1;
        lastTickMs  = millis();

        CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] begin()");
    }

    void setMotion(CL_M10_MotionLogic* p_motion) {
        motion = p_motion;
    }

    // --------------------------------------------------
    // UserProfile 제어 (외부 API)
    // --------------------------------------------------
    bool startUserProfileByNo(uint8_t p_profileNo) {
        if (!g_A10_config_root.userProfiles) return false;

        ST_A10_UserProfileConfig_t& v_cfg = *g_A10_config_root.userProfiles;
        for (uint8_t v_i = 0; v_i < v_cfg.count; v_i++) {
            const ST_A10_UserProfile_t& v_p = v_cfg.items[v_i];
            if (!v_p.enabled) continue;
            if (v_p.profileNo == p_profileNo) {
                runSource         = EN_CT10_RUN_USER_PROFILE;
                curProfileIndex   = (int8_t)v_i;
                profileSegRt.index     = -1;
                profileSegRt.onPhase   = true;
                profileSegRt.phaseStartMs = millis();

                _initAutoOffFromUserProfile(v_p);

                CL_D10_Logger::log(EN_L10_LOG_INFO,
                    "[CT10] Start UserProfile #%u (%s)", p_profileNo, v_p.name);
                return true;
            }
        }
        return false;
    }

    void stopUserProfile() {
        if (runSource == EN_CT10_RUN_USER_PROFILE) {
            runSource       = EN_CT10_RUN_NONE;
            curProfileIndex = -1;
            profileSegRt.index = -1;
            sim.stop();
            CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] UserProfile stopped");
        }
    }

    // --------------------------------------------------
    // Manual Override 제어 (외부 API)
    // --------------------------------------------------
    void startOverrideFixed(float p_percent, uint32_t p_seconds) {
        if (p_seconds == 0) return;

        memset(&overrideState, 0, sizeof(overrideState));
        overrideState.active       = true;
        overrideState.useFixed     = true;
        overrideState.fixedPercent = constrain(p_percent, 0.0f, 100.0f);
        overrideState.endMs        = millis() + (p_seconds * 1000UL);

        CL_D10_Logger::log(EN_L10_LOG_INFO,
            "[CT10] Override FIXED %.1f%% for %lu sec",
            overrideState.fixedPercent, (unsigned long)p_seconds);
    }

    void startOverridePreset(const char* p_presetCode,
                             const char* p_styleCode,
                             const ST_A10_AdjustDelta_t* p_adj,
                             uint32_t p_seconds) {
        if (p_seconds == 0 || !p_presetCode || !p_presetCode[0]) return;

        memset(&overrideState, 0, sizeof(overrideState));
        overrideState.active   = true;
        overrideState.useFixed = false;
        overrideState.endMs    = millis() + (p_seconds * 1000UL);

        strlcpy(overrideState.presetCode, p_presetCode, sizeof(overrideState.presetCode));
        if (p_styleCode) strlcpy(overrideState.styleCode, p_styleCode, sizeof(overrideState.styleCode));
        if (p_adj)       overrideState.adjust = *p_adj;

        CL_D10_Logger::log(EN_L10_LOG_INFO,
            "[CT10] Override PRESET (%s,%s) for %lu sec",
            overrideState.presetCode,
            overrideState.styleCode,
            (unsigned long)p_seconds);
    }

    void stopOverride() {
        if (!overrideState.active) return;
        memset(&overrideState, 0, sizeof(overrideState));
        CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] Override cleared");
    }

    // Web/API 편의용 이름 (W10 연동 시 사용)
    inline void applyManual(const ST_A10_ResolvedWind_t& p_resolved, uint32_t p_seconds) {
        // resolved 자체를 override로 쓸 경우: preset/style 기반 재요청 권장
        ST_A10_AdjustDelta_t v_adj;
        memset(&v_adj, 0, sizeof(v_adj));
        startOverridePreset(p_resolved.presetCode, p_resolved.styleCode, &v_adj, p_seconds);
    }
    inline void clearManual() { stopOverride(); }

    // --------------------------------------------------
    // Tick 루프 (주기 호출)
    // --------------------------------------------------
    void tick() {
        if (!active || !pwm) return;

        unsigned long v_now = millis();
        if (v_now - lastTickMs < 40UL) {
            // 너무 자주 계산하지 않음
            return;
        }
        lastTickMs = v_now;

        // 1) Override 최우선
        if (_tickOverride()) {
            sim.tick();
            return;
        }

        // 2) UserProfile 모드
        if (runSource == EN_CT10_RUN_USER_PROFILE) {
            if (_tickUserProfile()) {
                sim.tick();
                return;
            }
        }

        // 3) Schedule 모드
        if (_tickSchedule()) {
            sim.tick();
            return;
        }

        // 4) 활성 세그먼트/스케줄 없으면 stop
        if (sim.active) {
            sim.stop();
        }
    }

    // --------------------------------------------------
    // JSON Export
    // --------------------------------------------------
    void toJson(JsonDocument& p_doc) {
        JsonObject v_ctrl = p_doc["control"].to<JsonObject>();

        v_ctrl["active"]       = active;
        v_ctrl["runSource"]    = (int)runSource;
        v_ctrl["scheduleIdx"]  = curScheduleIndex;
        v_ctrl["profileIdx"]   = curProfileIndex;

        // override
        JsonObject v_ov = v_ctrl["override"].to<JsonObject>();
        v_ov["active"] = overrideState.active;
        if (overrideState.active) {
            unsigned long v_now = millis();
            uint32_t v_remain = 0;
            if (overrideState.endMs > v_now) {
                v_remain = (overrideState.endMs - v_now) / 1000UL;
            }
            v_ov["remainSec"] = v_remain;
            v_ov["useFixed"]  = overrideState.useFixed;
            if (overrideState.useFixed) {
                v_ov["fixedPercent"] = overrideState.fixedPercent;
            } else {
                v_ov["presetCode"] = overrideState.presetCode;
                v_ov["styleCode"]  = overrideState.styleCode;
            }
        }

        // autoOff (현재 무조건 RAM 상태만)
        JsonObject v_ao = v_ctrl["autoOff"].to<JsonObject>();
        v_ao["timerArmed"]     = autoOffRt.timerArmed;
        v_ao["timerMinutes"]   = autoOffRt.timerMinutes;
        v_ao["offTimeEnabled"] = autoOffRt.offTimeEnabled;
        v_ao["offTimeMinutes"] = autoOffRt.offTimeMinutes;
        v_ao["offTempEnabled"] = autoOffRt.offTempEnabled;
        v_ao["offTemp"]        = autoOffRt.offTemp;

        if (pwm) {
            v_ctrl["pwmDuty"] = pwm->P10_getDutyPercent();
        }

        // sim 상태 포함
        sim.toJson(p_doc);
    }

private:
    // ==================================================
    // Override 처리
    // ==================================================
    bool _tickOverride() {
        if (!overrideState.active) return false;

        unsigned long v_now = millis();
        if (v_now >= overrideState.endMs) {
            memset(&overrideState, 0, sizeof(overrideState));
            return false;
        }

        // Fixed 모드: S10 정지 후 PWM 고정
        if (overrideState.useFixed) {
            sim.stop();
            float v_pct = constrain(overrideState.fixedPercent, 0.0f, 100.0f);
            pwm->P10_setDutyPercent(v_pct);
            return true;
        }

        // Preset 모드: WindDict 필요
        if (!g_A10_config_root.windDict) return false;

        ST_A10_ResolvedWind_t v_resolved;
        bool v_ok = CL_C10_ConfigManager::C10_resolveWindParams(
            *g_A10_config_root.windDict,
            overrideState.presetCode,
            overrideState.styleCode,
            &overrideState.adjust,
            v_resolved
        );
        if (v_ok) {
            sim.applyResolvedWind(v_resolved);
            return true;
        }

        return false;
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

        if (_checkAutoOff()) {
            sim.stop();
            runSource       = EN_CT10_RUN_NONE;
            curProfileIndex = -1;
            profileSegRt.index = -1;
            return true;
        }

        return _tickSegmentSequence(
            v_p.repeatSegments,
            v_p.segments,
            v_p.segCount,
            profileSegRt
        );
    }

    // ==================================================
    // Schedule Tick
    // ==================================================
    bool _tickSchedule() {
        if (!g_A10_config_root.schedules) return false;

        ST_A10_ScheduleConfig& v_cfg = *g_A10_config_root.schedules;
        if (v_cfg.count == 0) return false;

        int v_idx = _findActiveScheduleIndex(v_cfg);
        if (v_idx < 0) {
            curScheduleIndex = -1;
            return false;
        }

        // 스케줄 변경 시 AutoOff 재설정
        if (curScheduleIndex != v_idx) {
            curScheduleIndex = (int8_t)v_idx;
            runSource        = EN_CT10_RUN_SCHEDULE;
            scheduleSegRt.index = -1;
            scheduleSegRt.onPhase = true;
            scheduleSegRt.phaseStartMs = millis();

            _initAutoOffFromSchedule(v_cfg.items[v_idx]);

            CL_D10_Logger::log(EN_L10_LOG_INFO,
                "[CT10] Active Schedule idx=%d (schNo=%d)",
                v_idx, v_cfg.items[v_idx].schNo);
        }

        ST_A10_ScheduleItem_t& v_s = v_cfg.items[curScheduleIndex];
        if (!v_s.enabled || v_s.segCount == 0) return false;

        if (_checkAutoOff()) {
            sim.stop();
            runSource        = EN_CT10_RUN_NONE;
            curScheduleIndex = -1;
            scheduleSegRt.index = -1;
            return true;
        }

        if (_isMotionBlocked(v_s.motion)) {
            sim.stop();
            return true;
        }

        return _tickSegmentSequence(
            true,                // 스케줄은 기본 반복
            v_s.segments,
            v_s.segCount,
            scheduleSegRt
        );
    }

    // ==================================================
    // Segment 처리 공통
    // ==================================================
    bool _tickSegmentSequence(bool p_repeat,
                              ST_A10_OpSegment_t* p_segs,
                              uint8_t p_count,
                              ST_CT10_SegmentRuntime_t& p_rt) {
        if (p_count == 0 || !p_segs) return false;

        unsigned long v_now = millis();

        // 초기 진입
        if (p_rt.index < 0) {
            p_rt.index        = 0;
            p_rt.onPhase      = true;
            p_rt.phaseStartMs = v_now;
            _applySegmentOn(p_segs[p_rt.index]);
            return true;
        }

        if (p_rt.index >= p_count) {
            if (!p_repeat) {
                sim.stop();
                return true;
            }
            // 반복일 경우 처음으로
            p_rt.index        = 0;
            p_rt.onPhase      = true;
            p_rt.phaseStartMs = v_now;
            _applySegmentOn(p_segs[p_rt.index]);
            return true;
        }

        ST_A10_OpSegment_t& v_seg = p_segs[p_rt.index];
        uint32_t v_onMs  = (uint32_t)v_seg.on_minutes  * 60000UL;
        uint32_t v_offMs = (uint32_t)v_seg.off_minutes * 60000UL;

        // ON → OFF 전환
        if (p_rt.onPhase && v_onMs > 0 && (v_now - p_rt.phaseStartMs) >= v_onMs) {
            p_rt.onPhase      = false;
            p_rt.phaseStartMs = v_now;
            _applySegmentOff();
            return true;
        }

        // OFF → 다음 세그먼트 진입
        if (!p_rt.onPhase && v_offMs > 0 && (v_now - p_rt.phaseStartMs) >= v_offMs) {
            p_rt.index++;
            if (p_rt.index >= p_count) {
                if (p_repeat) {
                    p_rt.index = 0;
                } else {
                    sim.stop();
                    return true;
                }
            }
            p_rt.onPhase      = true;
            p_rt.phaseStartMs = v_now;
            if (p_rt.index < p_count) {
                _applySegmentOn(p_segs[p_rt.index]);
            } else {
                sim.stop();
            }
            return true;
        }

        return true;
    }

    void _applySegmentOn(const ST_A10_OpSegment_t& p_seg) {
        if (!pwm) return;

        // FIXED 모드
        if (strcasecmp(p_seg.mode, "FIXED") == 0) {
            sim.stop();
            float v_pct = constrain(p_seg.fixed_speed, 0.0f, 100.0f);
            pwm->P10_setDutyPercent(v_pct);
            CL_D10_Logger::log(EN_L10_LOG_DEBUG,
                "[CT10] Segment FIXED segNo=%d speed=%.1f",
                p_seg.segNo, v_pct);
            return;
        }

        // PRESET 모드: WindDict + resolve 사용
        if (!g_A10_config_root.windDict) {
            CL_D10_Logger::log(EN_L10_LOG_WARN,
                "[CT10] No windDict for PRESET segNo=%d", p_seg.segNo);
            return;
        }

        ST_A10_ResolvedWind_t v_res;
        bool v_ok = CL_C10_ConfigManager::C10_resolveWindParams(
            *g_A10_config_root.windDict,
            p_seg.presetCode,
            p_seg.styleCode,
            &p_seg.adjust,
            v_res
        );
        if (v_ok) {
            sim.applyResolvedWind(v_res);
            CL_D10_Logger::log(EN_L10_LOG_DEBUG,
                "[CT10] Segment PRESET segNo=%d (%s,%s)",
                p_seg.segNo, p_seg.presetCode, p_seg.styleCode);
        } else {
            CL_D10_Logger::log(EN_L10_LOG_WARN,
                "[CT10] resolveWind failed segNo=%d", p_seg.segNo);
        }
    }

    void _applySegmentOff() {
        sim.stop();
        CL_D10_Logger::log(EN_L10_LOG_DEBUG, "[CT10] Segment OFF");
    }

    // ==================================================
    // AutoOff 초기화
    // ==================================================
    void _initAutoOffFromUserProfile(const ST_A10_UserProfile_t& p_up) {
        memset(&autoOffRt, 0, sizeof(autoOffRt));

        if (p_up.autoOff.timer.enabled && p_up.autoOff.timer.minutes > 0) {
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

        if (p_s.autoOff.timer.enabled && p_s.autoOff.timer.minutes > 0) {
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

    // ==================================================
    // AutoOff 조건 체크
    // ==================================================
    bool _checkAutoOff() {
        if (!autoOffRt.timerArmed &&
            !autoOffRt.offTimeEnabled &&
            !autoOffRt.offTempEnabled) {
            return false;
        }

        unsigned long v_now = millis();

        // 1) 타이머
        if (autoOffRt.timerArmed && autoOffRt.timerMinutes > 0) {
            uint32_t v_elapsedMin =
                (v_now - autoOffRt.timerStartMs) / 60000UL;
            if (v_elapsedMin >= autoOffRt.timerMinutes) {
                CL_D10_Logger::log(EN_L10_LOG_INFO,
                    "[CT10] AutoOff(timer %lu min) triggered",
                    (unsigned long)autoOffRt.timerMinutes);
                return true;
            }
        }

        // 2) 지정 시각
        if (autoOffRt.offTimeEnabled) {
            time_t v_t = time(nullptr);
            struct tm* v_lt = localtime(&v_t);
            if (v_lt) {
                uint16_t v_curMin = (uint16_t)(v_lt->tm_hour * 60 + v_lt->tm_min);
                if (v_curMin >= autoOffRt.offTimeMinutes) {
                    CL_D10_Logger::log(EN_L10_LOG_INFO,
                        "[CT10] AutoOff(time %u) triggered",
                        (unsigned int)autoOffRt.offTimeMinutes);
                    return true;
                }
            }
        }

        // 3) 온도
        if (autoOffRt.offTempEnabled) {
            float v_temp = _getCurrentTemperature();
            if (v_temp >= autoOffRt.offTemp) {
                CL_D10_Logger::log(EN_L10_LOG_INFO,
                    "[CT10] AutoOff(temp %.1f >= %.1f) triggered",
                    v_temp, autoOffRt.offTemp);
                return true;
            }
        }

        return false;
    }

    // ==================================================
    // 유틸 함수
    // ==================================================
    uint16_t _parseHHMMtoMin(const char* p_time) {
        if (!p_time) return 0;
        size_t v_len = strlen(p_time);
        if (v_len < 4) return 0;

        int v_h = atoi(p_time);
        const char* v_colon = strchr(p_time, ':');
        int v_m = (v_colon ? atoi(v_colon + 1) : 0);

        if (v_h < 0) v_h = 0;
        if (v_h > 23) v_h = 23;
        if (v_m < 0) v_m = 0;
        if (v_m > 59) v_m = 59;

        return (uint16_t)(v_h * 60 + v_m);
    }

    float _getCurrentTemperature() {
        // TODO: 실제 센서 연동 시 교체
        return 24.0f;
    }

    int _findActiveScheduleIndex(const ST_A10_ScheduleConfig& p_cfg) {
        if (p_cfg.count == 0) return -1;

        time_t v_now = time(nullptr);
        struct tm* v_lt = localtime(&v_now);
        if (!v_lt) return -1;

        // 0=월 ~ 6=일 형식으로 맞추기 (cfg의 days[7] 전제)
        uint8_t v_wday = (uint8_t)((v_lt->tm_wday + 6) % 7);
        uint16_t v_curMin = (uint16_t)(v_lt->tm_hour * 60 + v_lt->tm_min);

        for (int v_i = 0; v_i < p_cfg.count; v_i++) {
            const ST_A10_ScheduleItem_t& v_s = p_cfg.items[v_i];
            if (!v_s.enabled || !v_s.period.enabled) continue;
            if (!v_s.period.days[v_wday]) continue;

            uint16_t v_st = _parseHHMMtoMin(v_s.period.start_time);
            uint16_t v_en = _parseHHMMtoMin(v_s.period.end_time);

            if (v_st <= v_en) {
                // 일반 케이스
                if (v_curMin >= v_st && v_curMin < v_en) {
                    return v_i;
                }
            } else {
                // 자정 넘김
                if (v_curMin >= v_st || v_curMin < v_en) {
                    return v_i;
                }
            }
        }
        return -1;
    }

    bool _isMotionBlocked(const ST_A10_MotionBinding_t& p_motionCfg) {
        if (!motion) return false;

        bool v_needPir = p_motionCfg.pir.enabled;
        bool v_needBle = p_motionCfg.ble.enabled;

        if (!v_needPir && !v_needBle) {
            return false;
        }

        // CL_M10_MotionLogic 는 isPresenceActive() 제공한다고 가정
        if (!motion->isPresenceActive()) {
            CL_D10_Logger::log(EN_L10_LOG_DEBUG,
                "[CT10] Motion blocked (no presence)");
            return true;
        }
        return false;
    }
};

// 전역 인스턴스 (필요시 사용)
inline CL_CT10_ControlManager g_CT10_control;

