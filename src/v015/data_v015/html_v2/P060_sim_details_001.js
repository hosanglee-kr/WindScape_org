/*
 * ------------------------------------------------------
 * 소스명 : P060_sim_details_001.js
 * 모듈명 : Smart Nature Wind Simulation Details Controller (v001)
 * ------------------------------------------------------
 * 기능 요약:
 * - 🎯 /api/simulation (GET/POST) 및 /api/motion (GET/POST)을 통해 설정 로드 및 패치
 * - 입력 필드 변경 시 실시간으로 메모리에 PATCH 요청 (자동 저장)
 * - 전체 설정 파일 저장 및 초기화 기능 (/api/config/save, /api/config/init)
 * ------------------------------------------------------
 */

(() => {
    "use strict";

    // ======================= 1. 공통 헬퍼 함수 및 변수 =======================
    // SC10_dashboard_001.js에 정의된 공통 함수들을 사용한다고 가정합니다.
    const $ = (s, r = document) => r.querySelector(s);
    const $$ = (s, r = document) => Array.from(r.querySelectorAll(s));

    // ************* 공통 기능 대체 (SC10_common_001.js에 있어야 함) *************
    const KEY_API = 'sc10_api_key';
    const getKey = () => localStorage.getItem(KEY_API) || '';
    const setLoading = (flag) => { /* Loading 구현 */ };
    const showToast = (msg, type = "ok") => { console.log(`[TOAST] ${type}: ${msg}`); };

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
                showToast(`[401] ${desc} 실패: 인증 필요`, "err");
                throw new Error("Unauthorized");
            }
            if (!resp.ok) {
                const txt = await resp.text();
                showToast(`${desc} 실패: ${txt || resp.status}`, "err");
                throw new Error(txt || resp.status);
            }
            // PATCH 성공 시 토스트는 자동 저장 핸들러에서 처리
            if (method !== 'POST' || desc === '설정 불러오기') {
                showToast(`${desc} 성공`, "ok");
            }

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

    // ======================= 2. DOM 요소 및 설정 필드 매핑 =======================
    const SIM_FIELDS = [
        "windIntensity", "windVariability", "fanLimit", "minFan",
        "turbulence_intensity_sigma", "turbulence_length_scale",
        "thermal_bubble_strength", "thermal_bubble_radius"
    ];
    const MOTION_TIMING_FIELDS = ["sim_interval", "gust_interval", "thermal_interval"];

    const elDirtyStatus = $("#dirtyStatus");
    let initialLoadComplete = false;
    let patchTimeout; // 디바운싱을 위한 타이머

    // ======================= 3. 설정 로드 및 패치 로직 =======================

    // ✅ /api/simulation (GET)으로부터 데이터를 로드하여 폼 필드에 채움
    async function loadSimConfig() {
        const simData = await fetchApi("/api/simulation", "GET", null, "시뮬 설정 불러오기");
        if (simData && simData.sim) {
            SIM_FIELDS.forEach(field => {
                const el = $(`#${field}`);
                if (el && simData.sim[field] !== undefined) {
                    el.value = simData.sim[field];
                }
            });
            return true;
        }
        return false;
    }

    // ✅ /api/motion (GET)으로부터 타이밍 데이터를 로드하여 폼 필드에 채움
    async function loadMotionConfig() {
        const motionData = await fetchApi("/api/motion", "GET", null, "타이밍 설정 불러오기");
        if (motionData && motionData.motion && motionData.motion.timing) {
            MOTION_TIMING_FIELDS.forEach(field => {
                const el = $(`#${field}`);
                if (el && motionData.motion.timing[field] !== undefined) {
                    el.value = motionData.motion.timing[field];
                }
            });
            return true;
        }
        return false;
    }

    // ✅ /api/simulation (POST)에 데이터 패치 (메모리 업데이트)
    async function patchSimConfig(data) {
        const result = await fetchApi("/api/simulation", "POST", { sim: data }, "시뮬 설정 메모리 패치");
        if (result && result.updated) {
            showToast("시뮬레이션 설정 메모리 업데이트됨", "ok");
            checkDirtyStatus();
        }
    }

    // ✅ /api/motion (POST)에 타이밍 데이터 패치 (메모리 업데이트)
    async function patchMotionConfig(data) {
        // 백엔드 구조가 motion 아래 timing이므로, 전송 시도 마찬가지로 구성
        const result = await fetchApi("/api/motion", "POST", { motion: { timing: data } }, "타이밍 설정 메모리 패치");
        if (result && result.updated) {
            showToast("타이밍 설정 메모리 업데이트됨", "ok");
            checkDirtyStatus();
        }
    }

    // ======================= 4. 이벤트 핸들러 =======================

    // 자동 PATCH (디바운싱 적용)
    function handleInputChange(event) {
        if (!initialLoadComplete) return;

        clearTimeout(patchTimeout);
        patchTimeout = setTimeout(() => {
            const field = event.target.id;
            const value = parseFloat(event.target.value);

            if (isNaN(value)) {
                showToast("유효한 숫자를 입력해 주세요.", "err");
                return;
            }

            // 요청 바디 생성
            const payload = { [field]: value };

            if (SIM_FIELDS.includes(field)) {
                patchSimConfig(payload);
            } else if (MOTION_TIMING_FIELDS.includes(field)) {
                patchMotionConfig(payload);
            }

        }, 500); // 500ms 디바운스
    }

    // 전체 설정 파일 저장
    $("#btnSaveConfig")?.addEventListener('click', async () => {
        await fetchApi("/api/config/save", "POST", null, "전체 설정 파일 저장");
        checkDirtyStatus();
    });

    // 설정 초기화
    $("#btnInitConfig")?.addEventListener('click', async () => {
        if (confirm("경고: 시뮬레이션 설정(풍속/타이밍)을 공장 초기값으로 되돌리고 저장하시겠습니까?")) {
            // 백엔드의 /api/config/init은 모든 설정을 초기화하고 재부팅합니다.
            await fetchApi("/api/config/init", "POST", null, "공장 초기화 및 재부팅");
            // 성공 시 페이지 새로고침 또는 재부팅 대기 메시지 표시
            showToast("장치가 재부팅됩니다. 잠시 후 다시 접속해 주세요.", "warn");
        }
    });

    // Dirty 상태 확인 및 표시
    async function checkDirtyStatus() {
        const dirtyData = await fetchApi("/api/config/dirty", "GET", null, "저장 상태 확인");
        if (dirtyData) {
            const isDirty = dirtyData.sim || dirtyData.motion;

            if (isDirty) {
                elDirtyStatus.textContent = "⚠️ 저장되지 않은 시뮬레이션 설정이 있습니다. '전체 설정 파일 저장' 버튼을 눌러주세요.";
                elDirtyStatus.classList.remove('muted');
                elDirtyStatus.classList.add('warn');
            } else {
                elDirtyStatus.textContent = "변경 사항 없음";
                elDirtyStatus.classList.remove('warn');
                elDirtyStatus.classList.add('muted');
            }
        }
    }

    // ======================= 5. 초기화 =======================

    // 전체 설정 로드 함수
    async function loadAllConfig() {
        await loadSimConfig();
        await loadMotionConfig();
        await checkDirtyStatus();
        initialLoadComplete = true; // 로드 완료 후 자동 저장 활성화
        showToast("시뮬레이션 상세 설정 로드 완료", "ok");
    }

    document.addEventListener("DOMContentLoaded", () => {
        // 입력 필드에 변경 감지 이벤트 바인딩
        const allInputs = $$('input[type="number"]');
        allInputs.forEach(input => {
            input.addEventListener('change', handleInputChange);
            input.addEventListener('input', handleInputChange); // 슬라이더 등 실시간 입력 대응
        });

        // 버튼 이벤트 바인딩은 이미 위에서 완료됨
        $("#btnLoadConfig")?.addEventListener('click', loadAllConfig);

        // 페이지 로드 시 초기 설정 로드
        loadAllConfig();
    });

})();
