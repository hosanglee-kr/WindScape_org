#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : S10_Simulation_020.h
 * 모듈약어 : S10
 * 모듈명 : Smart Nature Wind 풍속 시뮬레이션 Manager (v018, Full)
 * ------------------------------------------------------
 * 기능 요약:
 * - 자연풍 시뮬레이션 핵심 엔진 (Phase / 난류 / 돌풍 / 열기포 / 관성)
 * - PWM 제어기(CL_P10_PWM)와 연동하여 실시간 풍속을 PWM Duty로 변환
 * - C10 해석 결과(ST_A10_ResolvedWind_t) 기반 파라미터 적용
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
#include <freertos/FreeRTOS.h> // FreeRTOS 헤더 추가
#include <freertos/task.h>     // Task 헤더 추가

#include <cmath>
#include <deque>

#include "A10_Const_015.h"
#include "C10_ConfigManager_024.h"
#include "D10_Logger_016.h"
#include "P10_PWM_ctrl_014.h"

// SC10 (Main Loop) 모듈과의 연동을 위한 전역 함수 선언 (외부 구현 필요)
extern void SC10_broadcastState(ArduinoJson::JsonDocument& doc, bool diffOnly);
extern void SC10_broadcastChart(ArduinoJson::JsonDocument& doc, bool diffOnly);
extern void SC10_broadcastMetrics(ArduinoJson::JsonDocument& doc, bool diffOnly);
extern void SC10_markDirty(const char* key);
extern int strcasecmp(const char* s1, const char* s2); // strcasecmp은 platformio에 있을 수 있으나 포함

// ======================================================
// CL_S10_Simulation
//  - 자연풍 시뮬레이션의 모든 상태와 알고리즘을 포함하는 핵심 클래스
// ======================================================
class CL_S10_Simulation {
    public:
    // ------------------ 상태/파라미터 ------------------
    bool active          = false;           // 시뮬레이션 활성화 여부
    bool fanPowerEnabled = true;            // 팬 전원 허용 여부

    float currentWindSpeed = 3.6f;  // 현재 적용되는 풍속 (내부 m/s 단위)
    float targetWindSpeed  = 3.6f;  // Phase, Gust, Thermal 계산 후 생성된 목표 풍속

    char presetCode[24] = {0};      // 현재 적용된 풍속 프리셋 코드 (예: OCEAN)
    char styleCode[24]  = {0};      // 현재 적용된 스타일 코드 (예: BALANCE)

    // 해석 결과 기반 사용자 제어 파라미터 (ST_A10_ResolvedWind_t 매핑)
    float userIntensity   = 70.0f;  // 사용자가 설정한 풍속 강도 (0~100)
    float userVariability = 50.0f;  // 사용자가 설정한 풍속 변화 폭 (0~100)
    float userGustFreq    = 45.0f;  // 사용자가 설정한 돌풍 발생 빈도 (0~100)
    float minFanPct       = 10.0f;  // 최소 Fan Duty (0~100)
    float fanLimitPct     = 90.0f;  // 최대 Fan Duty (0~100)

    // 물리 기반 난류 모델 파라미터
    float turbLenScale    = 40.0f;  // 난류 길이 스케일 (Von Kármán 모델)
    float turbSigma       = 0.5f;   // 난류 강도 (RMS of velocity fluctuation)
    float thermalStrength = 2.0f;   // 열기포 강도
    float thermalRadius   = 18.0f;  // 열기포 특성 반경 (사용처 확장 여지)

    // Phase 상태
    T_A10_WindPhase_t phase            = EN_A10_WEATHER_PHASE_NORMAL; // 현재 Phase (CALM, NORMAL, STRONG)
    float             phaseStartSec    = 0.0f;                        // 현재 Phase 시작 시간 (초 단위)
    float             phaseDurationSec = 120.0f;                      // 현재 Phase 지속 시간
    float             phaseMinWind     = 2.0f;                        // 현재 Phase의 최소 목표 풍속
    float             phaseMaxWind     = 6.0f;                        // 현재 Phase의 최대 목표 풍속

    // Preset 기반 기본 범위/확률
    float baseMinWind     = 1.8f;   // Preset의 기본 최소 풍속
    float baseMaxWind     = 5.5f;   // Preset의 기본 최대 풍속
    float gustProbBase    = 0.040f; // 기본 돌풍 발생 확률 (Base Probability)
    float gustStrengthMax = 2.1f;   // 최대 돌풍 강도 배율
    float thermalFreqBase = 0.022f; // 기본 열기포 발생 빈도

    // 난류(스펙트럼) 상태
    float spectralEnergyBuf = 0.0f; // 난류 모델에서 계산된 최종 에너지 버퍼 (풍속에 가산됨)
    float spectralPhaseAcc  = 0.0f; // 난류 계산에 사용되는 위상 누적 값
    float turbTimeScale     = 5.0f; // 난류 성분의 시간적 상관 관계 스케일

    // 돌풍 상태
    bool          gustActive      = false;    // 돌풍 활성 여부
    float         gustStartSec    = 0.0f;     // 돌풍 시작 시간 (초)
    float         gustDuration    = 3.0f;     // 돌풍 지속 시간
    float         gustIntensity   = 1.0f;     // 현재 PWM에 적용되는 돌풍 강도 배율
    unsigned long lastGustCheckMs = 0;        // 돌풍 발생 확률 체크를 위한 마지막 시간

    // 열기포 상태
    bool          thermalActive       = false;    // 열기포 활성 여부
    float         thermalStartSec     = 0.0f;     // 열기포 시작 시간 (초)
    float         thermalDuration     = 8.0f;     // 열기포 지속 시간
    float         thermalContribution = 0.0f;     // PWM에 가산되는 열기포 기여도
    unsigned long lastThermalCheckMs  = 0;        // 열기포 발생 확률 체크를 위한 마지막 시간

    // 관성/업데이트
    float         windChangeRate = 0.12f;     // 목표 풍속으로 수렴하는 변화율 (Variability에 의해 조정됨)
    float         windMomentum   = 0.0f;      // 풍속 변화의 관성 (Inertia)
    unsigned long lastUpdateMs   = 0;         // 마지막 tick 업데이트 시간 (Delta Time 계산용)

    // --------------------------------------------------
    // 최근 풍속 샘플 저장 (차트/메트릭 병렬 구조)
    // --------------------------------------------------
    static const uint8_t HISTORY_SIZE = 60;      // 1Hz 기준 60초
    float                history[HISTORY_SIZE];  // 최근 풍속 값만 저장 (순환 버퍼)
    uint8_t              historyIndex  = 0;      // 순환 버퍼의 다음 쓰기 위치 인덱스
    uint8_t              historyCount  = 0;      // 실제 저장된 유효한 샘플 개수
    float                avgWindCached = 0.0f;   // 최근 평균 풍속 캐시 (O(1) 접근)

    // 차트 버퍼 (최근 120 샘플, 1Hz 기준)
    struct ST_ChartEntry {
        unsigned long timestamp;    // 샘플링 시간 (ms)
        float         wind_speed;   // 실제 풍속 (m/s)
        float         pwm_duty;     // 적용된 PWM Duty (%)
        float         intensity;    // 시뮬레이션 당시의 강도
        float         variability;  // 시뮬레이션 당시의 변화 폭
        float         turbulence_sigma; // 시뮬레이션 당시의 난류 강도
        uint8_t       preset_index; // Preset의 인덱스
        bool          gust_active;  // 돌풍 활성 여부
        bool          thermal_active; // 열기포 활성 여부
    };
    static std::deque<ST_ChartEntry> s_chartBuffer;         // 차트 데이터 저장 (FIFO 구조)
    static unsigned long             s_lastChartLogMs;      // 차트 버퍼에 마지막 샘플 기록 시간 (1Hz 제어)
    static unsigned long             s_lastChartSampleMs;   // toChartJson에서 마지막 전송 시간 (10초 전송 제한용)

    public:
    // ==================================================
    // 초기화 / 정지 / 리셋
    // ==================================================
    void begin(CL_P10_PWM& p_pwm);  // PWM 제어기 연결 및 초기화
    void stop();                    // 시뮬레이션 정지 (풍속 0, active = false)
    void resetDefaults();           // 모든 파라미터를 기본값으로 초기화

    // ==================================================
    // 메인 tick (CT10에서 주기 호출)
    // ==================================================
    void tick(); // 메인 루프에서 주기적으로 호출되어 시뮬레이션 진행

    // ==================================================
    // 해석 결과 적용: resolveWindParams → 여기 호출
    // ==================================================
    void applyResolvedWind(const ST_A10_ResolvedWind_t& p_resolved); // C10 해석 결과를 시뮬레이터에 반영

    // ==================================================
    // JSON Export
    // ==================================================
    void toJson(JsonObject& p_obj);                           // 현재 시뮬레이션 상태를 JSON 객체로 변환
    void toChartJson(JsonDocument& p_doc, bool p_diffOnly = false); // 차트 버퍼 데이터를 JSON으로 변환 (WebAPI 용)

    private:

    CL_P10_PWM* _pwm = nullptr;                        // 연결된 PWM 제어기 포인터
    portMUX_TYPE _simMutex = portMUX_INITIALIZER_UNLOCKED; // FreeRTOS Mutex: 다중 Task 접근 시 안전성 확보

    // ==================================================
    // 내부 구현부
    // ==================================================
    void applyFan(float p_pct);         // 계산된 풍속을 min/max/intensity를 적용하여 PWM Duty로 변환
    void applyPresetCore(const char* p_code); // PresetCode에 따라 base 풍속 및 확률 기본값 설정
    void initPhaseFromBase();           // Preset의 baseWind를 바탕으로 초기 Phase 설정
    void updatePhase();                 // Phase 지속 시간 체크 및 다음 Phase로 확률적 전환
    void calcTurb(float p_dt);          // Von Kármán 난류 모델 기반 난류 에너지 계산
    void calcThermalEnvelope();         // 열기포 활성 시 풍속 가산값 포락선(Envelope) 계산
    void updateGust();                  // 돌풍 발생 확률 체크 및 활성 시 상태 값 업데이트
    void updateThermal();               // 열기포 발생 확률 체크 및 활성 시 상태 값 업데이트
    void generateTarget();              // Phase 범위 내에서 새로운 목표 풍속 targetWindSpeed 생성

    // --------------------------------------------------
    // 최근 풍속 이력 관리 (순환 버퍼 기반)
    // --------------------------------------------------
    void _updateWindHistory(float p_speed); // 순환 버퍼에 샘플을 추가하고 평균 캐시 갱신

    // --------------------------------------------------
    // 캐시된 평균 풍속 반환 (O(1))
    // --------------------------------------------------
    float _getAvgWindFast() const; // 캐시된 평균 풍속을 즉시 반환
};
