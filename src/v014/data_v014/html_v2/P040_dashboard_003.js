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
 * - P000_common_004.js 의 showToast / 메뉴 로직과 공존
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
    const wind = sim.wind !== undefined

