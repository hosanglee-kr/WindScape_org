#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : CT10_ControlManager_015.h
 * 모듈약어 : CT10
 * 모듈명 : Smart Nature Wind 통합 제어 Manager (v015)
 * ------------------------------------------------------
 * 기능 요약
 *  - 스케줄(cfg_schedules_024.json) + 유저 프로파일(cfg_uzOpProfile_025_final.json) 기반 통합 제어
 *  - Segment 단위 ON/OFF 사이클
 *  - Motion 게이팅(PIR/BLE)
 *  - Override (preset/style/adjust 강제)
 *  - AutoOff (timer / offTime / offTemp) 지원
 *  - Simulation(S10)/PWM(P10) 연동
 *  - NVS 최소-쓰기 정책으로 런타임 상태 보존
 *  - 상태 JSON Export
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
#include <cstring>

#include "A10_Const_014.h"
#include "C10_ConfigManager_014.h"
#include "D10_Logger_011.h"
#include "S10_Simulation_014.h"
#include "P10_PWM_ctrl_014.h"
#include "M10_MotionLogic_014.h"

// (선택) NVS 매니저가 있을 경우 사용
#ifdef __has_include
#  if __has_include("N10_NVSManager_014.h")
#    include "N10_NVSManager_014.h"
#    define G_CT10_HAS_NVS 1
#  endif
#endif
#ifndef G_CT10_HAS_NVS
#  define G_CT10_HAS_NVS 0
#endif

// ------------------------------------------------------
// 상수/유틸
// ------------------------------------------------------
namespace CT10_Const {
    constexpr uint32_t G_CT10_MIN_AUTOOFF_MINUTES     = 1;
    constexpr uint32_t G_CT10_MAX_AUTOOFF_MINUTES     = 24*60;
    constexpr uint32_t G_CT10_SEGMENT_MIN_ON_MIN      = 1;
    constexpr uint32_t G_CT10_SEGMENT_MAX_ON_MIN      = 24*60;
    constexpr uint32_t G_CT10_SEGMENT_MAX_OFF_MIN     = 24*60;
    constexpr uint32_t G_CT10_STATE_SAVE_DEBOUNCE_MS  = 1500; // NVS 최소-쓰기
    constexpr uint32_t G_CT10_TICK_MIN_INTERVAL_MS    = 25;   // 과도한 loop 억제
}

// ------------------------------------------------------
// 런상태/오버라이드/AutoOff 타입
// ------------------------------------------------------
typedef enum : uint8_t {
    EN_CT10_RUN_IDLE = 0,
    EN_CT10_RUN_PROFILE,
    EN_CT10_RUN_SCHEDULE,
    EN_CT10_RUN_OVERRIDE,
    EN_CT10_RUN_FAULT
} EN_CT10_RunState_t;

typedef struct {
    bool     active = false;
    uint32_t until_sec = 0;
    char     presetCode[24] = {0};
    char     styleCode[24]  = {0};
    ST_A10_AdjustDelta_t adj{};
} ST_CT10_Override_t;

typedef struct {
    // timer 기반
    bool     timerEnabled = false;
    uint32_t timerStartSec = 0;
    uint32_t timerMinutes  = 0;
    // offTime 기반 ("HH:MM", localtime)
    bool     offTimeEnabled = false;
    char     offTimeHHMM[6] = {0}; // "23:30"
    // offTemp 기반 (외부 온도센서)
    bool     offTempEnabled = false;
    float    offTempCelsius = 0.0f;
} ST_CT10_AutoOff_t;

typedef float (*T_CT10_ReadTempC_t)();

// ------------------------------------------------------
// 컨텍스트 보관
// ------------------------------------------------------
typedef struct {
    // 모드 식별자
    EN_CT10_RunState_t runState = EN_CT10_RUN_IDLE;

    // 프로파일 실행 컨텍스트
    int  activeProfileNo = -1;
    int  profileCurSeg   = -1;
    bool profileOnPhase  = false;
    uint32_t profilePhaseStartMs = 0;

    // 스케줄 실행 컨텍스트
    int  activeScheduleIdx = -1;  // schedules 배열 index
    int  schedCurSeg       = -1;
    bool schedOnPhase      = false;
    uint32_t schedPhaseStartMs = 0;

    // 오버라이드/AutoOff
    ST_CT10_Override_t override;
    ST_CT10_AutoOff_t  autoOff;

    // 모션 게이트 카운터(연속 실패 보호)
    uint8_t motionErrorCount = 0;

    // 상태 저장 델타 억제
    uint32_t lastStateSaveMs = 0;

    // 마지막 tick 간격 제어
    uint32_t lastTickMs = 0;
} ST_CT10_RunContext_t;

// ------------------------------------------------------
// 전역(필요 최소)
// ------------------------------------------------------
static inline uint32_t CT10_nowSec() { return millis() / 1000UL; }

// ------------------------------------------------------
// Control Manager 클래스 (header-only)
// ------------------------------------------------------
class CL_CT10_ControlManager {
public:
    // 외부 연결 모듈
    void attach(CL_S10_Simulation* p_sim, CL_P10_PWM* p_pwm, CL_M10_MotionLogic* p_motion) {
        sim = p_sim; pwm = p_pwm; motion = p_motion;
    }
    void setTempProvider(T_CT10_ReadTempC_t p_reader) { readTempC = p_reader; }

    // 초기화: 기존 상태 복원(NVS) + 안전정지
    void begin() {
        memset(&ctx, 0, sizeof(ctx));
        ctx.runState = EN_CT10_RUN_IDLE;
        ctx.activeProfileNo = -1;
        ctx.activeScheduleIdx = -1;
        ctx.profileCurSeg = ctx.schedCurSeg = -1;
        ctx.profileOnPhase = ctx.schedOnPhase = false;
        ctx.lastTickMs = millis();

#if G_CT10_HAS_NVS
        // 최소 복원: 마지막 활성 모드 정보
        ST_N10_RunState_t v_rs{};
        if (CL_N10_NVSManager::N10_loadRunState(v_rs)) {
            // 복원 정책: 자동 시작 대신 IDLE 유지(안전)
            // 필요시 설정에서 "부팅시 최근 상태 자동복원" 옵션이 도입되면 여기서 활성화
        }
#endif
        if (sim) sim->stop();
        if (pwm) pwm->P10_setDutyPercent(0.0f);
        CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] begin()");
    }

    // ---------------------------
    // 모드 전환 API
    // ---------------------------
    bool startProfile(uint8_t p_profileNo) {
        if (!g_A10_config_root.userProfiles || !sim || !pwm) return false;
        const ST_A10_UserProfilesRoot_t* v_up = g_A10_config_root.userProfiles;
        int idx = _findProfileIndexByNo(*v_up, p_profileNo);
        if (idx < 0) return false;

        _clearScheduleCtx();
        _armProfile(idx);
        _applyFirstProfileSegment();
        _saveRunStateDebounced(EN_CT10_RUN_PROFILE);
        CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] startProfile no=%d", (int)p_profileNo);
        return true;
    }

    bool startScheduleByNo(uint16_t p_scheduleNo) {
        if (!g_A10_config_root.schedules || !sim || !pwm) return false;
        int sidx = _findScheduleIndexByNo(*g_A10_config_root.schedules, p_scheduleNo);
        if (sidx < 0) return false;

        _clearProfileCtx();
        _armSchedule(sidx);
        _applyFirstScheduleSegment();
        _saveRunStateDebounced(EN_CT10_RUN_SCHEDULE);
        CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] startSchedule schNo=%d", (int)p_scheduleNo);
        return true;
    }

    void stopAll() {
        _clearProfileCtx();
        _clearScheduleCtx();
        _clearOverride();
        if (sim) sim->stop();
        if (pwm) pwm->P10_setDutyPercent(0.0f);
        ctx.runState = EN_CT10_RUN_IDLE;
        _saveRunStateDebounced(ctx.runState);
        CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] stopAll()");
    }

    // ---------------------------
    // Override API
    // ---------------------------
    bool overrideWind(const char* p_presetCode,
                      const char* p_styleCode,
                      const ST_A10_AdjustDelta_t* p_adj,
                      uint32_t p_duration_sec) {
        if (!sim || !pwm || !g_A10_config_root.windDict) return false;
        memset(&ctx.override, 0, sizeof(ctx.override));
        ctx.override.active = true;
        ctx.override.until_sec = CT10_nowSec() + p_duration_sec;
        if (p_presetCode) strlcpy(ctx.override.presetCode, p_presetCode, sizeof(ctx.override.presetCode));
        if (p_styleCode)  strlcpy(ctx.override.styleCode,  p_styleCode,  sizeof(ctx.override.styleCode));
        if (p_adj)        ctx.override.adj = *p_adj;

        ST_A10_ResolvedWind_t v_w{};
        if (!CL_C10_ConfigManager::C10_resolveWindParams(*g_A10_config_root.windDict,
                                                         ctx.override.presetCode,
                                                         ctx.override.styleCode,
                                                         &ctx.override.adj,
                                                         v_w)) {
            ctx.override.active = false;
            CL_D10_Logger::log(EN_L10_LOG_ERROR, "[CT10] overrideWind resolve failed");
            return false;
        }
        _applyResolvedWind(v_w);
        ctx.runState = EN_CT10_RUN_OVERRIDE;
        _saveRunStateDebounced(ctx.runState);
        CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] overrideWind %s/%s %us",
            ctx.override.presetCode, ctx.override.styleCode, (unsigned)p_duration_sec);
        return true;
    }

    void releaseOverride() {
        if (!ctx.override.active) return;
        _clearOverride();
        // 이전 모드로 즉시 복귀
        if (ctx.activeProfileNo >= 0) {
            _applyProfileSegmentTick(); // 현재 세그먼트 강제 재적용
            ctx.runState = EN_CT10_RUN_PROFILE;
        } else if (ctx.activeScheduleIdx >= 0) {
            _applyScheduleSegmentTick();
            ctx.runState = EN_CT10_RUN_SCHEDULE;
        } else {
            stopAll();
            return;
        }
        _saveRunStateDebounced(ctx.runState);
        CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] releaseOverride");
    }

    // ---------------------------
    // 주기 호출
    // ---------------------------
    void tick() {
        const uint32_t v_nowMs = millis();
        if (v_nowMs - ctx.lastTickMs < CT10_Const::G_CT10_TICK_MIN_INTERVAL_MS) return;
        ctx.lastTickMs = v_nowMs;

        if (!sim || !pwm) return;

        // 1) 오버라이드 유효성 체크
        if (ctx.override.active) {
            if (CT10_nowSec() >= ctx.override.until_sec) {
                releaseOverride();
            } else {
                // 오버라이드 동안에도 AutoOff/모션 게이트는 적용
                if (_motionGated()) {
                    sim->stop(); pwm->P10_setDutyPercent(0.0f);
                }
                _checkAutoOff();
                return; // 오버라이드가 최우선, 바람 변경은 유지
            }
        }

        // 2) 모션 게이팅
        if (_motionGated()) {
            sim->stop(); pwm->P10_setDutyPercent(0.0f);
            return;
        }

        // 3) 모드 분기
        switch (ctx.runState) {
            case EN_CT10_RUN_PROFILE:
                _applyProfileSegmentTick();
                break;
            case EN_CT10_RUN_SCHEDULE:
                _applyScheduleSegmentTick();
                break;
            case EN_CT10_RUN_IDLE:
            default:
                // do nothing
                break;
        }

        // 4) AutoOff 검사(모드 공통)
        _checkAutoOff();
    }

    // ---------------------------
    // 상태 JSON Export
    // ---------------------------
    void toJson(JsonDocument& p_doc) {
        JsonObject o = p_doc["ct10"].to<JsonObject>();
        o["runState"] = _runStateName(ctx.runState);

        JsonObject prof = o["profile"].to<JsonObject>();
        prof["activeNo"]     = ctx.activeProfileNo;
        prof["curSeg"]       = ctx.profileCurSeg;
        prof["onPhase"]      = ctx.profileOnPhase;

        JsonObject sch = o["schedule"].to<JsonObject>();
        sch["activeIndex"]   = ctx.activeScheduleIdx;
        sch["curSeg"]        = ctx.schedCurSeg;
        sch["onPhase"]       = ctx.schedOnPhase;

        JsonObject ov = o["override"].to<JsonObject>();
        ov["active"] = ctx.override.active;
        if (ctx.override.active) {
            ov["presetCode"] = ctx.override.presetCode;
            ov["styleCode"]  = ctx.override.styleCode;
            int32_t remain = (int32_t)ctx.override.until_sec - (int32_t)CT10_nowSec();
            ov["remainSec"] = remain>0?remain:0;
        }

        JsonObject ao = o["autoOff"].to<JsonObject>();
        ao["timerEnabled"] = ctx.autoOff.timerEnabled;
        ao["offTimeEnabled"] = ctx.autoOff.offTimeEnabled;
        ao["offTempEnabled"] = ctx.autoOff.offTempEnabled;

        if (sim) {
            JsonObject simo = o["sim"].to<JsonObject>();
            simo["active"] = sim->S10_active;
            simo["pwm"]    = pwm ? pwm->P10_getDutyPercent() : 0.0f;
            sim->S10_toJson(p_doc); // 시뮬레이터 표준 JSON도 병행 출력 (sim/* 키 사용)
        }
    }

private:
    // 모듈 포인터
    CL_S10_Simulation* sim   = nullptr;
    CL_P10_PWM*        pwm   = nullptr;
    CL_M10_MotionLogic* motion = nullptr;
    T_CT10_ReadTempC_t readTempC = nullptr;

    // 런타임 컨텍스트
    ST_CT10_RunContext_t ctx{};

    // ---------------------------
    // 내부 유틸
    // ---------------------------
    static const char* _runStateName(EN_CT10_RunState_t s) {
        switch (s) {
            case EN_CT10_RUN_PROFILE:  return "PROFILE";
            case EN_CT10_RUN_SCHEDULE: return "SCHEDULE";
            case EN_CT10_RUN_OVERRIDE: return "OVERRIDE";
            case EN_CT10_RUN_FAULT:    return "FAULT";
            default: return "IDLE";
        }
    }

    void _clearOverride() { memset(&ctx.override, 0, sizeof(ctx.override)); }
    void _clearProfileCtx() {
        ctx.activeProfileNo = -1; ctx.profileCurSeg = -1;
        ctx.profileOnPhase = false; ctx.profilePhaseStartMs = 0;
    }
    void _clearScheduleCtx() {
        ctx.activeScheduleIdx = -1; ctx.schedCurSeg = -1;
        ctx.schedOnPhase = false; ctx.schedPhaseStartMs = 0;
    }

    // ---------------------------
    // NVS 최소-쓰기
    // ---------------------------
    void _saveRunStateDebounced(EN_CT10_RunState_t p_state) {
#if G_CT10_HAS_NVS
        uint32_t v_now = millis();
        if (v_now - ctx.lastStateSaveMs < CT10_Const::G_CT10_STATE_SAVE_DEBOUNCE_MS) return;
        ctx.lastStateSaveMs = v_now;

        ST_N10_RunState_t v{};
        v.runState = (uint8_t)p_state;
        v.activeProfileNo   = ctx.activeProfileNo;
        v.activeScheduleNo  = (ctx.activeScheduleIdx>=0 && g_A10_config_root.schedules)
                                ? g_A10_config_root.schedules->items[ctx.activeScheduleIdx].schNo
                                : -1;
        CL_N10_NVSManager::N10_saveRunState(v);
#else
        (void)p_state;
#endif
    }

    // ---------------------------
    // 모션 게이팅
    // ---------------------------
    bool _motionGated() {
        if (!motion) return false;
        bool present = motion->M10_isMotionPresent();
        if (!present) {
            // 연속 실패 보호
            if (ctx.motionErrorCount < 250) ctx.motionErrorCount++;
        } else {
            ctx.motionErrorCount = 0;
        }
        // 단순 게이트: 존재하지 않으면 게이트 ON
        return !present;
    }

    // ---------------------------
    // AutoOff 검사
    // ---------------------------
    void _checkAutoOff() {
        uint32_t nowS = CT10_nowSec();

        // 1) Timer
        if (ctx.autoOff.timerEnabled && ctx.autoOff.timerMinutes >= CT10_Const::G_CT10_MIN_AUTOOFF_MINUTES) {
            if (ctx.autoOff.timerStartSec > 0) {
                uint32_t elapsed = nowS - ctx.autoOff.timerStartSec;
                if (elapsed >= ctx.autoOff.timerMinutes*60UL) {
                    CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] AutoOff: timer");
                    stopAll();
                    return;
                }
            }
        }

        // 2) OffTime ("HH:MM")
        if (ctx.autoOff.offTimeEnabled && ctx.autoOff.offTimeHHMM[0]) {
            int targetMin = _parseHHMM(ctx.autoOff.offTimeHHMM);
            if (targetMin >= 0) {
                time_t v_t = time(nullptr);
                struct tm* v_tm = localtime(&v_t);
                if (v_tm) {
                    int nowMin = v_tm->tm_hour*60 + v_tm->tm_min;
                    // 동일 분에 진입한 최초 tick 시 정지(중복 방지 간격은 tick 최소 간격으로 충분)
                    if (nowMin == targetMin) {
                        CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] AutoOff: offTime %s", ctx.autoOff.offTimeHHMM);
                        stopAll();
                        return;
                    }
                }
            }
        }

        // 3) OffTemp (외부 센서)
        if (ctx.autoOff.offTempEnabled && readTempC) {
            float t = readTempC();
            if (t >= ctx.autoOff.offTempCelsius && ctx.autoOff.offTempCelsius > 0.1f) {
                CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] AutoOff: offTemp %.1f°C >= %.1f°C", t, ctx.autoOff.offTempCelsius);
                stopAll();
                return;
            }
        }
    }

    // ---------------------------
    // 프로파일 실행 준비/적용
    // ---------------------------
    void _armProfile(int p_profilesIndex) {
        ctx.runState = EN_CT10_RUN_PROFILE;
        ctx.activeProfileNo = g_A10_config_root.userProfiles->profiles[p_profilesIndex].profileNo;
        ctx.profileCurSeg = -1;
        ctx.profileOnPhase = false;
        ctx.profilePhaseStartMs = 0;

        // AutoOff 설정 주입
        _loadProfileAutoOff(g_A10_config_root.userProfiles->profiles[p_profilesIndex]);
        if (ctx.autoOff.timerEnabled) ctx.autoOff.timerStartSec = CT10_nowSec();
    }

    void _applyFirstProfileSegment() {
        ctx.profileCurSeg = -1;
        _applyProfileSegmentTick(); // 내부에서 첫 세그먼트 진입
    }

    void _applyProfileSegmentTick() {
        const ST_A10_UserProfilesRoot_t* up = g_A10_config_root.userProfiles;
        if (!up) return;
        int pidx = _findProfileIndexByNo(*up, ctx.activeProfileNo);
        if (pidx < 0) { stopAll(); return; }
        const ST_A10_UserProfile_t& prof = up->profiles[pidx];
        if (!prof.enabled || prof.seg_count == 0) { stopAll(); return; }

        if (ctx.profileCurSeg < 0) {
            ctx.profileCurSeg = 0;
            ctx.profileOnPhase = true;
            ctx.profilePhaseStartMs = millis();
            _applyProfileSegOn(prof.segments[ctx.profileCurSeg], prof);
            return;
        }

        const auto& seg = prof.segments[ctx.profileCurSeg];
        uint32_t onMs  = max<uint32_t>(CT10_Const::G_CT10_SEGMENT_MIN_ON_MIN, seg.on_minutes) * 60000UL;
        uint32_t offMs = min<uint32_t>(seg.off_minutes, CT10_Const::G_CT10_SEGMENT_MAX_OFF_MIN) * 60000UL;

        if (ctx.profileOnPhase) {
            if (millis() - ctx.profilePhaseStartMs >= onMs) {
                ctx.profileOnPhase = false;
                ctx.profilePhaseStartMs = millis();
                _applySegOff();
            }
        } else {
            if (millis() - ctx.profilePhaseStartMs >= offMs) {
                // 다음 세그먼트
                int next = ctx.profileCurSeg + 1;
                if (next >= (int)prof.seg_count) {
                    if (prof.repeatSegments) next = 0; else { stopAll(); return; }
                }
                ctx.profileCurSeg = next;
                ctx.profileOnPhase = true;
                ctx.profilePhaseStartMs = millis();
                _applyProfileSegOn(prof.segments[ctx.profileCurSeg], prof);
            }
        }
    }

    void _applyProfileSegOn(const ST_A10_ProfileSegment_t& p_seg, const ST_A10_UserProfile_t&) {
        if (p_seg.mode == EN_A10_SEG_MODE_FIXED) {
            if (sim) sim->stop();
            if (pwm) pwm->P10_setDutyPercent(constrain(p_seg.fixed_speed, 0.0f, 100.0f));
            return;
        }
        // PRESET 모드: preset×style×adjust → resolved
        ST_A10_ResolvedWind_t v_w{};
        if (!CL_C10_ConfigManager::C10_resolveWindParams(*g_A10_config_root.windDict,
                                                         p_seg.presetCode,
                                                         p_seg.styleCode,
                                                         &p_seg.adjust,
                                                         v_w)) {
            // 실패 시 안전정지
            if (sim) sim->stop();
            if (pwm) pwm->P10_setDutyPercent(0.0f);
            CL_D10_Logger::log(EN_L10_LOG_ERROR, "[CT10] profile seg resolve failed");
            return;
        }
        _applyResolvedWind(v_w);
    }

    // ---------------------------
    // 스케줄 실행 준비/적용
    // ---------------------------
    void _armSchedule(int p_scheduleIndex) {
        ctx.runState = EN_CT10_RUN_SCHEDULE;
        ctx.activeScheduleIdx = p_scheduleIndex;
        ctx.schedCurSeg = -1;
        ctx.schedOnPhase = false;
        ctx.schedPhaseStartMs = 0;

        // 스케줄에는 AutoOffTimer만 존재(최근 합의), 필요 시 확장
        _clearAutoOff();
        const ST_A10_ScheduleItem_t& sch = g_A10_config_root.schedules->items[p_scheduleIndex];
        if (sch.autoOffTimer.enabled) {
            ctx.autoOff.timerEnabled = true;
            ctx.autoOff.timerMinutes = constrain((uint32_t)sch.autoOffTimer.minutes,
                CT10_Const::G_CT10_MIN_AUTOOFF_MINUTES, CT10_Const::G_CT10_MAX_AUTOOFF_MINUTES);
            ctx.autoOff.timerStartSec = CT10_nowSec();
        }
    }

    void _applyFirstScheduleSegment() {
        ctx.schedCurSeg = -1;
        _applyScheduleSegmentTick();
    }

    void _applyScheduleSegmentTick() {
        const ST_A10_SchedulesRoot_t* sc = g_A10_config_root.schedules;
        if (!sc) return;
        if (ctx.activeScheduleIdx < 0 || ctx.activeScheduleIdx >= (int)sc->count) { stopAll(); return; }
        const ST_A10_ScheduleItem_t& sch = sc->items[ctx.activeScheduleIdx];

        // period.enabled 체크 + 현재 시간대 유효성
        if (sch.period.enabled) {
            if (!_isNowWithinPeriod(sch.period)) {
                // 시간대 벗어나면 정지 유지
                if (sim) sim->stop();
                if (pwm) pwm->P10_setDutyPercent(0.0f);
                return;
            }
        }

        if (sch.seg_count == 0) { stopAll(); return; }

        if (ctx.schedCurSeg < 0) {
            ctx.schedCurSeg = 0;
            ctx.schedOnPhase = true;
            ctx.schedPhaseStartMs = millis();
            _applyScheduleSegOn(sch.segments[ctx.schedCurSeg], sch);
            return;
        }

        const auto& seg = sch.segments[ctx.schedCurSeg];
        uint32_t onMs  = max<uint32_t>(CT10_Const::G_CT10_SEGMENT_MIN_ON_MIN, seg.on_minutes) * 60000UL;
        uint32_t offMs = min<uint32_t>(seg.off_minutes, CT10_Const::G_CT10_SEGMENT_MAX_OFF_MIN) * 60000UL;

        if (ctx.schedOnPhase) {
            if (millis() - ctx.schedPhaseStartMs >= onMs) {
                ctx.schedOnPhase = false;
                ctx.schedPhaseStartMs = millis();
                _applySegOff();
            }
        } else {
            if (millis() - ctx.schedPhaseStartMs >= offMs) {
                int next = ctx.schedCurSeg + 1;
                if (next >= (int)sch.seg_count) next = 0; // 스케줄은 순환
                ctx.schedCurSeg = next;
                ctx.schedOnPhase = true;
                ctx.schedPhaseStartMs = millis();
                _applyScheduleSegOn(sch.segments[ctx.schedCurSeg], sch);
            }
        }
    }

    void _applyScheduleSegOn(const ST_A10_ScheduleSegment_t& p_seg, const ST_A10_ScheduleItem_t&) {
        if (p_seg.mode == EN_A10_SEG_MODE_FIXED) {
            if (sim) sim->stop();
            if (pwm) pwm->P10_setDutyPercent(constrain(p_seg.fixed_speed, 0.0f, 100.0f));
            return;
        }
        ST_A10_ResolvedWind_t v_w{};
        if (!CL_C10_ConfigManager::C10_resolveWindParams(*g_A10_config_root.windDict,
                                                         p_seg.presetCode,
                                                         p_seg.styleCode,
                                                         &p_seg.adjust,
                                                         v_w)) {
            if (sim) sim->stop();
            if (pwm) pwm->P10_setDutyPercent(0.0f);
            CL_D10_Logger::log(EN_L10_LOG_ERROR, "[CT10] schedule seg resolve failed");
            return;
        }
        _applyResolvedWind(v_w);
    }

    // ---------------------------
    // 공통: 세그먼트 OFF
    // ---------------------------
    void _applySegOff() {
        if (sim) sim->stop();
        if (pwm) pwm->P10_setDutyPercent(0.0f);
    }

    // ---------------------------
    // 해석 wind → S10 적용
    // ---------------------------
    void _applyResolvedWind(const ST_A10_ResolvedWind_t& p_w) {
        if (!sim || !pwm) return;
        sim->S10_setParams(
            p_w.wind_intensity,
            p_w.wind_variability,
            p_w.gust_frequency,
            p_w.min_fan,
            p_w.fan_limit,
            p_w.presetCode
        );
        sim->S10_applyPreset(p_w.presetCode);
        if (!sim->S10_active) sim->begin(*pwm, /*applyPreset*/ false);
    }

    // ---------------------------
    // 헬퍼들
    // ---------------------------
    static int _parseHHMM(const char* p) {
        if (!p) return -1;
        int h=-1,m=-1;
        if (sscanf(p, "%d:%d", &h,&m) != 2) return -1;
        if (h<0||h>23||m<0||m>59) return -1;
        return h*60 + m;
    }

    static int _parseHHMM(const String& p) { return _parseHHMM(p.c_str()); }

    bool _isNowWithinPeriod(const ST_A10_Period_t& p) {
        time_t v_t = time(nullptr);
        struct tm* v_tm = localtime(&v_t);
        if (!v_tm) return false;
        if (!p.enabled) return true; // period 비활성 시 무조건 허용

        int wday = v_tm->tm_wday; // 0=Sun..6=Sat
        if (wday<0 || wday>6) return false;
        if (p.days[wday] == 0) return false;

        int st = _parseHHMM(p.start_time);
        int ed = _parseHHMM(p.end_time);
        if (st<0 || ed<0) return false;

        int nowMin = v_tm->tm_hour*60 + v_tm->tm_min;
        if (st <= ed) { // 당일
            return (st <= nowMin && nowMin < ed);
        } else {        // 자정을 넘김
            return (nowMin >= st || nowMin < ed);
        }
    }

    static int _findProfileIndexByNo(const ST_A10_UserProfilesRoot_t& up, int profileNo) {
        for (int i=0;i<(int)up.count;i++) {
            if (up.profiles[i].profileNo == profileNo) return i;
        }
        return -1;
    }

    static int _findScheduleIndexByNo(const ST_A10_SchedulesRoot_t& sc, int schNo) {
        for (int i=0;i<(int)sc.count;i++) {
            if (sc.items[i].schNo == schNo) return i;
        }
        return -1;
    }

    void _loadProfileAutoOff(const ST_A10_UserProfile_t& p) {
        _clearAutoOff();
        // user profile의 autoOff (timer / offTime / offTemp) 채움
        if (p.autoOff.timer.enabled) {
            ctx.autoOff.timerEnabled = true;
            ctx.autoOff.timerMinutes =
                constrain((uint32_t)p.autoOff.timer.minutes,
                          CT10_Const::G_CT10_MIN_AUTOOFF_MINUTES,
                          CT10_Const::G_CT10_MAX_AUTOOFF_MINUTES);
        }
        if (p.autoOff.offTime.enabled) {
            ctx.autoOff.offTimeEnabled = true;
            strlcpy(ctx.autoOff.offTimeHHMM, p.autoOff.offTime.hhmm, sizeof(ctx.autoOff.offTimeHHMM));
        }
        if (p.autoOff.offTemp.enabled) {
            ctx.autoOff.offTempEnabled = true;
            ctx.autoOff.offTempCelsius = p.autoOff.offTemp.tempC;
        }
    }

    void _clearAutoOff() {
        memset(&ctx.autoOff, 0, sizeof(ctx.autoOff));
    }
};
