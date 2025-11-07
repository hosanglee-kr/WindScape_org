#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : CT10_ControlManager_014.h
 * 모듈약어 : CT10
 * 모듈명 : Smart Nature Wind 통합 제어 Manager (v014)
 * ------------------------------------------------------
 * 기능 요약
 *  - 스케줄(cfg_schedules_024.json) & 사용자 프로필(cfg_uzOpProfile_025_final.json) 기반 통합 제어
 *  - Segment 단위 PRESET/FIXED 운전
 *  - Motion 게이팅(PIR/BLE)
 *  - Override (강제 풍속/프리셋)
 *  - Simulation / PWM 연동
 *  - Localtime 기반 기간/시간대 처리(야간 걸침 포함)
 *  - autoOff (timer/offTime/offTemp) 반영
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
#include <ctime>
#include <string.h>

#include "A10_Const_014.h"          // Wind dict/types, enums, helpers
#include "C10_ConfigManager_014.h"  // C10_resolveWindParams, 로더 & 핸들
#include "D10_Logger_011.h"
#include "P10_PWM_ctrl_012.h"
#include "S10_Simulation_014.h"

// Motion 로직 포워드
class CL_M10_MotionLogic;

// ------------------------------------------------------
// 런타임 오버라이드 정의
// ------------------------------------------------------
typedef enum : uint8_t {
    EN_CT10_OVERRIDE_NONE   = 0,
    EN_CT10_OVERRIDE_FIXED  = 1,
    EN_CT10_OVERRIDE_PRESET = 2
} EN_CT10_override_mode_t;

typedef struct {
    bool                    active         = false;
    unsigned long           until_sec      = 0;      // millis()/1000 기준 런타임 만료
    EN_CT10_override_mode_t mode          = EN_CT10_OVERRIDE_NONE;
    float                   fixedPercent   = 0.0f;   // 0~100
    char                    presetCode[24] = {0};
    char                    styleCode[16]  = {0};
    ST_A10_AdjustDelta_t    adjust;                  // intensity/variability/gust 등 델타
} ST_CT10_overrideState_t;

// ------------------------------------------------------
// 상위 제어기
// ------------------------------------------------------
class CL_CT10_ControlManager {
public:
    // 외부 의존
    void begin(CL_S10_Simulation& p_sim, CL_P10_PWM& p_pwm) {
        sim  = &p_sim;
        pwm  = &p_pwm;
        active = true;

        // 기본 안전 정지
        if (!sim || !pwm) {
            CL_D10_Logger::log(EN_L10_LOG_ERROR, "[CT10] begin() failed: null sim/pwm");
            active = false;
            return;
        }

        // wind dict 핸들 체크
        dict = CL_C10_ConfigManager::C10_getWindDict();
        if (!dict) {
            CL_D10_Logger::log(EN_L10_LOG_ERROR, "[CT10] wind dict missing");
        }

        schedules = CL_C10_ConfigManager::C10_getSchedules();
        profiles  = CL_C10_ConfigManager::C10_getUserProfiles();

        // 초기 상태
        _resetScheduleState();
        _resetProfileState();
        CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] begin OK");
    }

    void setMotion(CL_M10_MotionLogic* p_motion) { motion = p_motion; }

    // 환경 센서(섭씨) 리더 (옵션)
    void setTempReader(float (*p_fnReadTempC)(void)) { fnReadTempC = p_fnReadTempC; }

    // 사용자 프로필 선택/해제
    bool selectProfile(uint8_t p_profileNo) {
        if (!profiles) return false;
        const ST_A10_UserOpProfiles_t& v = *profiles;
        int v_idx = _findProfileIndexByNo(v, p_profileNo);
        if (v_idx < 0) return false;

        _resetProfileState();
        profileActive      = true;
        profileIndex       = v_idx;
        profileSegIndex    = -1;
        profileOnPhase     = false;
        profilePhaseStart  = 0;
        profileStartSec    = millis()/1000UL;
        CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] selectProfile #%u (idx=%d)", p_profileNo, v_idx);
        return true;
    }

    void clearProfile() {
        if (!profileActive) return;
        _resetProfileState();
        CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] clearProfile()");
    }

    // 오버라이드 (런타임 전용)
    void overrideFixed(float p_percent, uint32_t p_seconds) {
        overrideState = ST_CT10_overrideState_t{};
        overrideState.active       = true;
        overrideState.mode         = EN_CT10_OVERRIDE_FIXED;
        overrideState.fixedPercent = constrain(p_percent, 0.0f, 100.0f);
        overrideState.until_sec    = (millis()/1000UL) + p_seconds;
        CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] Override FIXED %.1f%% %us",
                           p_percent, (unsigned)p_seconds);
        _applyOverride(); // 즉시 반영
    }

    void overridePreset(const char* p_presetCode,
                        const char* p_styleCode,
                        const ST_A10_AdjustDelta_t* p_adj,
                        uint32_t p_seconds) {
        overrideState = ST_CT10_overrideState_t{};
        overrideState.active = true;
        overrideState.mode   = EN_CT10_OVERRIDE_PRESET;
        strlcpy(overrideState.presetCode, p_presetCode ? p_presetCode : "OCEAN", sizeof(overrideState.presetCode));
        strlcpy(overrideState.styleCode,  p_styleCode  ? p_styleCode  : "BALANCE", sizeof(overrideState.styleCode));
        if (p_adj) memcpy(&overrideState.adjust, p_adj, sizeof(overrideState.adjust));
        overrideState.until_sec = (millis()/1000UL) + p_seconds;
        CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] Override PRESET %s/%s %us",
                           overrideState.presetCode, overrideState.styleCode, (unsigned)p_seconds);
        _applyOverride();
    }

    void releaseOverride() {
        if (!overrideState.active) return;
        overrideState = ST_CT10_overrideState_t{};
        CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] Override released");
        // 모드 재적용
        _reapplyCurrentMode();
    }

    // 주기 호출
    void tick() {
        if (!active || !pwm || !sim) return;

        // 1) 오버라이드 우선
        if (overrideState.active) {
            unsigned long v_now = millis()/1000UL;
            if (v_now >= overrideState.until_sec) {
                releaseOverride();
            } else {
                _applyOverride();
                return;
            }
        }

        // 2) 사용자 프로필이 활성이면 프로필 우선
        if (profileActive) {
            _tickProfile();
            return;
        }

        // 3) 스케줄 동작
        _tickSchedule();
    }

    // 상태 JSON Export (선택적 – 진단용)
    void toJson(JsonDocument& p_doc) {
        JsonObject o = p_doc["ct10"].to<JsonObject>();
        o["active"]         = active;
        o["profileActive"]  = profileActive;
        o["profileIndex"]   = profileIndex;
        o["scheduleIndex"]  = scheduleIndex;
        o["segmentIndex"]   = segIndex;

        JsonObject ov = o["override"].to<JsonObject>();
        ov["active"]   = overrideState.active;
        ov["mode"]     = (int)overrideState.mode;
        ov["remain_s"] = overrideState.active
                         ? (int32_t)(overrideState.until_sec - (millis()/1000UL))
                         : 0;
    }

private:
    // 외부 의존 포인터
    CL_S10_Simulation*            sim   = nullptr;
    CL_P10_PWM*                   pwm   = nullptr;
    CL_M10_MotionLogic*           motion= nullptr;
    const ST_A10_WindProfileDict_t*  dict      = nullptr;
    const ST_A10_ScheduleSet_t*      schedules = nullptr;
    const ST_A10_UserOpProfiles_t*   profiles  = nullptr;

    // 동작 상태
    bool     active          = false;

    // 스케줄 상태
    int      scheduleIndex   = -1;   // 현재 활성 스케줄 index
    int      segIndex        = -1;   // 현재 세그먼트 index
    bool     onPhase         = false;
    uint32_t phaseStartMs    = 0;

    // 프로필 상태
    bool     profileActive   = false;
    int      profileIndex    = -1;
    int      profileSegIndex = -1;
    bool     profileOnPhase  = false;
    uint32_t profilePhaseStartMs = 0;
    unsigned long profileStartSec = 0;

    ST_CT10_overrideState_t overrideState;

    // 선택적: 온도 읽기
    float    (*fnReadTempC)(void) = nullptr;

    // --------------------------
    // 공통 유틸
    // --------------------------
    static int _parseHHMM(const char* p_hhmm) {
        if (!p_hhmm) return -1;
        int v_h = 0, v_m = 0;
        const char* v_c = strchr(p_hhmm, ':');
        if (!v_c) return -1;
        v_h = atoi(String(p_hhmm).substring(0, v_c - p_hhmm).c_str());
        v_m = atoi(String(v_c + 1).c_str());
        if (v_h < 0 || v_h > 23 || v_m < 0 || v_m > 59) return -1;
        return v_h * 60 + v_m;
    }

    static int _nowLocalMinutes(int* p_wdayOut=nullptr) {
        time_t v_t = time(nullptr);
        struct tm* v_tm = localtime(&v_t);
        if (!v_tm) return -1;
        if (p_wdayOut) *p_wdayOut = v_tm->tm_wday; // 0=Sun
        return v_tm->tm_hour * 60 + v_tm->tm_min;
    }

    static bool _inPeriodNow(const ST_A10_SchedulePeriod_t& p_period) {
        if (!p_period.enabled) return false;
        int v_wday = 0;
        int v_nowMin = _nowLocalMinutes(&v_wday);
        if (v_nowMin < 0) return false;
        if (v_wday < 0 || v_wday > 6) return false;
        if (p_period.days[v_wday] == 0) return false;

        int v_st = _parseHHMM(p_period.start_time);
        int v_ed = _parseHHMM(p_period.end_time);
        if (v_st < 0 || v_ed < 0) return false;

        if (v_st <= v_ed) {
            return (v_st <= v_nowMin && v_nowMin < v_ed);
        } else {
            // 야간 걸침 (예: 23:00~07:00)
            return (v_nowMin >= v_st || v_nowMin < v_ed);
        }
    }

    static uint32_t _minutesToMs(uint16_t p_min) {
        return (uint32_t)p_min * 60000UL;
    }

    bool _motionBlocked(const ST_A10_MotionGate_t& p_gate) const {
        if (!motion) return false;
        if (!p_gate.pir.enabled && !p_gate.ble.enabled) return false;
        // Motion 로직 내부에서 PIR/BLE 통합 판정
        return !motion->M10_isMotionPresent();
    }

    // --------------------------
    // 스케줄 로직
    // --------------------------
    void _resetScheduleState() {
        scheduleIndex = -1;
        segIndex      = -1;
        onPhase       = false;
        phaseStartMs  = 0;
    }

    int _findActiveScheduleIndex() const {
        if (!schedules) return -1;
        const ST_A10_ScheduleSet_t& v = *schedules;
        for (uint16_t v_i = 0; v_i < v.count; ++v_i) {
            const ST_A10_ScheduleItem_t& s = v.items[v_i];
            if (!s.enabled) continue;
            if (_inPeriodNow(s.period)) return (int)v_i;
        }
        return -1;
    }

    void _tickSchedule() {
        if (!schedules || !dict) {
            // 안전 정지
            sim->stop();
            pwm->P10_setDutyPercent(0.0f);
            return;
        }

        int v_idx = _findActiveScheduleIndex();
        if (v_idx < 0) {
            // 비활성 시간대
            if (scheduleIndex != -1) {
                CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] schedule idle");
            }
            _resetScheduleState();
            sim->stop();
            pwm->P10_setDutyPercent(0.0f);
            return;
        }

        if (scheduleIndex != v_idx) {
            scheduleIndex = v_idx;
            segIndex      = -1;
            onPhase       = false;
            phaseStartMs  = 0;
            CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] schedule #%d entered", v_idx);
        }

        const ST_A10_ScheduleItem_t& sch = (*schedules).items[scheduleIndex];

        // 모션 게이트
        if (_motionBlocked(sch.motion)) {
            sim->stop();
            pwm->P10_setDutyPercent(0.0f);
            return;
        }

        _applyScheduleSegment(sch);
    }

    void _applyScheduleSegment(const ST_A10_ScheduleItem_t& p_sch) {
        if (p_sch.seg_count == 0) {
            sim->stop();
            pwm->P10_setDutyPercent(0.0f);
            return;
        }

        if (segIndex < 0) {
            segIndex = 0;
            onPhase  = true;
            phaseStartMs = millis();
            _segmentOn(p_sch.segments[segIndex], p_sch.motion);
            return;
        }

        const ST_A10_Segment_t& v_seg = p_sch.segments[segIndex];
        uint32_t v_onMs  = _minutesToMs(max<uint16_t>(1, v_seg.on_minutes));
        uint32_t v_offMs = _minutesToMs(v_seg.off_minutes);

        if (onPhase) {
            if (millis() - phaseStartMs >= v_onMs) {
                onPhase = false;
                phaseStartMs = millis();
                _segmentOff();
            }
        } else {
            if (millis() - phaseStartMs >= v_offMs) {
                segIndex = (segIndex + 1) % (int)p_sch.seg_count;
                onPhase  = true;
                phaseStartMs = millis();
                _segmentOn(p_sch.segments[segIndex], p_sch.motion);
            }
        }
    }

    // --------------------------
    // 프로필 로직
    // --------------------------
    void _resetProfileState() {
        profileActive       = false;
        profileIndex        = -1;
        profileSegIndex     = -1;
        profileOnPhase      = false;
        profilePhaseStartMs = 0;
        profileStartSec     = 0;
    }

    int _findProfileIndexByNo(const ST_A10_UserOpProfiles_t& p, uint8_t p_no) const {
        for (uint16_t v_i=0; v_i<p.count; ++v_i)
            if (p.items[v_i].profileNo == p_no) return (int)v_i;
        return -1;
    }

    bool _autoOffDue(const ST_A10_UserProfile_t& p_prof) const {
        const ST_A10_AutoOff_t& a = p_prof.autoOff;

        // timer
        if (a.timer.enabled && profileStartSec > 0) {
            unsigned long v_now = millis()/1000UL;
            unsigned long v_dur = (unsigned long)a.timer.minutes * 60UL;
            if (v_now >= profileStartSec + v_dur) return true;
        }

        // offTime (HH:MM)
        if (a.offTime.enabled) {
            int v_nowMin = _nowLocalMinutes(nullptr);
            int v_offMin = _parseHHMM(a.offTime.time);
            if (v_nowMin >= 0 && v_offMin >= 0 && v_nowMin == v_offMin) return true;
        }

        // offTemp (>= temp ℃)
        if (a.offTemp.enabled && fnReadTempC) {
            float v_t = fnReadTempC();
            if (v_t >= a.offTemp.temp) return true;
        }
        return false;
    }

    void _tickProfile() {
        if (!profiles || !dict || profileIndex < 0) {
            // 안전 정지 및 상태 초기화
            _resetProfileState();
            sim->stop();
            pwm->P10_setDutyPercent(0.0f);
            return;
        }

        const ST_A10_UserProfile_t& prof = (*profiles).items[profileIndex];

        // 모션 게이트
        if (_motionBlocked(prof.motion)) {
            sim->stop();
            pwm->P10_setDutyPercent(0.0f);
            return;
        }

        // autoOff
        if (_autoOffDue(prof)) {
            CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] profile autoOff");
            clearProfile();
            sim->stop();
            pwm->P10_setDutyPercent(0.0f);
            return;
        }

        // 세그먼트 적용
        _applyProfileSegment(prof);
    }

    void _applyProfileSegment(const ST_A10_UserProfile_t& p_prof) {
        if (p_prof.seg_count == 0) {
            sim->stop();
            pwm->P10_setDutyPercent(0.0f);
            return;
        }

        if (profileSegIndex < 0) {
            profileSegIndex = 0;
            profileOnPhase  = true;
            profilePhaseStartMs = millis();
            _segmentOn(p_prof.segments[profileSegIndex], p_prof.motion);
            return;
        }

        const ST_A10_Segment_t& v_seg = p_prof.segments[profileSegIndex];
        uint32_t v_onMs  = _minutesToMs(max<uint16_t>(1, v_seg.on_minutes));
        uint32_t v_offMs = _minutesToMs(v_seg.off_minutes);

        if (profileOnPhase) {
            if (millis() - profilePhaseStartMs >= v_onMs) {
                profileOnPhase = false;
                profilePhaseStartMs = millis();
                _segmentOff();
            }
        } else {
            if (millis() - profilePhaseStartMs >= v_offMs) {
                profileSegIndex++;
                if (profileSegIndex >= (int)p_prof.seg_count) {
                    if (p_prof.repeatSegments) {
                        profileSegIndex = 0;
                    } else {
                        // 반복 없음 → 종료
                        clearProfile();
                        sim->stop();
                        pwm->P10_setDutyPercent(0.0f);
                        return;
                    }
                }
                profileOnPhase = true;
                profilePhaseStartMs = millis();
                _segmentOn(p_prof.segments[profileSegIndex], p_prof.motion);
            }
        }
    }

    // --------------------------
    // 세그먼트 공통 적용
    // --------------------------
    void _segmentOn(const ST_A10_Segment_t& p_seg, const ST_A10_MotionGate_t& /*p_gate*/) {
        if (!dict || !sim || !pwm) return;

        if (p_seg.mode == EN_A10_SEG_MODE_FIXED) {
            sim->stop();
            pwm->P10_setDutyPercent(constrain(p_seg.fixed_speed, 0.0f, 100.0f));
            CL_D10_Logger::log(EN_L10_LOG_DEBUG, "[CT10] Segment FIXED %.1f%%", p_seg.fixed_speed);
            return;
        }

        // PRESET: presetCode + styleCode + adjust → resolve → applyResolvedWind
        ST_A10_ResolvedWind_t v_res {};
        if (!CL_C10_ConfigManager::C10_resolveWindParams(
                *dict,
                p_seg.presetCode,
                p_seg.styleCode,
                (p_seg.hasAdjust ? &p_seg.adjust : nullptr),
                v_res)) {
            // 해석 실패 시 안전 정지
            CL_D10_Logger::log(EN_L10_LOG_ERROR, "[CT10] resolveWind failed (%s/%s)",
                               p_seg.presetCode, p_seg.styleCode);
            sim->stop();
            pwm->P10_setDutyPercent(0.0f);
            return;
        }

        sim->applyResolvedWind(v_res);
        if (!sim->S10_active) sim->begin(*pwm, /*applyPreset*/ false);
        CL_D10_Logger::log(EN_L10_LOG_DEBUG, "[CT10] Segment PRESET %s/%s",
                           p_seg.presetCode, p_seg.styleCode);
    }

    void _segmentOff() {
        if (!sim || !pwm) return;
        sim->stop();
        pwm->P10_setDutyPercent(0.0f);
        CL_D10_Logger::log(EN_L10_LOG_DEBUG, "[CT10] Segment OFF");
    }

    // --------------------------
    // 오버라이드 적용
    // --------------------------
    void _applyOverride() {
        if (!overrideState.active || !sim || !pwm) return;

        if (overrideState.mode == EN_CT10_OVERRIDE_FIXED) {
            sim->stop();
            pwm->P10_setDutyPercent(constrain(overrideState.fixedPercent, 0.0f, 100.0f));
            return;
        }

        if (!dict) return;

        ST_A10_ResolvedWind_t v_res {};
        if (!CL_C10_ConfigManager::C10_resolveWindParams(
                *dict,
                overrideState.presetCode,
                overrideState.styleCode,
                &overrideState.adjust,
                v_res)) {
            // 해석 실패 시 정지
            sim->stop();
            pwm->P10_setDutyPercent(0.0f);
            return;
        }
        sim->applyResolvedWind(v_res);
        if (!sim->S10_active) sim->begin(*pwm, /*applyPreset*/ false);
    }

    void _reapplyCurrentMode() {
        // 프로필이 있으면 프로필 세그먼트 즉시 다시 on 적용
        if (profileActive && profiles && profileIndex >= 0) {
            const ST_A10_UserProfile_t& prof = (*profiles).items[profileIndex];
            if (profileSegIndex >= 0 && profileSegIndex < (int)prof.seg_count) {
                _segmentOn(prof.segments[profileSegIndex], prof.motion);
                return;
            }
        }
        // 아니면 스케줄 세그먼트 재적용
        if (schedules && scheduleIndex >= 0) {
            const ST_A10_ScheduleItem_t& sch = (*schedules).items[scheduleIndex];
            if (segIndex >= 0 && segIndex < (int)sch.seg_count) {
                _segmentOn(sch.segments[segIndex], sch.motion);
                return;
            }
        }
        // 없으면 정지
        sim->stop();
        pwm->P10_setDutyPercent(0.0f);
    }
};
