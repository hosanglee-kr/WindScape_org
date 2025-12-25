// SC10_main_015.js

(() => {
"use strict";
const $ = (s, r = document) => r.querySelector(s);
const text = (el, v) => el && (el.textContent = v);

const KEY_K = 'my_secure_api_key_123';
const getKey = () => localStorage.getItem(KEY_K) || '';
const setKey = (k) => localStorage.setItem(KEY_K, k);
let g_config = {};

// 프리셋 한글 매핑
const presetNameMap = {
  COUNTRY: "들판",
  MOUNTAIN: "산바람",
  HARBOR: "항구바람",
  DESERT: "사막열풍",
  FOREST: "숲속바람"
};
const displayPresetName = (n) => presetNameMap[n] || n;

document.addEventListener("DOMContentLoaded", () => {
  $('#apiKeyInput').value = getKey();
  bindEvents();
  refreshVersion();
  refreshState(true);
});

// ========================== 기본 UI 유틸 ==========================
function showToast(msg, type = "ok") {
  const cont = $("#toastContainer");
  const div = document.createElement("div");
  div.className = `toast ${type}`;
  div.textContent = msg;
  cont.appendChild(div);
  setTimeout(() => div.remove(), 3000);
}

function setLoading(flag) {
  $("#loadingOverlay").style.display = flag ? "flex" : "none";
}

// ========================== 이벤트 바인딩 ==========================
function bindEvents() {
  $('#btnRefresh').addEventListener('click', () => refreshState(true));
  $('#lnkRefresh').addEventListener('click', (e) => { e.preventDefault(); refreshState(true); });
  $('#btnPreviewPreset').addEventListener('click', previewPreset);

  $('#btnSaveSim').addEventListener('click', saveSim);
  $('#btnSaveTiming').addEventListener('click', saveTiming);
  $('#btnSaveWifiAP').addEventListener('click', saveWifiAP);
  $('#btnSaveWifiSTA').addEventListener('click', saveWifiSTA);
  $('#btnSavePWM').addEventListener('click', savePWMConfig);
  $('#btnSaveApiKey').addEventListener('click', saveApiKey);

  $('#btnScan').addEventListener('click', scanNetworks);
  $('#btnUseScan').addEventListener('click', useScanResult);
  $('#btnUpload').addEventListener('click', uploadStatic);
  $('#btnOTA').addEventListener('click', uploadOTA);
}

// ========================== API 호출 래퍼 ==========================
async function fetchApi(url, method = 'GET', body = null, desc = "작업") {
  setLoading(true);
  try {
    const opt = { method, headers: {} };
    if (body) {
      opt.body = JSON.stringify(body);
      opt.headers['Content-Type'] = 'application/json';
    }
    const k = getKey();
    if (k) opt.headers['X-API-Key'] = k;

    const resp = await fetch(url, opt);
    const txt = await resp.text();

    if (resp.status === 401) {
      showToast(`[401] ${desc} 실패: 인증 필요`, "err");
      throw new Error('Unauthorized');
    }
    if (!resp.ok) {
      showToast(`${desc} 실패: ${txt || resp.status}`, "err");
      throw new Error(txt || resp.status);
    }

    showToast(`${desc} 성공`, "ok");
    return txt;
  } catch (e) {
    if (e.message !== 'Unauthorized') showToast(`${desc} 실패: ${e.message}`, "err");
  } finally {
    setLoading(false);
  }
}

// ========================== 프리셋 미리보기 ==========================
function previewPreset() {
  const preset = $('#preset').value;
  $('#presetPreview').textContent = `프리셋 미리보기: ${displayPresetName(preset)}`;
  showToast(`"${displayPresetName(preset)}" 프리셋 미리보기 적용`, "ok");
}

// ========================== 그룹별 저장 ==========================
async function saveSim() {
  await fetchApi('/api/config', 'POST', { sim: getSimBody() }, '시뮬 저장');
  refreshState();
}

function getSimBody() {
  return {
    preset: $('#preset').value,
    intensity: Number($('#intensity').value),
    gust_freq: Number($('#gust_freq').value),
    variability: Number($('#variability').value),
    fan_limit: Number($('#fan_limit').value),
    min_fan: Number($('#min_fan').value),
    turb_len: Number($('#turb_len').value),
    turb_sig: Number($('#turb_sig').value),
    therm_str: Number($('#therm_str').value),
    therm_rad: Number($('#therm_rad').value),
  };
}

async function saveTiming() {
  const body = {
    timing: {
      sim_int: Number($('#sim_int').value),
      gust_int: Number($('#gust_int').value),
      thermal_int: Number($('#thermal_int').value)
    }
  };
  await fetchApi('/api/config', 'POST', body, '타이밍 저장');
  refreshState();
}

async function saveWifiAP() {
  const body = {
    wifi: {
      wifi_mode: Number($('#wifi_mode').value),
      ap_ssid: $('#ap_ssid').value,
      ap_password: $('#ap_password').value
    }
  };
  await fetchApi('/api/config', 'POST', body, 'AP 설정 저장');
  refreshState();
}

async function saveWifiSTA() {
  const sta = (g_config.config?.wifi?.sta_networks) || [];
  await fetchApi('/api/config', 'POST', { wifi: { sta_networks: sta } }, 'STA 목록 저장');
  refreshState();
}

async function savePWMConfig() {
  const body = {
    hw: {
      pwm_pin: Number($('#pwm_pin').value),
      pwm_channel: Number($('#pwm_channel').value),
      pwm_freq: Number($('#pwm_freq').value),
      pwm_res: Number($('#pwm_res').value)
    }
  };
  // 간단 검증
  if (body.hw.pwm_pin >= 6 && body.hw.pwm_pin <= 11)
    return showToast('GPIO6~11은 Flash용 핀으로 PWM 불가', 'warn');
  if (body.hw.pwm_pin >= 34)
    return showToast('GPIO34 이상은 입력전용으로 PWM 불가', 'warn');
  if (body.hw.pwm_channel < 0 || body.hw.pwm_channel > 7)
    return showToast('PWM 채널은 0~7', 'warn');
  if (body.hw.pwm_freq < 25000 || body.hw.pwm_freq > 40000)
    return showToast('PWM 주파수는 25~40kHz', 'warn');
  if (body.hw.pwm_res < 8 || body.hw.pwm_res > 16)
    return showToast('PWM 해상도는 8~16bit', 'warn');

  await fetchApi('/api/config', 'POST', body, 'PWM 설정 저장');
  refreshState();
}

async function saveApiKey() {
  const newKey = $('#apiKeyInput').value.trim();
  if (!newKey) return showToast('API Key를 입력하세요.', 'warn');
  setKey(newKey);
  await fetchApi('/api/config', 'POST', { security: { api_key: newKey } }, 'API Key 저장');
  showToast('API Key 저장 완료', 'ok');
}

// ========================== 상태 갱신 ==========================
async function refreshVersion() {
  try {
    const r = await fetch('/api/version');
    const j = await r.json();
    text($('#fwVer'), j.fw_version);
  } catch {
    text($('#fwVer'), 'version?');
  }
}

async function refreshState() {
  setLoading(true);
  try {
    const r = await fetch('/api/state');
    const j = await r.json();
    g_config = j;

    text($('#simActive'), j.status.sim_active ? 'Active' : 'Idle');
    text($('#phase'), j.status.phase_name);
    text($('#wind'), j.status.wind_speed.toFixed(2) + ' m/s');
    text($('#pwm'), j.status.fan_pwm_percent.toFixed(1) + '%');
    text($('#wifiMode'), j.status.wifi_mode);

    const ipParts = [];
    if (j.status.ip_ap) ipParts.push(`AP:${j.status.ip_ap}`);
    if (j.status.ip_sta) ipParts.push(`STA:${j.status.ip_sta}`);
    text($('#ip'), ipParts.join(' / ') || '-');
    text($('#curSsid'), j.status.ssid || j.status.ap_ssid || '-');

    // 프리셋
    const sel = $('#preset');
    sel.innerHTML = '';
    j.presets.forEach(p => {
      const o = document.createElement('option');
      o.value = p;
      o.textContent = displayPresetName(p);
      sel.appendChild(o);
    });
    sel.value = j.config.sim.preset;
    $('#presetPreview').textContent = `프리셋 미리보기: ${displayPresetName(sel.value)}`;

    Object.keys(j.config.sim).forEach(k => { const el = $(`#${k}`); if (el) el.value = j.config.sim[k]; });
    Object.keys(j.config.timing).forEach(k => { const el = $(`#${k}`); if (el) el.value = j.config.timing[k]; });

    $('#wifi_mode').value = j.config.wifi.wifi_mode;
    $('#ap_ssid').value = j.config.wifi.ap_ssid;
    $('#ap_password').value = j.config.wifi.ap_password;

    displayStaNetworks(j.config.wifi.sta_networks || []);

    $('#pwm_pin').value = j.config.hw.pwm_pin;
    $('#pwm_channel').value = j.config.hw.pwm_channel;
    $('#pwm_freq').value = j.config.hw.pwm_freq;
    $('#pwm_res').value = j.config.hw.pwm_res;

  } catch (e) {
    showToast(`상태 불러오기 실패: ${e.message}`, "err");
  } finally {
    setLoading(false);
  }
}

// ========================== STA 리스트 관리 ==========================
function displayStaNetworks(networks) {
  const list = $('#staList'); list.innerHTML = '';
  if (!networks.length) {
    list.innerHTML = '<div class="muted" style="text-align:center;">저장된 STA 네트워크 없음</div>';
    return;
  }
  const table = document.createElement('table');
  table.innerHTML = `<thead><tr><th>SSID</th><th>PW</th><th></th></tr></thead><tbody></tbody>`;
  const tbody = table.querySelector('tbody');
  networks.forEach((n, idx) => {
    const row = tbody.insertRow();
    row.innerHTML = `
      <td>${n.ssid}</td>
      <td>${n.pass ? '********' : 'OPEN'}</td>
      <td class="right tight"><button class="btn err btn-remove-sta" data-index="${idx}">삭제</button></td>
    `;
  });
  list.appendChild(table);
  list.querySelectorAll('.btn-remove-sta').forEach(btn => {
    btn.addEventListener('click', e => {
      const idx = Number(e.target.dataset.index);
      const removed = networks[idx]?.ssid || '';
      networks.splice(idx, 1);
      if (g_config.config && g_config.config.wifi)
        g_config.config.wifi.sta_networks = networks;
      showToast(`${removed} 삭제됨 (STA 저장 필요)`, "warn");
      displayStaNetworks(networks);
    });
  });
}

function useScanResult() {
  const ssid = $('#scanList').value;
  const pass = $('#scanPass').value;
  if (!ssid) return showToast("SSID를 선택하세요.", "warn");
  if (pass.length > 0 && pass.length < 8)
    return showToast("비밀번호는 8자 이상 (OPEN 제외)", "err");

  const arr = (g_config.config?.wifi?.sta_networks)
    ? g_config.config.wifi.sta_networks
    : (g_config.config.wifi.sta_networks = []);

  if (arr.some(n => n.ssid === ssid))
    return showToast(`"${ssid}"는 이미 목록에 있습니다.`, "warn");

  arr.push({ ssid, pass });
  displayStaNetworks(arr);
  $('#scanPass').value = '';
  showToast(`${ssid} 추가됨 (STA 저장 필요)`, "ok");
}

// ========================== Wi-Fi 스캔 ==========================
async function scanNetworks() {
  await fetchApi('/api/scan?async=true', 'GET', null, 'Wi-Fi 스캔 시작');
  showToast("Wi-Fi 스캔 중... 1.5초 후 결과 수집", "warn");
  await new Promise(r => setTimeout(r, 1500));
  const txt = await fetchApi('/api/scan', 'GET', null, 'Wi-Fi 스캔 결과');
  if (!txt) return;
  try {
    const arr = JSON.parse(txt);
    const sel = $('#scanList');
    sel.innerHTML = '<option value="">-- 선택하세요 --</option>';
    if (!arr.length) {
      sel.innerHTML += '<option value="" disabled>스캔된 네트워크 없음</option>';
      showToast("스캔 결과 없음", "warn");
      return;
    }
    arr.sort((a, b) => b.rssi - a.rssi);
    arr.forEach(n => {
      const o = document.createElement('option');
      o.value = n.ssid;
      o.textContent = `${n.ssid} [${n.rssi} dBm, ${n.enc}]`;
      sel.appendChild(o);
    });
    showToast(`총 ${arr.length}개 네트워크 스캔 완료`, "ok");
  } catch (e) {
    showToast(`스캔 결과 파싱 실패: ${e.message}`, "err");
  }
}

// ========================== 파일 업로드 / OTA ==========================
async function uploadFile(inputSel, url, msgSel, desc, isOTA = false) {
  const fileInput = $(inputSel);
  if (fileInput.files.length === 0)
    return showToast("업로드할 파일을 선택하세요.", "warn");
  if (isOTA && !confirm("펌웨어 OTA 진행? 실패 시 동작 중단 위험"))
    return;

  setLoading(true);
  text($(msgSel), `업로드 중... (${fileInput.files[0].size} bytes)`);

  const file = fileInput.files[0];
  const form = new FormData();
  form.append('file', file, file.name);

  try {
    const r = await fetch(url, { method: 'POST', body: form });
    const txt = await r.text();
    if (!r.ok) throw new Error(`[${r.status}] ${txt || r.statusText}`);
    if (isOTA) {
      showToast(`${desc} 성공. 재부팅 후 재접속`, "ok");
      text($(msgSel), "업데이트 성공! 장치 재부팅 중...");
      setTimeout(() => location.reload(), 5000);
    } else {
      showToast(`${desc} 성공`, "ok");
      text($(msgSel), `업로드 성공: ${txt}`);
    }
  } catch (e) {
    showToast(`${desc} 실패: ${e.message}`, "err");
    text($(msgSel), `업로드 실패: ${e.message}`);
  } finally {
    if (!isOTA) setLoading(false);
  }
}

async function uploadStatic() {
  await uploadFile('#fileUpload', '/upload', '#uploadMsg', '정적 파일 업로드');
}

async function uploadOTA() {
  await uploadFile('#fileOTA', '/update', '#otaMsg', '펌웨어 OTA 업데이트', true);
}

})();
