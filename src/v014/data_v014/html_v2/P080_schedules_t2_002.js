/*
 * ------------------------------------------------------
 * 소스명 : P080_schedules_t2_002.js
 * 모듈명 : Smart Nature Wind Schedule Manager Controller (v002)
 * ------------------------------------------------------
 * 기능 요약:
 * - 🎯 /api/schedules (GET, POST, PUT, DELETE) CRUD (v029 스펙)
 * - 🎯 /api/windProfile (GET) → 프로파일 선택 옵션 동적 로드
 * - UI: 시간(HH:MM) + 요일배열 ↔ cron 필드 변환
 * - 🎯 /api/config/dirty · /api/config/save 와 연동 (schedules dirty 표시)
 * ------------------------------------------------------
 */

(() => {
  "use strict";

  // ======================= 1. 공통 헬퍼 함수 및 변수 =======================

  const $ = (s, r = document) => r.querySelector(s);
  const $$ = (s, r = document) => Array.from(r.querySelectorAll(s));

  const KEY_API = "sc10_api_key";
  const getKey = () => localStorage.getItem(KEY_API) || "";

  const setLoading = (flag) => {
    const el = $("#loadingOverlay");
    if (el) el.style.display = flag ? "flex" : "none";
  };

  // P000_common_004.js 의 showToast가 있으면 우선 사용
  const showToast = (msg, type = "ok") => {
    if (typeof window.showToast === "function" && window.showToast !== showToast) {
      window.showToast(msg, type);
      return;
    }
    console.log(`[TOAST] ${type}: ${msg}`);
  };

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
      if (method !== "GET") showToast(`${desc} 성공`, "ok");

      const txt = await resp.text();
      try {
        return JSON.parse(txt);
      } catch {
        return txt;
      }
    } catch (e) {
      if (e.message !== "Unauthorized") console.error(e);
      return null;
    } finally {
      setLoading(false);
    }
  }

  let currentSchedules = []; // [{ id, name, enabled, cron, profileId, uiTime, uiDays }]
  let windProfiles = [];
  let configDirty = false; // schedules 섹션 Dirty 여부

  const DAY_MAP = ["일", "월", "화", "수", "목", "금", "토"];

  // ======================= 2. cron <-> UI 변환 헬퍼 =======================

  const pad2 = (v) => {
    const n = Number(v) || 0;
    return n < 10 ? `0${n}` : String(n);
  };

  // cron("M H * * dow") → { time: "HH:MM", days: [0..6] }
  function parseCronForUi(cron) {
    if (!cron || typeof cron !== "string") {
      return { time: "00:00", days: [] };
    }
    const parts = cron.trim().split(/\s+/);
    if (parts.length < 5) {
      return { time: "00:00", days: [] };
    }

    const minute = parts[0];
    const hour = parts[1];
    const dowPart = parts[4];

    const time = `${pad2(hour)}:${pad2(minute)}`;

    const days = new Set();
    if (dowPart === "*" || dowPart === "0-6") {
      for (let d = 0; d <= 6; d++) days.add(d);
    } else {
      dowPart.split(",").forEach((seg) => {
        if (!seg) return;
        if (seg.includes("-")) {
          const [a, b] = seg.split("-");
          const start = Number(a);
          const end = Number(b);
          if (!isNaN(start) && !isNaN(end)) {
            for (let d = start; d <= end; d++) {
              if (d >= 0 && d <= 6) days.add(d);
            }
          }
        } else {
          const v = Number(seg);
          if (!isNaN(v) && v >= 0 && v <= 6) days.add(v);
        }
      });
    }

    const dayArr = Array.from(days).sort((a, b) => a - b);
    return { time, days: dayArr };
  }

  // time("HH:MM"), days[0..6] → cron("M H * * d1,d2,...")
  function buildCronFromUi(time, days) {
    if (!time || !Array.isArray(days) || days.length === 0) return null;

    const [hhRaw, mmRaw] = time.split(":");
    let h = Number(hhRaw);
    let m = Number(mmRaw);

    if (isNaN(h) || h < 0 || h > 23) h = 0;
    if (isNaN(m) || m < 0 || m > 59) m = 0;

    const sortedDays = [...new Set(days)].sort((a, b) => a - b);
    const dow = sortedDays.join(",");
    return `${m} ${h} * * ${dow}`;
  }

  function formatDaysText(daysArr) {
    if (!Array.isArray(daysArr) || daysArr.length === 0) return "-";

    const sorted = [...daysArr].sort((a, b) => a - b);

    if (sorted.length === 7) return "매일";

    const isWeekdays =
      sorted.length === 5 &&
      sorted[0] === 1 &&
      sorted[4] === 5;

    if (isWeekdays) return "주중(월-금)";

    const isWeekend =
      sorted.length === 2 &&
      sorted.includes(0) &&
      sorted.includes(6);

    if (isWeekend) return "주말(토,일)";

    return sorted.map((d) => DAY_MAP[d] || "?").join(", ");
  }

  // ======================= 3. Config Dirty 상태 관리 =======================

  function setDirtyStatus(isDirty) {
    configDirty = !!isDirty;
    const btn = $("#btnSaveAllConfig");
    if (!btn) return;

    if (configDirty) {
      btn.style.backgroundColor = "#dc2626";
      btn.style.color = "#fff";
      btn.textContent = "⚠️ 전체 설정 저장 (미저장)";
    } else {
      btn.style.backgroundColor = "";
      btn.style.color = "";
      btn.textContent = "전체 설정 저장";
    }
  }

  async function checkConfigDirtyState() {
    try {
      const res = await fetch("/api/config/dirty", {
        headers: { "X-API-Key": getKey() }
      });
      if (!res.ok) throw new Error(res.statusText);
      const j = await res.json();
      setDirtyStatus(!!j.schedules);
    } catch (e) {
      console.warn("[Schedule] Dirty 상태 조회 실패:", e.message);
    } finally {
      setTimeout(checkConfigDirtyState, 5000);
    }
  }

  async function saveAllConfig() {
    if (!configDirty) {
      showToast("저장할 변경 사항이 없습니다.", "warn");
      return;
    }
    const res = await fetchApi("/api/config/save", "POST", {}, "전체 설정 파일 저장");
    if (res) {
      setDirtyStatus(false);
      await loadSchedules();
    }
  }

  // ======================= 4. 데이터 로드 및 렌더링 =======================

  async function loadWindProfiles() {
    const data = await fetchApi("/api/windProfile", "GET", null, "프로파일 목록 로드");
    if (data && Array.isArray(data.windProfiles)) {
      windProfiles = data.windProfiles;
      const selectEl = $("#profileSelect");
      if (selectEl) {
        selectEl.innerHTML = '<option value="">-- 프로파일 선택 --</option>';
        windProfiles.forEach((p) => {
          const option = document.createElement("option");
          option.value = p.id;
          option.textContent = `${p.id}: ${p.name || p.code}`;
          selectEl.appendChild(option);
        });
      }
    } else {
      windProfiles = [];
    }
  }

  async function loadSchedules() {
    const data = await fetchApi("/api/schedules", "GET", null, "스케줄 목록 불러오기");

    const noMsg = $("#noScheduleMessage");
    if (!$("#scheduleListBody")) return;

    if (data && Array.isArray(data.schedules)) {
      currentSchedules = data.schedules.map((s) => {
        const parsed = parseCronForUi(s.cron);
        return {
          ...s,
          uiTime: parsed.time,
          uiDays: parsed.days
        };
      });
      renderScheduleList(currentSchedules);
      if (noMsg) noMsg.style.display = currentSchedules.length === 0 ? "block" : "none";
    } else {
      currentSchedules = [];
      renderScheduleList([]);
      if (noMsg) noMsg.style.display = "block";
    }
  }

  function renderScheduleList(schedules) {
    const tbody = $("#scheduleListBody");
    if (!tbody) return;
    tbody.innerHTML = "";

    schedules.forEach((schedule) => {
      const tr = document.createElement("tr");
      tr.dataset.scheduleId = schedule.id;

      const profile = windProfiles.find((p) => p.id === schedule.profileId);
      const profileName = profile ? profile.name || profile.code : `[ID:${schedule.profileId} 없음]`;

      const daysText = formatDaysText(schedule.uiDays || []);
      const statusText = schedule.enabled ? "✅ 사용 중" : "❌ 비활성";

      tr.innerHTML = `
        <td>${schedule.id}</td>
        <td><strong>${schedule.name}</strong></td>
        <td>${schedule.uiTime || "00:00"}</td>
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
      tbody.appendChild(tr);
    });
  }

  // ======================= 5. 모달 및 CRUD 핸들러 =======================

  function resetDayCheckboxUI() {
    $$("#repeatDays label").forEach((lab) => lab.classList.remove("checked"));
    $$('#repeatDays input[type="checkbox"]').forEach((el) => (el.checked = false));
  }

  function applyDayCheckboxUI(days) {
    resetDayCheckboxUI();
    (days || []).forEach((d) => {
      const input = $(`#repeatDays input[type="checkbox"][value="${d}"]`);
      if (input) {
        input.checked = true;
        const label = input.closest("label");
        if (label) label.classList.add("checked");
      }
    });
  }

  function openModal(schedule = null) {
    const modal = $("#scheduleModal");
    const form = $("#scheduleForm");
    if (!modal || !form) return;

    form.reset();
    resetDayCheckboxUI();

    if (schedule) {
      $("#modalTitle").textContent = `스케줄 수정: ${schedule.name}`;
      $("#scheduleId").value = schedule.id;
      $("#scheduleName").value = schedule.name;
      $("#scheduleTime").value = schedule.uiTime || "00:00";
      $("#profileSelect").value = schedule.profileId;
      $("#isEnabled").checked = !!schedule.enabled;
      applyDayCheckboxUI(schedule.uiDays);
    } else {
      $("#modalTitle").textContent = "새 스케줄 생성";
      $("#scheduleId").value = "";
      $("#scheduleTime").value = "09:00";
      $("#isEnabled").checked = true;
    }

    modal.style.display = "flex";
  }

  function closeModal() {
    const modal = $("#scheduleModal");
    if (modal) modal.style.display = "none";
  }

  async function saveSchedule(event) {
    event.preventDefault();

    const id = $("#scheduleId").value;
    const isUpdate = !!id;

    const name = $("#scheduleName").value.trim();
    const time = $("#scheduleTime").value;
    const profileId = parseInt($("#profileSelect").value, 10);
    const enabled = $("#isEnabled").checked;

    const selectedDays = $$('input[name="days"]:checked').map((el) =>
      parseInt(el.value, 10)
    );

    if (!name || !time || !profileId || !selectedDays.length) {
      showToast("모든 필수 항목(이름, 시간, 프로파일, 요일)을 선택해주세요.", "err");
      return;
    }

    const cron = buildCronFromUi(time, selectedDays);
    if (!cron) {
      showToast("cron 생성 실패: 시간/요일 설정을 확인해주세요.", "err");
      return;
    }

    const body = {
      name,
      enabled,
      cron,
      profileId
    };

    let result;
    if (isUpdate) {
      result = await fetchApi(`/api/schedules/${id}`, "PUT", body, `스케줄 ${id} 수정`);
    } else {
      result = await fetchApi("/api/schedules", "POST", body, "새 스케줄 생성");
    }

    if (result) {
      setDirtyStatus(true);
      closeModal();
      await loadSchedules();
    }
  }

  async function deleteSchedule(id, name) {
    const result = await fetchApi(`/api/schedules/${id}`, "DELETE", null, `스케줄 ${name} 삭제`);
    if (result) {
      setDirtyStatus(true);
      await loadSchedules();
    }
  }

  async function handleScheduleActions(event) {
    const target = event.target;
    const id = target.dataset.id;
    if (!id) return;

    const schedule = currentSchedules.find((p) => String(p.id) === id);
    if (!schedule) return;

    if (target.classList.contains("btn-edit")) {
      openModal(schedule);
    } else if (target.classList.contains("btn-delete")) {
      if (confirm(`정말로 스케줄 [${schedule.name} (ID: ${id})] 을(를) 삭제하시겠습니까?`)) {
        await deleteSchedule(id, schedule.name);
      }
    }
  }

  // ======================= 6. 이벤트 바인딩 및 초기화 =======================

  function bindEvents() {
    $("#btnCreateNew")?.addEventListener("click", () => openModal(null));
    $("#btnRefreshList")?.addEventListener("click", loadSchedules);
    $("#btnSaveAllConfig")?.addEventListener("click", saveAllConfig);

    $("#btnCloseModal")?.addEventListener("click", closeModal);
    $("#btnCancelModal")?.addEventListener("click", closeModal);

    $("#scheduleForm")?.addEventListener("submit", saveSchedule);

    $("#scheduleListBody")?.addEventListener("click", handleScheduleActions);

    const repeatDays = $("#repeatDays");
    if (repeatDays) {
      repeatDays.addEventListener("change", (e) => {
        const input = e.target.closest('input[type="checkbox"][name="days"]');
        if (!input) return;
        const label = input.closest("label");
        if (!label) return;
        label.classList.toggle("checked", input.checked);
      });
    }
  }

  document.addEventListener("DOMContentLoaded", async () => {
    bindEvents();
    await loadWindProfiles();
    await loadSchedules();
    checkConfigDirtyState();
  });

})();

