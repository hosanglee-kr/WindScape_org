#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : C10_ConfigManager_014.h
 * 모듈약어 : C10
 * 모듈명 : Smart Nature Wind Configuration Manager (v014)
 * ------------------------------------------------------
 * 기능 요약
 *  - Schedules / UserProfiles / WindProfile JSON 로드
 *  - 프리셋/스타일 사전 로딩 및 조회 (코드 기반)
 *  - preset × style × adjust → 시뮬레이션 파라미터 해석
 *  - .bak 백업/복구, 안전 저장 API (필요 시)
 *  - NVS 연동 (현재 모드/프로파일/세그먼트/프리셋/스타일 상태 저장)
 * ------------------------------------------------------
 * [구현 규칙]
 *  - ArduinoJson v7.x 사용 (v6 이하 금지)
 *  - JsonDocument 단일 타입만 사용
 *  - createNestedArray/Object/containsKey 사용 금지
 *  - memset + strlcpy 기반 안전 초기화
 *  - 주석/필드명은 JSON 구조와 동일하게 유지
 *  - 모듈별 단일 헤더(h) 파일로만 구성 (cpp 없음)
 * ------------------------------------------------------
 * [코드 네이밍 규칙]
 *   - 전역 상수,매크로      : G_모듈약어_
 *   - 전역 변수             : g_모듈약어_
 *   - 전역 함수             : 모듈약어_
 *   - type                  : T_모듈약어_
 *   - typedef               : _t
 *   - enum 상수             : EN_모듈약어_
 *   - 구조체                : ST_모듈약어_
 *   - 클래스명              : CL_모듈약어_
 *   - 클래스 private 멤버   : _
 *   - 클래스 멤버(함수/변수) : 모듈약어 접두사 미사용
 * ------------------------------------------------------
 */

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

#include "A10_Const_014.h"
#include "N10_NvsManager_001.h"   // 별도 제공된 NVS 매니저
// 외부 인스턴스 (다른 소스에서 정의)
extern CL_N10_NvsManager g_N10_nvs;

/* ======================================================
 * 전역 Config Root 정의
 * ====================================================== */
ST_A10_ConfigRoot_t g_A10_config_root;

/* ======================================================
 * 내부 헬퍼
 * ====================================================== */
static bool C10_readJsonFile(const char* path, JsonDocument& doc) {
  if (!LittleFS.exists(path)) return false;
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  auto e = deserializeJson(doc, f);
  f.close();
  return !e;
}

static bool C10_writeJsonFile(const char* path, const JsonDocument& doc, const char* bakPath=nullptr) {
  if (bakPath && LittleFS.exists(path)) {
    LittleFS.remove(bakPath);
    LittleFS.rename(path, bakPath);
  }
  File f = LittleFS.open(path, "w");
  if (!f) return false;
  serializeJsonPretty(doc, f);
  f.close();
  return true;
}

/* ======================================================
 * windDict 탐색 구현
 * ====================================================== */
int16_t A10_findPresetIndexByCode(const ST_A10_WindProfileDict_t& dict, const char* code) {
  if (!code) return -1;
  for (uint8_t i=0;i<dict.preset_count;i++) {
    if (strcmp(dict.presets[i].code, code)==0) return (int16_t)i;
  }
  return -1;
}
int16_t A10_findStyleIndexByCode(const ST_A10_WindProfileDict_t& dict, const char* code) {
  if (!code) return -1;
  for (uint8_t i=0;i<dict.style_count;i++) {
    if (strcmp(dict.styles[i].code, code)==0) return (int16_t)i;
  }
  return -1;
}

/* ======================================================
 * preset × style × adjust → ResolvedWind 구현
 * ====================================================== */
bool A10_resolveWindParams(const ST_A10_WindProfileDict_t& dict,
                           const char* presetCode,
                           const char* styleCode,
                           const ST_A10_AdjustDelta_t* adj,
                           ST_A10_ResolvedWind_t& outResolved)
{
  int16_t pi = A10_findPresetIndexByCode(dict, presetCode);
  if (pi < 0) return false;
  int16_t si = A10_findStyleIndexByCode(dict, styleCode);
  if (si < 0) return false;

  const auto& base = dict.presets[pi].base;
  const auto& fac  = dict.styles[si].factors;

  A10_safe_strlcpy(outResolved.presetCode, presetCode, sizeof(outResolved.presetCode));
  A10_safe_strlcpy(outResolved.styleCode,  styleCode,  sizeof(outResolved.styleCode));

  // 1) base × factor
  outResolved.wind_intensity            = base.wind_intensity * fac.intensity_factor;
  outResolved.wind_variability          = base.wind_variability * fac.variability_factor;
  outResolved.gust_frequency            = base.gust_frequency * fac.gust_factor;
  outResolved.thermal_bubble_strength   = base.thermal_bubble_strength * fac.thermal_factor;

  // factor 비적용(절대치 유지) 항목
  outResolved.fan_limit                 = base.fan_limit;
  outResolved.min_fan                   = base.min_fan;
  outResolved.turbulence_length_scale   = base.turbulence_length_scale;
  outResolved.turbulence_intensity_sigma= base.turbulence_intensity_sigma;
  outResolved.thermal_bubble_radius     = base.thermal_bubble_radius;

  // 2) + adjust delta (있으면)
  if (adj) {
    outResolved.wind_intensity             += adj->wind_intensity;
    outResolved.gust_frequency             += adj->gust_frequency;
    outResolved.wind_variability           += adj->wind_variability;
    outResolved.fan_limit                  += adj->fan_limit;
    outResolved.min_fan                    += adj->min_fan;
    outResolved.turbulence_length_scale    += adj->turbulence_length_scale;
    outResolved.turbulence_intensity_sigma += adj->turbulence_intensity_sigma;
  }

  // 3) clamp
  outResolved.wind_intensity   = A10_clampf(outResolved.wind_intensity,   0.0f, 100.0f);
  outResolved.wind_variability = A10_clampf(outResolved.wind_variability, 0.0f, 100.0f);
  outResolved.gust_frequency   = A10_clampf(outResolved.gust_frequency,   0.0f, 100.0f);
  outResolved.fan_limit        = A10_clampf(outResolved.fan_limit,        0.0f, 100.0f);
  outResolved.min_fan          = A10_clampf(outResolved.min_fan,          0.0f, 100.0f);
  // 나머지는 물리적 범위를 따로 적용 (여기선 보수적으로 유지)
  outResolved.turbulence_intensity_sigma = A10_clampf(outResolved.turbulence_intensity_sigma, 0.0f, 5.0f);
  outResolved.thermal_bubble_strength    = A10_clampf(outResolved.thermal_bubble_strength,    0.5f, 3.5f);

  return true;
}

/* ======================================================
 * Config Manager
 * ====================================================== */
class CL_C10_ConfigManager {
public:
  // 로드 일괄
  static bool loadAll() {
    bool ok = true;
    ok &= loadWindProfileDict();
    ok &= loadSchedules();
    ok &= loadUserProfiles();
    return ok;
  }

  // WindProfile 사전 로드
  static bool loadWindProfileDict() {
    JsonDocument d;
    if (!C10_readJsonFile(A10_Const::WIND_PROFILE_FILE, d)) return false;

    auto root = d["windProfile"];
    if (!root.is<JsonObjectConst>()) return false;

    // presets
    g_A10_config_root.windDict.preset_count = 0;
    if (root["presets"].is<JsonArrayConst>()) {
      JsonArrayConst arr = root["presets"].as<JsonArrayConst>();
      for (JsonObjectConst jp : arr) {
        if (g_A10_config_root.windDict.preset_count >= 16) break;
        auto& dst = g_A10_config_root.windDict.presets[g_A10_config_root.windDict.preset_count++];

        A10_safe_strlcpy(dst.name, jp["name"] | "", sizeof(dst.name));
        A10_safe_strlcpy(dst.code, jp["code"] | "", sizeof(dst.code));

        dst.base.wind_intensity            = jp["base"]["wind_intensity"] | 70.0f;
        dst.base.gust_frequency            = jp["base"]["gust_frequency"] | 50.0f;
        dst.base.wind_variability          = jp["base"]["wind_variability"] | 50.0f;
        dst.base.fan_limit                 = jp["base"]["fan_limit"] | 95.0f;
        dst.base.min_fan                   = jp["base"]["min_fan"] | 10.0f;
        dst.base.turbulence_length_scale   = jp["base"]["turbulence_length_scale"] | 40.0f;
        dst.base.turbulence_intensity_sigma= jp["base"]["turbulence_intensity_sigma"] | 0.5f;
        dst.base.thermal_bubble_strength   = jp["base"]["thermal_bubble_strength"] | 2.0f;
        dst.base.thermal_bubble_radius     = jp["base"]["thermal_bubble_radius"] | 18.0f;
      }
    }

    // styles
    g_A10_config_root.windDict.style_count = 0;
    if (root["styles"].is<JsonArrayConst>()) {
      JsonArrayConst arr = root["styles"].as<JsonArrayConst>();
      for (JsonObjectConst js : arr) {
        if (g_A10_config_root.windDict.style_count >= 16) break;
        auto& dst = g_A10_config_root.windDict.styles[g_A10_config_root.windDict.style_count++];

        A10_safe_strlcpy(dst.name, js["name"] | "", sizeof(dst.name));
        A10_safe_strlcpy(dst.code, js["code"] | "", sizeof(dst.code));

        dst.factors.intensity_factor   = js["factors"]["intensity_factor"] | 1.0f;
        dst.factors.variability_factor = js["factors"]["variability_factor"] | 1.0f;
        dst.factors.gust_factor        = js["factors"]["gust_factor"] | 1.0f;
        dst.factors.thermal_factor     = js["factors"]["thermal_factor"] | 1.0f;
      }
    }
    return true;
  }

  // Schedules 로드
  static bool loadSchedules() {
    JsonDocument d;
    if (!C10_readJsonFile(A10_Const::SCHEDULES_FILE, d)) return false;

    auto arr = d["schedules"];
    if (!arr.is<JsonArrayConst>()) { g_A10_config_root.schedules.count=0; return true; }

    g_A10_config_root.schedules.count = 0;
    for (JsonObjectConst js : arr.as<JsonArrayConst>()) {
      if (g_A10_config_root.schedules.count >= A10_Const::MAX_SCHEDULES) break;
      auto& dst = g_A10_config_root.schedules.items[g_A10_config_root.schedules.count++];

      dst.schNo   = js["schNo"] | 0;
      A10_safe_strlcpy(dst.name, js["name"] | "", sizeof(dst.name));
      dst.enabled = js["enabled"] | true;

      // period
      dst.period.enabled = js["period"]["enabled"] | false;
      if (dst.period.enabled) {
        // days
        for (uint8_t i=0;i<7;i++) {
          if (js["period"]["days"][i].isNull()) dst.period.days[i] = 1;
          else dst.period.days[i] = (uint8_t)(js["period"]["days"][i] | 1);
        }
        A10_safe_strlcpy(dst.period.start_time, js["period"]["start_time"] | "00:00", sizeof(dst.period.start_time));
        A10_safe_strlcpy(dst.period.end_time,   js["period"]["end_time"]   | "23:59", sizeof(dst.period.end_time));
      }

      // segments
      dst.seg_count = 0;
      if (js["segments"].is<JsonArrayConst>()) {
        for (JsonObjectConst jseg : js["segments"].as<JsonArrayConst>()) {
          if (dst.seg_count >= A10_Const::MAX_SEGMENTS_PER_SCHEDULE) break;
          auto& sg = dst.segments[dst.seg_count++];

          sg.segNo       = jseg["segNo"] | 0;
          sg.on_minutes  = jseg["on_minutes"] | 0;
          sg.off_minutes = jseg["off_minutes"] | 0;

          const char* mode = jseg["mode"] | "PRESET";
          sg.mode = (strcmp(mode,"FIXED")==0) ? EN_A10_SEG_MODE_FIXED : EN_A10_SEG_MODE_PRESET;

          if (sg.mode == EN_A10_SEG_MODE_PRESET) {
            A10_safe_strlcpy(sg.presetCode, jseg["presetCode"] | "OCEAN", sizeof(sg.presetCode));
            A10_safe_strlcpy(sg.styleCode,  jseg["styleCode"]  | "BALANCE", sizeof(sg.styleCode));
            sg.adjust.wind_intensity             = jseg["adjust"]["wind_intensity"] | 0.0f;
            sg.adjust.wind_variability           = jseg["adjust"]["wind_variability"] | 0.0f;
            sg.adjust.gust_frequency             = jseg["adjust"]["gust_frequency"] | 0.0f;
            sg.adjust.fan_limit                  = jseg["adjust"]["fan_limit"] | 0.0f;
            sg.adjust.min_fan                    = jseg["adjust"]["min_fan"] | 0.0f;
            sg.adjust.turbulence_length_scale    = jseg["adjust"]["turbulence_length_scale"] | 0.0f;
            sg.adjust.turbulence_intensity_sigma = jseg["adjust"]["turbulence_intensity_sigma"] | 0.0f;
          } else {
            sg.fixed_speed = jseg["fixed_speed"] | 0.0f;
          }
        }
      }

      // autoOffTimer(통일 구조)
      dst.autoOff.timer.enabled   = js["autoOffTimer"]["timer"]["enabled"] | (bool)(js["autoOffTimer"]["enabled"] | false);
      dst.autoOff.timer.minutes   = js["autoOffTimer"]["timer"]["minutes"] | (uint32_t)(js["autoOffTimer"]["minutes"] | 0);
      dst.autoOff.offTime.enabled = js["autoOffTimer"]["offTime"]["enabled"] | false;
      A10_safe_strlcpy(dst.autoOff.offTime.time, js["autoOffTimer"]["offTime"]["time"] | "", sizeof(dst.autoOff.offTime.time));
      dst.autoOff.offTemp.enabled = js["autoOffTimer"]["offTemp"]["enabled"] | false;
      dst.autoOff.offTemp.temp    = js["autoOffTimer"]["offTemp"]["temp"] | 0.0f;

      // motion
      dst.motion.pir.enabled      = js["motion
