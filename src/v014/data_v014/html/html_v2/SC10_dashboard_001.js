/*
 * ------------------------------------------------------
 * 소스명 : SC10_dashboard_001.js
 * 모듈명 : Smart Nature Wind Dashboard UI Controller (v001)
 * ------------------------------------------------------
 * 기능 요약:
 * - 🎯 /ws/state WebSocket을 통한 실시간 풍속/PWM/상태 업데이트
 * - 🎯 /ws/log WebSocket을 통한 실시간 로그 메시지 출력
 * - /api/control/profile/select 및 /profile/stop을 이용한 시뮬레이션 제어
 * - /api/control/summary API를 이용한 초기 상태 로드
 * - 공통 JS 기능 (API Key, Toast, Loading)을 고려하여 구현
 * ------------------------------------------------------
 */

(() => {
    "use strict";

    // ======================= 1. 공통 변수 및 도우미 함수 =======================
    // 현재 페이지는 Dashboard이므로, Dashboard에 특화된 기능 외의 공통 요소는 여기에 정의하거나
    // 별도의 공통 JS 파일(SC10_common_001.js)이 있다고 가정하고 구현합니다.
    
    // 이 예시에서는 Common JS 파일이 없으므로, 핵심 공통 기능은 이 파일에 정의합니다.

    const KEY_API = 'sc10_api_key';
    const getKey = () => localStorage.getItem(KEY_API) || '';
    const getWSHost = (path) => `ws://${window.location.host}${path}`;

    const $ = (s, r = document) => r.querySelector(s);
    const text = (el, v) => el && (el.textContent = v);
    
    const loadingOverlay = $("#loadingOverlay");
    const toastContainer = $("#toastContainer");

    const setLoading = (flag) => { 
        if (loadingOverlay) loadingOverlay.style.display = flag ? "flex" : "none";
    };
    
    const showToast = (msg, type = "ok") => { 
        if (!toastContainer) {
            console.log(`[${type.toUpperCase()}] ${msg}`);
            return;
        }
        const toast = document.createElement("div");
        toast.className = `toast ${type}`;
        toast.textContent = msg;
        toastContainer.appendChild(toast);
        
        setTimeout(() => {
            toast.classList.add("fade-out");
            toast.addEventListener('transitionend', () => toastContainer.removeChild(toast));
        }, 3000);
    };

    // ======================= 2. 상태 갱신 DOM 요소 =======================
    const elWindSpeed = $("#currentWindSpeed");
    const elPWMDuty = $("#currentPWMDuty");
    const elSimState = $("#simState");
    const elNetworkInfo = $("#networkInfo");
    const elLogConsole = $("#logConsole");

    // ======================= 3. 공통 API Fetch 래퍼 함수 =======================

    async function fetchApi(url, method = "GET", body = null, desc = "작업") {
        setLoading(true);
        try {
            const opt = { method, headers: {} };
            const k = getKey();
            if (k) opt.headers["X-API-Key"] = k;

            if (body) {
                opt.body = JSON.stringify(body);
                opt.headers["Content-Type"] = "application/json";
            }

            const resp = await fetch(url, opt);
            
            if (resp.status === 401) {
                showToast(`[401] ${desc} 실패: 인증 필요`, "err");
                throw new Error("Unauthorized");
            }
            if (!resp.ok) {
                const txt = await resp.text();
                showToast(`${desc} 실패: ${txt || resp.status}`, "err");
                throw new Error(txt || resp.status);
            }
            
            showToast(`${desc} 성공`, "ok");
            
            // 응답이 JSON이 아닐 수도 있으므로, text를 먼저 읽고 JSON 파싱 시도
            const txt = await resp.text();
            try {
                return JSON.parse(txt);
            } catch {
                return txt;
            }
            
        } catch (e) {
            if (e.message !== "Unauthorized") showToast(`${desc} 실패: ${e.message}`, "err");
            return null;
        } finally {
            setLoading(false);
        }
    }


    // ======================= 4. 상태 및 로그 갱신 로직 =======================
    
    // ✅ WS State 데이터 처리
    function updateState(data) {
        if (!data || !data.status) return;
        
        // 1. 풍속 및 PWM 업데이트
        const currentWind = data.status.current_wind || 0.0;
        const currentPWM = data.status.current_pwm || 0;
        
        text(elWindSpeed, currentWind.toFixed(2));
        text(elPWMDuty, Math.round(currentPWM));

        // 2. 시뮬레이션 상태 업데이트
        const mode = data.status.mode || 'STOPPED'; // STOPPED, RUNNING, OVERRIDE
        const modeText = mode === 'RUNNING' ? 'RUNNING' : (mode === 'OVERRIDE' ? 'OVERRIDE' : 'STOPPED');
        
        text(elSimState, modeText);
        elSimState.className = `large-value status-text ${mode.toLowerCase() === 'stopped' ? 'stopped' : 'running'}`;
        
        // 3. 네트워크 정보 업데이트 (summary API에 IP, Mode가 포함된다고 가정)
        const wifiMode = data.wifi?.mode === 1 ? 'STA' : (data.wifi?.mode === 2 ? 'AP' : 'OFF');
        const ipAddress = data.wifi?.ip_address || 'N/A';
        text(elNetworkInfo, `${wifiMode} | ${ipAddress}`);
    }

    // ✅ WS Log 데이터 처리
    function appendLog(data) {
        if (!elLogConsole || !data.message) return;

        const log = document.createElement('div');
        log.className = 'log-message';
        
        // 로그 레벨에 따른 스타일 적용
        let levelClass = '';
        switch (data.level) {
            case 'ERROR': levelClass = 'log-error'; break;
            case 'WARN': levelClass = 'log-warn'; break;
            default: levelClass = 'log-info'; break; // INFO, DEBUG 등
        }

        // 포맷: [시간] [LEVEL] 메시지
        const time = new Date(data.t || Date.now()).toLocaleTimeString();
        log.innerHTML = `<span class="${levelClass}">[${time}] [${data.level}]</span> ${data.message}`;
        
        // 로그 콘솔에 추가 및 스크롤 자동 이동
        elLogConsole.appendChild(log);
        if (elLogConsole.children.length > 50) { // 로그 메시지 50개 제한
             elLogConsole.removeChild(elLogConsole.firstChild);
        }
        elLogConsole.scrollTop = elLogConsole.scrollHeight;
    }
    
    // 초기 상태 로드 (REST API)
    async function refreshInitialState() {
        const summary = await fetchApi("/api/control/summary", "GET", null, "초기 상태 로드");
        if (summary) {
            updateState(summary);
            // 로그 콘솔에 초기 메시지 추가
            appendLog({ t: Date.now(), level: 'INFO', message: '초기 상태 로드 완료.' });
        } else {
             // API Key 요청
             if (!getKey()) {
                 showToast("API Key를 입력하지 않았습니다. 시스템 설정 페이지에서 입력해 주세요.", "warn");
             }
        }
    }


    // ======================= 5. WebSocket 초기화 =======================

    function initWebSockets() {
        // 1. State/Summary WS 연결
        const wsState = new WebSocket(getWSHost("/ws/state"));
        wsState.onopen = () => { appendLog({ t: Date.now(), level: 'INFO', message: 'WS State 연결 성공.' }); };
        wsState.onmessage = (event) => { 
            try {
                const data = JSON.parse(event.data);
                updateState(data); // Summary 데이터와 유사한 구조라고 가정
            } catch (e) {
                 appendLog({ t: Date.now(), level: 'ERROR', message: `WS State 파싱 오류: ${e.message}` });
            }
        };
        wsState.onclose = () => { 
            appendLog({ t: Date.now(), level: 'WARN', message: 'WS State 연결 끊김. 5초 후 재시도.' });
            setTimeout(() => initWebSockets(), 5000); 
        };
        wsState.onerror = (e) => { 
            appendLog({ t: Date.now(), level: 'ERROR', message: `WS State 오류: ${e.message}` });
        };
        
        // 2. Log WS 연결
        const wsLog = new WebSocket(getWSHost("/ws/log"));
        wsLog.onopen = () => { appendLog({ t: Date.now(), level: 'INFO', message: 'WS Log 연결 성공.' }); };
        wsLog.onmessage = (event) => { 
            try {
                const data = JSON.parse(event.data);
                appendLog(data); // {t, level, message} 구조라고 가정
            } catch (e) {
                 // 로그 메시지 파싱 오류는 로그에 남기지 않음
            }
        };
        wsLog.onclose = () => { 
            appendLog({ t: Date.now(), level: 'WARN', message: 'WS Log 연결 끊김.' });
            // 로그 WS는 복잡도를 줄이기 위해 재시도 로직 생략
        };
        wsLog.onerror = (e) => { 
            appendLog({ t: Date.now(), level: 'ERROR', message: `WS Log 오류: ${e.message}` });
        };
    }

    // ======================= 6. 이벤트 바인딩 및 제어 로직 =======================

    function bindEvents() {
        // 1. Sim Start 버튼
        $("#btnStartSim")?.addEventListener("click", async () => {
            // ✅ 시뮬레이션 시작은 기본 프로필 (id=1 가정) 선택으로 대체
            const body = { id: 1 }; 
            await fetchApi("/api/control/profile/select", "POST", body, "시뮬레이션 시작");
        });

        // 2. Sim Stop 버튼
        $("#btnStopSim")?.addEventListener("click", async () => {
            // ✅ 시뮬레이션 중지는 Profile Stop API 사용
            await fetchApi("/api/control/profile/stop", "POST", {}, "시뮬레이션 중지");
        });

        // 3. 시스템 진단 버튼
        $("#btnDiag")?.addEventListener("click", async () => {
            const diag = await fetchApi("/api/diag", "GET", null, "시스템 진단 정보 로드");
            if (diag) {
                 appendLog({ 
                    t: Date.now(), 
                    level: 'INFO', 
                    message: `[Diag] Heap: ${diag.heap} bytes, FS Used: ${diag.fs_used} / ${diag.fs_total} bytes` 
                });
            }
        });

        // 4. 로그 지우기 버튼
        $("#btnClearLog")?.addEventListener("click", () => {
            if (elLogConsole) elLogConsole.innerHTML = '';
            appendLog({ t: Date.now(), level: 'INFO', message: '로그 콘솔이 지워졌습니다.' });
        });
    }

    // ======================= 7. 초기화 =======================

    document.addEventListener("DOMContentLoaded", () => {
        bindEvents(); // 이벤트 바인딩
        refreshInitialState(); // 초기 상태 (REST) 로드
        initWebSockets(); // 실시간 WS 연결 시작
    });

})();

