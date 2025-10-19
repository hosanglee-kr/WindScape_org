/*
 * ------------------------------------------------------
 * 소스명 : SC10_main_016.js
 * 모듈명 : WindScape WebUI Controller
 * ------------------------------------------------------
 * 기능 요약:
 *  - /api/state 기반 상태 갱신 및 UI 반영
 *  - /api/config 통한 설정 저장(PATCH) 및 그룹별 업데이트
 *  - Wi-Fi / HW / Timing / Sim / Web 설정 관리
 *  - 버튼/슬라이더 이벤트 연결 및 실시간 상태 표시
 * ------------------------------------------------------
 * - 네이밍 규칙(프론트):
 *    - 전역: SC10_ 접두사
 *    - 로컬 변수: v_
 *    - 함수 인자: p_
 * ------------------------------------------------------
 */

"use strict";

const SC10_API_BASE = "/api";
let g_SC10_state = null;
let g_SC10_presets = [];

async function SC10_fetchJSON(p_url, p_method = "GET", p_body = null) {
  const v_opt = { method: p_method, headers: { "Content-Type": "application/json" } };
  if (p_body) v_opt.body = JSON.stringify(p_body);
  const v_res = await fetch(p_url, v_opt);
  if (!v_res.ok) throw new Error(`HTTP ${v_res.status}`);
  return await v_res.json();
}

// -----------------------------
// UI 초기화
// -----------------------------
window.addEventListener("DOMContentLoaded", async () => {
  await SC10_loadState();
  SC10_bindEvents();
  setInterval(SC10_refreshState, 3000);
});

// -----------------------------
// 상태 로드
// -----------------------------
async function SC10_loadState() {
  try {
    const v_json = await SC10_fetchJSON(`${SC10_API_BASE}/state`);
    g_SC10_state = v_json.status;
    g_SC10_presets = v_json.presets;
    SC10_renderUI(v_json);
  } catch (e) {
    console.error("loadState fail", e);
    document.body.innerHTML = `<div style="padding:20px;color:#f87171">상태 불러오기 실패: ${e.message}</div>`;
  }
}

// -----------------------------
// 상태 새로고침
// -----------------------------
async function SC10_refreshState() {
  try {
    const v_json = await SC10_fetchJSON(`${SC10_API_BASE}/state`);
    g_SC10_state = v_json.status;
    document.getElementById("txt_speed").textContent = `${v_json.status.wind_speed} m/s`;
    document.getElementById("txt_phase").textContent = v_json.status.phase_name;
    document.getElementById("txt_ip").textContent = v_json.status.ip_addr;
    document.getElementById("txt_ssid").textContent = v_json.status.ssid;
  } catch (e) {
    console.warn("refresh fail", e);
  }
}

// -----------------------------
// UI 렌더링
// -----------------------------
function SC10_renderUI(p_json) {
  const v_cfg = p_json.config;
  const v_state = p_json.status;

  document.getElementById("txt_fw").textContent = p_json.status.fw_version || "SC10";

  // Wi-Fi
  document.getElementById("sel_wifi_mode").value = v_cfg.wifi.wifi_mode;
  document.getElementById("ap_ssid").value = v_cfg.wifi.ap_network.ap_ssid;
  document.getElementById("ap_password").value = v_cfg.wifi.ap_network.ap_password;

  // HW
  document.getElementById("hw_pwm_pin").value = v_cfg.hw.pwm_pin;
  document.getElementById("hw_pwm_freq").value = v_cfg.hw.pwm_freq;

  // Timing
  document.getElementById("tim_sim").value = v_cfg.timing.sim_int;
  document.getElementById("tim_gust").value = v_cfg.timing.gust_int;
  document.getElementById("tim_therm").value = v_cfg.timing.thermal_int;

  // SIM
  document.getElementById("sel_preset").innerHTML = g_SC10_presets.map(p => `<option value="${p}">${p}</option>`).join("");
  document.getElementById("sel_preset").value = v_cfg.sim.preset;
  document.getElementById("rng_intensity").value = v_cfg.sim.intensity;
  document.getElementById("rng_variability").value = v_cfg.sim.variability;
  document.getElementById("rng_gust").value = v_cfg.sim.gust_freq;

  // Status
  document.getElementById("txt_speed").textContent = `${v_state.wind_speed} m/s`;
  document.getElementById("txt_phase").textContent = v_state.phase_name;
  document.getElementById("txt_ip").textContent = v_state.ip_addr;
  document.getElementById("txt_ssid").textContent = v_state.ssid;
}

// -----------------------------
// 이벤트 바인딩
// -----------------------------
function SC10_bindEvents() {
  document.getElementById("btnSaveWifi").addEventListener("click", SC10_saveWifi);
  document.getElementById("btnSaveSim").addEventListener("click", SC10_saveSim);
  document.getElementById("btnSaveHw").addEventListener("click", SC10_saveHw);
  document.getElementById("btnSaveTiming").addEventListener("click", SC10_saveTiming);
}

// -----------------------------
// 설정 저장
// -----------------------------
async function SC10_saveWifi() {
  const v_body = {
    wifi: {
      wifi_mode: Number(document.getElementById("sel_wifi_mode").value),
      ap_network: {
        ap_ssid: document.getElementById("ap_ssid").value,
        ap_password: document.getElementById("ap_password").value
      }
    }
  };
  await SC10_postConfig(v_body, "Wi-Fi 설정 저장 완료");
}

async function SC10_saveHw() {
  const v_body = {
    hw: {
      pwm_pin: Number(document.getElementById("hw_pwm_pin").value),
      pwm_freq: Number(document.getElementById("hw_pwm_freq").value)
    }
  };
  await SC10_postConfig(v_body, "하드웨어 설정 저장 완료");
}

async function SC10_saveTiming() {
  const v_body = {
    timing: {
      sim_int: Number(document.getElementById("tim_sim").value),
      gust_int: Number(document.getElementById("tim_gust").value),
      thermal_int: Number(document.getElementById("tim_therm").value)
    }
  };
  await SC10_postConfig(v_body, "타이밍 설정 저장 완료");
}

async function SC10_saveSim() {
  const v_body = {
    sim: {
      preset: document.getElementById("sel_preset").value,
      intensity: Number(document.getElementById("rng_intensity").value),
      variability: Number(document.getElementById("rng_variability").value),
      gust_freq: Number(document.getElementById("rng_gust").value)
    }
  };
  await SC10_postConfig(v_body, "시뮬레이션 설정 저장 완료");
}

// -----------------------------
// 공통 설정 POST
// -----------------------------
async function SC10_postConfig(p_body, p_msg) {
  try {
    await SC10_fetchJSON(`${SC10_API_BASE}/config`, "POST", p_body);
    alert(p_msg);
  } catch (e) {
    alert("저장 실패: " + e.message);
  }
}
