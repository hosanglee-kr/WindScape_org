/*
 * ------------------------------------------------------
 * 소스명 : SC10_main_017.js
 * 모듈명 : Smart Nature Wind Web UI Controller
 * ------------------------------------------------------
 * 기능 요약:
 *  - 설정 그룹별 저장(Wi-Fi / HW / Timing / Sim / API Key)
 *  - /api/state 상태 반영
 *  - /api/scan /api/version 대응
 *  - 정적 파일 & 펌웨어 업로드 분리
 * ------------------------------------------------------
 */

(() => {
"use strict";

// DOM Helper
const $   = (s, r=document) => r.querySelector(s);
const text = (el, v) => el && (el.textContent = v);

// API Key (localStorage)
const KEY_API = 'sc10_api_key';
const getKey = () => localStorage.getItem(KEY_API) || '';
const setKey = (k) => localStorage.setItem(KEY_API, k);

// 상태 캐시
let g_config = {};
let g_presets = [];

// 프리셋 한글 매핑
const presetNameMap = {
  COUNTRY: "들판",
  MEDITERRANEAN: "지중해",
  OCEAN: "바다",
  MOUNTAIN: "산바람",
  PLAINS: "평야",
  FOREST_CANOPY: "숲속",
  HARBOR_BREEZE: "항구바람",
  URBAN_SUNSET: "도심석양",
  TROPICAL_RAIN: "열대우림",
  DESERT_NIGHT: "사막밤"
};
const displayPresetName = (n) => presetNameMap[n] || n;

// 초기화
document.addEventListener("DOMContentLoaded", () => {
  const k = getKey();
  if ($("#apiKeyInput")) $("#apiKeyInput").value = k;
  bindEvents();
  refreshVersion();
  refreshState(true);
});

// 유틸 UI
function showToast(msg, type="ok") {
  const cont = $("#toastContainer");
  if (!cont) return;
  const div = document.createElement("div");
  div.className = `toast ${type}`;
  div.textContent = msg;
  cont.appendChild(div);
  setTimeout(() => div.remove(), 3000);
}
function setLoading(flag) {
  const ov = $("#loadingOverlay");
  if (ov) ov.style.display = flag ? "flex" : "none";
}

// 공통 API
async function fetchApi(url, method="GET", body=null, desc="작업") {
  setLoading(true);
  try {
    const opt = { method, headers: {} };
    if (body) {
      opt.body = JSON.stringify(body);
      opt.headers["Content-Type"] = "application/json";
    }
    const k = getKey();
    if (k) opt.headers["X-API-Key"] = k;

    const resp = await fetch(url, opt);
    const txt = await resp.text();

    if (resp.status === 401) {
      showToast(`[401] ${desc} 실패: 인증 필요`, "err");
      throw new Error("Unauthorized");
    }
    if (!resp.ok) {
      showToast(`${desc} 실패: ${txt || resp.status}`, "err");
      throw new Error(txt || resp.status);
    }
    showToast(`${desc} 성공`, "ok");
    return txt;
  } catch (e) {
    if (e.message !== "Unauthorized") showToast(`${desc} 실패: ${e.message}`, "err");
  } finally {
    setLoading(false);
  }
}

// 이벤트 바인딩
function bindEvents() {
  $("#btnRefresh")?.addEventListener("click", () => refreshState(true));
  $("#btnPreviewPreset")?.addEventListener("click", previewPreset);

  $("#btnSaveSim")?.addEventListener("click", saveSim);
  $("#btnSaveSimInit")?.addEventListener("click", saveSimInit);
  $("#btnSaveTiming")?.addEventListener("click", saveTiming);
  $("#btnSaveWifiAP")?.addEventListener("click", saveWifiAP);
  $("#btnSaveWifiSTA")?.addEventListener("click", saveWifiSTA);
  $("#btnSavePWM")?.addEventListener("click", savePWMConfig);
  $("#btnSaveApiKey")?.addEventListener("click", saveApiKey);

  $("#btnScan")?.addEventListener("click", scanNetworks);
  $("#btnUseScan")?.addEventListener("click", useScanResult);

  $("#btnUploadStatic")?.addEventListener("click", uploadStatic);
  $("#btnUploadOTA")?.addEventListener("click", uploadOTA);
}

// 프리셋 미리보기
function previewPreset() {
  const preset = $("#preset").value;
  $("#presetPreview").textContent = `프리셋 미리보기: ${displayPresetName(preset)}`;
  showToast(`"${displayPresetName(preset)}" 미리보기 적용`, "ok");
}

// 그룹별 저장
async function saveSim() {
  const body = {
    sim: {
      preset: $("#preset").value,
      intensity: Number($("#intensity").value),
      gust_freq: Number($("#gust_freq").value),
      variability: Number($("#variability").value),
      fan_limit: Number($("#fan_limit").value),
      min_fan: Number($("#min_fan").value),
      turb_len: Number($("#turb_len").value),
      turb_sig: Number($("#turb_sig").value),
      therm_str: Number($("#therm_str").value),
      therm_rad: Number($("#therm_rad").value)
    }
  };
  await fetchApi("/api/config", "POST", body, "시뮬 설정 저장");
  refreshState();
}

async function saveSimInit() {
  const body = {
  };
  await fetchApi("/api/config/init", "POST", body, "시뮬 설정 초기화");
  refreshState();
}
async function saveTiming() {
  const body = {
    timing: {
      sim_int: Number($("#sim_int").value),
      gust_int: Number($("#gust_int").value),
      thermal_int: Number($("#thermal_int").value)
    }
  };
  await fetchApi("/api/config", "POST", body, "타이밍 설정 저장");
  refreshState();
}

async function saveWifiAP() {
  // 백엔드 구조: wifi { wifi_mode, ap_network{ap_ssid, ap_password} }
  const body = {
    wifi: {
      wifi_mode: Number($("#wifi_mode").value),
      ap_network: {
        ap_ssid: $("#ap_ssid").value,
        ap_password: $("#ap_password").value
      }
    }
  };
  await fetchApi("/api/config", "POST", body, "AP 설정 저장");
  refreshState();
}

async function saveWifiSTA() {
  const sta = g_config.config?.wifi?.sta_networks || [];
  await fetchApi("/api/config", "POST", { wifi: { sta_networks: sta } }, "STA 목록 저장");
  refreshState();
}

async function savePWMConfig() {
  const body = {
    hw: {
      pwm_pin: Number($("#pwm_pin").value),
      pwm_channel: Number($("#pwm_channel").value),
      pwm_freq: Number($("#pwm_freq").value),
      pwm_res: Number($("#pwm_res").value)
    }
  };
  if (body.hw.pwm_pin >= 6 && body.hw.pwm_pin <= 11)
    return showToast("GPIO6~11은 Flash용 핀으로 PWM 불가", "warn");
  if (body.hw.pwm_pin >= 34)
    return showToast("GPIO34 이상은 입력전용으로 PWM 불가", "warn");
  if (body.hw.pwm_channel < 0 || body.hw.pwm_channel > 7)
    return showToast("PWM 채널은 0~7 범위", "warn");
  await fetchApi("/api/config", "POST", body, "PWM 설정 저장");
  refreshState();
}

async function saveApiKey() {
  const newKey = $("#apiKeyInput").value.trim();
  if (!newKey) return showToast("API Key를 입력하세요.", "warn");
  setKey(newKey);
  await fetchApi("/api/config", "POST", { security: { api_key: newKey } }, "API Key 저장");
  showToast("API Key 저장 완료", "ok");
}

// 상태/버전
async function refreshVersion() {
  try {
    const r = await fetch("/api/version");
    const j = await r.json();
    text($("#fwVer"), j.fw_version);
  } catch {
    text($("#fwVer"), "version?");
  }
}

async function refreshState(showToastMsg=false) {
  setLoading(true);
  try {
    const r = await fetch("/api/state");
    const j = await r.json();
    g_config = j;
    g_presets = j.presets || [];

    // Status
    text($("#simActive"), j.status.sim_active ? "Active" : "Idle");
    text($("#phase"), j.status.phase_name);
    text($("#wind"), `${Number(j.status.wind_speed || 0).toFixed(2)} m/s`);
    text($("#pwm"), `${Number(j.status.fan_pwm_percent || 0).toFixed(1)}%`);
    text($("#wifiMode"), j.status.wifi_mode || "-");

    // 백엔드가 ip_addr(단일) 또는 ip_ap/ip_sta(복수)를 줄 수 있어 둘 다 처리
    const ipStr = (j.status.ip_ap || j.status.ip_sta)
      ? [j.status.ip_ap && `AP:${j.status.ip_ap}`, j.status.ip_sta && `STA:${j.status.ip_sta}`].filter(Boolean).join(" / ")
      : (j.status.ip_addr || "-");
    text($("#ip"), ipStr);

    text($("#curSsid"), j.status.ssid || j.status.ap_ssid || "-");

    // Presets
    const sel = $("#preset");
    sel.innerHTML = "";
    g_presets.forEach(p => {
      const o = document.createElement("option");
      o.value = p;
      o.textContent = displayPresetName(p);
      sel.appendChild(o);
    });
    sel.value = j.config.sim.preset || "";
    $("#presetPreview").textContent = `프리셋 미리보기: ${displayPresetName(sel.value)}`;

    // Sim / Timing
    Object.entries(j.config.sim || {}).forEach(([k, v]) => { const el = $(`#${k}`); if (el) el.value = v; });
    Object.entries(j.config.timing || {}).forEach(([k, v]) => { const el = $(`#${k}`); if (el) el.value = v; });

    // Wi-Fi (새 구조: ap_network)
    $("#wifi_mode").value = j.config.wifi.wifi_mode;
    $("#ap_ssid").value   = j.config.wifi.ap_network?.ap_ssid || "";
    $("#ap_password").value = j.config.wifi.ap_network?.ap_password || "";
    displayStaNetworks(j.config.wifi.sta_networks || []);

    // HW
    $("#pwm_pin").value     = j.config.hw.pwm_pin;
    $("#pwm_channel").value = j.config.hw.pwm_channel;
    $("#pwm_freq").value    = j.config.hw.pwm_freq;
    $("#pwm_res").value     = j.config.hw.pwm_res;

    if (showToastMsg) showToast("상태 갱신 완료", "ok");
  } catch (e) {
    showToast(`상태 불러오기 실패: ${e.message}`, "err");
  } finally {
    setLoading(false);
  }
}

// STA 리스트
function displayStaNetworks(networks) {
  const list = $("#staList");
  list.innerHTML = "";
  if (!networks.length) {
    list.innerHTML = `<div class="muted" style="text-align:center;">저장된 STA 네트워크 없음</div>`;
    return;
  }
  const table = document.createElement("table");
  table.innerHTML = `<thead><tr><th>SSID</th><th>PW</th><th></th></tr></thead><tbody></tbody>`;
  const tbody = table.querySelector("tbody");
  networks.forEach((n, idx) => {
    const row = tbody.insertRow();
    row.innerHTML = `
      <td>${n.ssid}</td>
      <td>${n.pass ? "********" : "OPEN"}</td>
      <td class="right tight"><button class="btn err btn-remove-sta" data-index="${idx}">삭제</button></td>
    `;
  });
  list.appendChild(table);
  list.querySelectorAll(".btn-remove-sta").forEach(btn => {
    btn.addEventListener("click", e => {
      const idx = Number(e.target.dataset.index);
      const removed = networks[idx]?.ssid || "";
      networks.splice(idx, 1);
      if (g_config.config?.wifi) g_config.config.wifi.sta_networks = networks;
      showToast(`${removed} 삭제됨 (STA 저장 필요)`, "warn");
      displayStaNetworks(networks);
    });
  });
}

// Wi-Fi 스캔
async function scanNetworks() {
  await fetchApi("/api/scan?async=true", "GET", null, "Wi-Fi 스캔 시작");
  showToast("스캔 중... 1.5초 후 결과 수집", "warn");
  await new Promise(r => setTimeout(r, 1500));
  const txt = await fetchApi("/api/scan", "GET", null, "Wi-Fi 스캔 결과");
  if (!txt) return;
  try {
    const arr = JSON.parse(txt);
    const sel = $("#scanList");
    sel.innerHTML = '<option value="">-- 선택하세요 --</option>';
    if (!arr.length) {
      sel.innerHTML += '<option value="" disabled>스캔된 네트워크 없음</option>';
      showToast("스캔 결과 없음", "warn");
      return;
    }
    arr.sort((a, b) => b.rssi - a.rssi);
    arr.forEach(n => {
      const o = document.createElement("option");
      o.value = n.ssid;
      o.textContent = `${n.ssid} [${n.rssi} dBm, ${n.enc}]`;
      sel.appendChild(o);
    });
    showToast(`총 ${arr.length}개 네트워크 스캔 완료`, "ok");
  } catch (e) {
    showToast(`스캔 결과 파싱 실패: ${e.message}`, "err");
  }
}

function useScanResult() {
  const ssid = $("#scanList").value;
  const pass = $("#scanPass").value;
  if (!ssid) return showToast("SSID를 선택하세요.", "warn");
  if (pass.length > 0 && pass.length < 8) return showToast("비밀번호는 8자 이상 (OPEN 제외)", "err");

  const arr = g_config.config?.wifi?.sta_networks || [];
  if (arr.some(n => n.ssid === ssid)) return showToast(`"${ssid}"는 이미 등록됨`, "warn");

  arr.push({ ssid, pass });
  if (g_config.config?.wifi) g_config.config.wifi.sta_networks = arr;
  displayStaNetworks(arr);
  $("#scanPass").value = "";
  showToast(`${ssid} 추가됨 (STA 저장 필요)`, "ok");
}

// 파일 업로드
async function uploadFile(inputSel, url, msgSel, desc, isOTA=false) {
  const fileInput = $(inputSel);
  if (!fileInput || fileInput.files.length === 0)
    return showToast("업로드할 파일을 선택하세요.", "warn");
  if (isOTA && !confirm("펌웨어 OTA를 진행하시겠습니까?")) return;

  setLoading(true);
  const file = fileInput.files[0];
  const form = new FormData();
  form.append("file", file, file.name);
  text($(msgSel), `업로드 중... (${file.size} bytes)`);

  try {
    const r = await fetch(url, { method: "POST", body: form });
    const txt = await r.text();
    if (!r.ok) throw new Error(`[${r.status}] ${txt}`);
    if (isOTA) {
      showToast(`${desc} 완료, 재부팅 중`, "ok");
      text($(msgSel), "업데이트 성공! 재시작 중...");
      setTimeout(() => location.reload(), 5000);
    } else {
      showToast(`${desc} 성공`, "ok");
      text($(msgSel), `업로드 완료: ${txt}`);
    }
  } catch (e) {
    showToast(`${desc} 실패: ${e.message}`, "err");
    text($(msgSel), `업로드 실패: ${e.message}`);
  } finally {
    if (!isOTA) setLoading(false);
  }
}
async function uploadStatic() { await uploadFile("#fileUpload", "/upload", "#uploadMsg", "정적 파일 업로드"); }
async function uploadOTA()    { await uploadFile("#fileOTA", "/update", "#otaMsg", "펌웨어 OTA", true); }

})();

