/*
 * ------------------------------------------------------
 * 소스명 : P070_profile_001.js
 * 모듈명 : Smart Nature Wind WindProfile Manager Controller (v001)
 * ------------------------------------------------------
 * 기능 요약:
 * - 🎯 /api/windProfile (GET, POST, PUT, DELETE) CRUD 기능 구현
 * - /api/control/profile/select를 이용한 프로파일 즉시 적용 기능
 * - 프로파일 목록 렌더링 및 모달을 통한 생성/수정 관리
 * ------------------------------------------------------
 */

(() => {
    "use strict";

    // ======================= 1. 공통 헬퍼 함수 및 변수 =======================

    const $ = (s, r = document) => r.querySelector(s);
    const $$ = (s, r = document) => Array.from(r.querySelectorAll(s));

    // ************* 공통 기능 대체 (SC10_common_001.js에 있어야 함) *************
    const KEY_API = 'sc10_api_key';
    const getKey = () => localStorage.getItem(KEY_API) || '';
    const setLoading = (flag) => {
        const el = $("#loadingOverlay");
        if (el) el.style.display = flag ? "flex" : "none";
    };
    const showToast = (msg, type = "ok") => { console.log(`[TOAST] ${type}: ${msg}`); }; // 실제 구현 필요
    // *************************************************************************

    let currentProfiles = []; // 현재 로드된 프로파일 데이터

    // 공통 API Fetch 래퍼 함수 (CRUD 지원)
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
            showToast(`${desc} 성공`, "ok");

            const txt = await resp.text();
            try { return JSON.parse(txt); } catch { return txt; }
        } catch (e) {
            if (e.message !== "Unauthorized") console.error(e);
            return null;
        } finally {
            setLoading(false);
        }
    }

    // ======================= 2. 프로파일 목록 렌더링 =======================

    async function loadProfiles() {
        const data = await fetchApi("/api/windProfile", "GET", null, "프로파일 목록 불러오기");

        if (data && data.profiles && Array.isArray(data.profiles)) {
            currentProfiles = data.profiles;
            renderProfileList(currentProfiles);
        } else {
            currentProfiles = [];
            renderProfileList([]);
            $("#noProfileMessage").style.display = 'block';
        }
    }

    function renderProfileList(profiles) {
        const tbody = $("#profileListBody");
        tbody.innerHTML = ""; // 기존 목록 비우기
        $("#noProfileMessage").style.display = profiles.length === 0 ? 'block' : 'none';

        profiles.forEach(profile => {
            const tr = document.createElement('tr');
            tr.dataset.profileId = profile.id;

            // 현재 적용된 프로파일을 확인하는 로직은 Summary API가 필요하나, 여기서는 ID만 표시
            const isActive = profile.is_active || false; // 백엔드 응답에 is_active가 있다고 가정

            const rowHtml = `
                <td>${profile.id}</td>
                <td><strong>${profile.name}</strong></td>
                <td>${(profile.params?.windIntensity || 0.0).toFixed(1)}</td>
                <td>${(profile.params?.windVariability || 0.0).toFixed(1)}</td>
                <td>${(profile.params?.turbulence_intensity_sigma || 0.0).toFixed(1)}</td>
                <td><span class="info-label">${isActive ? '✅ 활성' : '비활성'}</span></td>
                <td>
                    <div class="action-buttons">
                        <button class="btn btn-small btn-select ${isActive ? 'warn' : 'ok'}" data-id="${profile.id}">
                            ${isActive ? '재선택' : '적용'}
                        </button>
                        <button class="btn btn-small btn-edit" data-id="${profile.id}">수정</button>
                        <button class="btn btn-small btn-err btn-delete" data-id="${profile.id}">삭제</button>
                    </div>
                </td>
            `;
            tr.innerHTML = rowHtml;
            tbody.appendChild(tr);
        });

        // 액션 버튼 이벤트 바인딩 (델리게이션)
        tbody.addEventListener('click', handleProfileActions);
    }

    // ======================= 3. 모달 및 CRUD 핸들러 =======================

    function openModal(profile = null) {
        const modal = $("#profileModal");
        const form = $("#profileForm");

        form.reset();

        if (profile) {
            // 수정 모드
            $("#modalTitle").textContent = `프로파일 수정: ${profile.name}`;
            $("#profileId").value = profile.id;
            $("#profileName").value = profile.name;
            $("#intensity").value = profile.params.windIntensity;
            $("#variability").value = profile.params.windVariability;
            $("#turb_sigma").value = profile.params.turbulence_intensity_sigma;
            $("#turb_length").value = profile.params.turbulence_length_scale;
        } else {
            // 생성 모드
            $("#modalTitle").textContent = "새 프로파일 생성";
            $("#profileId").value = "";
        }

        modal.style.display = "flex";
    }

    function closeModal() {
        $("#profileModal").style.display = "none";
    }

    async function saveProfile(event) {
        event.preventDefault();

        const id = $("#profileId").value;
        const isUpdate = !!id;

        const data = {
            name: $("#profileName").value,
            params: {
                windIntensity: parseFloat($("#intensity").value),
                windVariability: parseFloat($("#variability").value),
                turbulence_intensity_sigma: parseFloat($("#turb_sigma").value),
                turbulence_length_scale: parseFloat($("#turb_length").value),
            },
            // 다른 파라미터는 기본값 또는 현재 메모리 값 사용을 가정
        };

        let result;
        if (isUpdate) {
            // PUT /api/windProfile/{id}
            result = await fetchApi(`/api/windProfile/${id}`, "PUT", data, `프로파일 ${id} 수정`);
        } else {
            // POST /api/windProfile
            result = await fetchApi("/api/windProfile", "POST", data, "새 프로파일 생성");
        }

        if (result) {
            closeModal();
            loadProfiles(); // 목록 새로고침
        }
    }

    async function handleProfileActions(event) {
        const target = event.target;
        const id = target.dataset.id;
        if (!id) return;

        // 프로파일 데이터 찾기
        const profile = currentProfiles.find(p => String(p.id) === id);
        if (!profile) return;

        if (target.classList.contains('btn-select')) {
            // 프로파일 즉시 적용 (별도 API)
            await selectProfile(id, profile.name);

        } else if (target.classList.contains('btn-edit')) {
            // 수정 모달 열기
            openModal(profile);

        } else if (target.classList.contains('btn-delete')) {
            // 삭제 확인
            if (confirm(`정말로 프로파일 [${profile.name} (ID: ${id})] 을(를) 삭제하시겠습니까?`)) {
                await deleteProfile(id, profile.name);
            }
        }
    }

    async function selectProfile(id, name) {
        // POST /api/control/profile/select
        const result = await fetchApi("/api/control/profile/select", "POST", { id: parseInt(id) }, `프로파일 ${name} 적용`);
        if (result) {
            loadProfiles(); // 활성 상태 업데이트를 위해 목록 새로고침
        }
    }

    async function deleteProfile(id, name) {
        // DELETE /api/windProfile/{id}
        const result = await fetchApi(`/api/windProfile/${id}`, "DELETE", null, `프로파일 ${name} 삭제`);
        if (result) {
            loadProfiles(); // 목록 새로고침
        }
    }

    // ======================= 4. 이벤트 바인딩 및 초기화 =======================

    function bindEvents() {
        // 메인 액션 버튼
        $("#btnCreateNew")?.addEventListener('click', () => openModal(null));
        $("#btnRefreshList")?.addEventListener('click', loadProfiles);

        // 모달 닫기 버튼
        $("#btnCloseModal")?.addEventListener('click', closeModal);
        $("#btnCancelModal")?.addEventListener('click', closeModal);

        // 모달 폼 저장 버튼
        $("#profileForm")?.addEventListener('submit', saveProfile);
    }

    document.addEventListener("DOMContentLoaded", () => {
        bindEvents();
        loadProfiles(); // 페이지 로드 시 목록 자동 로드
    });

})();
