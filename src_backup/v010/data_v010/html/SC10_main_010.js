// SC10_main_010.js

/* SC10_main_010.js
 * - SC10 관리자용 UI 스크립트 (모든 주석 한글)
 * - /api/state 읽기 → 폼 반영
 * - /api/config POST → 설정 저장
 * - /api/scan, /api/diag, /api/logs, /api/reboot, /api/reset
 * - /upload 정적 파일 업로드, /update 펌웨어 OTA
 */

(() => {
    "use strict";

    // ---------- DOM 헬퍼 ----------
    const $ = (sel, root = document) => root.querySelector(sel);
    const $$ = (sel, root = document) => Array.from(root.querySelectorAll(sel));
    const text = (el, v) => el && (el.textContent = v);
    const val = (el, v) => (v === undefined ? el.value : (el.value = v));
    const num = (el) => (el.value === "" ? null : Number(el.value));

    // ---------- 전역 상태 ----------
    let g_state = null;          // /api/state 결과 캐시
    let g_staList = [];          // STA 목록 편집용 (UI)
    let g_presets = [];          // 프리셋 목록

    // ---------- 초기화 ----------
    document.addEventListener('DOMContentLoaded', async () => {
        bindEvents();
        await refreshVersion();
        await refreshState(true);
    });

    function bindEvents() {
        $('#btnRefresh').addEventListener('click', () => refreshState(true));
        $('#lnkRefresh').addEventListener('click', (e) => { e.preventDefault(); refreshState(true); });

        $('#btnSaveConfig').addEventListener('click', saveSimConfig);
        $('#btnSaveWifi').addEventListener('click', saveWifiConfig);

        $('#btnScan').addEventListener('click', scanNetworks);
        $('#btnAddSta').addEventListener('click', addStaEntry);
        $('#btnClearSta').addEventListener('click', () => { g_staList = []; renderStaList(); });

        $('#btnUseScan').addEventListener('click', useSelectedScan);

        $('#btnDiag').addEventListener('click', showDiag);
        $('#btnLogs').addEventListener('click', showLogs);
        $('#btnReboot').addEventListener('click', rebootDevice);
        $('#btnReset').addEventListener('click', resetFactory);

        $('#btnUpload').addEventListener('click', uploadStaticFile);
        $('#btnOTA').addEventListener('click', uploadOTA);
    }

    // ---------- 상태 새로고침 ----------
    async function refreshVersion() {
        try {
            const r = await fetch('/api/version');
            const j = await r.json();
            text($('#fwVer'), j.fw_version || 'unknown');
        } catch (e) {
            text($('#fwVer'), 'version?');
        }
    }

    async function refreshState(withFormApply = false) {
        try {
            const r = await fetch('/api/state');
            g_state = await r.json();
            // 상단 상태
            const st = g_state.status || {};
            text($('#simActive'), st.sim_active ? 'Active' : 'Idle');
            text($('#phase'), st.phase_name || '-');
            text($('#wind'), st.wind_speed ?? '-');
            text($('#pwm'), st.fan_pwm ?? '-');
            text($('#wifiMode'), st.wifi_mode || '-');
            text($('#curSsid'), st.ssid || '-');
            text($('#ip'), st.ip_addr || '-');

            // 프리셋/설정 반영
            const cfg = g_state.config || {};
            g_presets = g_state.presets || [];
            renderPresets(cfg?.sim?.preset);

            if (withFormApply) applyConfigToForm(cfg);
        } catch (e) {
            console.error(e);
            alert('상태를 불러오지 못했습니다.');
        }
    }

    function renderPresets(curName) {
        const sel = $('#preset');
        sel.innerHTML = '';
        g_presets.forEach(nm => {
            const opt = document.createElement('option');
            opt.value = nm; opt.textContent = nm;
            if (nm === curName) opt.selected = true;
            sel.appendChild(opt);
        });
    }

    function applyConfigToForm(cfg) {
        // 시뮬레이션 파라미터
        const s = cfg.sim || {};
        if ($('#preset').options.length) $('#preset').value = s.preset || $('#preset').options[0].value;
        val($('#intensity'), s.intensity ?? 100);
        val($('#gust_freq'), s.gust_freq ?? 30);
        val($('#variability'), s.variability ?? 40);
        val($('#fan_limit'), s.fan_limit ?? 80);
        val($('#min_fan'), s.min_fan ?? 0);
        val($('#turb_len'), s.turb_len ?? 30);
        val($('#turb_sig'), s.turb_sig ?? 0.3);
        val($('#therm_str'), s.therm_str ?? 1.8);
        val($('#therm_rad'), s.therm_rad ?? 15);

        // 타이밍
        const t = cfg.timing || {};
        val($('#sim_int'), t.sim_int ?? 250);
        val($('#gust_int'), t.gust_int ?? 500);
        val($('#thermal_int'), t.thermal_int ?? 2000);

        // Wi-Fi
        const w = cfg.wifi || {};
        val($('#wifi_mode'), w.wifi_mode ?? 0);
        val($('#ap_ssid'), w.ap_ssid ?? 'SC10_Config_AP');
        // 보안상 상태에서 ap_password 비노출 권장. 입력칸은 빈 값 유지.
        $('#ap_password').value = '';

        // STA 목록 (SSID만 내려오므로, 비번은 사용자가 입력)
        g_staList = (w.sta_networks || []).map(n => ({ ssid: n.ssid || '', pass: '' }));
        renderStaList();
    }

    // ---------- STA 목록 UI ----------
    function renderStaList() {
        const host = $('#staList');
        host.innerHTML = '';
        if (!g_staList.length) {
            host.innerHTML = `<div class="muted">등록된 STA 네트워크가 없습니다.</div>`;
            return;
        }
        const tbl = document.createElement('table');
        tbl.innerHTML = `<thead><tr><th>SSID</th><th>비밀번호(저장시에만 반영)</th><th></th></tr></thead>`;
        const tb = document.createElement('tbody');
        g_staList.forEach((it, idx) => {
            const tr = document.createElement('tr');
            tr.innerHTML = `
        <td><input value="${esc(it.ssid)}" data-k="ssid" data-i="${idx}"></td>
        <td><input type="password" placeholder="변경 시 입력" data-k="pass" data-i="${idx}"></td>
        <td class="nowrap">
          <button class="btn" data-act="up"   data-i="${idx}">▲</button>
          <button class="btn" data-act="down" data-i="${idx}">▼</button>
          <button class="btn err" data-act="del"  data-i="${idx}">삭제</button>
        </td>`;
            tb.appendChild(tr);
        });
        tbl.appendChild(tb);
        host.appendChild(tbl);

        // 이벤트 바인딩
        host.addEventListener('input', onStaEdit);
        host.addEventListener('click', onStaClick);
    }
    function onStaEdit(e) {
        const t = e.target;
        if (!t.dataset || t.dataset.i === undefined) return;
        const i = Number(t.dataset.i);
        const k = t.dataset.k;
        if (k === 'ssid') g_staList[i].ssid = t.value;
        if (k === 'pass') g_staList[i].pass = t.value;
    }
    function onStaClick(e) {
        const t = e.target.closest('button');
        if (!t || t.dataset.i === undefined) return;
        const i = Number(t.dataset.i);
        const act = t.dataset.act;
        if (act === 'del') g_staList.splice(i, 1);
        if (act === 'up' && i > 0) [g_staList[i - 1], g_staList[i]] = [g_staList[i], g_staList[i - 1]];
        if (act === 'down' && i < g_staList.length - 1) [g_staList[i + 1], g_staList[i]] = [g_staList[i], g_staList[i + 1]];
        renderStaList();
    }
    function addStaEntry() {
        g_staList.push({ ssid: '', pass: '' });
        renderStaList();
    }

    // ---------- 스캔/선택 추가 ----------
    async function scanNetworks() {
        try {
            const r = await fetch('/api/scan');
            const arr = await r.json();
            const sel = $('#scanList');
            sel.innerHTML = '';
            (arr || []).forEach(n => {
                const opt = document.createElement('option');
                opt.value = n.ssid;
                opt.textContent = `${n.ssid}  (RSSI:${n.rssi}, CH:${n.chan})`;
                sel.appendChild(opt);
            });
        } catch (e) {
            alert('스캔 실패');
        }
    }

    function useSelectedScan() {
        const ssid = $('#scanList').value;
        const pass = $('#scanPass').value || '';
        if (!ssid) return alert('SSID를 선택하세요.');
        g_staList.push({ ssid, pass });
        $('#scanPass').value = '';
        renderStaList();
    }

    // ---------- 설정 저장 (시뮬 파라미터) ----------
    async function saveSimConfig() {
        const body = collectPatchBody(false);
        await postConfig(body);
    }

    // ---------- Wi-Fi 저장 ----------
    async function saveWifiConfig() {
        const body = collectPatchBody(true);
        await postConfig(body);
    }

    function collectPatchBody(includeWifi) {
        const body = {};

        // 프리셋/시뮬
        body.preset = $('#preset').value;
        body.intensity = num($('#intensity'));
        body.gust_freq = num($('#gust_freq'));
        body.variability = num($('#variability'));
        body.fan_limit = num($('#fan_limit'));
        body.min_fan = num($('#min_fan'));
        body.turb_len = num($('#turb_len'));
        body.turb_sig = Number($('#turb_sig').value);
        body.therm_str = Number($('#therm_str').value);
        body.therm_rad = num($('#therm_rad'));

        body.sim_int = num($('#sim_int'));
        body.gust_int = num($('#gust_int'));
        body.thermal_int = num($('#thermal_int'));

        if (includeWifi) {
            body.wifi_mode = Number($('#wifi_mode').value);
            body.ap_ssid = $('#ap_ssid').value;
            const apPass = $('#ap_password').value;
            if (apPass && apPass.length) body.ap_password = apPass;

            // STA: 입력된 값만 → 빈 SSID 제거
            const sta = g_staList.filter(x => x.ssid && x.ssid.trim().length > 0)
                .map(x => ({ ssid: x.ssid.trim(), pass: x.pass || "" }));
            body.sta_networks = sta;
        }
        return body;
    }

    async function postConfig(body) {
        try {
            const r = await fetch('/api/config', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(body)
            });
            if (!r.ok) throw new Error('bad status');
            alert('저장되었습니다. (Wi-Fi 변경 시 자동 재연결될 수 있습니다)');
            await refreshState(true);
        } catch (e) {
            console.error(e);
            alert('저장 실패');
        }
    }

    // ---------- 진단/로그 ----------
    async function showDiag() {
        try {
            const r = await fetch('/api/diag'); const j = await r.json();
            $('#diagOut').textContent = JSON.stringify(j, null, 2);
        } catch (e) { $('#diagOut').textContent = '실패'; }
    }
    async function showLogs() {
        try {
            const r = await fetch('/api/logs'); const j = await r.json();
            $('#logsOut').textContent = JSON.stringify(j, null, 2);
        } catch (e) { $('#logsOut').textContent = '실패'; }
    }

    // ---------- 재부팅/리셋 ----------
    async function rebootDevice() {
        if (!confirm('기기를 재부팅할까요?')) return;
        try {
            await fetch('/api/reboot', { method: 'POST' });
            alert('재부팅 명령 전송. 잠시 후 접속을 다시 시도하세요.');
        } catch (e) { alert('실패'); }
    }
    async function resetFactory() {
        if (!confirm('공장 초기화(설정 삭제 후 재부팅) 하시겠어요?')) return;
        try {
            await fetch('/api/reset', { method: 'POST' });
            alert('초기화 명령 전송. 잠시 후 재연결하세요.');
        } catch (e) { alert('실패'); }
    }

    // ---------- 업로드 ----------
    async function uploadStaticFile() {
        const f = $('#fileUpload').files[0];
        if (!f) return alert('파일을 선택하세요.');
        const fd = new FormData();
        fd.append('file', f, f.name);
        try {
            const r = await fetch('/upload', { method: 'POST', body: fd });
            if (!r.ok) throw 0;
            $('#uploadMsg').textContent = `업로드 완료: ${f.name}`;
        } catch (e) {
            $('#uploadMsg').textContent = '업로드 실패';
        }
    }
    async function uploadOTA() {
        const f = $('#fileOTA').files[0];
        if (!f) return alert('펌웨어(.bin)를 선택하세요.');
        if (!confirm('펌웨어를 업로드하고 재부팅합니다. 계속할까요?')) return;
        const fd = new FormData();
        fd.append('file', f, f.name);
        try {
            const r = await fetch('/update', { method: 'POST', body: fd });
            if (!r.ok) throw 0;
            $('#otaMsg').textContent = 'OTA 업로드 완료. 기기가 재부팅됩니다.';
        } catch (e) {
            $('#otaMsg').textContent = 'OTA 업로드 실패';
        }
    }

    // ---------- 유틸 ----------
    function esc(s) {
        return String(s).replace(/[&<>"']/g, m => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[m]));
    }

})();


