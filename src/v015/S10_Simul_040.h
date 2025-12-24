#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : S10_Simul_040.h
 * 모듈약어 : S10
 * 모듈명 : Smart Nature Wind 풍속 시뮬레이션 Manager
 * ------------------------------------------------------
 * 기능 요약:
 * - 자연풍 시뮬레이션 핵심 엔진 (Phase / 난류 / 돌풍 / 열기포 / 관성)
 * - PWM 제어기(CL_P10_PWM)와 연동하여 실시간 풍속을 PWM Duty로 변환
 * - C10 해석 결과(ST_A20_ResolvedWind_t) 기반 파라미터 적용
 * - PresetCode + StyleCode 기반 풍속 특성(범위·확률·스펙트럼) 자동 세팅
 * - Von Kármán 스펙트럼 난류 모델 + Phase별 풍속 재생성 로직
 * - 돌풍(Gust), 열기포(Thermal Bubble), 자연감 지터(Jitter) 확률적 발생
 * - 최근 60초 풍속 이력 순환 버퍼(history) 및 평균 캐시 관리
 * - 최근 120초 Chart 버퍼(1Hz 샘플링) 관리 및 JSON 직렬화 지원
 * - diffOnly 모드 지원 (WebSocket/REST 효율 전송)
 * - Phase 변화 또는 급격한 풍속 변화 시 실시간 WebSocket 브로드캐스트
 * - C10_ControlManager 및 W10_WebAPI와 완전 호환 구조
 * ------------------------------------------------------
 * [구현 규칙]
 * - 항상 소스 시작 주석 부분 체계 유지 및 내용 업데이트
 * - 소스 시작 주석 부분 구현규칙, 코드네이밍규칙 내용 그대로 유지, 수정금지
 * - ArduinoJson v7.x.x 사용 (v6 이하 사용 금지)
 * - JsonDocument 단일 타입만 사용
 * - createNestedArray/Object/containsKey 사용 금지
 * - memset + strlcpy 기반 안전 초기화
 * - 주석/필드명은 JSON 구조와 동일하게 유지
 * - 모듈별 단일 헤더(h)파일로만 구성 (cpp 없음) -> 구현부 분리 요청으로 인해 CPP 파일 생성됨.
 * ------------------------------------------------------
 * [코드 네이밍 규칙]
 * - 전역 상수,매크로      : G_모듈약어_ 접두사
 * - 전역 변수             : g_모듈약어_ 접두사
 * - 전역 함수             : 모듈약어_ 접두사
 * - type                  : T_모듈약어_ 접두사
 * - typedef               : _t  접미사
 * - enum 상수             : EN_모듈약어_ 접두사
 * - 구조체                : ST_모듈약어_ 접미사
 * - 클래스명              : CL_모듈약어_ 접미사
 * - 클래스 private 멤버   : _ 접두사
 * - 클래스 멤버(함수/변수) : 모듈약어 접두사 미사용
 * - 클래스 정적 멤버      : s_ 접두사
 * - 함수 로컬 변수        : v_ 접두사
 * - 함수 인자             : p_ 접두사
 * ------------------------------------------------------
 */

#include <Arduino.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

#include <cmath>
#include <deque>

#include "A20_Const_040.h"
#include "C10_Config_041.h"
#include "D10_Logger_040.h"
#include "P10_PWM_ctrl_040.h"

// A00(메인 루프)의 전역 함수 전방 선언: WebAPI로 상태를 브로드캐스팅
extern void A00_broadcastState(ArduinoJson::JsonDocument& doc, bool diffOnly);
extern void A00_broadcastChart(ArduinoJson::JsonDocument& doc, bool diffOnly);
extern void A00_broadcastMetrics(ArduinoJson::JsonDocument& doc, bool diffOnly);
extern void A00_markDirty(const char* key);
extern int strcasecmp(const char* s1, const char* s2);

// ==================================================
// [S10 주기 정책 상수] (시간 단위 주석 필수)
// - Core/IO/Physic CPP에서 동일 상수를 공유하기 위해 Header로 승격
// ==================================================

// tick() 업데이트 최소 간격 정책(ms)
// - BASE(40ms) + jitter(0~59ms) => 40~99ms 범위
// - 자연풍 “미세 불규칙성”을 만들되, 과도한 루프/부하를 제한
static const uint32_t G_S10_TICK_MIN_BASE_MS	   = 40u;  // [ms]
static const uint32_t G_S10_TICK_JITTER_RANGE_MS   = 60u;  // [ms] (0~59)

// 돌풍/열기포 “발생 여부 평가” 최소 간격(ms)
// - 확률은 평가 주기(dt)에 의해 자동 보정되어야 함(초당 rate 기반)
static const uint32_t G_S10_GUST_EVAL_MIN_MS	   = 500u;	// [ms]
static const uint32_t G_S10_THERM_EVAL_MIN_MS	   = 700u;	// [ms]

// 차트 샘플링 간격(ms)
static const uint32_t G_S10_CHART_HZ1_MS		   = 1000u;	 // [ms] 평상시 1Hz
static const uint32_t G_S10_CHART_HZ2_MS		   = 500u;	 // [ms] 이벤트 중 2Hz

// 차트 Full Dump 최소 전송 간격(ms) (Web 부하 제어)
static const uint32_t G_S10_CHART_FULL_MIN_MS	   = 10000u;  // [ms] 10s

// ==================================================
// [S10 초(Seconds) 정책 상수] (Phase/Thermal duration)
// - "초" 계열은 정책화하여 Physic CPP에서 사용
// ==================================================

// Phase 지속시간 범위(초)
static const float	  G_S10_PHASE_CALM_DUR_MIN_S   = 90.0f;	  // [s]
static const float	  G_S10_PHASE_CALM_DUR_MAX_S   = 210.0f;  // [s]
static const float	  G_S10_PHASE_NORM_DUR_MIN_S   = 120.0f;  // [s]
static const float	  G_S10_PHASE_NORM_DUR_MAX_S   = 300.0f;  // [s]
static const float	  G_S10_PHASE_STRONG_DUR_MIN_S = 60.0f;	  // [s]
static const float	  G_S10_PHASE_STRONG_DUR_MAX_S = 150.0f;  // [s]

// Thermal(열기포) 지속시간 범위(초)
static const float	  G_S10_THERM_DUR_MIN_S		   = 8.0f;	 // [s]
static const float	  G_S10_THERM_DUR_MAX_S		   = 14.0f;	 // [s]

// Thermal phase 보정 배율(지속시간에 적용)
static const float	  G_S10_THERM_DUR_MUL_CALM	   = 1.3f;	// [-]
static const float	  G_S10_THERM_DUR_MUL_STRONG   = 0.8f;	// [-]

// ======================================================
// CL_S10_Simulation
// ======================================================
class CL_S10_Simulation {
  public:
	// ------------------ 상태/파라미터 ------------------
	bool				 active				 = false;
	bool				 fanPowerEnabled	 = true;

	float				 currentWindSpeed	 = 3.6f;
	float				 targetWindSpeed	 = 3.6f;

	char				 presetCode[24]		 = { 0 };
	char				 styleCode[24]		 = { 0 };

	// C10 해석 결과 기반 파라미터
	float				 userIntensity		 = 70.0f;  // (0~100)
	float				 userVariability	 = 50.0f;  // (0~100)
	float				 userGustFreq		 = 45.0f;  // (0~100)
	float				 minFanPct			 = 10.0f;  // (0~100)
	float				 fanLimitPct		 = 90.0f;  // (0~100)

	float				 turbLenScale		 = 40.0f;
	float				 turbSigma			 = 0.5f;
	float				 thermalStrength	 = 2.0f;
	float				 thermalRadius		 = 18.0f;

	// Phase 상태
	T_A20_WindPhase_t	 phase				 = EN_A20_WEATHER_PHASE_NORMAL;
	float				 phaseStartSec		 = 0.0f;	// [s]
	float				 phaseDurationSec	 = 120.0f;	// [s]
	float				 phaseMinWind		 = 2.0f;
	float				 phaseMaxWind		 = 6.0f;

	// Preset 기반 범위/확률 (applyPresetCore에서 설정됨)
	float				 baseMinWind		 = 1.8f;
	float				 baseMaxWind		 = 5.5f;

	// -------------------- 중요: 확률 의미 정규화 --------------------
	// gustProbBase / thermalFreqBase는 "Tick당 확률"이 아니라,
	// "초당 발생률(rate, 1/sec)"로 정의한다.
	// - 구현부에서는 평가 간격(dtSec)에 따라
	//   P = 1 - exp(-ratePerSec * dtSec) 로 자동 보정해야 한다.
	float				 gustProbBase		 = 0.040f;	// [1/sec] 돌풍 기본 발생률(초당)
	float				 gustStrengthMax	 = 2.1f;	// [-]     돌풍 최대 강도 배율
	float				 thermalFreqBase	 = 0.022f;	// [1/sec] 열기포 기본 발생률(초당)
	// ----------------------------------------------------------------

	// 난류(스펙트럼) 상태
	float				 spectralEnergyBuf	 = 0.0f;
	float				 spectralPhaseAcc	 = 0.0f;
	float				 turbTimeScale		 = 5.0f;

	// 돌풍 상태
	bool				 gustActive			 = false;
	float				 gustStartSec		 = 0.0f;  // [s]
	float				 gustDuration		 = 3.0f;  // [s]
	float				 gustIntensity		 = 1.0f;
	unsigned long		 lastGustCheckMs	 = 0;

	// 열기포 상태
	bool				 thermalActive		 = false;
	float				 thermalStartSec	 = 0.0f;  // [s]
	float				 thermalDuration	 = 8.0f;  // [s]
	float				 thermalContribution = 0.0f;
	unsigned long		 lastThermalCheckMs	 = 0;

	// 관성/업데이트
	float				 windChangeRate		 = 0.12f;
	float				 windMomentum		 = 0.0f;
	unsigned long		 lastUpdateMs		 = 0;

	// 최근 풍속 샘플 저장
	static const uint8_t HISTORY_SIZE		 = 60;	// [sample] 1Hz 기준 60초
	float				 history[HISTORY_SIZE];
	uint8_t				 historyIndex  = 0;
	uint8_t				 historyCount  = 0;
	float				 avgWindCached = 0.0f;

	struct ST_ChartEntry {
		unsigned long timestamp;
		float		  wind_speed;
		float		  pwm_duty;
		float		  intensity;
		float		  variability;
		float		  turbulence_sigma;
		uint8_t		  preset_index;
		bool		  gust_active;
		bool		  thermal_active;
	};
	static std::deque<ST_ChartEntry> s_chartBuffer;
	static unsigned long			 s_lastChartLogMs;
	static unsigned long			 s_lastChartSampleMs;

  public:
	void begin(CL_P10_PWM& p_pwm);
	void stop();
	void resetDefaults();

	void tick();
	void applyResolvedWind(const ST_A20_ResolvedWind_t& p_resolved);

	bool patchFromJson(const JsonDocument& p_doc);

	void toJson(JsonDocument& p_doc);
	void toChartJson(JsonDocument& p_doc, bool p_diffOnly = false);

  private:
	CL_P10_PWM*				  _pwm		  = nullptr;

	const ST_A20_FanConfig_t* _fanCfgSnap = nullptr;

	portMUX_TYPE			  _simMutex	  = portMUX_INITIALIZER_UNLOCKED;

	unsigned long			  _tickNowMs  = 0;
	float					  _tickNowSec = 0.0f;

	void applyFan(float p_pct);
	void applyPresetCore(const char* p_code);
	void initPhaseFromBase();
	void updatePhase();
	void calcTurb(float p_dt);
	void calcThermalEnvelope();
	void updateGust();
	void updateThermal();
	void generateTarget();

	void _updateWindHistory(float p_speed);
	float _getAvgWindFast() const;
};
