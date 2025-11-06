#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : S10_Simulation_014.h
 * 모듈약어 : S10
 * 모듈명 : Smart Nature Wind 풍속 시뮬레이션 Manager (v014)
 * ------------------------------------------------------
 * 기능 요약:
 *  - 해석된 바람 파라미터(ResolvedWind)를 입력 받아 자연풍 물리 시뮬레이션 수행
 *  - Phase/난류/돌풍/열기포/지터를 합성하여 목표 풍속 → PWM 듀티로 변환
 *  - PWM 제어기(CL_P10_PWM)와 연동
 *  - 차트 버퍼 제공(웹 UI 실시간 확인용)
 *  - 외부 Motion 게이트(정지/재개) 신호 반영
 * ------------------------------------------------------
 * [구현 규칙]
 *  - 항상 소스 시작 주석 체계 유지
 *  - ArduinoJson v7.x.x 사용 (v6 이하 사용 금지)
 *  - JsonDocument 단일 타입만 사용
 *  - createNestedArray/Object/containsKey 사용 금지
 *  - memset + strlcpy 기반 안전 초기화
 *  - 주석/필드명은 JSON 구조와 동일하게 유지
 *  - 모듈별 단일 헤더(h)파일로만 구성 (cpp 없음)
 *  - 소스 앞부분 구현규칙, 코드네이밍규칙 변경 금지
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
#include <deque>
#include <cmath>
#include <string.h>

#include "A10_Const_014.h"        // ST_A10_ResolvedWind_t, enums, utils
#include "C10_ConfigManager_014.h"// (사전 로딩/해석 담당, 여기서 직접 사용하지 않음)
#include "D10_Logger_011.h"
#include "P10_PWM_ctrl_012.h"     // PWM 드라이버 (duty % 기반)

// ------------------------------------------------------
// S10 내부 상수/유틸
// ------------------------------------------------------
typedef enum : uint8_t {
	EN_S10_PHASE_CALM = 0,
	EN_S10_PHASE_NORMAL,
	EN_S10_PHASE_STRONG
} EN_S10_phase_t;

static inline float S10_clampf(float p_v, float p_lo, float p_hi) {
	if (p_v < p_lo) return p_lo;
	if (p_v > p_hi) return p_hi;
	return p_v;
}

// ------------------------------------------------------
// 차트 로깅 엔트리
// ------------------------------------------------------
typedef struct {
	unsigned long t_ms;
	float wind;          // 현재 풍속(모델상의 내부 단위)
	float target;        // 목표 풍속
	float pwm;           // 실제 PWM 듀티(%)
	float intensity;     // 사용중 intensity(해석 결과)
	float variability;   // 사용중 variability(해석 결과)
	float gust;          // 돌풍 이득(1.0 = 없음)
	float thermal;       // 열기포 기여(가중치)
	uint8_t phase;       // EN_S10_PHASE_*
	char presetCode[24]; // 현재 적용 프리셋 코드(진입 시점 스냅샷)
	char styleCode[24];  // 현재 적용 스타일 코드(진입 시점 스냅샷)
} ST_S10_ChartEntry_t;

// ------------------------------------------------------
// S10 시뮬레이션 클래스
// ------------------------------------------------------
class CL_S10_Simulation {
public:
	// 외부에 공개되는 상태
	bool  active               = false;   // 시뮬레이터 가동 여부
	bool  fanPowerEnabled      = true;    // 팬 전원 게이트(외부 모션 등에서 제어)
	float currentWind          = 3.5f;    // 현재 풍속(내부 가상 단위)
	float targetWind           = 3.5f;    // 목표 풍속
	float windMomentum         = 0.0f;    // 모멘텀(관성)

	// 현재 적용중 파라미터(해석된 바람)
	ST_A10_ResolvedWind_t resolved;       // intensity/variability/gust/min_fan/fan_limit/… 포함
	char  presetCode[24] = {0};
	char  styleCode[24]  = {0};

	// 페이즈 관리
	EN_S10_phase_t phase          = EN_S10_PHASE_NORMAL;
	float          phaseStartSec  = 0.0f;
	float          phaseDurSec    = 150.0f;
	float          phaseMinWind   = 1.0f;
	float          phaseMaxWind   = 6.0f;

	// 난류 합성 버퍼
	float spectralBuf             = 0.0f;
	float spectralPhaseAcc        = 0.0f;
	float turbTimeScale           = 5.0f;

	// 돌풍
	bool          gustActive      = false;
	float         gustStartSec    = 0.0f;
	float         gustDurSec      = 3.0f;
	float         gustGain        = 1.0f;   // 1.0 이상
	unsigned long lastGustCheckMs = 0;

	// 열기포
	bool          thermalActive        = false;
	float         thermalStartSec      = 0.0f;
	float         thermalDurSec        = 8.0f;
	float         thermalContribution  = 0.0f;
	unsigned long lastThermCheckMs     = 0;

	// 주기
	unsigned long lastUpdateMs         = 0;

	// 차트 버퍼(최근 N초)
	static std::deque<ST_S10_ChartEntry_t> s_chart;
	static unsigned long s_lastChartMs;

public:
	// --------------------------------------------------
	// 초기화
	// --------------------------------------------------
	void begin(CL_P10_PWM& p_pwm, const ST_A10_ResolvedWind_t* p_resolved = nullptr) {
		pwm = &p_pwm;
		if (p_resolved) {
			applyResolvedWind(*p_resolved);
		} else {
			memset(&resolved, 0, sizeof(resolved));
			// 안전 기본값
			resolved.wind_intensity            = 60.0f;
			resolved.wind_variability          = 50.0f;
			resolved.gust_frequency            = 40.0f;
			resolved.fan_limit                 = 90.0f;
			resolved.min_fan                   = 10.0f;
			resolved.turbulence_length_scale   = 40.0f;
			resolved.turbulence_intensity_sigma= 0.50f;
			resolved.thermal_bubble_strength   = 1.80f;
			resolved.thermal_bubble_radius     = 18.0f;
			strlcpy(presetCode, "OCEAN", sizeof(presetCode));
			strlcpy(styleCode , "BALANCE", sizeof(styleCode));
		}
		active = true;
		CL_D10_Logger::log(EN_L10_LOG_INFO, "[S10] begin");
	}

	// --------------------------------------------------
	// 정지
	// --------------------------------------------------
	void stop() {
		active = false;
		phase  = EN_S10_PHASE_CALM;
		targetWind  = 0.0f;
		currentWind = 0.0f;
		if (pwm) pwm->P10_setDutyPercent(0.0f);
	}

	// --------------------------------------------------
	// 파라미터 적용 (CT10에서 해석 완료 전달)
	// --------------------------------------------------
	void applyResolvedWind(const ST_A10_ResolvedWind_t& p_resolved,
	                       const char* p_presetCode = nullptr,
	                       const char* p_styleCode  = nullptr) {
		resolved = p_resolved;
		if (p_presetCode) strlcpy(presetCode, p_presetCode, sizeof(presetCode));
		if (p_styleCode)  strlcpy(styleCode , p_styleCode , sizeof(styleCode));

		// 페이즈 리셋
		active       = true;
		gustActive   = false;
		thermalActive= false;
		gustGain     = 1.0f;

		// 프리셋 영역에서 기본 바람 범위를 추정(대략적 스케일)
		float baseMin = 0.8f + 0.02f * resolved.wind_intensity;   // (예: 0.8 ~ 2.8)
		float baseMax = 4.0f + 0.06f * resolved.wind_intensity;   // (예: 4.0 ~ 9.6)
		baseMin = S10_clampf(baseMin, 0.3f, 6.0f);
		baseMax = S10_clampf(baseMax, 1.2f, 11.0f);

		// 페이즈 스팬에서 초기 구간 결정
		phase = EN_S10_PHASE_NORMAL;
		phaseStartSec = millis() / 1000.0f;
		phaseDurSec   = 120.0f;
		phaseMinWind  = baseMin + (baseMax - baseMin) * 0.15f;
		phaseMaxWind  = baseMin + (baseMax - baseMin) * 0.85f;

		// 난류 시정척도
		turbTimeScale = 5.0f;

		// 초기 타겟 & 현재치
		float mid = (phaseMinWind + phaseMaxWind) * 0.5f;
		currentWind = mid;
		targetWind  = mid;
		windMomentum = 0.0f;
		spectralBuf  = 0.0f;
		spectralPhaseAcc = 0.0f;

		_generateNextTarget();
	}

	// --------------------------------------------------
	// 주기 갱신
	// --------------------------------------------------
	void tick() {
		if (!active) return;
		unsigned long v_now = millis();

		// 40~99ms 사이의 불규칙 지터 기반 주기
		static uint32_t s_j = 0;
		uint32_t v_interval = 40 + (s_j % 60);
		if (v_now - lastUpdateMs < v_interval) {
			yield();
			return;
		}
		s_j = esp_random();

		float v_dt = (v_now - lastUpdateMs) / 1000.0f;
		lastUpdateMs = v_now;

		_updatePhase();
		_calcTurbulence(v_dt);
		_calcThermalEnvelope();
		_updateGust();
		_updateThermal();

		// 목표치 접근(관성 포함)
		float v_diff   = targetWind - currentWind;
		float v_speed  = 0.12f; // 기본 접근율
		// 변동성이 높을수록 천천히 따라가게 약간 보정
		v_speed *= (1.0f - 0.003f * S10_clampf(resolved.wind_variability, 0.0f, 100.0f));

		float v_change = v_diff * v_speed * v_dt;
		windMomentum = windMomentum * 0.86f + v_change * 0.14f;
		windMomentum = S10_clampf(windMomentum, -0.6f, 0.6f);

		float v_new = currentWind + windMomentum + spectralBuf;
		v_new = S10_clampf(v_new, 0.2f, 11.0f);
		currentWind = v_new;

		// 타겟 갱신 빈도: 근접/원거리 조건에 따라 확률적으로
		float v_changeTh = 0.5f + (currentWind / 20.0f);
		float v_pClose   = 28.0f;
		float v_pFar     = 7.0f;
		if (resolved.wind_variability > 70.0f) { v_pClose *= 1.4f; v_pFar *= 1.4f; }
		if (fabsf(v_diff) < v_changeTh) {
			if (A10_getRandom01() * 100.0f < v_pClose) _generateNextTarget();
		} else {
			if (A10_getRandom01() * 100.0f < v_pFar)   _generateNextTarget();
		}

		// PWM 반영
		_applyFan(_computeDutyFromWind());

		// 차트 로그(1s 주기)
		if (millis() - s_lastChartMs > 1000UL) {
			ST_S10_ChartEntry_t e{};
			e.t_ms       = millis();
			e.wind       = currentWind;
			e.target     = targetWind;
			e.pwm        = pwm ? pwm->P10_getDutyPercent() : 0.0f;
			e.intensity  = resolved.wind_intensity;
			e.variability= resolved.wind_variability;
			e.gust       = gustGain;
			e.thermal    = thermalContribution;
			e.phase      = (uint8_t)phase;
			strlcpy(e.presetCode, presetCode, sizeof(e.presetCode));
			strlcpy(e.styleCode , styleCode , sizeof(e.styleCode));
			s_chart.push_back(e);
			while (s_chart.size() > 180) s_chart.pop_front(); // 최근 3분
			s_lastChartMs = millis();
		}

		yield();
	}

	// --------------------------------------------------
	// JSON Export
	// --------------------------------------------------
	void toJson(JsonDocument& p_doc) {
		JsonObject o = p_doc["sim"].to<JsonObject>();
		o["active"]     = active;
		o["phase"]      = (phase == EN_S10_PHASE_CALM ? "CALM" :
		                  (phase == EN_S10_PHASE_NORMAL ? "NORMAL" : "STRONG"));
		o["wind"]       = currentWind;
		o["target"]     = targetWind;
		o["gustActive"] = gustActive;
		o["thermalActive"] = thermalActive;
		o["pwm"]        = pwm ? pwm->P10_getDutyPercent() : 0.0f;

		o["presetCode"] = presetCode;
		o["styleCode"]  = styleCode;

		JsonObject r = o["resolved"].to<JsonObject>();
		r["wind_intensity"]             = resolved.wind_intensity;
		r["wind_variability"]           = resolved.wind_variability;
		r["gust_frequency"]             = resolved.gust_frequency;
		r["fan_limit"]                  = resolved.fan_limit;
		r["min_fan"]                    = resolved.min_fan;
		r["turbulence_length_scale"]    = resolved.turbulence_length_scale;
		r["turbulence_intensity_sigma"] = resolved.turbulence_intensity_sigma;
		r["thermal_bubble_strength"]    = resolved.thermal_bubble_strength;
		r["thermal_bubble_radius"]      = resolved.thermal_bubble_radius;
	}

	void toChartJson(JsonDocument& p_doc) {
		JsonArray arr = p_doc["chart"].to<JsonArray>();
		for (const auto& e : s_chart) {
			JsonObject jo = arr.add<JsonObject>();
			jo["t"] = (uint32_t)e.t_ms;
			jo["w"] = e.wind;
			jo["u"] = e.pwm;
			jo["tg"]= e.target;
			jo["g"] = e.gust;
			jo["h"] = e.thermal;
			jo["ph"]= e.phase;
		}
	}

	// --------------------------------------------------
	// 제어 관련 유틸
	// --------------------------------------------------
	void setPwmDriver(CL_P10_PWM* p_driver) { pwm = p_driver; }

	void setFanPowerEnabled(bool p_enabled) {
		fanPowerEnabled = p_enabled;
		if (!fanPowerEnabled && pwm) pwm->P10_setDutyPercent(0.0f);
	}

private:
	CL_P10_PWM* pwm = nullptr;

	// --------------------------------------------------
	// 난류 계산 (Von Kármán 근사 스펙트럼-합성)
	// --------------------------------------------------
	void _calcTurbulence(float p_dt) {
		float v_L     = S10_clampf(resolved.turbulence_length_scale, 5.0f, 80.0f);
		float v_sigma = S10_clampf(resolved.turbulence_intensity_sigma, 0.05f, 1.2f);
		float v_U     = S10_clampf(currentWind, 0.1f, 11.0f);

		float v_sum = 0.0f;
		for (int v_i = 1; v_i <= 12; ++v_i) {
			float n     = v_i * 0.1f;
			float f     = n * v_U / v_L;
			float fLU   = f * v_L / v_U;
			float term  = 70.8f * fLU * fLU;
			float numer = 4.0f * v_sigma * v_sigma * (v_L / v_U) * (1.0f + term);
			float denom = powf(1.0f + term, 5.0f / 6.0f);
			float S     = numer / denom;                 // 파워 스펙트럼 밀도
			float phase_rate = 2.0f * M_PI * f;
			float phase_inc  = phase_rate * p_dt;
			float phase      = spectralPhaseAcc * v_i + phase_inc + A10_randRange(-0.12f, 0.12f);
			float amp        = sqrtf(2.0f * S * 0.083f); // Δf ≈ 0.083 근사
			v_sum += amp * sinf(phase);
		}

		spectralPhaseAcc += p_dt * 0.55f;
		if (spectralPhaseAcc > 2.0f * M_PI) spectralPhaseAcc -= 2.0f * M_PI;

		float corr = expf(-p_dt / S10_clampf(turbTimeScale, 1.0f, 12.0f));
		spectralBuf = spectralBuf * corr + v_sum * (1.0f - corr);
	}

	// --------------------------------------------------
	// 열기포 포락선
	// --------------------------------------------------
	void _calcThermalEnvelope() {
		if (!active || !thermalActive) return;

		float v_now = millis() / 1000.0f;
		float v_age = v_now - thermalStartSec;
		if (v_age >= thermalDurSec) { thermalActive = false; return; }

		float v_prog = v_age / thermalDurSec;
		float v_env = 0.0f;
		if (v_prog < 0.2f) {
			// Attack
			v_env = 1.0f - powf(1.0f - v_prog / 0.2f, 2.0f);
		} else if (v_prog < 0.6f) {
			// Sustain(약간의 미세 진동)
			v_env  = 1.0f;
			v_env += sinf(v_age * (0.9f + (float)phase * 0.25f) * 2.0f * M_PI) * 0.14f;
		} else {
			// Release
			float v_d = (v_prog - 0.6f) / 0.4f;
			v_env = 1.0f - powf(S10_clampf(v_d,0.0f,1.0f), 1.35f);
		}

		float v_strength = S10_clampf(resolved.thermal_bubble_strength, 0.5f, 3.0f);
		thermalContribution = (v_strength - 1.0f) * v_env;
	}

	// --------------------------------------------------
	// 돌풍 상태 갱신
	// --------------------------------------------------
	void _updateGust() {
		float v_nowSec = millis() / 1000.0f;

		// 진행중이면 포락선만 업데이트
		if (gustActive) {
			float v_age = v_nowSec - gustStartSec;
			if (v_age >= gustDurSec) { gustActive = false; gustGain = 1.0f; return; }

			float v_prog = v_age / gustDurSec;
			float v_env;
			if (v_prog < 0.25f) {
				v_env = 1.0f - powf(1.0f - v_prog / 0.25f, 1.8f);
			} else if (v_prog < 0.65f) {
				v_env  = 1.0f;
				v_env += sinf(v_age * (1.4f + (float)phase * 0.5f)) * 0.08f;
			} else {
				v_env = 1.0f - powf(S10_clampf((v_prog - 0.65f) / 0.35f,0.0f,1.0f), 1.5f);
			}
			float v_maxGain = 1.2f + 1.2f * (resolved.wind_variability / 100.0f); // 강한 변동일수록 상한 ↑
			gustGain = S10_clampf(1.0f + (v_maxGain - 1.0f) * v_env, 1.0f, v_maxGain);
			return;
		}

		// 트리거 확률 검사(0.5s 주기)
		if (millis() - lastGustCheckMs < 500UL) return;
		lastGustCheckMs = millis();

		float v_base = S10_clampf(resolved.gust_frequency / 100.0f, 0.0f, 2.0f); // 0.0~2.0
		float v_wfac = 1.0f + (currentWind / 8.5f) * 0.5f;
		float v_pmul = (phase == EN_S10_PHASE_CALM ? 0.35f * v_wfac
		              : phase == EN_S10_PHASE_STRONG ? 2.1f * v_wfac
		              : 1.0f * v_wfac);
		float v_p = v_base * v_pmul * 0.04f; // 최종 확률 스케일

		if (A10_getRandom01() < v_p) {
			gustActive  = true;
			gustStartSec= v_nowSec;
			float v_speedF = currentWind / 7.0f;
			if (phase == EN_S10_PHASE_CALM) {
				gustDurSec = A10_randRange(3.0f, 8.0f);
			} else if (phase == EN_S10_PHASE_STRONG) {
				gustDurSec = A10_randRange(0.8f, 3.0f);
			} else {
				gustDurSec = A10_randRange(1.8f, 5.8f);
			}
			float v_max = 1.2f + 1.0f * (1.0f + v_speedF * 0.3f);
			gustGain = S10_clampf(A10_randRange(1.05f, v_max), 1.0f, v_max);
		}
	}

	// --------------------------------------------------
	// 열기포 트리거 갱신
	// --------------------------------------------------
	void _updateThermal() {
		if (!active || thermalActive) return;
		if (millis() - lastThermCheckMs < 700UL) return;
		lastThermCheckMs = millis();

		float v_strength = S10_clampf(resolved.thermal_bubble_strength, 0.5f, 3.0f);
		float v_wfac = 1.0f + (currentWind / 8.0f) * 0.3f;
		float v_pmul = (phase == EN_S10_PHASE_CALM ? 1.2f :
		                phase == EN_S10_PHASE_STRONG ? 0.75f : 1.0f);
		// 기본 발생 빈도 근사 (스타일/프리셋에 따라 C10 해석에서 이미 조정됨)
		float v_freq = 0.022f * v_pmul * v_wfac * (0.6f + 0.4f * S10_clampf(v_strength,0.5f,3.0f));
		if (A10_getRandom01() < v_freq) {
			thermalActive   = true;
			thermalStartSec = millis() / 1000.0f;
			float v_d = A10_randRange(8.0f, 14.0f);
			if      (phase == EN_S10_PHASE_CALM)   v_d *= 1.25f;
			else if (phase == EN_S10_PHASE_STRONG) v_d *= 0.85f;
			thermalDurSec = v_d;
		}
	}

	// --------------------------------------------------
	// 페이즈 전환
	// --------------------------------------------------
	void _updatePhase() {
		if (!active) return;
		float v_now = millis() / 1000.0f;
		if (v_now - phaseStartSec < phaseDurSec) return;

		EN_S10_phase_t v_old = phase;
		float v_r = A10_getRandom01();
		if (v_old == EN_S10_PHASE_CALM) {
			phase = (v_r < 0.72f) ? EN_S10_PHASE_NORMAL : EN_S10_PHASE_STRONG;
		} else if (v_old == EN_S10_PHASE_STRONG) {
			phase = (v_r < 0.70f) ? EN_S10_PHASE_NORMAL : EN_S10_PHASE_CALM;
		} else {
			if      (v_r < 0.40f) phase = EN_S10_PHASE_CALM;
			else if (v_r < 0.82f) phase = EN_S10_PHASE_NORMAL;
			else                  phase = EN_S10_PHASE_STRONG;
		}

		phaseStartSec = v_now;
		// 기본 범위 추정 재계산
		float baseMin = 0.8f + 0.02f * resolved.wind_intensity;
		float baseMax = 4.0f + 0.06f * resolved.wind_intensity;
		baseMin = S10_clampf(baseMin, 0.3f, 6.0f);
		baseMax = S10_clampf(baseMax, 1.2f, 11.0f);
		float span = baseMax - baseMin;

		if (phase == EN_S10_PHASE_CALM) {
			phaseDurSec = A10_randRange(90.0f, 210.0f);
			phaseMinWind = baseMin;
			phaseMaxWind = baseMin + span * 0.60f;
		} else if (phase == EN_S10_PHASE_NORMAL) {
			phaseDurSec = A10_randRange(120.0f, 300.0f);
			phaseMinWind = baseMin + span * 0.15f;
			phaseMaxWind = baseMin + span * 0.85f;
		} else { // STRONG
			phaseDurSec = A10_randRange(60.0f, 160.0f);
			phaseMinWind = baseMin + span * 0.40f;
			phaseMaxWind = baseMax;
		}
		phaseMinWind = S10_clampf(phaseMinWind, 0.2f, 11.0f);
		phaseMaxWind = S10_clampf(phaseMaxWind, 0.8f, 11.0f);

		_generateNextTarget();
	}

	// --------------------------------------------------
	// 다음 목표 풍속 생성
	// --------------------------------------------------
	void _generateNextTarget() {
		if (!active) return;
		float v_range = phaseMaxWind - phaseMinWind;
		if (v_range < 0.2f) v_range = 0.2f;

		float v_w   = phaseMinWind + A10_getRandom01() * v_range;
		float v_mid = (phaseMinWind + phaseMaxWind) * 0.5f;
		float v_bias= A10_randRange(0.0f, 1.0f);
		v_w = (v_w + v_mid * v_bias) / (1.0f + v_bias);

		targetWind = v_w;

		// 변화율은 변동성, 길이척도에 영향을 받음
		float v_var = S10_clampf(resolved.wind_variability, 0.0f, 100.0f) / 100.0f;
		float v_U   = S10_clampf(currentWind, 0.1f, 11.0f);
		float v_ts  = resolved.turbulence_length_scale / v_U;
		(void)v_ts; // 현재는 내부 조정에 사용하지 않음(상단 접근율에서 반영)

		// (추가적인 개별 접근율 조정이 필요하면 여기에서 계산 가능)
	}

	// --------------------------------------------------
	// 현재 풍속 → PWM 듀티 변환
	// --------------------------------------------------
	float _computeDutyFromWind() const {
		// 기본 스케일: 내부 풍속 → [%]
		float v_pct = currentWind * 10.0f + 10.0f;

		// 돌풍/열기포 영향
		v_pct *= gustGain;                     // 1.0 이상
		v_pct += thermalContribution * 5.0f;   // 열기포는 부가 가중치

		// intensity 반영
		float v_int = S10_clampf(resolved.wind_intensity, 0.0f, 100.0f) / 100.0f;
		v_pct *= v_int;

		// min_fan/limit 클램프
		float v_min = S10_clampf(resolved.min_fan, 0.0f, 100.0f);
		float v_max = S10_clampf(resolved.fan_limit, 0.0f, 100.0f);
		if (v_max < v_min) v_max = v_min;
		v_pct = S10_clampf(v_pct, v_min, v_max);

		// 전원 게이트
		if (!fanPowerEnabled) v_pct = 0.0f;

		return v_pct;
	}

	// --------------------------------------------------
	// PWM 적용
	// --------------------------------------------------
	void _applyFan(float p_pct) {
		if (!pwm) return;
		pwm->P10_setDutyPercent(S10_clampf(p_pct, 0.0f, 100.0f));
	}
};

// ------------------------------------------------------
// 정적 멤버 정의
// ------------------------------------------------------
std::deque<ST_S10_ChartEntry_t> CL_S10_Simulation::s_chart;
unsigned long                    CL_S10_Simulation::s_lastChartMs = 0;
