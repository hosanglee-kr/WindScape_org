/*
 * ------------------------------------------------------
 * 소스명 : P040_dashboard_003.js
 * 모듈명 : Smart Nature Wind Dashboard UI Controller (v001, Backend v029 기준)
 * ------------------------------------------------------
 * 기능 요약:
 * - /ws/state WebSocket을 통한 실시간 풍속/PWM/상태 업데이트
 * - /ws/log WebSocket을 통한 실시간 로그 메시지 출력
 * - /api/control/profile/select 및 /api/control/profile/stop 으로 시뮬레이션 제어
 * - /api/state를 이용한 초기 상태 로드
 * - P000_common_006.js 의 showToast / 메뉴 로직과 공존
 * ------------------------------------------------------
 */

(() => {
  "use strict";

  // ======================= 1. 공통 유틸 =======================

  const API_KEY_STORAGE_KEY = "snw_api_key"; // Main / Chart와 통일

  const getStoredApiKey = () => {
    try {
      return localStorage.getItem(API_KEY_STORAGE_KEY) || "";
    } catch (e) {
      console.warn("[Dashboard] Unable to read API key:", e);
      return "";
    }
  };

  // WebSocket URL 생성 (ws / wss + apiKey 쿼리)
  const buildWsUrl = (path) => {
    const protocol = window.location.protocol === "https:" ? "wss" : "ws";
    const base = `${protocol}://${window.location.host}${path}`;
    const apiKey = getStoredApiKey();
    if (!apiKey) return base;
    const sep = path.includes("?") ? "&" : "?";
    return `${base}${sep}apiKey=${encodeURIComponent(apiKey)}`;
  };

  const $ = (s, r = document) => r.querySelector(s);
  const text = (el, v) => {
    if (el) el.textContent = v;
  };

  const elLoadingOverlay = $("#loadingOverlay");

  const setLoading = (flag) => {
    if (elLoadingOverlay) elLoadingOverlay.style.display = flag ? "flex" : "none";
  };

  // 공통 showToast가 있으면 우선 사용
  const notify = (message, type = "info") => {
    if (typeof showToast === "function") {
      showToast(message, type);
    } else {
      console.log(`[Toast ${type}] ${message}`);
    }
  };

  // 공통 Fetch 래퍼
  async function fetchApi(url, method = "GET", body = null, desc = "작업") {
    setLoading(true);
    try {
      const opt = { method, headers: {} };
      const apiKey = getStoredApiKey();
      if (apiKey) opt.headers["X-API-Key"] = apiKey;

      if (body) {
        opt.body = JSON.stringify(body);
        opt.headers["Content-Type"] = "application/json";
      }

      const resp = await fetch(url, opt);
      const textResp = await resp.text();

      if (resp.status === 401) {
        notify(`[401] ${desc} 실패: 인증 필요`, "err");
        throw new Error("Unauthorized");
      }

      if (!resp.ok) {
        notify(`${desc} 실패: ${textResp || resp.status}`, "err");
        throw new Error(textResp || String(resp.status));
      }

      if (desc) notify(`${desc} 성공`, "ok");

      try {
        return textResp ? JSON.parse(textResp) : null;
      } catch {
        return textResp;
      }
    } catch (e) {
      if (e.message !== "Unauthorized") {
        notify(`${desc} 실패: ${e.message}`, "err");
      }
      return null;
    } finally {
      setLoading(false);
    }
  }

  // ======================= 2. DOM 요소 =======================

  const elWindSpeed = $("#currentWindSpeed");
  const elPWMDuty = $("#currentPWMDuty");
  const elSimState = $("#simState");
  const elNetworkInfo = $("#networkInfo");
  const elLogConsole = $("#logConsole");

  // ======================= 3. 상태 / 로그 렌더링 =======================

  /**
   * /api/state 또는 /ws/state 에서 받은 JSON을 Dashboard용으로 반영
   * Backend v029 기준, Main 페이지 로직과 최대한 정합
   */
  function applyStateJson(data) {
    if (!data) return;

    // sim 객체: sim / motion / state 중 하나를 우선 사용
    const sim = data.sim || data.motion || data.state || {};
    // wifi 객체: data.wifi.state 우선, 없으면 data.wifi
    const wifi = (data.wifi && data.wifi.state) ? data.wifi.state : data.wifi || {};

    const simActive = sim.active !== undefined ? sim.active : sim.simActive;
    const wind = sim.wind !== undefined ? sim.wind : sim.wind_ms;
    const pwm = sim.pwm !== undefined ? sim.pwm : sim.pwm_val;

    // 1) 풍속 / PWM
    const windVal = wind != null ? Number(wind) : 0;
    const pwmVal = pwm != null ? Number(pwm) : 0;

    text(elWindSpeed, windVal.toFixed(2));
    text(elPWMDuty, String(Math.round(pwmVal)));

    // 2) 시뮬레이션 상태: active 여부 + override 등 추가 플래그가 있으면 확장 가능
    let stateText = "STOPPED";
    let stateClass = "stopped";

    if (simActive === true || simActive === 1 || simActive === "on" || simActive === "ACTIVE") {
      stateText = "RUNNING";
      stateClass = "running";
    }

    // 추후 override / offline 등 추가 상태가 있으면 여기서 분기 확장
    if (elSimState) {
      elSimState.className = `large-value status-text ${stateClass}`;
      elSimState.textContent = stateText;
    }

    // 3) 네트워크 정보: mode_name / mode + ip
    const modeName = wifi.mode_name || wifi.mode || "-";
    const ip = wifi.ip || wifi.ip_address || "-";
    text(elNetworkInfo, `${modeName} | ${ip}`);
  }

  function appendLog(record) {
    if (!elLogConsole || !record) return;

    const { t, level, message } = record;
    if (!message) return;

    const el = document.createElement("div");
    el.className = "log-message";

    let levelClass = "log-info";
    const lv = (level || "INFO").toUpperCase();
    if (lv === "ERROR" || lv === "ERR") levelClass = "log-error";
    else if (lv === "WARN" || lv === "WARNING") levelClass = "log-warn";

    const timeStr = new Date(t || Date.now()).toLocaleTimeString();

    el.innerHTML = `<span class="${levelClass}">[${timeStr}] [${lv}]</span> ${message}`;
    elLogConsole.appendChild(el);

    // 로그 개수 제한 (50개)
    while (elLogConsole.children.length > 50) {
      elLogConsole.removeChild(elLogConsole.firstChild);
    }

    elLogConsole.scrollTop = elLogConsole.scrollHeight;
  }

  // ======================= 4. 초기 상태 로드 =======================

  async function refreshInitialState() {
    const state = await fetchApi("/api/state", "GET", null, "초기 상태 로드");
    if (state) {
      applyStateJson(state);
      appendLog({
        t: Date.now(),
        level: "INFO",
        message: "초기 상태 로드 완료 (/api/state)."
      });
    } else {
      if (!getStoredApiKey()) {
        notify(
          "API Key가 비어 있습니다. System / Config 페이지에서 API Key를 설정해 주세요.",
          "warn"
        );
      }
    }
  }

  // ======================= 5. WebSocket (state / log) =======================

  function initWsState() {
    const url = buildWsUrl("/ws/state");
    let ws;

    try {
      ws = new WebSocket(url);
    } catch (e) {
      appendLog({
        t: Date.now(),
        level: "ERROR",
        message: `WS State 연결 실패: ${e.message}`
      });
      return;
    }

    ws.onopen = () => {
      appendLog({
        t: Date.now(),
        level: "INFO",
        message: "WS State 연결 성공."
      });
    };

    ws.onmessage = (event) => {
      try {
        const data = JSON.parse(event.data);
        applyStateJson(data);
      } catch (e) {
        appendLog({
          t: Date.now(),
          level: "ERROR",
          message: `WS State 파싱 오류: ${e.message}`
        });
      }
    };

    ws.onclose = () => {
      appendLog({
        t: Date.now(),
        level: "WARN",
        message: "WS State 연결 끊김. 5초 후 재시도."
      });
      setTimeout(initWsState, 5000);
    };

    ws.onerror = (e) => {
      appendLog({
        t: Date.now(),
        level: "ERROR",
        message: `WS State 오류: ${e.message || e}`
      });
    };
  }

  function initWsLog() {
    const url = buildWsUrl("/ws/log");
    let ws;

    try {
      ws = new WebSocket(url);
    } catch (e) {
      appendLog({
        t: Date.now(),
        level: "ERROR",
        message: `WS Log 연결 실패: ${e.message}`
      });
      return;
    }

    ws.onopen = () => {
      appendLog({
        t: Date.now(),
        level: "INFO",
        message: "WS Log 연결 성공."
      });
    };

    ws.onmessage = (event) => {
      try {
        // 백엔드에서 {t, level, message} JSON으로 보내준다고 가정
        const data = JSON.parse(event.data);
        appendLog(data);
      } catch (e) {
        // 로그 파싱 오류는 조용히 무시 (문자열 로그일 수도 있음)
      }
    };

    ws.onclose = () => {
      appendLog({
        t: Date.now(),
        level: "WARN",
        message: "WS Log 연결 끊김."
      });
      // 복잡도 줄이기 위해 자동 재연결은 생략
    };

    ws.onerror = (e) => {
      appendLog({
        t: Date.now(),
        level: "ERROR",
        message: `WS Log 오류: ${e.message || e}`
      });
    };
  }

  // ======================= 6. 버튼 이벤트 (시뮬 제어 / 진단 / 로그) =======================

  function bindEvents() {
    // 시뮬 시작: 기본 프로필 id=1 가정
    $("#btnStartSim")?.addEventListener("click", async () => {
      await fetchApi(
        "/api/control/profile/select",
        "POST",
        { id: 1 },
        "시뮬레이션 시작"
      );
    });

    // 시뮬 중지
    $("#btnStopSim")?.addEventListener("click", async () => {
      await fetchApi(
        "/api/control/profile/stop",
        "POST",
        {},
        "시뮬레이션 중지"
      );
    });

    // 시스템 진단
    $("#btnDiag")?.addEventListener("click", async () => {
      const diag = await fetchApi("/api/diag", "GET", null, "시스템 진단 정보 로드");
      if (diag) {
        appendLog({
          t: Date.now(),
          level: "INFO",
          message: `[Diag] Heap: ${diag.heap} bytes, FS Used: ${diag.fs_used} / ${diag.fs_total} bytes`
        });
      }
    });

    // 로그 지우기
    $("#btnClearLog")?.addEventListener("click", () => {
      if (elLogConsole) elLogConsole.innerHTML = "";
      appendLog({
        t: Date.now(),
        level: "INFO",
        message: "로그 콘솔이 지워졌습니다."
      });
    });
  }

  // ======================= 7. 초기화 =======================

  document.addEventListener("DOMContentLoaded", () => {
    bindEvents();
    refreshInitialState();
    initWsState();
    initWsLog();
  });
})();
