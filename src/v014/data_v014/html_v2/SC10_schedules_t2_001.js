/*
 * ------------------------------------------------------
 * 소스명 : SC10_schedules_t2_001.js
 * 모듈명 : Smart Nature Wind Schedule Manager Controller (v001)
 * ------------------------------------------------------
 * 기능 요약:
 * - 🎯 /api/schedule (GET, POST, PUT, DELETE) CRUD 기능 구현
 * - /api/windProfile (GET)을 호출하여 프로파일 선택 옵션 동적 로드
 * - 스케줄 목록 렌더링 및 모달을 통한 생성/수정 관리
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
    const setLoading = (flag) => { 
        const el = $("#loadingOverlay");
        if (el) el.style.display = flag ? "flex" : "none";
    };
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

    let currentSchedules = []; // 현재 로드된 스케줄 데이터
    let windProfiles = [];     // 로드된 Wind Profile 목록

    const DAY_MAP = ['일', '월', '화', '수', '목', '금', '토'];

    // ======================= 2. 데이터 로드 및 렌더링 =======================
    
    // ✅ Wind Profile 목록 로드 (스케줄 모달의 <select> 옵션 채우기용)
    async function loadWindProfiles() {
        const data = await fetchApi("/api/windProfile", "GET", null, "프로파일 목록 로드");
        if (data && data.profiles) {
            windProfiles = data.profiles;
            const selectEl = $("#profileSelect");
            selectEl.innerHTML = '<option value="">-- 프로파일 선택 --</option>';
            windProfiles.forEach(p => {
                const option = document.createElement('option');
                option.value = p.id;
                option.textContent = `${p.id}: ${p.name}`;
                selectEl.appendChild(option);
            });
        }
    }

    // ✅ 스케줄 목록 로드 및 렌더링
    async function loadSchedules() {
        const data = await fetchApi("/api/schedule", "GET", null, "스케줄 목록 불러오기");
        
        if (data && data.schedules && Array.isArray(data.schedules)) {
            currentSchedules = data.schedules;
            renderScheduleList(currentSchedules);
        } else {
            currentSchedules = [];
            renderScheduleList([]);
            $("#noScheduleMessage").style.display = 'block';
        }
    }

    function renderScheduleList(schedules) {
        const tbody = $("#scheduleListBody");
        tbody.innerHTML = "";
        $("#noScheduleMessage").style.display = schedules.length === 0 ? 'block' : 'none';

        schedules.forEach(schedule => {
            const tr = document.createElement('tr');
            tr.dataset.scheduleId = schedule.id;
            
            const profile = windProfiles.find(p => p.id === schedule.profile_id);
            const profileName = profile ? profile.name : `[ID:${schedule.profile_id} 없음]`;
            
            // 반복 요일 문자열 변환
            const daysArr = schedule.days || [];
            let daysText = daysArr.map(d => DAY_MAP[d] || '?').join(', ');
            if (daysArr.length === 7) {
                daysText = '매일';
            } else if (daysArr.includes(1) && daysArr.includes(2) && daysArr.includes(3) && daysArr.includes(4) && daysArr.includes(5) && daysArr.length === 5) {
                daysText = '주중(월-금)';
            } else if ((daysArr.includes(0) || daysArr.includes(6)) && daysArr.length === 2) {
                 daysText = '주말(토,일)';
            }

            const statusText = schedule.is_enabled ? '✅ 사용 중' : '❌ 비활성';
            
            const rowHtml = `
                <td>${schedule.id}</td>
                <td><strong>${schedule.name}</strong></td>
                <td>${schedule.time || '00:00'}</td>
                <td>${daysText}</td>
                <td>${profileName}</td>
                <td><span class="info-label">${statusText}</span></td>
                <td>
                    <div class="action-buttons">
                        <button class="btn btn-small btn-edit" data-id="${schedule.id}">수정</button>
                        <button class="btn btn-small btn-err btn-delete" data-id="${schedule.id}">삭제</button>
                    </div>
                </td>
            `;
            tr.innerHTML = rowHtml;
            tbody.appendChild(tr);
        });

        tbody.addEventListener('click', handleScheduleActions);
    }
    
    // ======================= 3. 모달 및 CRUD 핸들러 =======================
    
    function openModal(schedule = null) {
        const modal = $("#scheduleModal");
        const form = $("#scheduleForm");
        
        form.reset();
        $$('.day-checkboxes input[type="checkbox"]').forEach(el => el.checked = false); // 체크박스 초기화
        
        if (schedule) {
            // 수정 모드
            $("#modalTitle").textContent = `스케줄 수정: ${schedule.name}`;
            $("#scheduleId").value = schedule.id;
            $("#scheduleName").value = schedule.name;
            $("#scheduleTime").value = schedule.time || "00:00";
            $("#profileSelect").value = schedule.profile_id;
            $("#isEnabled").checked = schedule.is_enabled;
            
            // 요일 체크박스 설정
            (schedule.days || []).forEach(day => {
                const el = $(`input[name="days"][value="${day}"]`);
                if (el) el.checked = true;
            });
            
        } else {
            // 생성 모드
            $("#modalTitle").textContent = "새 스케줄 생성";
            $("#scheduleId").value = "";
        }

        modal.style.display = "flex";
    }

    function closeModal() {
        $("#scheduleModal").style.display = "none";
    }

    async function saveSchedule(event) {
        event.preventDefault();

        const id = $("#scheduleId").value;
        const isUpdate = !!id;

        // 선택된 요일 값 (배열) 가져오기
        const selectedDays = $$('input[name="days"]:checked').map(el => parseInt(el.value));

        const data = {
            name: $("#scheduleName").value,
            time: $("#scheduleTime").value,
            profile_id: parseInt($("#profileSelect").value),
            days: selectedDays.sort((a, b) => a - b), // 정렬하여 저장
            is_enabled: $("#isEnabled").checked,
        };
        
        if (data.profile_id === 0 || isNaN(data.profile_id) || !data.time || data.days.length === 0) {
            showToast("모든 필수 항목(이름, 시간, 프로파일, 요일)을 선택해주세요.", "err");
            return;
        }

        let result;
        if (isUpdate) {
            // PUT /api/schedule/{id}
            result = await fetchApi(`/api/schedule/${id}`, "PUT", data, `스케줄 ${id} 수정`);
        } else {
            // POST /api/schedule
            result = await fetchApi("/api/schedule", "POST", data, "새 스케줄 생성");
        }

        if (result) {
            closeModal();
            loadSchedules(); // 목록 새로고침
        }
    }
    
    async function handleScheduleActions(event) {
        const target = event.target;
        const id = target.dataset.id;
        if (!id) return;
        
        const schedule = currentSchedules.find(p => String(p.id) === id);
        if (!schedule) return;

        if (target.classList.contains('btn-edit')) {
            openModal(schedule);
            
        } else if (target.classList.contains('btn-delete')) {
            if (confirm(`정말로 스케줄 [${schedule.name} (ID: ${id})] 을(를) 삭제하시겠습니까?`)) {
                await deleteSchedule(id, schedule.name);
            }
        }
    }
    
    async function deleteSchedule(id, name) {
        // DELETE /api/schedule/{id}
        const result = await fetchApi(`/api/schedule/${id}`, "DELETE", null, `스케줄 ${name} 삭제`);
        if (result) {
            loadSchedules(); // 목록 새로고침
        }
    }

    // ======================= 4. 이벤트 바인딩 및 초기화 =======================

    function bindEvents() {
        // 메인 액션 버튼
        $("#btnCreateNew")?.addEventListener('click', () => openModal(null));
        $("#btnRefreshList")?.addEventListener('click', loadSchedules);
        
        // 모달 닫기 버튼
        $("#btnCloseModal")?.addEventListener('click', closeModal);
        $("#btnCancelModal")?.addEventListener('click', closeModal);
        
        // 모달 폼 저장 버튼
        $("#scheduleForm")?.addEventListener('submit', saveSchedule);
    }

    document.addEventListener("DOMContentLoaded", async () => {
        bindEvents();
        await loadWindProfiles(); // 프로파일 목록을 먼저 로드
        await loadSchedules(); // 스케줄 목록 로드 시작
    });

})();

