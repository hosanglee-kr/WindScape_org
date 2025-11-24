#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : CT10_ControlManager_022.h
 * 모듈약어 : CT10
 * 모듈명 : Smart Nature Wind 제어 통합 Manager (v021)
 * ------------------------------------------------------
 * 기능 요약:
 * - Schedule / UserProfile / Manual Override 기반 풍속 제어
 * - WindDict 기반 해석 (resolveWindParams) 후 S10.applyResolvedWind 연동
 * - PWM(P10) / Simulation(S10) 통합 제어
 * - Motion (PIR / BLE) 및 AutoOff 조건 훅 제공
 * - Web UI / 버튼에서 Profile 선택, Override 즉시 반영
 * - JSON 상태 Export (control / override / autoOff / sim / metrics)
 * - Dirty 플래그 기반 SC10 diffOnly API 연동 지원 (SC10이 브로드캐스트 처리)
 * - Override 타임아웃 및 AutoOff 발생 시 Dirty 플래그 자동 설정
 * - 정적 싱글톤 인터페이스 제공 (W10_WebAPI에서 직접 사용)
 * ------------------------------------------------------
 * [구현 규칙]
 * - 주석 구조, 네이밍 규칙, ArduinoJson v7 단일 문서 정책 준수
 * - 소스 시작 주석 부분 구현규칙, 코드네이밍규칙 내용 그대로 유지, 수정금지
 * - ArduinoJson v7.x.x 사용 (v6 이하 사용 금지)
 * - JsonDocument 단일 타입만 사용
 * - createNestedArray/Object/containsKey 사용 금지
 * - memset + strlcpy 기반 안전 초기화
 * - 주석/필드명은 JSON 구조와 동일하게 유지
 * - 모듈별 단일 헤더(h)파일로만 구성 (cpp 없음)
 * ------------------------------------------------------
 * [코드 네이밍 규칙]
 * - 전역 상수,매크로      : G_모듈약어_ 접두사
 * - 전역 변수             : g_모듈약어_ 접두사
 * - 전역 함수             : 모듈약어_ 접두사
 * - type                  : T_모듈약어_ 접두사
 * - typedef               : _t  접미사
 * - enum 상수             : EN_모듈약어_ 접두사
 * - 구조체                : ST_모듈약어_ 접두사
 * - 클래스명              : CL_모듈약어_ 접두사
 * - 클래스 private 멤버   : _ 접두사
 * - 클래스 멤버(함수/변수) : 모듈약어 접두사 미사용
 * - 클래스 정적 멤버      : s_ 접두사
 * - 함수 로컬 변수        : v_ 접두사
 * - 함수 인자             : p_ 접두사
 * ------------------------------------------------------
 */

#include <Arduino.h>
#include <ArduinoJson.h>
#include <string.h>
#include <time.h> // AutoOff 시간 체크용

// 종속성 모듈 헤더
#include "A10_Const_015.h"
#include "C10_ConfigManager_024.h"
#include "D10_Logger_016.h"
#include "M10_MotionLogic_016.h"
#include "P10_PWM_ctrl_014.h"
#include "S10_Simulation_020.h"
#include "S20_WindSolver_021.h"



// ------------------------------------------------------
// 런타임 상태 구조체
// ------------------------------------------------------

// 현재 제어를 실행 중인 주체 (Schedule, UserProfile 등)
typedef enum : uint8_t {
	EN_CT10_RUN_NONE		 = 0,
	EN_CT10_RUN_SCHEDULE	 = 1,
	EN_CT10_RUN_USER_PROFILE = 2
} EN_CT10_run_source_t;

// Override 제어 상태 (ResolvedWind 기반)
typedef struct {
	bool				  active;			// Override 활성 여부
	bool				  useFixed;			// true: 고정 PWM 비율 사용, false: ResolvedWind 사용
	bool				  resolvedApplied;	// ResolvedWind를 S10에 1회 적용했는지 여부 (S10 상태 리셋 방지)
	unsigned long		  endMs;			// 0이면 타이머 없음(무한)
	float				  fixedPercent;		// 0~100, useFixed==true 일 때만 사용
	ST_A10_ResolvedWind_t resolved;			// 수동 바람 설정 (useFixed==false 일 때의 바람 파라미터)
} ST_CT10_Override_t;

// Segment 실행 상태 (Schedule 또는 Profile)
typedef struct {
	int8_t		  index;		 // 현재 seg index (-1이면 아직 시작 전)
	bool		  onPhase;		 // true: On 구간(팬 작동), false: Off 구간(팬 정지)
	unsigned long phaseStartMs;	 // 현재 phase 시작 시간 (Millis 기준)
} ST_CT10_SegmentRuntime_t;

// AutoOff 런타임 상태 (현재 적용된 AutoOff 조건)
typedef struct {
	bool		  timerArmed;		// 타이머 AutoOff 활성 여부
	unsigned long timerStartMs;		// 타이머 시작 시간
	uint32_t	  timerMinutes;		// 설정된 타이머 시간 (분)

	bool	 offTimeEnabled;	// 지정 시간 AutoOff 활성 여부
	uint16_t offTimeMinutes;	// AutoOff가 발동되는 하루 중 시간 (분 단위, 0~1439)

	bool  offTempEnabled;	// 온도 기반 AutoOff 활성 여부
	float offTemp;			// AutoOff가 발동되는 온도 (섭씨)
} ST_CT10_AutoOffRuntime_t;

// ======================================================
// CL_CT10_ControlManager 클래스
//  - 인스턴스 기반 + 정적 싱글톤 인터페이스 제공
// ======================================================
class CL_CT10_ControlManager {
   private:
        // Magic Number 상수화
	    static constexpr uint32_t S_TICK_MIN_INTERVAL_MS	  = 40UL;   // 최소 틱 주기 (25Hz)
        static constexpr uint32_t S_METRICS_PUSH_INTERVAL_MS = 1500UL; // 메트릭 Dirty 플래그 설정 주기
    
   public:
	    // --------------------------------------------------
	    // 싱글톤 정적 인터페이스 (W10 등 외부 모듈용)
	    // --------------------------------------------------
        // 싱글톤 인스턴스 반환
	    static CL_CT10_ControlManager& instance() {
		    static CL_CT10_ControlManager v_inst;
		    return v_inst;
	    }
    
	    // 전역 PWM 인스턴스(g_P10_pwm)를 이용한 초기화
	    static bool begin() {
		    instance().begin(g_P10_pwm);
		    return true;
	    }
    
	    // 주기 호출 (메인 루프에서 호출)
	    static void tick() {
		    instance()._tick();
	    }
    
	    // 상태 JSON Export (전체 상태)
	    static void toJson(JsonDocument& p_doc) {
		    instance()._toJson(p_doc);
	    }
	    // 차트 JSON Export
	    static void toChartJson(JsonDocument& p_doc, bool p_diffOnly = false) {
		    instance()._toChartJson(p_doc, p_diffOnly);
	    }
    
    
	    // 모드 설정: false=schedule 모드, true=profile 전용 모드
	    static void setMode(bool p_profileMode) {
		    instance()._setMode(p_profileMode);
	    }
    
	    // UserProfile 선택/시작 (profileNo 기준)
	    static bool setActiveUserProfile(uint8_t p_profileNo) {
		    return instance().startUserProfileByNo(p_profileNo);
	    }
    
	    // 수동 override 적용 (ResolvedWind 기반)
	    static void applyManual(const ST_A10_ResolvedWind_t& p_wind) {
		    instance().applyManual(p_wind, 0);	// 0 => 타임아웃 없이 유지
	    }
    
	    // 수동 override 해제
	    static void clearManual() {
		    instance().stopOverride();
	    }
    
	    // Config 전체 재로드
	    static bool reloadAll() {
		    bool v_ok = CL_C10_ConfigManager::loadAll(g_A10_config_root);
		    if (!v_ok)
			    return false;
    
		    CL_CT10_ControlManager& v_inst = instance();
            // 런타임 상태 초기화
		    v_inst.runSource			   = EN_CT10_RUN_NONE;
		    v_inst.curScheduleIndex		   = -1;
		    v_inst.curProfileIndex		   = -1;
		    v_inst.useProfileMode		   = false;
    
		    memset(&v_inst.overrideState, 0, sizeof(v_inst.overrideState));
		    memset(&v_inst.autoOffRt, 0, sizeof(v_inst.autoOffRt));
		    memset(&v_inst.scheduleSegRt, 0, sizeof(v_inst.scheduleSegRt));
		    memset(&v_inst.profileSegRt, 0, sizeof(v_inst.profileSegRt));
		    v_inst.scheduleSegRt.index = -1;
		    v_inst.profileSegRt.index  = -1;
    
		    v_inst.sim.stop();
		    // 상태 변경 후 SC10이 브로드캐스트하도록 Dirty 플래그 설정
		    v_inst.markDirty("state");
		    v_inst.markDirty("metrics");
    
		    CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] reloadAll done");
		    return true;
	    }

   public:
	    // --------------------------------------------------
	    // 인스턴스 멤버 변수 (제어 상태 저장)
	    // --------------------------------------------------
	    bool active			= false;		// 매니저 활성 여부
	    bool useProfileMode    = false;		// true면 schedule 무시하고 profile만 운전
    
	    EN_CT10_run_source_t runSource = EN_CT10_RUN_NONE; // 현재 작동 소스
    
	    int8_t curScheduleIndex   = -1;		// 현재 활성 Schedule Index
	    int8_t curProfileIndex	= -1;		// 현재 활성 UserProfile Index
    
	    ST_CT10_SegmentRuntime_t scheduleSegRt;	// Schedule 세그먼트 런타임
	    ST_CT10_SegmentRuntime_t profileSegRt;	// UserProfile 세그먼트 런타임
    
	    ST_CT10_Override_t	   overrideState;	// Override 상태
	    ST_CT10_AutoOffRuntime_t autoOffRt;		// AutoOff 상태
    
	    CL_P10_PWM*			pwm	   = nullptr;	// PWM 제어기 포인터
	    CL_M10_MotionLogic* motion    = nullptr;	// 모션 로직 포인터
    
	    CL_S10_Simulation sim;					// 시뮬레이션 엔진 인스턴스
    
	    unsigned long lastTickMs		= 0;	// 마지막 틱 실행 시간
	    unsigned long lastMetricsPushMs = 0;	// 메트릭 Dirty 플래그 설정 시간
    
   public:
	    // --------------------------------------------------
	    // 초기화 (PWM 인스턴스 주입)
	    // --------------------------------------------------
	    void begin(CL_P10_PWM& p_pwm) {
		    pwm = &p_pwm;
    
		    // 런타임 상태 구조체 초기화 (0 클리어)
		    memset(&overrideState, 0, sizeof(overrideState));
		    memset(&scheduleSegRt, 0, sizeof(scheduleSegRt));
		    memset(&profileSegRt, 0, sizeof(profileSegRt));
		    memset(&autoOffRt, 0, sizeof(autoOffRt));
    
		    // 특별 초기화 (index는 -1이 시작값)
		    scheduleSegRt.index = -1;
		    profileSegRt.index	= -1;
    
		    useProfileMode	  = false;
		    runSource		  = EN_CT10_RUN_NONE;
		    lastTickMs		  = 0;
		    lastMetricsPushMs = 0;
    
		    sim.begin(p_pwm); // 시뮬레이션 엔진 초기화
		    active = true;
		    
		    // 초기화 후 상태 변경 플래그 설정 (SC10이 브로드캐스트 처리)
		    markDirty("state");
		    markDirty("metrics");
		    CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] begin()");
	    }
    
        // M10 MotionLogic 포인터 설정 (선택적)
	    void setMotion(CL_M10_MotionLogic* p_motion) {
		    motion = p_motion;
	    }
    
	    // 모드 설정
	    void _setMode(bool p_profileMode) {
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
		    // 상태 변경 후 플래그 설정
		    markDirty("state");
		    markDirty("metrics");
		    CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] setMode(profileMode=%d)", p_profileMode ? 1 : 0);
	    }
    
	    // --------------------------------------------------
	    // UserProfile 작동 제어
	    // --------------------------------------------------
	    bool startUserProfileByNo(uint8_t p_profileNo) {
		    if (!g_A10_config_root.userProfiles)
			    return false;
    
		    ST_A10_UserProfilesRoot_t& v_cfg = *g_A10_config_root.userProfiles;
		    for (uint8_t v_i = 0; v_i < v_cfg.count; v_i++) {
			    const ST_A10_UserProfileItem_t& v_p = v_cfg.items[v_i];
			    if (!v_p.enabled)
				    continue;
			    if (v_p.profileNo == p_profileNo) {
				    runSource				  = EN_CT10_RUN_USER_PROFILE;
				    curProfileIndex			  = v_i;
				    profileSegRt.index		  = -1; // 세그먼트 시퀀스 초기화
				    profileSegRt.onPhase	  = true;
				    profileSegRt.phaseStartMs = millis();
    
				    _initAutoOffFromUserProfile(v_p); // 프로파일에 설정된 AutoOff 조건 로드
    
				    CL_D10_Logger::log(EN_L10_LOG_INFO,
								       "[CT10] Start UserProfile #%u (%s)",
								       (unsigned)p_profileNo, v_p.name);
				    // 상태 변경 후 플래그 설정
				    markDirty("state");
				    markDirty("metrics");
				    return true;
			    }
		    }
		    return false;
	    }
    
	    void stopUserProfile() {
		    if (runSource == EN_CT10_RUN_USER_PROFILE) {
			    runSource		   = EN_CT10_RUN_NONE;
			    curProfileIndex	   = -1;
			    profileSegRt.index = -1;
			    sim.stop(); // 시뮬레이션 정지
			    // 상태 변경 후 플래그 설정
			    markDirty("state");
			    markDirty("metrics");
			    CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] UserProfile stopped");
		    }
	    }
    
	    // --------------------------------------------------
	    // Manual Override (수동 제어)
	    // --------------------------------------------------
	    // 1) 고정 PWM 비율로 일정 시간 또는 무한 Override
	    void startOverrideFixed(float p_percent, uint32_t p_seconds) {
		    memset(&overrideState, 0, sizeof(overrideState));
		    overrideState.active	   = true;
		    overrideState.useFixed	   = true;
		    overrideState.fixedPercent = constrain(p_percent, 0.0f, 100.0f);
    
		    if (p_seconds > 0)
			    overrideState.endMs = millis() + (p_seconds * 1000UL);
		    else
			    overrideState.endMs = 0; // 타임아웃 없음
    
		    // 상태 변경 후 플래그 설정
		    markDirty("state");
		    markDirty("metrics");
    
		    CL_D10_Logger::log(EN_L10_LOG_INFO,
						       "[CT10] Override FIXED %.1f%% (sec=%lu)",
						       p_percent, (unsigned long)p_seconds);
	    }
    
	    // 2) preset+style+adjust → ResolvedWind 해석 후 Override 시작
	    void startOverridePreset(const char*				 p_presetCode,
							     const char*				 p_styleCode,
							     const ST_A10_AdjustDelta_t* p_adj,
							     uint32_t					 p_seconds) {
		    if (!g_A10_config_root.windDict)
			    return;
    
		    ST_A10_ResolvedWind_t v_resolved;
		    memset(&v_resolved, 0, sizeof(v_resolved));
		    bool v_ok = S20_resolveWindParams( // WindSolver 모듈을 통해 파라미터 해석
			    *g_A10_config_root.windDict,
			    p_presetCode,
			    p_styleCode,
			    p_adj,
			    v_resolved);
    
		    if (!v_ok || !v_resolved.valid) {
			    CL_D10_Logger::log(EN_L10_LOG_WARN,
							       "[CT10] startOverridePreset resolve failed (%s,%s)",
							       p_presetCode ? p_presetCode : "",
							       p_styleCode ? p_styleCode : "");
			    return;
		    }
		    applyManual(v_resolved, p_seconds); // 해석된 ResolvedWind 적용
		    // applyManual 내부에서 이미 markDirty 수행됨
	    }
    
	    // 3) ResolvedWind 직접 수동 적용 (W10 API와 연동)
	    void applyManual(const ST_A10_ResolvedWind_t& p_wind, uint32_t p_seconds) {
		    if (!p_wind.valid) {
			    CL_D10_Logger::log(EN_L10_LOG_WARN, "[CT10] applyManual: invalid ResolvedWind");
			    return;
		    }
    
		    // Fixed 모드라면 PWM Override로 처리 (재귀 호출)
		    if (p_wind.fixedMode) {
			    startOverrideFixed(p_wind.fixedSpeed, p_seconds);
			    return;
		    }
    
		    // ResolvedWind 기반 Override 설정
		    memset(&overrideState, 0, sizeof(overrideState));
		    overrideState.active		  = true;
		    overrideState.useFixed		  = false;
		    overrideState.resolvedApplied = false; // ResolvedWind 적용 전 플래그
		    overrideState.fixedPercent	  = 0.0f;
		    overrideState.resolved		  = p_wind;
    
		    if (p_seconds > 0) {
			    overrideState.endMs = millis() + (p_seconds * 1000UL);
		    } else {
			    overrideState.endMs = 0;  // 타임아웃 없음
		    }
    
		    // 상태 변경 후 플래그 설정 (SC10이 브로드캐스트 처리)
		    markDirty("state");
		    markDirty("metrics");
		    markDirty("chart"); // ResolvedWind 적용으로 시뮬레이터 차트 변경 예상
    
		    CL_D10_Logger::log(EN_L10_LOG_INFO,
						       "[CT10] applyManual: preset=%s style=%s (sec=%lu)",
						       p_wind.presetCode,
						       p_wind.styleCode,
						       (unsigned long)p_seconds);
	    }
    
	    void stopOverride() {
		    if (!overrideState.active)
			    return;
		    memset(&overrideState, 0, sizeof(overrideState));
		    CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] Override cleared");
		    // 상태 변경 후 플래그 설정
		    markDirty("state");
		    markDirty("metrics");
	    }
    
	    // --------------------------------------------------
	    // Tick 루프 (핵심 제어 로직)
	    // --------------------------------------------------
	    void _tick() {
		    if (!active || !pwm)
			    return;
    
		    unsigned long v_now = millis();
		    // 최소 틱 주기 검사
		    if (v_now - lastTickMs < S_TICK_MIN_INTERVAL_MS)
			    return;
		    lastTickMs = v_now;
    
		    // 1) Override 우선 처리 (가장 높은 우선순위)
		    if (_tickOverride()) {
			    sim.tick(); // Override 시뮬레이터 작동
			    _maybeBroadcastMetrics(); // 주기적 메트릭 체크
			    return;
		    }
    
		    // 2) Profile 전용 모드
		    if (useProfileMode) {
			    if (runSource == EN_CT10_RUN_USER_PROFILE && _tickUserProfile()) {
				    sim.tick();
				    _maybeBroadcastMetrics();
			    } else if (sim.active) {
				    // Profile이 정지 상태인데 시뮬레이션이 켜져있다면 정지
				    sim.stop();
				    _maybeBroadcastMetrics();
			    }
			    return;
		    }
    
		    // 3) UserProfile 수동 실행 모드 (스케줄과 별개)
		    if (runSource == EN_CT10_RUN_USER_PROFILE && _tickUserProfile()) {
			    sim.tick();
			    _maybeBroadcastMetrics();
			    return;
		    }
    
		    // 4) Schedule 기반 운전
		    if (_tickSchedule()) {
			    sim.tick(); // Schedule 기반 시뮬레이터 작동
    
			    // Schedule 변경 시 차트 갱신 플래그 설정
			    markDirty("chart");
    
			    _maybeBroadcastMetrics();
			    return;
		    }
    
		    // 5) 그 외: 모든 제어 소스가 꺼졌는데 동작 중이면 정지
		    if (sim.active) {
			    sim.stop();
			    _maybeBroadcastMetrics();
			    // 정지 시 상태 변경 플래그 설정
			    markDirty("state");
		    }
	    }
    
	    // --------------------------------------------------
	    // JSON Export 함수들
	    // --------------------------------------------------
	    // 전체 제어 상태 JSON 생성 (control + sim)
	    void _toJson(JsonDocument& p_doc) {
		    JsonObject v_o		  = p_doc["control"].to<JsonObject>();
		    v_o["active"]		  = active;
		    v_o["useProfileMode"] = useProfileMode;
		    v_o["runSource"]	  = (int)runSource;
		    v_o["scheduleIdx"]	  = curScheduleIndex;
		    v_o["profileIdx"]	  = curProfileIndex;
    
		    // override 상태 상세
		    JsonObject v_ov	  = v_o["override"].to<JsonObject>();
		    v_ov["active"]	  = overrideState.active;
		    v_ov["useFixed"]  = overrideState.useFixed;
		    v_ov["resolved"]  = (!overrideState.useFixed && overrideState.active);
		    v_ov["remainSec"] = _calcOverrideRemainSec();
		    if (overrideState.useFixed && overrideState.active) {
			    v_ov["fixedPercent"] = overrideState.fixedPercent;
		    } else if (overrideState.active && overrideState.resolved.valid) {
			    v_ov["presetCode"] = overrideState.resolved.presetCode;
			    v_ov["styleCode"]  = overrideState.resolved.styleCode;
		    }
    
		    // autoOff 상태 상세
		    JsonObject v_ao		   = v_o["autoOff"].to<JsonObject>();
		    v_ao["timerArmed"]	   = autoOffRt.timerArmed;
		    v_ao["timerMinutes"]   = autoOffRt.timerMinutes;
		    v_ao["offTimeEnabled"] = autoOffRt.offTimeEnabled;
		    v_ao["offTimeMinutes"] = autoOffRt.offTimeMinutes;
		    v_ao["offTempEnabled"] = autoOffRt.offTempEnabled;
		    v_ao["offTemp"]		   = autoOffRt.offTemp;
    
		    // pwm 및 sim 상태 포함
		    v_o["pwmDuty"] = pwm ? pwm->P10_getDutyPercent() : 0.0f;
    
		    // sim 상태 포함
		    JsonObject v_sim = p_doc["sim"].to<JsonObject>();
		    sim.toJson(v_sim);
	    }
    
	    // 외부용: 시뮬레이션 차트 Export (S10에 위임 + 메타만 추가)
	    void _toChartJson(JsonDocument& p_doc, bool p_diffOnly = false) {
		    // 1) S10 차트 데이터 생성 ("sim.chart")
		    sim.toChartJson(p_doc, p_diffOnly);
    
		    // 2) CT10 메타 정보는 "sim.meta"에 병합
		    JsonObject v_meta	= p_doc["sim"]["meta"].to<JsonObject>();
		    v_meta["pwmDuty"]	= pwm ? pwm->P10_getDutyPercent() : 0.0f;
		    v_meta["active"]	= active;
		    v_meta["runSource"] = (int)runSource;
		    v_meta["phase"]		= g_A10_WEATHER_PHASE_NAMES_Arr[(uint8_t)sim.phase];
		    v_meta["override"]	= overrideState.active
								      ? (overrideState.useFixed ? "fixed" : "resolved")
								      : "none";
	    }
    
	    // 요약 상태: 가벼운 폴링/심플 UI용
	    void toSummaryJson(JsonDocument& p_doc) {
		    JsonObject v_sum	 = p_doc["summary"].to<JsonObject>();
		    v_sum["phase"]		 = g_A10_WEATHER_PHASE_NAMES_Arr[(uint8_t)sim.phase];
		    v_sum["wind"]		 = sim.currentWindSpeed;
		    v_sum["target"]		 = sim.targetWindSpeed;
		    v_sum["pwmDuty"]	 = pwm ? pwm->P10_getDutyPercent() : 0.0f;
		    v_sum["override"]	 = overrideState.active;
		    v_sum["useProfile"]	 = useProfileMode;
		    v_sum["scheduleIdx"] = curScheduleIndex;
		    v_sum["profileIdx"]	 = curProfileIndex;
	    }
    
	    // 메트릭 전용: /api/metrics, /ws/metrics 용 (시스템 진단/모니터링 데이터)
	    void toMetricsJson(JsonDocument& p_doc) {
		    JsonObject v_m = p_doc["metrics"].to<JsonObject>();
    
		    v_m["active"]		  = active;
		    v_m["runSource"]	  = (int)runSource;
		    v_m["useProfileMode"] = useProfileMode;
		    v_m["scheduleIdx"]	  = curScheduleIndex;
		    v_m["profileIdx"]	  = curProfileIndex;
    
		    v_m["overrideActive"] = overrideState.active;
		    v_m["overrideFixed"]  = overrideState.useFixed;
		    v_m["overrideRemain"] = _calcOverrideRemainSec();
    
		    v_m["pwmDuty"] = pwm ? pwm->P10_getDutyPercent() : 0.0f;
    
		    // sim 메트릭
		    v_m["simActive"]  = sim.active;
		    v_m["simPhase"]	  = g_A10_WEATHER_PHASE_NAMES_Arr[(uint8_t)sim.phase];
		    v_m["simWind"]	  = sim.currentWindSpeed;
		    v_m["simTarget"]  = sim.targetWindSpeed;
		    v_m["simGust"]	  = sim.gustActive;
		    v_m["simThermal"] = sim.thermalActive;
    
		    // AutoOff 메트릭
		    v_m["autoOffTimerArmed"]   = autoOffRt.timerArmed;
		    v_m["autoOffTimerMinutes"] = autoOffRt.timerMinutes;
		    v_m["autoOffOffTime"]	   = autoOffRt.offTimeEnabled ? autoOffRt.offTimeMinutes : 0;
		    v_m["autoOffOffTemp"]	   = autoOffRt.offTempEnabled ? autoOffRt.offTemp : 0.0f;
	    }
    
	    // --------------------------------------------------
	    // Dirty 플래그 관리 (SC10 diffOnly 연동용)
	    // --------------------------------------------------
        // 지정된 키에 대해 브로드캐스트 필요 플래그 설정
	    void markDirty(const char* p_key) {
		    if (strcmp(p_key, "state") == 0)
			    _dirtyState = true;
		    else if (strcmp(p_key, "chart") == 0)
			    _dirtyChart = true;
		    else if (strcmp(p_key, "metrics") == 0)
			    _dirtyMetrics = true;
		    else if (strcmp(p_key, "summary") == 0)
			    _dirtySummary = true;
	    }
    
        // Dirty 플래그를 소비하고 값 반환 (SC10이 호출)
	    bool consumeDirtyState() {
		    bool v		= _dirtyState;
		    _dirtyState = false;
		    return v;
	    }
    
	    bool consumeDirtyMetrics() {
		    bool v		  = _dirtyMetrics;
		    _dirtyMetrics = false;
		    return v;
	    }
    
	    bool consumeDirtyChart() {
		    bool v		= _dirtyChart;
		    _dirtyChart = false;
		    return v;
	    }
	    
	    bool consumeDirtySummary() {
		    bool v		  = _dirtySummary;
		    _dirtySummary = false;
		    return v;
	    }

   private:
        // Dirty 플래그 (private 멤버)
	    bool _dirtyState   = false;   // 주요 제어 상태 변경
	    bool _dirtyMetrics = false;   // 시스템 메트릭 변경 (주기적)
	    bool _dirtyChart   = false;   // 시뮬레이션 차트 데이터 변경
	    bool _dirtySummary = false;
    
        // Override 남은 시간 계산 (초 단위)
	    uint32_t _calcOverrideRemainSec() const {
		    if (!overrideState.active || overrideState.endMs == 0)
			    return 0;
		    unsigned long v_now = millis();
		    if (overrideState.endMs <= v_now)
			    return 0;
		    return (uint32_t)((overrideState.endMs - v_now) / 1000UL);
	    }
    
	    // ==================================================
	    // Override 처리
	    // ==================================================
	    bool _tickOverride() {
		    if (!overrideState.active)
			    return false;
    
		    unsigned long v_now = millis();
    
		    // 타임아웃 처리
		    if (overrideState.endMs != 0 && v_now >= overrideState.endMs) {
			    CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] Override timeout");
			    memset(&overrideState, 0, sizeof(overrideState));
    
			    // 타임아웃 발생 시 상태 변경 플래그 설정
			    markDirty("state");
			    markDirty("metrics");
			    return false;
		    }
    
		    // 1) 고정 PWM Override
		    if (overrideState.useFixed) {
			    sim.stop(); // 시뮬레이션 정지
			    if (pwm) {
				    pwm->P10_setDutyPercent(overrideState.fixedPercent); // 고정 PWM 적용
			    }
			    return true;
		    }
    
		    // 2) ResolvedWind 기반 Override
		    if (!overrideState.resolved.valid) {
			    CL_D10_Logger::log(EN_L10_LOG_WARN, "[CT10] Override resolved invalid, clear");
			    memset(&overrideState, 0, sizeof(overrideState));
			    markDirty("state");
			    markDirty("metrics");
			    return false;
		    }
    
		    // 첫 적용 시에만 S10에 파라미터 전달
		    if (!overrideState.resolvedApplied) {
			    sim.applyResolvedWind(overrideState.resolved);
			    overrideState.resolvedApplied = true;
    
			    // override 적용 직후 차트 갱신 플래그 설정
			    markDirty("chart");
		    }
    
		    return true;
	    }
    
	    // ==================================================
	    // UserProfile Tick
	    // ==================================================
	    bool _tickUserProfile() {
		    if (!g_A10_config_root.userProfiles)
			    return false;
		    if (curProfileIndex < 0)
			    return false;
    
		    ST_A10_UserProfilesRoot_t& v_cfg = *g_A10_config_root.userProfiles;
		    if ((uint8_t)curProfileIndex >= v_cfg.count)
			    return false;
    
		    ST_A10_UserProfileItem_t& v_p = v_cfg.items[curProfileIndex];
		    if (!v_p.enabled || v_p.seg_count == 0)
			    return false;
    
		    // 1. AutoOff 조건 체크
		    if (_checkAutoOff()) {
			    sim.stop();
			    runSource		= EN_CT10_RUN_NONE;
			    curProfileIndex = -1;
			    CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] UserProfile AutoOff stop");
    
			    // 상태 변경 후 플래그 설정
			    markDirty("state");
			    markDirty("metrics");
			    return true;
		    }
    
		    // 2. Motion 조건 체크
		    if (_isMotionBlocked(v_p.motion)) {
			    sim.stop(); // 모션이 감지되지 않으면 시뮬레이션 정지
			    return true;
		    }
    
		    // 3. Segment 시퀀스 실행
		    return _tickSegmentSequence(
			    v_p.repeatSegments, // 반복 설정
			    v_p.segments,
			    v_p.seg_count,
			    profileSegRt);
	    }
    
	    // ==================================================
	    // Schedule Tick
	    // ==================================================
	    bool _tickSchedule() {
		    if (!g_A10_config_root.schedules)
			    return false;
    
		    ST_A10_SchedulesRoot_t& v_cfg = *g_A10_config_root.schedules;
		    int						v_idx = _findActiveScheduleIndex(v_cfg); // 현재 시간대 활성 Schedule 찾기
		    if (v_idx < 0) {
			    curScheduleIndex = -1;
			    return false;
		    }
    
		    // 활성 Schedule이 변경된 경우 (초기화)
		    if (curScheduleIndex != v_idx) {
			    curScheduleIndex = v_idx;
			    _initAutoOffFromSchedule(v_cfg.items[v_idx]);
			    runSource				   = EN_CT10_RUN_SCHEDULE;
			    scheduleSegRt.index		   = -1; // Segment 시퀀스 초기화
			    scheduleSegRt.onPhase	   = true;
			    scheduleSegRt.phaseStartMs = millis();
			    CL_D10_Logger::log(EN_L10_LOG_INFO,
							       "[CT10] Active Schedule idx=%d",
							       v_idx);
			    // 상태 변경 후 플래그 설정
			    markDirty("state");
			    markDirty("metrics");
		    }
    
		    ST_A10_ScheduleItem_t& v_s = v_cfg.items[curScheduleIndex];
		    if (!v_s.enabled || v_s.seg_count == 0)
			    return false;
    
		    // AutoOff 조건 체크
		    if (_checkAutoOff()) {
			    sim.stop();
			    runSource		 = EN_CT10_RUN_NONE;
			    curScheduleIndex = -1;
			    CL_D10_Logger::log(EN_L10_LOG_INFO, "[CT10] Schedule AutoOff stop");
			    
			    // 상태 변경 후 플래그 설정
			    markDirty("state");
			    markDirty("metrics");
			    return true;
		    }
    
		    // Motion 조건 체크
		    if (_isMotionBlocked(v_s.motion)) {
			    sim.stop();
			    return true;
		    }
    
		    // Segment 시퀀스 실행 (Schedule은 항상 반복)
		    return _tickSegmentSequence(
			    true,
			    v_s.segments,
			    v_s.seg_count,
			    scheduleSegRt);
	    }
    
	    // ==================================================
	    // Segment 시퀀스 처리 (Template 함수)
	    // ==================================================
	    template <typename T_segment>
	    bool _tickSegmentSequence(bool						p_repeat,
							      T_segment*				p_segs,
							      uint8_t					p_count,
							      ST_CT10_SegmentRuntime_t& p_rt) {
		    unsigned long v_now = millis();
    
		    // 1. 초기 시작 (-1 상태)
		    if (p_rt.index < 0) {
			    p_rt.index		  = 0;
			    p_rt.onPhase	  = true;
			    p_rt.phaseStartMs = v_now;
			    _applySegmentOn(p_segs[0]);
			    return true;
		    }
    
		    // 2. 시퀀스 끝 도달 시 처리
		    if ((uint8_t)p_rt.index >= p_count) {
			    if (!p_repeat) { // 반복이 아니면 정지 후 종료
				    sim.stop();
				    return true;
			    }
			    // 반복이면 첫 세그먼트로 복귀
			    p_rt.index		  = 0;
			    p_rt.onPhase	  = true;
			    p_rt.phaseStartMs = v_now;
			    _applySegmentOn(p_segs[0]);
			    return true;
		    }
    
	    
		    T_segment& v_seg	= p_segs[p_rt.index];
            // 분 단위를 밀리초로 변환
		    uint32_t			v_onMs	= v_seg.on_minutes * 60000UL;
		    uint32_t			v_offMs = v_seg.off_minutes * 60000UL;
    
		    if (p_rt.onPhase && v_onMs > 0 && v_now - p_rt.phaseStartMs >= v_onMs) {
			    // On 구간 종료: On → Off 전환
			    p_rt.onPhase	  = false;
			    p_rt.phaseStartMs = v_now;
			    _applySegmentOff();
		    } else if (!p_rt.onPhase && v_offMs > 0 && v_now - p_rt.phaseStartMs >= v_offMs) {
			    // Off 구간 종료: Off → 다음 Segment On
			    p_rt.index++;
			    if ((uint8_t)p_rt.index >= p_count && p_repeat) {
				    p_rt.index = 0; // 반복 시 인덱스 리셋
			    }
			    p_rt.onPhase	  = true;
			    p_rt.phaseStartMs = v_now;
    
			    if ((uint8_t)p_rt.index < p_count) {
				    _applySegmentOn(p_segs[p_rt.index]); // 다음 Segment On
			    } else {
				    sim.stop(); // 마지막 세그먼트 종료 (반복 아닐 때)
			    }
		    }
		    return true;
	    }
    
    
	    // Segment On (바람 파라미터 적용)
        template <typename T_segment>
        void _applySegmentOn(const T_segment& p_seg) {
	        if (!g_A10_config_root.windDict)
		        return;
        
	        // FIXED 모드 (고정 PWM)
	        // NOTE: p_seg.mode는 EN_A10_SEG_MODE_PRESET 타입과 값이 동일하므로 문자열 비교 대신 값 비교 필요
	        if (p_seg.mode == EN_A10_SEG_MODE_FIXED) { 
		        sim.stop();
		        if (pwm) {
			        pwm->P10_setDutyPercent(p_seg.fixed_speed);
		        }
		        return;
	        }
        
	        // PRESET 모드 (ResolvedWind 기반)
	        ST_A10_ResolvedWind_t v_res;
	        memset(&v_res, 0, sizeof(v_res));
        
	        bool v_ok = S20_resolveWindParams( // S20_WindSolver 호출
		        *g_A10_config_root.windDict,
		        p_seg.presetCode,
		        p_seg.styleCode,
		        &p_seg.adjust,
		        v_res);
	        if (v_ok && v_res.valid) {
		        sim.applyResolvedWind(v_res); // S10 시뮬레이터에 적용
        
		        // Segment 변경 시 상태 브로드캐스트 플래그 설정
		        markDirty("state");
		        markDirty("chart");
	        } else {
		        CL_D10_Logger::log(EN_L10_LOG_WARN,
						           "[CT10] Segment resolve failed (mode=%s,preset=%s,style=%s)",
						           p_seg.mode, // enum 값 출력은 주의 필요
						           p_seg.presetCode,
						           p_seg.styleCode);
	        }
        }
    
	    // Segment Off (팬 정지)
	    void _applySegmentOff() {
		    sim.stop();
		    // Off 시 상태 변경 플래그 설정
		    markDirty("state");
		    markDirty("chart");
	    }
    
	    // ==================================================
	    // AutoOff 초기화
	    // ==================================================
        // UserProfile 설정에서 AutoOff 조건 로드
	    void _initAutoOffFromUserProfile(const ST_A10_UserProfileItem_t& p_up) {
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
			    autoOffRt.offTemp		 = p_up.autoOff.offTemp.temp;
		    }
	    }
    
        // Schedule 설정에서 AutoOff 조건 로드
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
			    autoOffRt.offTemp		 = p_s.autoOff.offTemp.temp;
		    }
	    }
    
	    // --------------------------------------------------
	    // AutoOff 조건 점검
	    // --------------------------------------------------
	    bool _checkAutoOff() {
		    if (!autoOffRt.timerArmed &&
			    !autoOffRt.offTimeEnabled &&
			    !autoOffRt.offTempEnabled) {
			    return false; // AutoOff 조건이 설정되지 않음
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
    
		    // 2) 지정시간 오프 (RTC 필요)
		    if (autoOffRt.offTimeEnabled) {
			    time_t	   v_t		= time(nullptr);
			    struct tm* v_lt		= localtime(&v_t); // 로컬 시간 얻기
			    uint16_t   v_curMin = (uint16_t)v_lt->tm_hour * 60 + (uint16_t)v_lt->tm_min; // 현재 분
    
			    if (v_curMin >= autoOffRt.offTimeMinutes) {
				    CL_D10_Logger::log(EN_L10_LOG_INFO,
								       "[CT10] AutoOff(time %u) triggered",
								       (unsigned)autoOffRt.offTimeMinutes);
				    return true;
			    }
		    }
    
		    // 3) 온도 기반 (센서 데이터 필요)
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
	    // 유틸리티 함수 (Static으로 독립성 확보)
	    // --------------------------------------------------
	    // "HH:MM" 문자열을 하루 중 총 분 (0~1439)으로 변환
	    static uint16_t _parseHHMMtoMin(const char* p_time) {
		    if (!p_time || strlen(p_time) < 4)
			    return 0;
		    int			v_hh	= atoi(p_time);
		    const char* v_colon = strchr(p_time, ':');
		    int			v_mm	= (v_colon) ? atoi(v_colon + 1) : 0;
		    return (uint16_t)(v_hh * 60 + v_mm);
	    }
    
	    // 현재 온도 센서 값 반환 (Mockup)
	    static float _getCurrentTemperature() {
		    // 실제 온도 센서 연동 시 교체.
		    return 24.0f; // 기본값 반환 (테스트용)
	    }
    
        // 현재 활성화되어야 하는 Schedule Index 찾기
	    int _findActiveScheduleIndex(const ST_A10_SchedulesRoot_t& p_cfg) {
		    if (p_cfg.count == 0)
			    return -1;
    
		    time_t	   v_now = time(nullptr);
		    struct tm* v_lt	 = localtime(&v_now);
    
		    uint8_t	 v_wday	  = (v_lt->tm_wday == 0) ? 6 : (uint8_t)(v_lt->tm_wday - 1);  // 일요일 보정 (Arduino: 0=일요일, Config: 0=월요일)
		    uint16_t v_curMin = (uint16_t)v_lt->tm_hour * 60 + (uint16_t)v_lt->tm_min;
    
		    for (int v_i = 0; v_i < p_cfg.count; v_i++) {
			    const ST_A10_ScheduleItem_t& v_s = p_cfg.items[v_i];
			    if (!v_s.enabled || !v_s.period.enabled)
				    continue;
			    if (!v_s.period.days[v_wday]) // 요일 체크
				    continue;
    
			    uint16_t v_st = _parseHHMMtoMin(v_s.period.start_time);
			    uint16_t v_en = _parseHHMMtoMin(v_s.period.end_time);
    
			    if (v_st <= v_en) {
				    // 일반 구간 (예: 10:00 ~ 18:00)
				    if (v_curMin >= v_st && v_curMin < v_en)
					    return v_i;
			    } else {
				    // 자정을 넘기는 구간 (예: 22:00 ~ 06:00)
				    if (v_curMin >= v_st || v_curMin < v_en)
					    return v_i;
			    }
		    }
		    return -1;
	    }
    
        // 모션 감지 기반 작동 차단 여부 체크
	    bool _isMotionBlocked(const ST_A10_Motion_t& p_motionCfg) {
		    if (!motion)
			    return false; // Motion Logic 모듈이 설정되지 않음
		    if (!p_motionCfg.pir.enabled && !p_motionCfg.ble.enabled)
			    return false; // Motion 감지 조건이 꺼짐
    
		    // M10::isActive()는 PIR 또는 BLE 감지 상태를 종합적으로 반환
		    bool v_active = motion->isActive();
		    if (!v_active) {
			    CL_D10_Logger::log(EN_L10_LOG_DEBUG, "[CT10] Motion blocked (no presence)");
			    return true; // 감지된 움직임이나 BLE 신호 없음 -> 작동 차단
		    }
		    return false; // 감지됨 -> 차단 안함
	    }
    
	    // --------------------------------------------------
	    // 상태 변경 시 WebSocket 브로드캐스트 헬퍼 (Dirty 플래그로 대체됨)
	    // --------------------------------------------------
	    void _broadcastState(bool p_diffOnly = true) {
		    // 기존 브로드캐스트 로직은 제거됨.
		    // markDirty("state")로 대체됨
	    }
    
	    // --------------------------------------------------
	    // Metrics WebSocket 브로드캐스트 (주기 + diffOnly) (Dirty 플래그로 대체됨)
	    // --------------------------------------------------
	    void _maybeBroadcastMetrics() {
		    unsigned long v_now = millis();
		    // 설정된 주기마다 Dirty 플래그 설정
		    if (v_now - lastMetricsPushMs < S_METRICS_PUSH_INTERVAL_MS)
			    return;
		    lastMetricsPushMs = v_now;
    
		    // SC10_run()에서 최종 브로드캐스트 처리
		    markDirty("metrics");
	    }
};


