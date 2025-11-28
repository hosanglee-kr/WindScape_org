#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : S10_Simulation_020.h
 * 모듈약어 : S10
 * 모듈명 : Smart Nature Wind 풍속 시뮬레이션 Manager
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
#include <string.h>
#include <freertos/FreeRTOS.h> // FreeRTOS 환경 사용을 위한 헤더
#include <freertos/task.h>     // Task 관리를 위한 헤더

#include <cmath>
#include <deque> // 차트 데이터 관리를 위한 덱(양방향 큐) 컨테이너

#include "A10_Const_015.h" // 공통 상수 및 타입 정의
#include "C10_Config_026.h" // 설정 관리 모듈
#include "D10_Logger_016.h"        // 로거 모듈
#include "P10_PWM_ctrl_014.h"      // PWM 제어 모듈

// SC10(메인 루프)의 전역 함수 전방 선언: WebAPI로 상태를 브로드캐스팅
extern void SC10_broadcastState(ArduinoJson::JsonDocument& doc, bool diffOnly);
extern void SC10_broadcastChart(ArduinoJson::JsonDocument& doc, bool diffOnly);
extern void SC10_broadcastMetrics(ArduinoJson::JsonDocument& doc, bool diffOnly);
extern void SC10_markDirty(const char* key); // NVS 또는 Config 저장을 위한 Dirty 플래그 설정
extern int strcasecmp(const char* s1, const char* s2); // 대소문자 무시 문자열 비교

// ======================================================
// CL_S10_Simulation
//  - 풍속 시뮬레이션 및 PWM Duty 계산의 핵심 클래스
// ======================================================
class CL_S10_Simulation {
    public:
    // ------------------ 상태/파라미터 ------------------
    bool active          = false; // 시뮬레이션 활성화 여부
    bool fanPowerEnabled = true;  // 팬 전원 자체 활성화 여부

    float currentWindSpeed = 3.6f;  // 현재 시뮬레이션 풍속 (내부 단위 m/s 개념)
    float targetWindSpeed  = 3.6f;  // 목표 풍속 (난류/관성으로 점진적 접근)

    char presetCode[24] = {0}; // 현재 적용된 Preset 코드 (예: OCEAN)
    char styleCode[24]  = {0};  // 현재 적용된 Style 코드 (예: BALANCE)

    // C10 해석 결과 기반 파라미터 (ST_A10_ResolvedWind_t 매핑)
    float userIntensity   = 70.0f;  // 사용자 설정 강도 (0~100)
    float userVariability = 50.0f;  // 사용자 설정 변화율/불규칙성 (0~100)
    float userGustFreq    = 45.0f;  // 사용자 설정 돌풍 빈도 (0~100)
    float minFanPct       = 10.0f;  // 최소 팬 Duty (0~100)
    float fanLimitPct     = 90.0f;  // 최대 팬 Duty (0~100, Duty Limit)

    float turbLenScale    = 40.0f;  // 난류 길이 스케일 (Von Kármán 모델 파라미터)
    float turbSigma       = 0.5f;   // 난류 강도 (Intensity of Turbulence, 표준편차)
    float thermalStrength = 2.0f;   // 열기포 강도
    float thermalRadius   = 18.0f;  // 열기포 특성 반경 (모델 확장 여지)

    // Phase 상태
    T_A10_WindPhase_t phase            = EN_A10_WEATHER_PHASE_NORMAL; // 현재 바람의 Phase (잔잔, 보통, 강풍)
    float             phaseStartSec    = 0.0f;  // 현재 Phase 시작 시간 (초)
    float             phaseDurationSec = 120.0f; // 현재 Phase의 목표 지속 시간 (초)
    float             phaseMinWind     = 2.0f;  // 현재 Phase의 최소 목표 풍속
    float             phaseMaxWind     = 6.0f;  // 현재 Phase의 최대 목표 풍속

    // Preset 기반 범위/확률 (applyPresetCore에서 설정됨)
    float baseMinWind     = 1.8f;   // Preset 기반 최소 풍속
    float baseMaxWind     = 5.5f;   // Preset 기반 최대 풍속
    float gustProbBase    = 0.040f; // 돌풍 기본 발생 확률 (Tick당)
    float gustStrengthMax = 2.1f;   // 돌풍 최대 강도 (PWM 배율)
    float thermalFreqBase = 0.022f; // 열기포 기본 발생 빈도 (Tick당)

    // 난류(스펙트럼) 상태
    float spectralEnergyBuf = 0.0f; // 현재 계산된 난류 에너지 기여분
    float spectralPhaseAcc  = 0.0f; // 난류 스펙트럼 모델의 위상 누적값
    float turbTimeScale     = 5.0f; // 난류의 변화 속도 (시간 상수)

    // 돌풍 상태
    bool          gustActive      = false; // 돌풍 활성화 여부
    float         gustStartSec    = 0.0f;  // 돌풍 시작 시간 (초)
    float         gustDuration    = 3.0f;  // 돌풍 지속 시간 (초)
    float         gustIntensity   = 1.0f;  // 최종 PWM Duty에 곱해지는 배율 (1.0 이상)
    unsigned long lastGustCheckMs = 0;     // 마지막 돌풍 발생 체크 시점

    // 열기포 상태
    bool          thermalActive       = false; // 열기포 활성화 여부
    float         thermalStartSec     = 0.0f;  // 열기포 시작 시간 (초)
    float         thermalDuration     = 8.0f;  // 열기포 지속 시간 (초)
    float         thermalContribution = 0.0f;  // 최종 PWM Duty에 가산되는 영향
    unsigned long lastThermalCheckMs  = 0;     // 마지막 열기포 발생 체크 시점

    // 관성/업데이트
    float         windChangeRate = 0.12f; // 목표 풍속으로 수렴하는 속도
    float         windMomentum   = 0.0f;  // 풍속 변화에 적용되는 관성 성분
    unsigned long lastUpdateMs   = 0;     // 마지막 tick 업데이트 시간 (millis)

    // --------------------------------------------------
    // 최근 풍속 샘플 저장 (차트/메트릭 병렬 구조)
    // --------------------------------------------------
    static const uint8_t HISTORY_SIZE = 60;      // 1Hz 기준 60초
    float                history[HISTORY_SIZE];  // 최근 풍속 값만 저장 (순환 버퍼)
    uint8_t              historyIndex  = 0;      // 순환 버퍼의 다음 쓰기 인덱스
    uint8_t              historyCount  = 0;      // 현재 저장된 유효 샘플 개수
    float                avgWindCached = 0.0f;   // 최근 평균 풍속 캐시 (O(1) 접근)

    // 차트 버퍼 (최근 120 샘플, 1Hz 기준)
    // ST_ChartEntry: 차트 플롯을 위한 상세 이력 구조체
    struct ST_ChartEntry {
        unsigned long timestamp;      // 샘플링 시간 (millis)
        float         wind_speed;     // 당시의 현재 풍속
        float         pwm_duty;       // 당시의 최종 PWM Duty (%)
        float         intensity;      // 당시의 사용자 강도
        float         variability;    // 당시의 사용자 변화율
        float         turbulence_sigma; // 당시의 난류 강도
        uint8_t       preset_index;   // 당시의 Preset 인덱스
        bool          gust_active;    // 돌풍 활성화 여부
        bool          thermal_active; // 열기포 활성화 여부
    };
    static std::deque<ST_ChartEntry> s_chartBuffer;      // 덱을 사용한 차트 데이터 버퍼
    static unsigned long             s_lastChartLogMs;   // 마지막 차트 로그 기록 시간 (1Hz 주기 제어)
    static unsigned long             s_lastChartSampleMs;// 마지막 차트 전송/샘플 시간 (10초 전송 제어용)

    public:
    // ==================================================
    // 초기화 / 정지 / 리셋
    // ==================================================
    void begin(CL_P10_PWM& p_pwm); // 초기화 및 PWM 제어기 포인터 설정
    void stop();                   // 시뮬레이션 중지 및 팬 정지
    void resetDefaults();          // 모든 파라미터를 기본값으로 초기화

    // ==================================================
    // 메인 tick (CT10에서 주기 호출)
    // ==================================================
    void tick(); // 시뮬레이션의 시간 진행 및 풍속 업데이트

    // ==================================================
    // 해석 결과 적용: resolveWindParams → 여기 호출
    // ==================================================
    // C10_ConfigManager에서 해석된 최종 풍속 파라미터를 적용
    void applyResolvedWind(const ST_A10_ResolvedWind_t& p_resolved);

    // ==================================================
    // JSON Export
    // ==================================================
    void toJson(JsonDocument& p_doc);  //현재 시뮬레이션 상태를 JSON 객체로 직렬화 (WebAPI 상태 응답용)
    //void toJson_old(JsonObject& p_obj); // 현재 시뮬레이션 상태를 JSON 객체로 직렬화 (WebAPI 상태 응답용)
    void toChartJson(JsonDocument& p_doc, bool p_diffOnly = false); // 차트 이력 데이터를 JSON 배열로 직렬화

    private:

    CL_P10_PWM* _pwm = nullptr; // PWM 제어기 포인터
    portMUX_TYPE _simMutex = portMUX_INITIALIZER_UNLOCKED; // FreeRTOS Mutex: 다중 Task 접근 보호용


    // ==================================================
    // 내부 구현부
    // ==================================================
    void applyFan(float p_pct);        // 계산된 Duty Percent를 PWM 제어기에 적용 (Min/Max/Intensity 적용)
    void applyPresetCore(const char* p_code); // Preset 코드에 따라 기본 물리 상수를 설정
    void initPhaseFromBase();          // 기본 Base Wind를 기준으로 초기 Phase 상태 설정
    void updatePhase();                // Phase 변화 로직 (Duration 만료 시 확률적 전환)
    void calcTurb(float p_dt);         // 난류(Turbulence) 성분 계산 (Von Kármán 모델)
    void calcThermalEnvelope();        // 열기포(Thermal) 영향의 포락선(Envelope) 계산
    void updateGust();                 // 돌풍(Gust) 발생 확률 체크 및 상태 갱신
    void updateThermal();              // 열기포(Thermal) 발생 확률 체크 및 상태 갱신
    void generateTarget();             // Phase 범위 내에서 새로운 목표 풍속(targetWindSpeed) 설정

    // --------------------------------------------------
    // 최근 풍속 이력 관리 (순환 버퍼 기반)
    // --------------------------------------------------
    void _updateWindHistory(float p_speed); // 새로운 풍속 값을 순환 버퍼에 저장하고 평균 캐시 갱신

    // --------------------------------------------------
    // 캐시된 평균 풍속 반환 (O(1))
    // --------------------------------------------------
    float _getAvgWindFast() const; // O(1) 시간복잡도로 캐시된 최근 평균 풍속 반환
};
