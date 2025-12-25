#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : CT10_Control_040.h
 * 모듈약어 : CT10
 * 모듈명 : Smart Nature Wind 제어 통합 Manager (v026)
 * ------------------------------------------------------
 * 기능 요약:
 * - Schedule / UserProfile / Manual Override 기반 풍속 제어
 * - WindDict 기반 해석 (resolveWindParams) 후 S10.applyResolvedWind 연동
 * - PWM(P10) / Simulation(S10) 통합 제어
 * - Motion (PIR / BLE) 및 AutoOff 조건 훅 제공
 * - Web UI / 버튼에서 Profile 선택, Override 즉시 반영
 * - JSON 상태 Export (control / override / autoOff / sim / metrics)
 * - Dirty 플래그 기반 A00 diffOnly API 연동 지원 (A00이 브로드캐스트 처리)
 * - Override 타임아웃 및 AutoOff 발생 시 Dirty 플래그 자동 설정
 * - 정적 싱글톤 인터페이스 제공 (W10_WebAPI에서 직접 사용)
 * - 구현은 cpp 3개로 분리:
 *    1) json 처리, 2) control 처리, 3) misc/유틸/보조
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
#include <time.h>

// 종속성 모듈 헤더
#include "A20_Const_040.h"
#include "C10_Config_041.h"
#include "D10_Logger_040.h"
#include "M10_MotionLogic_040.h"
#include "P10_PWM_ctrl_040.h"
#include "S10_Simul_040.h"
#include "S20_WindSolver_040.h"

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
	ST_A20_ResolvedWind_t resolved;			// 수동 바람 설정 (useFixed==false 일 때의 바람 파라미터)
} ST_CT10_Override_t;

// Segment 실행 상태 (Schedule 또는 Profile)
typedef struct {
	int8_t		  index;		 // 현재 seg index (-1이면 아직 시작 전)
	bool		  onPhase;		 // true: On 구간(팬 작동), false: Off 구간(팬 정지)
	unsigned long phaseStartMs;	 // 현재 phase 시작 시간 (Millis 기준)
	uint8_t		  loopCount;	 // 반복 횟수 카운트 (0부터 시작)
} ST_CT10_SegmentRuntime_t;

// AutoOff 런타임 상태 (현재 적용된 AutoOff 조건)
typedef struct {
	bool		  timerArmed;	   // 타이머 AutoOff 활성 여부
	unsigned long timerStartMs;	   // 타이머 시작 시간
	uint32_t	  timerMinutes;	   // 설정된 타이머 시간 (분)

	bool		  offTimeEnabled;  // 지정 시간 AutoOff 활성 여부
	uint16_t	  offTimeMinutes;  // AutoOff가 발동되는 하루 중 시간 (분 단위, 0~1439)

	bool		  offTempEnabled;  // 온도 기반 AutoOff 활성 여부
	float		  offTemp;		   // AutoOff가 발동되는 온도 (섭씨)
} ST_CT10_AutoOffRuntime_t;

// ======================================================
// CL_CT10_ControlManager 클래스
// ======================================================
class CL_CT10_ControlManager {
  public:
	// --------------------------------------------------
	// 싱글톤 정적 인터페이스 (W10 등 외부 모듈용)
	// --------------------------------------------------
	static CL_CT10_ControlManager& instance();

	static bool begin();  // 전역 PWM 인스턴스(g_P10_pwm) 기반 begin
	static void tick();	  // 주기 호출
	static void toJson(JsonDocument& p_doc);
	static void toChartJson(JsonDocument& p_doc, bool p_diffOnly = false);

	static void setMode(bool p_profileMode);
	static bool setActiveUserProfile(uint8_t p_profileNo);

	static void applyManual(const ST_A20_ResolvedWind_t& p_wind);
	static void clearManual();

	static bool reloadAll();

	static void toSummaryJson(JsonDocument& p_doc) {
		instance().exportSummaryJson(p_doc);
	}
	static void toMetricsJson(JsonDocument& p_doc) {
		instance().exportMetricsJson(p_doc);
	}

  public:
	// --------------------------------------------------
	// 외부에서 필요한 인스턴스 API
	// --------------------------------------------------
	void begin(CL_P10_PWM& p_pwm);
	void setMotion(CL_M10_MotionLogic* p_motion);

	// JSON 관련(구현은 json cpp)
	void exportStateJson(JsonDocument& p_doc);
	void exportChartJson(JsonDocument& p_doc, bool p_diffOnly);
	void exportSummaryJson(JsonDocument& p_doc);
	void exportMetricsJson(JsonDocument& p_doc);

	// 모드/프로파일/오버라이드
	void setProfileMode(bool p_profileMode);
	bool startUserProfileByNo(uint8_t p_profileNo);
	void stopUserProfile();

	void startOverrideFixed(float p_percent, uint32_t p_seconds);
	void startOverridePreset(const char* p_presetCode, const char* p_styleCode, const ST_A20_AdjustDelta_t* p_adj, uint32_t p_seconds);
	void applyManualResolved(const ST_A20_ResolvedWind_t& p_wind, uint32_t p_seconds);
	void stopOverride();

	// tick(구현은 control cpp)
	void tickLoop();

	// Dirty 플래그
	void markDirty(const char* p_key);
	bool consumeDirtyState();
	bool consumeDirtyMetrics();
	bool consumeDirtyChart();
	bool consumeDirtySummary();

  public:
	// --------------------------------------------------
	// 런타임 상태 (멤버)
	// --------------------------------------------------
	bool					 active			  = false;
	bool					 useProfileMode	  = false;

	EN_CT10_run_source_t	 runSource		  = EN_CT10_RUN_NONE;

	int8_t					 curScheduleIndex = -1;
	int8_t					 curProfileIndex  = -1;

	ST_CT10_SegmentRuntime_t scheduleSegRt;
	ST_CT10_SegmentRuntime_t profileSegRt;

	ST_CT10_Override_t		 overrideState;
	ST_CT10_AutoOffRuntime_t autoOffRt;

	CL_P10_PWM*				 pwm	= nullptr;
	CL_M10_MotionLogic*		 motion = nullptr;

	CL_S10_Simulation		 sim;

	unsigned long			 lastTickMs		   = 0;
	unsigned long			 lastMetricsPushMs = 0;

  private:
	// --------------------------------------------------
	// 내부 상수 (Magic number)
	// --------------------------------------------------
	static constexpr uint32_t S_TICK_MIN_INTERVAL_MS	 = 40UL;	// 25Hz
	static constexpr uint32_t S_METRICS_PUSH_INTERVAL_MS = 1500UL;	// metrics dirty 주기

  private:
	// Dirty 플래그 (private)
	bool _dirtyState   = false;
	bool _dirtyMetrics = false;
	bool _dirtyChart   = false;
	bool _dirtySummary = false;

  private:
	// --------------------------------------------------
	// 내부 유틸 / control / misc (구현은 cpp 분리)
	// --------------------------------------------------
	uint32_t calcOverrideRemainSec() const;

	bool tickOverride();
	bool tickUserProfile();
	bool tickSchedule();

	// segment 처리: schedule/profile 오버로드(템플릿 제거)
	bool tickSegmentSequence(bool p_repeat, uint8_t p_repeatCount, ST_A20_ScheduleSegment_t* p_segs, uint8_t p_count, ST_CT10_SegmentRuntime_t& p_rt);

	bool tickSegmentSequence(bool p_repeat, uint8_t p_repeatCount, ST_A20_UserProfileSegment_t* p_segs, uint8_t p_count, ST_CT10_SegmentRuntime_t& p_rt);

	void applySegmentOn(const ST_A20_ScheduleSegment_t& p_seg);
	void applySegmentOn(const ST_A20_UserProfileSegment_t& p_seg);
	void applySegmentOff();

	void initAutoOffFromUserProfile(const ST_A20_UserProfileItem_t& p_up);
	void initAutoOffFromSchedule(const ST_A20_ScheduleItem_t& p_s);
	bool checkAutoOff();

	static uint16_t parseHHMMtoMin(const char* p_time);
	static float getCurrentTemperatureMock();

	int findActiveScheduleIndex(const ST_A20_SchedulesRoot_t& p_cfg);
	bool isMotionBlocked(const ST_A20_Motion_t& p_motionCfg);

	void maybePushMetricsDirty();

	// 로그 개선용: preset/style 이름 조회
	const char* findPresetNameByCode(const char* p_code) const;
	const char* findStyleNameByCode(const char* p_code) const;

  private:
	// 생성자 숨김
	CL_CT10_ControlManager() = default;
};
