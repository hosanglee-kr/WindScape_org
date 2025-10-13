// SC10_main_013.js

(() => {
    "use strict";

    const $ = (sel, root = document) => root.querySelector(sel);
    const text = (el, v) => el && (el.textContent = v);

    const KEY_K = 'my_secure_api_key_123';
    const getKey = () => localStorage.getItem(KEY_K) || '';
    const setKey = (k) => localStorage.setItem(KEY_K, k);


    document.addEventListener("DOMContentLoaded", () => {
        bindEvents();
        refreshVersion();
        refreshState(true); // 초기 설정 로드 및 상태 표시
    });

    // --------- 전역 설정 객체 (상태 저장용) ---------
    // 서버 응답에서 받은 설정 데이터를 임시로 저장하여 UI 상태와 비교, 관리에 사용
    let g_config = {};

    // --------- 토스트 ---------
    function showToast(msg, type = "ok") {
        const cont = $("#toastContainer");
        const div = document.createElement("div");
        div.className = `toast ${type}`;
        div.textContent = msg;
        cont.appendChild(div);
        // 3초 후 애니메이션을 위해 10ms 후 제거
        setTimeout(() => div.remove(), 3000);
    }

    // --------- 로딩 ---------
    function setLoading(flag) {
        const ov = $("#loadingOverlay");
        ov.style.display = flag ? "flex" : "none";
    }

    // --------- 프리셋 미리보기 ---------
    function previewPreset() {
        const preset = $('#preset').value;
        $('#presetPreview').textContent = `프리셋 미리보기: ${preset}`;
        showToast(`"${preset}" 프리셋 미리보기 적용`, "ok");
        // 실제로는 프리셋 JSON을 서버에서 가져와 하단 상세 설정값들을 UI에 반영해야 함.
        // 여기서는 UI 표시만 업데이트합니다.
    }

    // --------- 이벤트 바인딩 ---------
    function bindEvents() {
        $('#btnRefresh').addEventListener('click', () => refreshState(true));
        $('#lnkRefresh').addEventListener('click', (e) => {
            e.preventDefault();
            refreshState(true)
        });
        // SC10_ConfigManager::patchFromJson 에 대응하여 통합 설정 저장 함수 바인딩
        $('#btnSaveConfig').addEventListener('click', saveConfig);
        $('#btnSaveWifi').addEventListener('click', saveConfig);
        $('#btnScan').addEventListener('click', scanNetworks);
        $('#btnPreviewPreset').addEventListener('click', previewPreset);
        $('#btnUseScan').addEventListener('click', useScanResult); // 스캔 결과 추가
        $('#btnDiag').addEventListener('click', () => fetchApi('/api/diag', 'GET', null, '진단 조회'));
        $('#btnLogs').addEventListener('click', () => fetchApi('/api/logs', 'GET', null, '로그 조회'));
        $('#btnReboot').addEventListener('click', () => {
            if (confirm("정말로 장치를 재부팅하시겠습니까?")) fetchApi('/api/reboot', 'POST', null, '재부팅');
        });
        $('#btnReset').addEventListener('click', () => {
            if (confirm("경고: 모든 설정이 공장 초기화됩니다. 계속하시겠습니까?")) fetchApi('/api/reset', 'POST', null, '공장 초기화');
        });
        $('#btnUpload').addEventListener('click', uploadStatic);
        $('#btnOTA').addEventListener('click', uploadOTA);

        // Wi-Fi 목록 삭제는 동적 바인딩 (refreshState -> displayStaNetworks 함수 내부)
    }

    // --------- API 유틸리티 ---------
    async function fetchApi(url, method = 'GET', body = null, desc = "작업") {
        setLoading(true);
        try {
            const opt = {
                method,
                headers: {}
            };
            if (body) {
                opt.body = JSON.stringify(body);
                opt.headers['Content-Type'] = 'application/json';
            }

            // ★ API Key 자동 주입
            const k = getKey();
            if (k) opt.headers['X-API-Key'] = k;
            
            // API Key가 설정되어 있다면 헤더에 추가 (C/C++ 백엔드 로직에 대응)
            // if (g_config.security && g_config.security.api_key_set) {
            //    // 실제 키 값은 노출하지 않고, UI에서 입력받거나 저장되어야 합니다.
            //     // 여기서는 간단히 키를 설정하는 텍스트 입력 필드가 없으므로, 설정하지 않는다고 가정합니다.
            // }

            const r = await fetch(url, opt);
            const txt = await r.text();

            if (r.status === 401) {
                showToast(`[401] ${desc} 실패: 인증이 필요합니다.`, "err");
                throw new Error('Unauthorized');
            }
            if (!r.ok) {
                // 서버에서 상세 에러 메시지가 온 경우 사용
                showToast(`${desc} 실패: ${txt || r.status}`, "err");
                throw new Error(txt || r.status);
            }

            showToast(`${desc} 성공`, "ok");

            // 진단/로그 응답 처리
            if (url.includes("diag")) $('#diagOut').textContent = txt;
            if (url.includes("logs")) $('#logsOut').textContent = txt;

            return txt;
        } catch (e) {
            // 이미 토스트 처리된 401 외의 오류만 처리
            if (e.message !== 'Unauthorized') showToast(`${desc} 실패: ${e.message}`, "err");
        } finally {
            setLoading(false);
        }
    }

    // --------- 상태 및 설정 로드 ---------
    async function refreshVersion() {
        try {
            const r = await fetch('/api/version');
            const j = await r.json();
            text($('#fwVer'), j.fw_version);
        } catch {
            text($('#fwVer'), 'version?');
        }
    }

    // 상태 및 설정 동기화
    async function refreshState() {
        setLoading(true);
        try {
            const r = await fetch('/api/state');
            const j = await r.json();
            g_config = j; // 전역 설정 객체 업데이트

            // 1. 상태 표시
            text($('#simActive'), j.status.sim_active ? 'Active' : 'Idle');
            text($('#phase'), j.status.phase_name);
            text($('#wind'), j.status.wind_speed.toFixed(2) + ' m/s');
            text($('#pwm'), j.status.fan_pwm_percent.toFixed(1) + '%'); // PWM 값을 백분율로
            text($('#wifiMode'), j.status.wifi_mode);
            text($('#curSsid'), j.status.ssid);
            text($('#ip'), j.status.ip_addr);

            // 2. 시뮬레이션 설정 UI 채우기
            // 프리셋 드롭다운 채우기
            const sel = $('#preset');
            sel.innerHTML = '';
            j.presets.forEach(p => {
                let o = document.createElement('option');
                o.value = p;
                o.textContent = p;
                sel.appendChild(o);
            });
            // 현재 값 반영
            sel.value = j.config.sim.preset;
            // 상세 설정 필드 채우기
            Object.keys(j.config.sim).forEach(key => {
                const el = $(`#${key}`);
                if (el) el.value = j.config.sim[key];
            });
            // 타이밍 설정 필드 채우기
            Object.keys(j.config.timing).forEach(key => {
                const el = $(`#${key}`);
                if (el) el.value = j.config.timing[key];
            });

            // 3. Wi-Fi 설정 UI 채우기
            $('#wifi_mode').value = j.config.wifi.wifi_mode;
            $('#ap_ssid').value = j.config.wifi.ap_ssid;
            $('#ap_password').value = j.config.wifi.ap_password;
            displayStaNetworks(j.config.wifi.sta_networks);

        } catch (e) {
            showToast(`상태 불러오기 실패: ${e.message}`, "err");
        } finally {
            setLoading(false);
        }
    }

    // 저장된 STA 네트워크 목록을 UI에 표시 및 삭제 버튼 바인딩
    function displayStaNetworks(networks) {
        const list = $('#staList');
        list.innerHTML = '';

        if (networks.length === 0) {
            list.innerHTML = `<div class="muted" style="text-align:center;">저장된 STA 네트워크가 없습니다.</div>`;
            return;
        }

        const table = document.createElement('table');
        table.innerHTML = `<thead><tr><th>SSID</th><th>PW</th><th></th></tr></thead><tbody></tbody>`;
        const tbody = table.querySelector('tbody');

        networks.forEach((n, index) => {
            const row = tbody.insertRow();
            // 패스워드는 표시하지 않고, '********'로 대체
            row.innerHTML = `
        <td class="ssid" data-ssid="${n.ssid}">${n.ssid}</td>
        <td>${n.pass ? '********' : 'OPEN'}</td>
        <td class="right tight">
          <button class="btn err btn-remove-sta" data-index="${index}">삭제</button>
        </td>
      `;
        });
        list.appendChild(table);

        // 삭제 이벤트 동적 바인딩
        list.querySelectorAll('.btn-remove-sta').forEach(btn => {
            btn.addEventListener('click', (e) => {
                const indexToRemove = parseInt(e.target.dataset.index);

                // g_config에서 해당 네트워크 제거 후 UI 갱신
                if (g_config.config && g_config.config.wifi && g_config.config.wifi.sta_networks) {
                    g_config.config.wifi.sta_networks.splice(indexToRemove, 1);
                    showToast(`${networks[indexToRemove].ssid} 목록에서 제거됨 (저장 필요)`, "warn");
                    // UI를 다시 그려서 인덱스를 업데이트합니다.
                    displayStaNetworks(g_config.config.wifi.sta_networks);
                }
            });
        });
    }

    // 스캔 목록에서 선택된 SSID를 저장 목록에 추가
    function useScanResult() {
        const ssid = $('#scanList').value;
        const pass = $('#scanPass').value;

        if (!ssid) {
            showToast("SSID를 선택하세요.", "warn");
            return;
        }

        // 비밀번호 길이 체크 (WPA/WPA2는 최소 8자 필요)
        if (pass.length > 0 && pass.length < 8) {
            showToast("비밀번호는 8자 이상이어야 합니다 (OPEN 제외).", "err");
            return;
        }

        // g_config의 임시 목록에 추가
        if (g_config.config && g_config.config.wifi && g_config.config.wifi.sta_networks) {
            // 중복 체크
            if (g_config.config.wifi.sta_networks.some(n => n.ssid === ssid)) {
                showToast(`"${ssid}"는 이미 목록에 있습니다.`, "warn");
                return;
            }

            g_config.config.wifi.sta_networks.push({
                ssid: ssid,
                pass: pass
            });
            displayStaNetworks(g_config.config.wifi.sta_networks); // UI 갱신
            $('#scanPass').value = ''; // 비밀번호 필드 초기화
            showToast(`${ssid}를 저장 목록에 추가했습니다. (Wi-Fi 저장 버튼 필요)`, "ok");
        }
    }

    // --------- 저장 ---------
    async function saveConfig() {
        // 1. 시뮬레이션 및 타이밍 설정 수집 (PATCH JSON 객체 생성)
        const patchBody = {
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
            // 2. Wi-Fi 설정 수집 (g_config의 최신 목록 사용)
            wifi: {
                wifi_mode: Number($('#wifi_mode').value),
                ap_ssid: $('#ap_ssid').value,
                ap_password: $('#ap_password').value,
                sta_networks: g_config.config.wifi.sta_networks || [], // g_config에 저장된 임시 목록 사용
            }
        };

        await fetchApi('/api/config', 'POST', patchBody, '전체 설정 저장 및 적용');
        refreshState(); // 저장 후 상태 재갱신
    }

    // --------- 스캔 ---------
    async function scanNetworks() {
        // 1차: 스캔 트리거 (SC10_WiFiManager::scanNetworksJson(true)에 대응)
        await fetchApi('/api/scan?async=true', 'GET', null, 'Wi-Fi 스캔 시작');
        showToast("Wi-Fi 스캔 중... 1.5초 후 결과 수집", "warn");

        // 스캔 시간 대기 (SC10_WiFiManager::scanNetworksJson의 사용법에 대응)
        await new Promise(resolve => setTimeout(resolve, 1500));

        // 2차: 결과 수집 (SC10_WiFiManager::scanNetworksJson(false)에 대응)
        const jTxt = await fetchApi('/api/scan', 'GET', null, 'Wi-Fi 스캔 결과 수집');

        if (jTxt) {
            try {
                const arr = JSON.parse(jTxt);
                const sel = $('#scanList');
                sel.innerHTML = '<option value="">-- 선택하세요 --</option>'; // 기본 옵션

                if (arr.length === 0) {
                    sel.innerHTML += '<option value="" disabled>스캔된 네트워크 없음</option>';
                    showToast("스캔된 네트워크가 없습니다.", "warn");
                    return;
                }

                // RSSI가 높은 순으로 정렬 (더 강한 신호 우선)
                arr.sort((a, b) => b.rssi - a.rssi);

                arr.forEach(n => {
                    const o = document.createElement('option');
                    // RSSI, 암호화 타입(enc)을 옵션 텍스트에 포함하여 정보 제공
                    o.value = n.ssid;
                    o.textContent = `${n.ssid} [${n.rssi} dBm, ${n.enc}]`;
                    sel.appendChild(o);
                });
                showToast(`총 ${arr.length}개 네트워크 스캔 완료`, "ok");
            } catch (e) {
                showToast(`스캔 결과 파싱 실패: ${e.message}`, "err");
            }
        }
    }

    // --------- 파일 업로드 (OTA 포함) ---------
    async function uploadFile(inputSelector, url, msgSelector, desc, isOTA = false) {
        const fileInput = $(inputSelector);
        if (fileInput.files.length === 0) {
            showToast("업로드할 파일을 선택하세요.", "warn");
            return;
        }

        if (isOTA && !confirm("펌웨어 OTA를 진행하시겠습니까? 실패 시 장치가 동작하지 않을 수 있습니다.")) {
            return;
        }

        setLoading(true);
        text($(msgSelector), `업로드 중... (파일 크기: ${fileInput.files[0].size} Bytes)`);

        const file = fileInput.files[0];
        const formData = new FormData();
        // ESPAsyncWebServer의 핸들러에 맞게 'file' 필드 사용
        formData.append('file', file, file.name);

        try {
            const r = await fetch(url, {
                method: 'POST',
                body: formData,
                // FormData 사용 시 Content-Type 헤더는 자동으로 설정됩니다 (boundary 포함)
            });

            const txt = await r.text();

            if (!r.ok) {
                throw new Error(`[${r.status}] ${txt || r.statusText}`);
            }

            // OTA 업데이트 성공 후 재부팅 처리 (SC10_WebAPI::mountApi 로직에 대응)
            if (isOTA) {
                showToast(`${desc} 성공. 장치가 재부팅됩니다. 잠시 후 새로고침하세요.`, "ok");
                text($(msgSelector), "업데이트 성공! 장치 재부팅 중...");
                // 5초 후 새로고침 시도
                setTimeout(() => { location.reload(); }, 5000);
            } else {
                showToast(`${desc} 성공: ${txt}`, "ok");
                text($(msgSelector), `업로드 성공! 서버 응답: ${txt}`);
            }

        } catch (e) {
            showToast(`${desc} 실패: ${e.message}`, "err");
            text($(msgSelector), `업로드 실패: ${e.message}`);
        } finally {
            // OTA 성공 시에는 로딩을 계속 유지하고 새로고침해야 하므로 제외
            if (!isOTA || !r || !r.ok) {
                setLoading(false);
            }
        }
    }

    // SC10_main_012.js - 버튼에 연결할 최종 함수 정의
    async function uploadStatic() {
        await uploadFile('#fileUpload', '/upload', '#uploadMsg', '정적 파일 업로드');
    }

    async function uploadOTA() {
        await uploadFile('#fileOTA', '/update', '#otaMsg', '펌웨어 OTA 업데이트', true);
    }

})();
