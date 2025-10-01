// SC10_ConfigManager.h

#pragma once
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "SC10_Const.h"
#include "SC10_Logger.h"

// 설정 파일 로드/저장/백업/복구/리셋 담당
class ConfigManager {
public:
  // 설정 로드 (백업 자동 복구 지원)
  static bool load(WindConfig &p_cfg) {
    if (!LittleFS.begin(true)) {
      SC10_Logger::log(SC10_LOG_ERROR, "LittleFS mount failed");
      return false;
    }
    if (!LittleFS.exists(SC10_Const::CONFIG_FILE)) {
      SC10_Logger::log(SC10_LOG_WARN, "Config not found, using defaults");
      return false;
    }
    File f = LittleFS.open(SC10_Const::CONFIG_FILE, "r");
    if (!f) return false;
    JsonDocument doc;
    auto err = deserializeJson(doc, f);
    f.close();
    if (err) {
      SC10_Logger::log(SC10_LOG_ERROR, "Config parse failed: %s", err.c_str());
      return restoreBackup(p_cfg);
    }
    return parseJson(p_cfg, doc);
  }

  // 설정 저장 (백업 생성)
  static bool save(WindConfig &p_cfg) {
    // 백업
    if (LittleFS.exists(SC10_Const::CONFIG_FILE)) {
      LittleFS.remove(SC10_Const::BACKUP_FILE);
      LittleFS.rename(SC10_Const::CONFIG_FILE, SC10_Const::BACKUP_FILE);
    }
    JsonDocument doc; toJson(p_cfg, doc);
    File f = LittleFS.open(SC10_Const::CONFIG_FILE, "w");
    if (!f) return false;
    serializeJson(doc, f);
    f.close();
    SC10_Logger::log(SC10_LOG_INFO, "Config saved");
    return true;
  }

  // 공장 초기화
  static bool reset() {
    LittleFS.remove(SC10_Const::CONFIG_FILE);
    LittleFS.remove(SC10_Const::BACKUP_FILE);
    return true;
  }

  // 백업 복구
  static bool restoreBackup(WindConfig &p_cfg) {
    if (!LittleFS.exists(SC10_Const::BACKUP_FILE)) return false;
    File f = LittleFS.open(SC10_Const::BACKUP_FILE, "r");
    if (!f) return false;
    JsonDocument doc;
    auto err = deserializeJson(doc, f);
    f.close();
    if (err) return false;
    SC10_Logger::log(SC10_LOG_WARN, "Restored from backup");
    return parseJson(p_cfg, doc);
  }

  // /api/config POST 바디를 받아 cfg 갱신
  static bool patchFromJson(WindConfig &p_cfg, const JsonDocument &doc, bool &p_wifiChanged) {
    p_wifiChanged = false;
    JsonObject root = doc.as<JsonObject>();

    // 프리셋
    if (!root["preset"].isNull()) {
      const char* nm = root["preset"];
      for (int i=0;i<SC10_PRESET_COUNT;i++) {
        if (strcmp(nm, G_SC10_PRESET_MODE_NAMES[i])==0) {
          p_cfg.preset_mode_index = i;
          break;
        }
      }
    }

    // 시뮬레이션 파라미터
    if (!root["intensity"].isNull())   p_cfg.wind_intensity = root["intensity"];
    if (!root["gust_freq"].isNull())   p_cfg.gust_frequency = root["gust_freq"];
    if (!root["variability"].isNull()) p_cfg.wind_variability = root["variability"];
    if (!root["fan_limit"].isNull())   p_cfg.fan_speed_limit = root["fan_limit"];
    if (!root["min_fan"].isNull())     p_cfg.minimum_fan_speed = root["min_fan"];
    if (!root["turb_len"].isNull())    p_cfg.turbulence_length_scale = root["turb_len"];
    if (!root["turb_sig"].isNull())    p_cfg.turbulence_intensity_sigma = root["turb_sig"];
    if (!root["therm_str"].isNull())   p_cfg.thermal_bubble_strength = root["therm_str"];
    if (!root["therm_rad"].isNull())   p_cfg.thermal_bubble_radius = root["therm_rad"];

    // 타이밍
    if (!root["sim_int"].isNull())     p_cfg.wind_sim_interval_ms = root["sim_int"];
    if (!root["gust_int"].isNull())    p_cfg.gust_check_interval_ms = root["gust_int"];
    if (!root["thermal_int"].isNull()) p_cfg.thermal_check_interval_ms = root["thermal_int"];

    // Wi-Fi (변경되면 재초기화 필요)
    if (!root["wifi_mode"].isNull()) { p_cfg.wifi_mode = root["wifi_mode"]; p_wifiChanged = true; }
    if (!root["ap_ssid"].isNull())   { strlcpy(p_cfg.ap_ssid, root["ap_ssid"], sizeof(p_cfg.ap_ssid)); p_wifiChanged = true; }
    if (!root["ap_password"].isNull()){ strlcpy(p_cfg.ap_password, root["ap_password"], sizeof(p_cfg.ap_password)); p_wifiChanged = true; }

    if (root["sta_networks"].is<JsonArray>()) {
      JsonArray arr = root["sta_networks"].as<JsonArray>();
      p_cfg.sta_network_count = 0;
      for (JsonObject o : arr) {
        if (p_cfg.sta_network_count >= SC10_Const::MAX_STA_NETWORKS) break;
        strlcpy(p_cfg.sta_networks[p_cfg.sta_network_count].ssid, o["ssid"]|"", sizeof(SC10_StaCredential::ssid));
        strlcpy(p_cfg.sta_networks[p_cfg.sta_network_count].password, o["pass"]|"", sizeof(SC10_StaCredential::password));
        if (strlen(p_cfg.sta_networks[p_cfg.sta_network_count].ssid)>0) {
          p_cfg.sta_network_count++;
        }
      }
      p_wifiChanged = true;
    }
    return true;
  }

  // 상태+설정 JSON 생성에 쓰일 직렬화 도우미
  static void toJson(const WindConfig &c, JsonDocument &doc) {
    JsonObject root = doc.to<JsonObject>();
    root["sim"]["intensity"] = c.wind_intensity;
    root["sim"]["gust_freq"] = c.gust_frequency;
    root["sim"]["variability"] = c.wind_variability;
    root["sim"]["fan_limit"] = c.fan_speed_limit;
    root["sim"]["min_fan"]   = c.minimum_fan_speed;
    root["sim"]["turb_len"]  = c.turbulence_length_scale;
    root["sim"]["turb_sig"]  = c.turbulence_intensity_sigma;
    root["sim"]["therm_str"] = c.thermal_bubble_strength;
    root["sim"]["therm_rad"] = c.thermal_bubble_radius;
    root["sim"]["preset"]    = G_SC10_PRESET_MODE_NAMES[c.preset_mode_index];

    root["timing"]["sim_int"]    = c.wind_sim_interval_ms;
    root["timing"]["gust_int"]   = c.gust_check_interval_ms;
    root["timing"]["thermal_int"]= c.thermal_check_interval_ms;

    JsonObject w = root["wifi"].to<JsonObject>();
    w["wifi_mode"]   = c.wifi_mode;
    w["ap_ssid"]     = c.ap_ssid;
    // 보안상 비번은 상태 응답에서 기본 비노출 권장 -> 필요 시 w["ap_password"]=c.ap_password;
    JsonArray arr = w["sta_networks"].to<JsonArray>();
    for (int i=0;i<c.sta_network_count;i++) {
      JsonObject net = arr.add<JsonObject>();
      net["ssid"] = c.sta_networks[i].ssid;
      // net["pass"] = c.sta_networks[i].password; // 비노출 권장
    }
  }

private:
  static bool parseJson(WindConfig &c, JsonDocument &doc) {
    JsonObject root = doc.as<JsonObject>();
    c.wifi_mode = root["wifi"]["wifi_mode"] | c.wifi_mode;
    strlcpy(c.ap_ssid, root["wifi"]["ap_ssid"] | c.ap_ssid, sizeof(c.ap_ssid));
    strlcpy(c.ap_password, root["wifi"]["ap_password"] | c.ap_password, sizeof(c.ap_password));
    c.sta_network_count = 0;
    JsonArray arr = root["wifi"]["sta_networks"].as<JsonArray>();
    for (JsonObject net : arr) {
      if (c.sta_network_count >= SC10_Const::MAX_STA_NETWORKS) break;
      strlcpy(c.sta_networks[c.sta_network_count].ssid, net["ssid"]|"", sizeof(SC10_StaCredential::ssid));
      strlcpy(c.sta_networks[c.sta_network_count].password, net["pass"]|"", sizeof(SC10_StaCredential::password));
      c.sta_network_count++;
    }
    // 시뮬/타이밍/프리셋은 필요 시 추가 파싱 (여기선 /api/config를 통해 주로 갱신하므로 생략 가능)
    return true;
  }
};


