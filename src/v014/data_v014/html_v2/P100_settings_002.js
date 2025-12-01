/*
 * ------------------------------------------------------
 * 소스명 : P100_settings_002.js
 * 모듈명 : Smart Nature Wind System Settings Controller (v001)
 * ------------------------------------------------------
 * 기능 요약:
 * - 🎯 /api/system, /api/network, /api/auth, /api/control API 호출 및 데이터 표시
 * - 로컬 스토리지에 API Key 저장 및 인증 상태 표시
 * - 장치 제어 기능 (재부팅, 초기화, 설정 저장) 구현
 * - [추가됨] 네트워크 설정, 시간 설정, 펌웨어 업데이트 확인 기능 구현
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
    // Toast 메시지 구현 (console.log를 실제 Toast UI로 대체해야 함)
    const showToast = (msg, type = "ok") => { 
        console.log(`[TOAST] ${type}: ${msg}`); 
        // 실제 구현: UI 요소에 메시지 표시
        // const toastEl = $("#toastMessage");
        // if (toastEl) { toastEl.textContent = msg; toastEl.className = `toast ${type}`; }
    };
    
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
            
            // 네트워크 정보도 함께 로드
            $("#ipAddress").textContent = data.ip_address || "0.0.0.0";
            $("#wifiSsid").textContent = data.ssid || "연결 안 됨";
            $("#netMode").textContent = data.mode || "AP";
            
            $("#apiKeyStatus").textContent = getKey() ? "저장됨 (확인 필요)" : "설정 필요";
            $("#apiKeyStatus").className = getKey() ? "info-label warn" : "info-label err";
            
            // ✅ 네트워크 설정 모달에 현재 SSID 표시 (옵션)
            if (data.ssid) $("#networkSsid").value = data.ssid;
            
            // 인증 테스트를 바로 실행하여 키 상태 업데이트
            await checkAuth(); 
        }
    }

    // ======================= 3. API Key 관리 및 인증 =======================
    
    function openApiKeyModal() {
        // ✅ 현재 저장된 키를 입력창에 표시
        $("#newApiKey").value = getKey(); 
        $("#apiKeyModal").style.display = "flex";
    }
    
    function closeApiKeyModal() {
        $("#apiKeyModal").style.display = "none";
        // 닫을 때 값 초기화 방지: 사용자가 수정 중일 수 있음. 대신 저장/취소 시 명확히 처리
    }

    async function saveApiKey(event) {
        event.preventDefault();
        const newKey = $("#newApiKey").value.trim();

        if (newKey) {
            setKey(newKey);
            showToast("API Key가 로컬에 저장되었습니다. 인증 테스트를 진행합니다.", "ok");
            closeApiKeyModal();
            await checkAuth(); 
        } else {
            setKey(""); // 키를 빈 값으로 저장하여 삭제 처리
            showToast("API Key가 삭제되었습니다. 인증이 필요합니다.", "warn");
            closeApiKeyModal();
            await checkAuth();
        }
    }
    
    async function checkAuth() {
        // GET /api/auth/test 
        const result = await fetchApi("/api/auth/test", "GET", null, "인증 테스트");
        
        const statusEl = $("#apiKeyStatus");
        
        if (result && result.authenticated) {
            statusEl.textContent = "✅ 인증 성공";
            statusEl.className = "info-label ok";
        } else {
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
    
    // 이 함수는 유지 (handleDeviceControl)

    // ======================= 5. 신규 구현: 설정 기능 =======================

    // ✅ 네트워크 설정 모달 로직
    function openNetworkSetupModal() {
        // 모달 열 때 현재 네트워크 정보 다시 로드 (최신 정보 반영)
        // loadSystemInfo()에서 이미 로드되었다고 가정하고 생략 가능
        $("#networkModal").style.display = "flex";
    }
    
    function closeNetworkSetupModal() {
        $("#networkModal").style.display = "none";
    }
    
    async function saveNetworkSettings(event) {
        event.preventDefault();
        const ssid = $("#networkSsid").value.trim();
        const password = $("#networkPassword").value;
        const mode = $("#networkMode").value; // 예: "STA", "AP"

        if (!ssid && mode === "STA") {
            showToast("STA 모드 설정 시 SSID는 필수입니다.", "err");
            return;
        }

        const body = { 
            mode: mode,
            ssid: ssid, 
            password: password 
        };

        // POST /api/network/wifi/config
        const result = await fetchApi("/api/network/wifi/config", "POST", body, "네트워크 설정 저장");
        
        if (result) {
            showToast("네트워크 설정이 저장되었습니다. 장치가 재접속을 시도합니다.", "warn");
            closeNetworkSetupModal();
            
            // 네트워크 변경 후 재접속이 필요하므로 페이지 새로고침
            setTimeout(() => window.location.reload(), 5000); 
        }
    }
    
    // ✅ 시간 설정 모달 로직
    function openTimeSetupModal() {
        // 현재 시간 로드 기능은 생략 (GET API 호출 필요)
        $("#timeModal").style.display = "flex";
    }
    
    function closeTimeSetupModal() {
        $("#timeModal").style.display = "none";
    }
    
    async function saveTimeSettings(event) {
        event.preventDefault();
        const ntpServer = $("#ntpServer").value.trim();
        const timezone = parseInt($("#timezoneOffset").value, 10);
        
        if (!ntpServer || isNaN(timezone)) {
             showToast("유효한 NTP 서버 주소와 시간대 오프셋을 입력하세요.", "err");
             return;
        }

        const body = { 
            ntp_server: ntpServer, 
            timezone_offset: timezone 
        };

        // POST /api/system/time/set
        const result = await fetchApi("/api/system/time/set", "POST", body, "시간 설정 저장");
        
        if (result) {
            showToast("시간(NTP/시간대) 설정이 성공적으로 저장되었습니다.", "ok");
            closeTimeSetupModal();
        }
    }
    
    // ✅ 펌웨어 업데이트 확인 로직
    async function checkFirmwareUpdate() {
        showToast("펌웨어 업데이트 서버 확인 중...", "info");
        
        // GET /api/system/firmware/check
        const data = await fetchApi("/api/system/firmware/check", "GET", null, "펌웨어 업데이트 확인");
        
        if (data && data.status === "available") {
            showToast(`새 펌웨어 버전 ${data.latest_version}이(가) 확인되었습니다.`, "warn");
            // 여기에 업데이트 버튼 활성화 로직 추가
        } else if (data && data.status === "latest") {
            showToast(`현재 최신 버전(${data.current_version})입니다.`, "ok");
        } else {
            showToast("펌웨어 업데이트 정보를 가져오지 못했습니다.", "err");
        }
    }

    // ======================= 6. 이벤트 바인딩 및 초기화 =======================

    function handleDeviceControl(event) {
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
            const result = fetchApi(url, "POST", null, target.textContent.trim());
            if (result) {
                showToast(successMsg, "warn");
                if (target.id === 'btnReboot' || target.id === 'btnFactoryReset') {
                    setTimeout(() => window.location.reload(), 5000); 
                }
            }
        }
    }
    
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
        
        // ✅ 펌웨어 및 네트워크 버튼 (추가된 기능)
        $("#btnCheckUpdate")?.addEventListener('click', checkFirmwareUpdate);
        
        // 네트워크 설정 모달 관련
        $("#btnNetworkSetup")?.addEventListener('click', openNetworkSetupModal);
        $("#networkForm")?.addEventListener('submit', saveNetworkSettings);
        $("#btnCloseNetworkModal")?.addEventListener('click', closeNetworkSetupModal);
        $("#btnCancelNetworkModal")?.addEventListener('click', closeNetworkSetupModal);
        
        // 시간 설정 모달 관련
        $("#btnTimeSetup")?.addEventListener('click', openTimeSetupModal);
        $("#timeForm")?.addEventListener('submit', saveTimeSettings);
        $("#btnCloseTimeModal")?.addEventListener('click', closeTimeSetupModal);
        $("#btnCancelTimeModal")?.addEventListener('click', closeTimeSetupModal);
    }

    document.addEventListener("DOMContentLoaded", () => {
        bindEvents();
        loadSystemInfo(); // 페이지 로드 시 정보 자동 로드 및 인증 체크
    });

})();
