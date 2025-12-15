/*
 * ------------------------------------------------------
 * 소스명 : S10_Simul_IO_021.cpp
 * 모듈약어 : S10
 * 모듈명 : Smart Nature Wind 풍속 시뮬레이션 Manager (v019, Full)
 * ------------------------------------------------------
 * 기능 요약:
 * - CL_S10_Simulation 클래스의 구현부
 * - Von Kármán 난류 모델, 관성, 돌풍/열기포 확률 모델 구현
 * ------------------------------------------------------
 */

#include "S10_Simulation_020.h" // 해당 클래스 헤더 파일 포함

// 외부 종속성 헤더 포함 (외부에서 제공되어야 함: 시스템 상수, 설정, 로그, PWM 제어)
#include "A10_Const_016.h"
#include "C10_Config_029.h"
#include "D10_Logger_016.h"
#include "P10_PWM_ctrl_014.h"


// ------------------------------------------------------
// 정적 멤버 정의 (클래스 인스턴스와 무관하게 유지되는 공유 데이터)
// ------------------------------------------------------
// 차트 데이터를 저장하는 순환 버퍼 (최대 120개 샘플 = 2분 분량)
std::deque<CL_S10_Simulation::ST_ChartEntry> CL_S10_Simulation::s_chartBuffer;
// 차트 로그를 기록한 마지막 시간 (Hz 제어용)
unsigned long                                CL_S10_Simulation::s_lastChartLogMs    = 0;
// 차트 JSON을 웹으로 전송한 마지막 시간 (API 부하 제어용)
unsigned long                                CL_S10_Simulation::s_lastChartSampleMs = 0;


// ==================================================
// JSON Export (현재 시뮬레이션 상태)
// ==================================================
/**
 * @brief 현재 시뮬레이션 상태 변수들을 JSON Object에 직렬화합니다.
 */

void CL_S10_Simulation::toJson(JsonDocument& p_doc) {
    portENTER_CRITICAL(&_simMutex); // 상태 읽기 중 변수 변경 방지

    JsonObject v_objSim = p_doc["sim"].to<JsonObject>();
    
    v_objSim["active"]        = active;
    v_objSim["phase"]         = g_A10_WEATHER_PHASE_NAMES_Arr[(uint8_t)phase];
    v_objSim["windSpeed"]     = currentWindSpeed;
    v_objSim["targetWind"]    = targetWindSpeed;
    v_objSim["gustActive"]    = gustActive;
    v_objSim["thermalActive"] = thermalActive;
    v_objSim["pwmDuty"]       = _pwm ? _pwm->P10_getDutyPercent() : 0.0f;
    v_objSim["presetCode"]    = presetCode;
    v_objSim["styleCode"]     = styleCode;
    v_objSim["intensity"]     = userIntensity;
    v_objSim["variability"]   = userVariability;
    v_objSim["gustFreq"]      = userGustFreq;
    v_objSim["fan_limit"]     = fanLimitPct;
    v_objSim["min_fan"]       = minFanPct;
    v_objSim["turbSigma"]     = turbSigma;
    v_objSim["turbScale"]     = turbLenScale;
    v_objSim["thermalPower"]  = thermalStrength;
    v_objSim["thermalRadius"] = thermalRadius;

    portEXIT_CRITICAL(&_simMutex);
}


// ==================================================
// 차트 데이터 JSON Export (/api/sim/chart)
// ==================================================
/**
 * @brief 차트 버퍼(s_chartBuffer)의 내용을 JSON Array로 직렬화합니다.
 * @param p_doc JSON 문서
 * @param p_diffOnly true인 경우, 마지막 1개 샘플만 전송 (WebSocket용)
 */
void CL_S10_Simulation::toChartJson(JsonDocument& p_doc, bool p_diffOnly) {
    // JSON 구조: p_doc["sim"]["chart"] 배열
    JsonArray arr = p_doc["sim"]["chart"].to<JsonArray>();

    // 메타 정보 추가 (차트의 현재 상태)
    JsonObject meta = p_doc["sim"]["meta"].to<JsonObject>();
    meta["phase"]   = g_A10_WEATHER_PHASE_NAMES_Arr[(uint8_t)phase];
    meta["avgWind"] = _getAvgWindFast();
    meta["gust"]    = gustActive;
    meta["thermal"] = thermalActive;
    meta["samples"] = historyCount;

    // WebAPI 부하 감소를 위해 Full Dump는 10초 간격으로 제한
    if (millis() - s_lastChartSampleMs < 10000UL)
        return;
    s_lastChartSampleMs = millis();

    if (s_chartBuffer.empty())
        return;

    if (p_diffOnly) {
        // diffOnly 모드: 마지막 1개 샘플만 전송 (WebSocket 실시간 업데이트용)
        const ST_ChartEntry& e  = s_chartBuffer.back();
        JsonObject           jo = arr.add<JsonObject>();
        jo["ts"]                = e.timestamp / 1000UL;
        jo["wind"]              = e.wind_speed;
        jo["pwm"]               = e.pwm_duty;
        jo["gust"]              = e.gust_active;
        jo["thermal"]           = e.thermal_active;
        jo["phase"]             = g_A10_WEATHER_PHASE_NAMES_Arr[(uint8_t)phase];
        jo["avgWind"]           = _getAvgWindFast();
        jo["samples"]           = historyCount;
        return;
    }

    // 기본 모드: 전체 chartBuffer 전송 (Full Dump)
    for (const auto& e : s_chartBuffer) {
        JsonObject jo = arr.add<JsonObject>();
        jo["ts"]      = e.timestamp / 1000UL;
        jo["wind"]    = e.wind_speed;
        jo["pwm"]     = e.pwm_duty;
        jo["gust"]    = e.gust_active;
        jo["thermal"] = e.thermal_active;
    }

    p_doc["sim"]["chartCount"] = (int)s_chartBuffer.size();
}


/**
 * @brief 주어진 JSON 문서로부터 시뮬레이션 설정(사용자 파라미터/물리 파라미터)을 패치(부분 업데이트)합니다.
 * * @param p_doc 시뮬레이션 설정 업데이트 데이터가 포함된 JsonDocument (ArduinoJson V7.x)
 * @return 설정이 변경되었으면 true, 아니면 false를 반환합니다.
 */
bool CL_S10_Simulation::patchFromJson(const JsonDocument& p_doc) {
    // 1. 뮤텍스 획득 (Critical Section 시작)
    // S10_Simulation_020.cpp의 tick() 함수와 동일하게 _simMutex 사용
    portENTER_CRITICAL(&_simMutex);

    bool v_changed = false;
    // 클라이언트가 전달하는 최상위 객체는 { "sim": { ... } } 형태이므로, "sim" 내부로 들어갑니다.
    JsonObjectConst j_sim = p_doc["sim"].as<JsonObjectConst>();
    if (j_sim.isNull()) {
        CL_D10_Logger::log(EN_L10_LOG_ERROR, "[S10] patchFromJson failed: 'sim' object not found in JSON.");
        portEXIT_CRITICAL(&_simMutex);
        return false;
    }

    // -------------------------------------------------------------------------
    // 2. 사용자 설정 (userIntensity, fanLimitPct 등) 패치
    // -------------------------------------------------------------------------
    
    // preset (문자열)
    if (j_sim["preset"].is<const char*>()) {
        const char* v_new = j_sim["preset"];
        if (strcasecmp(v_new, this->presetCode) != 0) {
            strlcpy(this->presetCode, v_new, sizeof(this->presetCode));
            // Preset이 변경되면 Core Wind Params를 즉시 재적용
            applyPresetCore(this->presetCode); 
            initPhaseFromBase(); // Phase도 재설정
            CL_D10_Logger::log(EN_L10_LOG_INFO, "[S10] Preset changed to %s", this->presetCode);
            v_changed = true;
        }
    }

    // intensity (userIntensity, 0.0f ~ 100.0f)
    if (j_sim["intensity"].is<float>()) {
        float v_new = constrain(j_sim["intensity"].as<float>(), 0.0f, 100.0f);
        if (v_new != this->userIntensity) {
            this->userIntensity = v_new;
            v_changed = true;
        }
    }
    
    // gust_freq (userGustFreq, 0.0f ~ 100.0f)
    if (j_sim["gust_freq"].is<float>()) {
        float v_new = constrain(j_sim["gust_freq"].as<float>(), 0.0f, 100.0f);
        if (v_new != this->userGustFreq) {
            this->userGustFreq = v_new;
            v_changed = true;
        }
    }
    
    // variability (userVariability, 0.0f ~ 100.0f)
    if (j_sim["variability"].is<float>()) {
        float v_new = constrain(j_sim["variability"].as<float>(), 0.0f, 100.0f);
        if (v_new != this->userVariability) {
            this->userVariability = v_new;
            // variability 변경 시 windChangeRate 즉시 갱신
            float v_varNorm = this->userVariability / 100.0f;
            this->windChangeRate = constrain(0.10f + v_varNorm * 0.20f, 0.06f, 0.34f);
            v_changed = true;
        }
    }
    
    // fan_limit (fanLimitPct, 0.0f ~ 100.0f)
    if (j_sim["fan_limit"].is<float>()) {
        float v_new = constrain(j_sim["fan_limit"].as<float>(), 0.0f, 100.0f);
        if (v_new != this->fanLimitPct) {
            this->fanLimitPct = v_new;
            v_changed = true;
        }
    }
    
    // min_fan (minFanPct, 0.0f ~ 100.0f)
    if (j_sim["min_fan"].is<float>()) {
        float v_new = constrain(j_sim["min_fan"].as<float>(), 0.0f, 100.0f);
        if (v_new != this->minFanPct) {
            this->minFanPct = v_new;
            v_changed = true;
        }
    }

    // -------------------------------------------------------------------------
    // 3. 물리 파라미터 (turbLenScale, thermalStrength 등) 패치
    // -------------------------------------------------------------------------

    // turb_len (turbLenScale, 난류 길이 스케일)
    if (j_sim["turb_len"].is<float>()) {
        float v_new = max(1.0f, j_sim["turb_len"].as<float>());
        if (v_new != this->turbLenScale) {
            this->turbLenScale = v_new;
            v_changed = true;
        }
    }

    // turb_sig (turbSigma, 난류 세기)
    if (j_sim["turb_sig"].is<float>()) {
        float v_new = max(0.0f, j_sim["turb_sig"].as<float>());
        if (v_new != this->turbSigma) {
            this->turbSigma = v_new;
            v_changed = true;
        }
    }

    // therm_str (thermalStrength, 열기포 강도)
    if (j_sim["therm_str"].is<float>()) {
        float v_new = max(1.0f, j_sim["therm_str"].as<float>());
        if (v_new != this->thermalStrength) {
            this->thermalStrength = v_new;
            v_changed = true;
        }
    }

    // therm_rad (thermalRadius, 열기포 반경)
    if (j_sim["therm_rad"].is<float>()) {
        float v_new = max(0.0f, j_sim["therm_rad"].as<float>());
        if (v_new != this->thermalRadius) {
            this->thermalRadius = v_new;
            v_changed = true;
        }
    }

    // -------------------------------------------------------------------------
    // 4. 변경 사항 처리 및 뮤텍스 반납
    // -------------------------------------------------------------------------
    
    if (v_changed) {
        CL_D10_Logger::log(EN_L10_LOG_INFO, "[S10] Simulation parameters patched. Intensity: %.1f, TurbSigma: %.2f", 
                           this->userIntensity, this->turbSigma);
        
        // 시뮬레이션 상태가 변경되었으므로, ConfigManager를 통해 Dirty 플래그 설정 필요
        // (W10_Web_Routes_027.cpp의 API 핸들러에서 이 함수 호출 후 Dirty 플래그를 설정하는 것이 더 일반적입니다.)
    }

    // 뮤텍스 반납 (Critical Section 종료)
    portEXIT_CRITICAL(&_simMutex); 
    
    return v_changed;
}



// --------------------------------------------------
// 최근 풍속 이력 관리 (순환 버퍼 기반)
// --------------------------------------------------
/**
 * @brief 현재 풍속을 순환 버퍼(history)에 저장하고 평균 풍속 캐시를 갱신합니다.
 */
void CL_S10_Simulation::_updateWindHistory(float p_speed) {
    history[historyIndex] = p_speed;
    historyIndex          = (historyIndex + 1) % HISTORY_SIZE; // 인덱스 순환
    if (historyCount < HISTORY_SIZE)
        historyCount++; // 카운트 증가

    // 평균 풍속 캐시 갱신
    float v_sum = 0.0f;
    for (uint8_t i = 0; i < historyCount; i++) v_sum += history[i];
    avgWindCached = v_sum / (float)historyCount;
}

// --------------------------------------------------
// 캐시된 평균 풍속 반환 (O(1))
// --------------------------------------------------
/**
 * @brief 캐시된 평균 풍속을 반환합니다. (O(1) 접근)
 */
float CL_S10_Simulation::_getAvgWindFast() const {
    return (historyCount > 0) ? avgWindCached : currentWindSpeed;
}

