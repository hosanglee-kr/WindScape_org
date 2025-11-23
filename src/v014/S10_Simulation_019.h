#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : S10_Simulation_019.h
 * 모듈약어 : S10
 * 모듈명 : Smart Nature Wind 풍속 시뮬레이션 Manager (v018, Full)
 * ------------------------------------------------------
 * 기능 요약:
 *  - 자연풍 시뮬레이션 핵심 엔진 (Phase / 난류 / 돌풍 / 열기포 / 관성)
 *  - PWM 제어기(CL_P10_PWM)와 연동하여 실시간 풍속을 PWM Duty로 변환
 *  - C10 해석 결과(ST_A10_ResolvedWind_t) 기반 파라미터 적용
 *  - PresetCode + StyleCode 기반 풍속 특성(범위·확률·스펙트럼) 자동 세팅
 *  - Von Kármán 스펙트럼 난류 모델 + Phase별 풍속 재생성 로직
 *  - 돌풍(Gust), 열기포(Thermal Bubble), 자연감 지터(Jitter) 확률적 발생
 *  - 최근 60초 풍속 이력 순환 버퍼(history) 및 평균 캐시 관리
 *  - 최근 120초 Chart 버퍼(1Hz 샘플링) 관리 및 JSON 직렬화 지원
 *  - diffOnly 모드 지원 (WebSocket/REST 효율 전송)
 *  - Phase 변화 또는 급격한 풍속 변화 시 실시간 WebSocket 브로드캐스트
 *  - C10_ControlManager 및 W10_WebAPI와 완전 호환 구조
 * ------------------------------------------------------
 * [구현 규칙]
 *  - 항상 소스 시작 주석 부분 체계 유지 및 내용 업데이트
 *  - 소스 시작 주석 부분 구현규칙, 코드네이밍규칙 내용 그대로 유지, 수정금지
 *  - ArduinoJson v7.x.x 사용 (v6 이하 사용 금지)
 *  - JsonDocument 단일 타입만 사용
 *  - createNestedArray/Object/containsKey 사용 금지
 *  - memset + strlcpy 기반 안전 초기화
 *  - 주석/필드명은 JSON 구조와 동일하게 유지
 *  - 모듈별 단일 헤더(h)파일로만 구성 (cpp 없음) -> 구현부 분리 요청으로 인해 CPP 파일 생성됨.
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
#include <string.h>
#include <freertos/FreeRTOS.h> // ✅ FreeRTOS 헤더 추가
#include <freertos/task.h>     // ✅ Task 헤더 추가

#include <cmath>
#include <deque>

#include "A10_Const_015.h"
#include "C10_ConfigManager_024.h"
#include "D10_Logger_016.h"
#include "P10_PWM_ctrl_014.h"

// 전방 선언
// class CL_P10_PWM;
// struct ST_A10_ResolvedWind_t;
// extern 함수 선언
extern void SC10_broadcastState(ArduinoJson::JsonDocument& doc, bool diffOnly);
extern void SC10_broadcastChart(ArduinoJson::JsonDocument& doc, bool diffOnly);
extern void SC10_broadcastMetrics(ArduinoJson::JsonDocument& doc, bool diffOnly);
extern void SC10_markDirty(const char* key);
extern int strcasecmp(const char* s1, const char* s2); // strcasecmp은 platformio에 있을 수 있으나 포함

// 외부 함수 및 상수 선언 (원본에 포함된 헤더의 내용을 추정하여 포함)
// 실제 환경에서는 A10_Const_015.h, C10_ConfigManager_023.h, D10_Logger_016.h, P10_PWM_ctrl_014.h가 필요
// 하지만 분리를 위해 최소한의 선언만 유지


// typedef enum {
//     EN_A10_WEATHER_PHASE_CALM = 0,
//     EN_A10_WEATHER_PHASE_NORMAL,
//     EN_A10_WEATHER_PHASE_STRONG
// } T_A10_WindPhase_t;

// // 외부 변수 선언 (가정)
// extern const char* g_A10_WEATHER_PHASE_NAMES_Arr[];

// // 외부 함수 선언 (가정)
// extern float A10_randRange(float p_min, float p_max);
// extern float A10_getRandom01();
// extern int A10_getPresetIndexByCode(const char* p_code);


// ======================================================
// CL_S10_Simulation
//  - 헤더 전용, 전체 알고리즘 포함
// ======================================================
class CL_S10_Simulation {
    public:
    // ------------------ 상태/파라미터 ------------------
    bool active          = false;
    bool fanPowerEnabled = true;

    float currentWindSpeed = 3.6f;  // m/s 개념의 내부 단위
    float targetWindSpeed  = 3.6f;

    char presetCode[24] = {0};
    char styleCode[24]  = {0};

    // 해석 결과 기반 파라미터 (ST_A10_ResolvedWind_t 매핑)
    float userIntensity   = 70.0f;  // 0~100
    float userVariability = 50.0f;  // 0~100
    float userGustFreq    = 45.0f;  // 0~100
    float minFanPct       = 10.0f;  // 0~100
    float fanLimitPct     = 90.0f;  // 0~100

    float turbLenScale    = 40.0f;  // 난류 스케일
    float turbSigma       = 0.5f;   // 난류 세기
    float thermalStrength = 2.0f;   // 열기포 강도
    float thermalRadius   = 18.0f;  // 열기포 특성 반경 (사용처 확장 여지)

    // Phase 상태
    T_A10_WindPhase_t phase            = EN_A10_WEATHER_PHASE_NORMAL;
    float             phaseStartSec    = 0.0f;
    float             phaseDurationSec = 120.0f;
    float             phaseMinWind     = 2.0f;
    float             phaseMaxWind     = 6.0f;

    // Preset 기반 범위/확률
    float baseMinWind     = 1.8f;
    float baseMaxWind     = 5.5f;
    float gustProbBase    = 0.040f;
    float gustStrengthMax = 2.1f;
    float thermalFreqBase = 0.022f;

    // 난류(스펙트럼) 상태
    float spectralEnergyBuf = 0.0f;
    float spectralPhaseAcc  = 0.0f;
    float turbTimeScale     = 5.0f;

    // 돌풍 상태
    bool          gustActive      = false;
    float         gustStartSec    = 0.0f;
    float         gustDuration    = 3.0f;
    float         gustIntensity   = 1.0f;  // PWM 배율
    unsigned long lastGustCheckMs = 0;

    // 열기포 상태
    bool          thermalActive       = false;
    float         thermalStartSec     = 0.0f;
    float         thermalDuration     = 8.0f;
    float         thermalContribution = 0.0f;  // PWM 가산 영향
    unsigned long lastThermalCheckMs  = 0;

    // 관성/업데이트
    float         windChangeRate = 0.12f;
    float         windMomentum   = 0.0f;
    unsigned long lastUpdateMs   = 0;

    // --------------------------------------------------
    // 최근 풍속 샘플 저장 (차트/메트릭 병렬 구조)
    // --------------------------------------------------
    static const uint8_t HISTORY_SIZE = 60;      // 1Hz 기준 60초
    float                history[HISTORY_SIZE];  // 최근 풍속 값만 저장
    uint8_t              historyIndex  = 0;      // 순환 인덱스
    uint8_t              historyCount  = 0;      // 실제 저장 개수
    float                avgWindCached = 0.0f;   // 최근 평균 캐시

    // 차트 버퍼 (최근 120 샘플, 1Hz 기준)
    struct ST_ChartEntry {
        unsigned long timestamp;
        float         wind_speed;
        float         pwm_duty;
        float         intensity;
        float         variability;
        float         turbulence_sigma;
        uint8_t       preset_index;
        bool          gust_active;
        bool          thermal_active;
    };
    static std::deque<ST_ChartEntry> s_chartBuffer;
    static unsigned long             s_lastChartLogMs;
    static unsigned long             s_lastChartSampleMs;

    public:
    // ==================================================
    // 초기화 / 정지 / 리셋 (Public Method Declarations)
    // ==================================================
    void begin(CL_P10_PWM& p_pwm);
    void stop();
    void resetDefaults();

    // ==================================================
    // 메인 tick (CT10에서 주기 호출) (Public Method Declarations)
    // ==================================================
    void tick();

    // ==================================================
    // 해석 결과 적용: resolveWindParams → 여기 호출 (Public Method Declarations)
    // ==================================================
    void applyResolvedWind(const ST_A10_ResolvedWind_t& p_resolved);

    // ==================================================
    // JSON Export (Public Method Declarations)
    // ==================================================
    void toJson(JsonObject& p_obj);
    void toChartJson(JsonDocument& p_doc, bool p_diffOnly = false);

    private:

    CL_P10_PWM* _pwm = nullptr;
    portMUX_TYPE _simMutex = portMUX_INITIALIZER_UNLOCKED; // ✅ Mutex 정의


    // ==================================================
    // 내부 구현부 (Private Method Declarations)
    // ==================================================
    void applyFan(float p_pct);
    void applyPresetCore(const char* p_code);
    void initPhaseFromBase();
    void updatePhase();
    void calcTurb(float p_dt);
    void calcThermalEnvelope();
    void updateGust();
    void updateThermal();
    void generateTarget();

    // --------------------------------------------------
    // ✅ 최근 풍속 이력 관리 (순환 버퍼 기반)
    // --------------------------------------------------
    void _updateWindHistory(float p_speed);

    // --------------------------------------------------
    // ✅ 캐시된 평균 풍속 반환 (O(1))
    // --------------------------------------------------
    float _getAvgWindFast() const;
};
