// SC10_main_014.js
(() => {
  "use strict";

  const $ = (sel, root = document) => root.querySelector(sel);
  const text = (el, v) => el && (el.textContent = v);

  const KEY_K = 'my_secure_api_key_123';
  const getKey = () => localStorage.getItem(KEY_K) || '';
  const setKey = (k) => localStorage.setItem(KEY_K, k);

  let g_config = {};

  document.addEventListener("DOMContentLoaded", () => {
    $('#apiKeyInput').value = getKey(); // 저장된 키 자동 반영
    bindEvents();
    refreshVersion();
    refreshState(true);
  });

  // 토스트
  function showToast(msg, type = "ok") {
    const cont = $("#toastContainer");
    const div = document.createElement("div");
    div.className = `toast ${type}`;
    div.textContent = msg;
    cont.appendChild(div);
    setTimeout(() => div.remove(), 3000);
  }
  // 로딩
  function setLoading(flag) {
    $("#loadingOverlay").style.display = flag ? "flex" : "none";
  }
  // 프리셋 미리보기
  function previewPreset() {
    const preset = $('#preset').value;
    $('#presetPreview').textContent = `프리셋 미리보기: ${preset}`;
    showToast(`"${preset}" 프리셋 미리보기 적용`, "ok");
  }

  function bindEvents() {
    $('#btnRefresh').addEventListener('click', () => refreshState(true));
    $('#lnkRefresh').addEventListener('click', (e) => { e.preventDefault(); refreshState(true); });

    $('#btnSaveConfig').addEventListener('click', saveConfig);
    $('#btnSaveWifi').addEventListener('click', saveConfig);
    $('#btnScan').addEventListener('click', scanNetworks);
    $('#btnPreviewPreset').addEventListener('click', previewPreset);
    $('#btnUseScan').addEventListener('click', useScanResult);
    $('#btnDiag').addEventListener('click', () => fetchApi('/api/diag', 'GET', null, '진단 조회'));
    $('#btnLogs').addEventListener('click', () => fetchApi('/api/logs', 'GET', null, '로그 조회'));
    $('#btnReboot').addEventListener('click', () => { if (confirm("정말로 재부팅?")) fetchApi('/api/reboot', 'POST', null, '재부팅'); });
    $('#btnReset').addEventListener('click', () => { if (confirm("공장 초기화 진행?")) fetchApi('/api/reset', 'POST', null, '공장 초기화'); });

    $('#btnUpload').addEventListener('click', uploadStatic);
    $('#btnOTA').addEventListener('click', uploadOTA);

    $('#btnSaveApiKey').addEventListener('click', async () => {
      const newKey = $('#apiKeyInput').value.trim();
      if (!newKey) { showToast('API Key를 입력하세요.', 'warn'); return; }
      setKey(newKey);
      await fetchApi('/api/config', 'POST', { security: { api_key: newKey } }, 'API Key 저장');
      showToast('API Key 저장 완료', 'ok');
    });

    $('#btnSavePWM').addEventListener('click', savePWMConfig);
  }

  async function fetchApi(url, method = 'GET', body = null, desc = "작업") {
    setLoading(true);
    let resp = null;
    try {
      const opt = { method, headers: {} };
      if (body) {
        opt.body = JSON.stringify(body);
        opt.headers['Content-Type'] = 'application/json';
      }
      const k = getKey();
      if (k) opt.headers['X-API-Key'] = k;

      resp = await fetch(url, opt);
      const txt = await resp.text();

      if (resp.status === 401) {
        showToast(`[401] ${desc} 실패: 인증이 필요합니다.`, "err");
        throw new Error('Unauthorized');
      }
      if (!resp.ok) {
        showToast(`${desc} 실패: ${txt || resp.status}`, "err");
        throw new Error(txt || resp.status);
      }

      showToast(`${desc} 성공`, "ok");

      if (url.includes("/api/diag")) $('#diagOut').textContent = txt;
      if (url.includes("/api/logs")) $('#logsOut').textContent = txt;

      return txt;
    } catch (e) {
      if (e.message !== 'Unauthorized') showToast(`${desc} 실패: ${e.message}`, "err");
    } finally {
      setLoading(false);
    }
  }

  async function savePWMConfig() {
    const body = {
      hw: {
        pwm_pin: Number($('#pwm_pin').value),
        pwm_channel: Number($('#pwm_channel').value),
        pwm_freq: Number($('#pwm_freq').value),
        pwm_res: Number($('#pwm_res').value),
      }
    };
    // 간단 검증(ESP32 기반)
    if (body.hw.pwm_pin >= 6 && body.hw.pwm_pin <= 11) { showToast('GPIO6~11은 Flash용 핀으로 PWM 불가', 'warn'); return; }
    if (body.hw.pwm_pin >= 34) { showToast('GPIO34 이상은 입력전용으로 PWM 불가', 'warn'); return; }
    if (body.hw.pwm_channel < 0 || body.hw.pwm_channel > 7) { showToast('PWM 채널은 0~7', 'warn'); return; }
    if (body.hw.pwm_freq < 25000 || body.hw.pwm_freq > 40000) { showToast('PWM 주파수는 25,000~40,000Hz', 'warn'); return; }
    if (body.hw.pwm_res < 8 || body.hw.pwm_res > 16) { showToast('PWM 해상도는 8~16bit', 'warn'); return; }

    await fetchApi('/api/config', 'POST', body, 'PWM 설정 저장');
    refreshState();
  }

  async function refreshVersion() {
    try {
      const r = await fetch('/api/version');
      const j = await r.json();
      text($('#fwVer'), j.fw_version);
    } catch { text($('#fwVer'), 'version?'); }
  }

  async function refreshState() {
    setLoading(true);
    try {
      const r = await fetch('/api/state');
      const j = await r.json();
      g_config = j;

      // 상태
      text($('#simActive'), j.status.sim_active ? 'Active' : 'Idle');
      text($('#phase'), j.status.phase_name);
      text($('#wind'), j.status.wind_speed.toFixed(2) + ' m/s');
      text($('#pwm'), j.status.fan_pwm_percent.toFixed(1) + '%');
      text($('#wifiMode'), j.status.wifi_mode);
      text($('#curSsid'), j.status.ssid);
      text($('#ip'), j.status.ip_addr);

      // 프리셋
      const sel = $('#preset'); sel.innerHTML = '';
      j.presets.forEach(p => {
        const o = document.createElement('option'); o.value = p; o.textContent = p; sel.appendChild(o);
      });
      sel.value = j.config.sim.preset;

      // sim / timing 반영
      Object.keys(j.config.sim).forEach(k => { const el = $(`#${k}`); if (el) el.value = j.config.sim[k]; });
      Object.keys(j.config.timing).forEach(k => { const el = $(`#${k}`); if (el) el.value = j.config.timing[k]; });

      // wifi
      $('#wifi_mode').value = j.config.wifi.wifi_mode;
      $('#ap_ssid').value = j.config.wifi.ap_ssid;
      $('#ap_password').value = j.config.wifi.ap_password;
      displayStaNetworks(j.config.wifi.sta_networks);

      // hw → PWM 폼 기본값
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

  function displayStaNetworks(networks) {
    const list = $('#staList'); list.innerHTML = '';
    if (!networks || networks.length === 0) {
      list.innerHTML = `<div class="muted" style="text-align:center;">저장된 STA 네트워크가 없습니다.</div>`;
      return;
    }
    const table = document.createElement('table');
    table.innerHTML = `<thead><tr><th>SSID</th><th>PW</th><th></th></tr></thead><tbody></tbody>`;
    const tbody = table.querySelector('tbody');
    networks.forEach((n, index) => {
      const row = tbody.insertRow();
      row.innerHTML = `
        <td class="ssid" data-ssid="${n.ssid}">${n.ssid}</td>
        <td>${n.pass ? '********' : 'OPEN'}</td>
        <td class="right tight"><button class="btn err btn-remove-sta" data-index="${index}">삭제</button></td>
      `;
    });
    list.appendChild(table);
    list.querySelectorAll('.btn-remove-sta').forEach(btn => {
      btn.addEventListener('click', (e) => {
        const idx = Number(e.target.dataset.index);
        if (g_config.config && g_config.config.wifi && g_config.config.wifi.sta_networks) {
          const removed = g_config.config.wifi.sta_networks[idx]?.ssid || '';
          g_config.config.wifi.sta_networks.splice(idx, 1);
          showToast(`${removed} 목록에서 제거됨 (저장 필요)`, "warn");
          displayStaNetworks(g_config.config.wifi.sta_networks);
        }
      });
    });
  }

  function useScanResult() {
    const ssid = $('#scanList').value;
    const pass = $('#scanPass').value;
    if (!ssid) { showToast("SSID를 선택하세요.", "warn"); return; }
    if (pass.length > 0 && pass.length < 8) { showToast("비밀번호는 8자 이상 (OPEN 제외)", "err"); return; }

    if (g_config.config && g_config.config.wifi && g_config.config.wifi.sta_networks) {
      if (g_config.config.wifi.sta_networks.some(n => n.ssid === ssid)) { showToast(`"${ssid}"는 이미 목록에 있습니다.`, "warn"); return; }
      g_config.config.wifi.sta_networks.push({ ssid, pass });
      displayStaNetworks(g_config.config.wifi.sta_networks);
      $('#scanPass').value = '';
      showToast(`${ssid}를 저장 목록에 추가 (Wi-Fi 저장 버튼 필요)`, "ok");
    }
  }

  async function saveConfig() {
    const patchBody = {
      security: { api_key: getKey() }, // ✅ 백엔드 authorize와 동기화
      sim: {
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
      },
      timing: {
        sim_int: Number($('#sim_int').value),
        gust_int: Number($('#gust_int').value),
        thermal_int: Number($('#thermal_int').value),
      },
      wifi: {
        wifi_mode: Number($('#wifi_mode').value),
        ap_ssid: $('#ap_ssid').value,
        ap_password: $('#ap_password').value,
        sta_networks: (g_config.config && g_config.config.wifi && g_config.config.wifi.sta_networks) ? g_config.config.wifi.sta_networks : [],
      },
      hw: {
        pwm_pin: Number($('#pwm_pin').value),
        pwm_channel: Number($('#pwm_channel').value),
        pwm_freq: Number($('#pwm_freq').value),
        pwm_res: Number($('#pwm_res').value),
      }
    };
    await fetchApi('/api/config', 'POST', patchBody, '전체 설정 저장 및 적용');
    refreshState();
  }

  async function scanNetworks() {
    await fetchApi('/api/scan?async=true', 'GET', null, 'Wi-Fi 스캔 시작');
    showToast("Wi-Fi 스캔 중... 1.5초 후 결과 수집", "warn");
    await new Promise(r => setTimeout(r, 1500));

    const jTxt = await fetchApi('/api/scan', 'GET', null, 'Wi-Fi 스캔 결과 수집');
    if (jTxt) {
      try {
        const arr = JSON.parse(jTxt);
        const sel = $('#scanList');
        sel.innerHTML = '<option value="">-- 선택하세요 --</option>';
        if (arr.length === 0) {
          sel.innerHTML += '<option value="" disabled>스캔된 네트워크 없음</option>';
          showToast("스캔된 네트워크가 없습니다.", "warn");
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
      } catch (e) { showToast(`스캔 결과 파싱 실패: ${e.message}`, "err"); }
    }
  }

  async function uploadFile(inputSelector, url, msgSelector, desc, isOTA = false) {
    const fileInput = $(inputSelector);
    if (fileInput.files.length === 0) { showToast("업로드할 파일을 선택하세요.", "warn"); return; }
    if (isOTA && !confirm("펌웨어 OTA를 진행하시겠습니까? 실패 시 장치가 동작하지 않을 수 있습니다.")) return;

    setLoading(true);
    text($(msgSelector), `업로드 중... (파일 크기: ${fileInput.files[0].size} Bytes)`);

    const file = fileInput.files[0];
    const formData = new FormData();
    formData.append('file', file, file.name);

    try {
      const r = await fetch(url, { method: 'POST', body: formData });
      const txt = await r.text();
      if (!r.ok) throw new Error(`[${r.status}] ${txt || r.statusText}`);

      if (isOTA) {
        showToast(`${desc} 성공. 장치가 재부팅됩니다. 잠시 후 새로고침하세요.`, "ok");
        text($(msgSelector), "업데이트 성공! 장치 재부팅 중...");
        setTimeout(() => location.reload(), 5000);
      } else {
        showToast(`${desc} 성공: ${txt}`, "ok");
        text($(msgSelector), `업로드 성공! 서버 응답: ${txt}`);
      }
    } catch (e) {
      showToast(`${desc} 실패: ${e.message}`, "err");
      text($(msgSelector), `업로드 실패: ${e.message}`);
    } finally {
      if (!isOTA) setLoading(false);
    }
  }
  async function uploadStatic() { await uploadFile('#fileUpload', '/upload', '#uploadMsg', '정적 파일 업로드'); }
  async function uploadOTA() { await uploadFile('#fileOTA', '/update', '#otaMsg', '펌웨어 OTA 업데이트', true); }

})();
