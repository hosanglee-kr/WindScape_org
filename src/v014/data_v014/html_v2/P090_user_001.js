/*
 * ------------------------------------------------------
 * 소스명 : P090_user_001.js
 * 모듈명 : Smart Nature Wind Help & Info Controller (v001)
 * ------------------------------------------------------
 * 기능 요약:
 * - 🎯 /api/system/info를 호출하여 펌웨어 버전 정보를 표시
 * - 다른 기능은 정보 제공에 집중하며 API 호출 없음
 * ------------------------------------------------------
 */

(() => {
    "use strict";

    // ======================= 1. 공통 헬퍼 함수 및 변수 =======================
    
    const $ = (s, r = document) => r.querySelector(s);

    // ************* 공통 기능 대체 *************
    const KEY_API = 'sc10_api_key';
    const getKey = () => localStorage.getItem(KEY_API) || '';
    const setLoading = (flag) => { 
        const el = $("#loadingOverlay");
        if (el) el.style.display = flag ? "flex" : "none";
    };
    const showToast = (msg, type = "ok") => { console.log(`[TOAST] ${type}: ${msg}`); };
    
    // API Fetch 래퍼 함수 (GET만 사용하여 정보 로드)
    async function fetchApi(url, desc = "작업") {
        setLoading(true);
        const opt = { method: "GET", headers: {} };
        const k = getKey();
        if (k) opt.headers["X-API-Key"] = k;

        try {
            const resp = await fetch(url, opt);
            if (!resp.ok) throw new Error(resp.status);
            
            const txt = await resp.text();
            try { return JSON.parse(txt); } catch { return txt; }
        } catch (e) {
            console.warn(`${desc} 실패 (API 키 오류 또는 연결 문제): ${e.message}`);
            return null;
        } finally {
            setLoading(false);
        }
    }
    // *************************************************************************

    // ======================= 2. 시스템 정보 로드 =======================

    // ✅ 펌웨어 버전 정보를 로드하여 표시
    async function loadFwVersion() {
        // GET /api/system/info 호출
        const data = await fetchApi("/api/system/info", "펌웨어 버전 로드");
        
        const fwVersionEl = $("#fwVersionInfo");
        
        if (data && data.version) {
            fwVersionEl.textContent = data.version;
        } else {
            fwVersionEl.textContent = "연결 불가";
            fwVersionEl.style.color = "#e74c3c";
        }
    }

    // ======================= 3. 초기화 =======================

    document.addEventListener("DOMContentLoaded", () => {
        loadFwVersion(); // 페이지 로드 시 펌웨어 버전 정보 로드
    });

})();
