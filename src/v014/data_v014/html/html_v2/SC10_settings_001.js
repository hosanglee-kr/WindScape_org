/*
 * ------------------------------------------------------
 * 소스명 : SC10_settings_001.js
 * 모듈명 : Smart Nature Wind System Settings Controller (v001)
 * ------------------------------------------------------
 * 기능 요약:
 * - 🎯 /api/system, /api/network, /api/auth, /api/control API 호출 및 데이터 표시
 * - 로컬 스토리지에 API Key 저장 및 인증 상태 표시
 * - 장치 제어 기능 (재부팅, 초기화) 구현
 * ------------------------------------------------------
 */

(() => {
    "use strict";

    // ======================= 1. 공통 헬퍼 함수 및 변수 =======================
    
    const $ = (s, r = document) => r.querySelector(s);
    const $$ = (s, r = document) => Array.from(r.querySelectorAll(s));

    // ************* 공통 기능 대체 *************
    const KEY_API = 'sc10_api_key';
    const getKey = () => localStorage.getItem(KEY_API) || '';
    const setKey = (key) => localStorage.setItem(KEY_API, key);
    const setLoading = (flag) => { 
        const el = $("#loadingOverlay");
        if (el) el.style.display = flag ? "flex" : "none";
    };
    const showToast = (msg, type = "ok") => { console.log(`[TOAST] ${type}: ${msg}`); };
    
    // API Fetch 래퍼 함수 (인증 키 포함)
    async function fetchApi(url, method = "GET", body = null, desc = "작업") {
        setLoading(true);
        const opt = { method, headers: {} };
        const k = getKey();
        if (k) opt.headers["X-API-Key"] = k;

        if (body) {
            opt.body = JSON.stringify(body);
            opt.headers["Content-Type"] = "application/json";
        }

        try {
            const resp = await fetch(url, opt);
            if (resp.status === 401) {
                showToast(`[401] ${desc} 실패: 인증 실패 (API Key 확인 필요)`, "err");
                $("#apiKeyStatus").textContent = "인증 실패";
                $("#apiKeyStatus").className = "info-label err";
                throw new Error("Unauthorized");
            }
            if (!resp.ok) {
                const txt = await resp.text();
                showToast(`${desc} 실패: ${txt || resp.status}`, "err");
                throw new Error(txt || resp.status);
            }
            if (method !== 'GET') showToast(`${desc} 성공`, "ok");
            
            const txt = await resp.text();
            try { return JSON.parse(txt); } catch { return txt; }
        } catch (e) {
            if (e.message !== "Unauthorized") console.error(e);
            return null;
        } finally {
            setLoading(false);
        }
    }
    // *************************************************************************

    // ======================= 2. 데이터 로드 및 렌더링 =======================

    // ✅ 시스템 정보 로드
    async function loadSystemInfo() {
        // GET /api/system/info
        const data = await fetchApi("/api/system/info", "GET", null, "시스템 정보 로드");
        if (data) {
            $("#fwVersion").textContent = data.version || "V1.0.0";
            $("#osName").textContent = data.platform || "ESP32-Arduino-v7";
            $("#uptime").textContent = data.uptime || "0m 0s";
            
            // 네트워크 정보도 함께 로드 (동기화)
            $("#ipAddress").textContent = data.ip_address || "0.0.0.0";
            $("#wifiSsid").textContent = data.ssid || "연결 안 됨";
            $("#netMode").textContent = data.mode || "AP";
            
            $("#apiKeyStatus").textContent = getKey() ? "저장됨 (확인 필요)" : "설정 필요";
            $("#apiKeyStatus").className = getKey() ? "info-label warn" : "info-label err";
        }
    }

    // ======================= 3. API Key 관리 및 인증 =======================
    
    function openApiKeyModal() {
        $("#apiKeyModal").style.display = "flex";
    }
    
    function closeApiKeyModal() {
        $("#apiKeyModal").style.display = "none";
        $("#newApiKey").value = "";
    }

    async function saveApiKey(event) {
        event.preventDefault();
        const newKey = $("#newApiKey").value.trim();

        if (newKey) {
            // 키를 로컬 스토리지에 저장
            setKey(newKey);
            showToast("API Key가 로컬에 저장되었습니다. 인증 테스트를 진행합니다.", "ok");
            closeApiKeyModal();
            await checkAuth(); // 저장 후 즉시 인증 테스트
        } else {
            showToast("유효한 API Key를 입력해야 합니다.", "err");
        }
    }
    
    async function checkAuth() {
        // GET /api/auth/test (인증 테스트용 더미 API)
        const result = await fetchApi("/api/auth/test", "GET", null, "인증 테스트");
        
        const statusEl = $("#apiKeyStatus");
        
        if (result && result.authenticated) {
            statusEl.textContent = "✅ 인증 성공";
            statusEl.className = "info-label ok";
        } else {
            // fetchApi가 401을 처리하여 이미 에러 메시지를 띄웠을 수 있음
            if (getKey()) {
                statusEl.textContent = "인증 실패 (키 만료/오류)";
                statusEl.className = "info-label err";
            } else {
                statusEl.textContent = "설정 필요";
                statusEl.className = "info-label warn";
            }
        }
    }

    // ======================= 4. 장치 제어 기능 =======================

    async function handleDeviceControl(event) {
        const target = event.target;
        let url = "";
        let confirmMsg = "";
        let successMsg = "";
        
        if (target.id === 'btnConfigSave') {
            url = "/api/config/save";
            confirmMsg = "현재 설정값들을 장치 메모리에 영구 저장하시겠습니까?";
            successMsg = "설정 파일 저장 성공";
        } else if (target.id === 'btnReboot') {
            url = "/api/control/reboot";
            confirmMsg = "장치를 재부팅하시겠습니까? (연결이 끊어집니다)";
            successMsg = "장치 재부팅 요청됨. 잠시 후 다시 접속해 주세요.";
        } else if (target.id === 'btnFactoryReset') {
            url = "/api/control/factoryReset";
            confirmMsg = "경고: 모든 설정(네트워크, 프로파일, 스케줄 등)을 공장 초기화하고 재부팅하시겠습니까? 되돌릴 수 없습니다.";
            successMsg = "공장 초기화 요청됨. 장치가 재부팅됩니다.";
        } else {
            return;
        }

        if (confirm(confirmMsg)) {
            const result = await fetchApi(url, "POST", null, target.textContent.trim());
            if (result) {
                showToast(successMsg, "warn");
                // 재부팅/리셋 후에는 페이지를 새로고침하거나 연결 대기 화면으로 이동해야 함.
                if (target.id === 'btnReboot' || target.id === 'btnFactoryReset') {
                    setTimeout(() => window.location.reload(), 5000); 
                }
            }
        }
    }

    // ======================= 5. 이벤트 바인딩 및 초기화 =======================

    function bindEvents() {
        // API Key 모달 관련
        $("#btnSetApiKey")?.addEventListener('click', openApiKeyModal);
        $("#btnCheckAuth")?.addEventListener('click', checkAuth);
        $("#apiKeyForm")?.addEventListener('submit', saveApiKey);
        $("#btnCloseApiKeyModal")?.addEventListener('click', closeApiKeyModal);
        $("#btnCancelApiKeyModal")?.addEventListener('click', closeApiKeyModal);

        // 장치 제어 관련
        $("#btnConfigSave")?.addEventListener('click', handleDeviceControl);
        $("#btnReboot")?.addEventListener('click', handleDeviceControl);
        $("#btnFactoryReset")?.addEventListener('click', handleDeviceControl);
        
        // 펌웨어 및 네트워크 버튼 (실제 로직은 생략하고 토스트만 표시)
        $("#btnCheckUpdate")?.addEventListener('click', () => {
            showToast("펌웨어 업데이트 서버 확인 기능은 추후 구현 예정입니다.", "info");
        });
        $("#btnNetworkSetup")?.addEventListener('click', () => {
            showToast("네트워크 설정 페이지는 추후 구현 예정입니다.", "info");
        });
        $("#btnTimeSetup")?.addEventListener('click', () => {
            showToast("시간 및 NTP 설정 기능은 추후 구현 예정입니다.", "info");
        });
    }

    document.addEventListener("DOMContentLoaded", () => {
        bindEvents();
        loadSystemInfo(); // 페이지 로드 시 정보 자동 로드
    });

})();
