/*
 * ------------------------------------------------------
 * 소스명 : P080_schedules_t2_003.js
 * 모듈명 : Smart Nature Wind Schedule Manager Controller (t2, v003)
 * ------------------------------------------------------
 * 기능 요약:
 * - /api/schedules (GET, POST, PUT, DELETE) : C10 풀 구조 기반 CRUD
 * - /api/windProfile (GET) : 프리셋/스타일 목록 로드 → 세그먼트에서 선택
 * - ST_A20_ScheduleItem_t 구조를 그대로 JS 객체로 다루고 JSON 직렬화
 * - /api/config/dirty · /api/config/save 연동 (schedules dirty 표시)
 * - API Key: localStorage["snw_api_key"] 사용 (Main/Dashboard와 통일)
 * ------------------------------------------------------
 */

(() => {
  "use strict";

  // ======================= 1. 공통 헬퍼 / 상수 =======================

  const API_BASE = "/api";

  const API_SCHEDULES = `${API_BASE}/schedules`;
  const API_WIND_PROFILE = `${API_BASE}/windProfile`;
  const API_CONFIG_DIRTY = `${API_BASE}/config/dirty`;
  const API_CONFIG_SAVE = `${API_BASE}/config/save`;

  const API_KEY_STORAGE_KEY = "snw_api_key";

  const $ = (s, r = document) => r.querySelector(s);
  const $$ = (s, r = document) => Array.from(r.querySelectorAll(s));

  const getApiKey = () => {
    try {
      return localStorage.getItem(API_KEY_STORAGE_KEY) || "";
    } catch {
      return "";
    }
  };

  const setLoading = (flag) => {
    const el = $("#loadingOverlay");
    if (el) el.style.display = flag ? "flex" : "none";
  };

  const toast = (msg, type = "info") => {
    if (typeof window.showToast === "function") {
      window.showToast(msg, type);
    } else {
      console.log(`[TOAST-${type}]`, msg);
    }
  };

  async function fetchApi(url, method = "GET", body = null, desc = "") {
    setLoading(true);
    try {
      const opt = {
        method,
        headers: {
          Accept: "application/json",
        },
      };
      const apiKey = getApiKey();
      if (apiKey) opt.headers["X-API-Key"] = apiKey;

      if (body) {
        opt.headers["Content-Type"] = "application/json";
        opt.body = JSON.stringify(body);
      }

      const resp = await fetch(url, opt);
      const text = await resp.text();

      if (resp.status === 401) {
        toast(`[401] ${desc || "작업"} 실패: 인증 필요`, "err");
        throw new Error("Unauthorized");
      }
      if (!resp.ok) {
        toast(`${desc || "작업"} 실패: ${text || resp.status}`, "err");
        throw new Error(text || String(resp.status));
      }

      if (desc && method !== "GET") {
        toast(`${desc} 성공`, "ok");
      }

      if (!text) return null;
      try {
        return JSON.parse(text);
      } catch {
        return text;
      }
    } catch (e) {
      if (e.message !== "Unauthorized") {
        console.error(e);
        if (desc) toast(`${desc} 실패: ${e.message}`, "err");
      }
      return null;
    } finally {
      setLoading(false);
    }
  }

  const DAY_LABELS = ["일", "월", "화", "수", "목", "금", "토"];

  const formatTimeRange = (start, end) => {
    if (!start && !end) return "-";
    return `${start || "??:??"} ~ ${end || "??:??"}`;
  };

  const formatDaysFromBoolArray = (days) => {
    if (!Array.isArray(days) || days.length !== 7) return "-";

    const onIndices = [];
    for (let i = 0; i < 7; i++) {
      if (days[i]) onIndices.push(i);
    }

    if (onIndices.length === 0) return "미사용";
    if (onIndices.length === 7) return "매일";

    const isWeekdays =
      onIndices.length === 5 &&
      onIndices[0] === 1 &&
      onIndices[4] === 5;
    if (isWeekdays) return "주중(월-금)";

    const isWeekend =
      onIndices.length === 2 && onIndices.includes(0) && onIndices.includes(6);
    if (isWeekend) return "주말(토,일)";

    return onIndices.map((d) => DAY_LABELS[d]).join(", ");
  };

  const summarizeSegments = (segments) => {
    if (!Array.isArray(segments) || segments.length === 0) return "-";
    const first = segments[0];
    const mode = first.mode || "PRESET";
    if (mode === "FIXED") {
      return `FIXED ${first.fixed_speed ?? 0}% 포함, 총 ${segments.length}개`;
    }
    const preset = first.presetCode || "PRESET";
    const style = first.styleCode || "";
    return `${preset}${style ? "/" + style : ""} 포함, 총 ${segments.length}개`;
  };

  // ======================= 2. 상태 변수 =======================

  /** @type {Array<any>} */
  let currentSchedules = [];

  /** windProfile에서 presets/styles */
  let windPresets = [];
  let windStyles = [];

  /** config dirty 상태(schedules) */
  let configDirty = false;

  // ======================= 3. Config Dirty 관리 =======================

  function setDirtyStatus(isDirty) {
    configDirty = !!isDirty;
    const btn = $("#btnSaveAllConfig");
    if (!btn) return;

    if (configDirty) {
      btn.style.backgroundColor = "#dc2626";
      btn.style.color = "#ffffff";
      btn.textContent = "⚠️ 전체 설정 저장 (스케줄 변경 미저장)";
    } else {
      btn.style.backgroundColor = "";
      btn.style.color = "";
      btn.textContent = "전체 설정 저장";
    }
  }

  async function pollConfigDirty() {
    try {
      const apiKey = getApiKey();
      const resp = await fetch(API_CONFIG_DIRTY, {
        headers: {
          Accept: "application/json",
          ...(apiKey ? { "X-API-Key": apiKey } : {}),
        },
      });
      if (resp.ok) {
        const j = await resp.json();
        setDirtyStatus(!!j.schedules);
      }
    } catch (e) {
      console.warn("[Schedule] config dirty 조회 실패:", e.message);
    } finally {
      setTimeout(pollConfigDirty, 5000);
    }
  }

  async function saveAllConfig() {
    if (!configDirty) {
      toast("저장할 변경 사항이 없습니다.", "warn");
      return;
    }
    const res = await fetchApi(
      API_CONFIG_SAVE,
      "POST",
      {}, // v029 스펙: 빈 바디로 더티 섹션 저장
      "전체 설정 파일 저장"
    );
    if (res !== null) {
      setDirtyStatus(false);
      await loadSchedules();
    }
  }

  // ======================= 4. WindProfile 로딩 =======================

  async function loadWindProfileDict() {
    const data = await fetchApi(
      API_WIND_PROFILE,
      "GET",
      null,
      "WindProfile 사전 로드"
    );
    if (!data || !data.windProfile) {
      windPresets = [];
      windStyles = [];
      return;
    }
    const wp = data.windProfile;
    windPresets = Array.isArray(wp.presets) ? wp.presets : [];
    windStyles = Array.isArray(wp.styles) ? wp.styles : [];
  }

  // ======================= 5. 스케줄 목록 로드/렌더 =======================

  async function loadSchedules() {
    const data = await fetchApi(API_SCHEDULES, "GET", null, "스케줄 목록 불러오기");
    const tbody = $("#scheduleListBody");
    const noMsg = $("#noScheduleMessage");
    if (!tbody) return;

    if (data && Array.isArray(data.schedules)) {
      currentSchedules = data.schedules;
    } else {
      currentSchedules = [];
    }
    renderScheduleList(currentSchedules);

    if (noMsg) noMsg.style.display = currentSchedules.length === 0 ? "block" : "none";
  }

  function renderScheduleList(schedules) {
    const tbody = $("#scheduleListBody");
    if (!tbody) return;
    tbody.innerHTML = "";

    schedules.forEach((s) => {
      const tr = document.createElement("tr");
      tr.dataset.schId = s.schId;

      const period = s.period || {};
      const days = Array.isArray(period.days) ? period.days : [1, 1, 1, 1, 1, 1, 1];

      const enabled = !!s.enabled;
      const statusClass = enabled ? "on" : "off";
      const statusText = enabled ? "사용 중" : "비활성";

      const timeText = formatTimeRange(period.startTime, period.endTime);
      const daysText = formatDaysFromBoolArray(days);
      const segSummary = summarizeSegments(s.segments);

      tr.innerHTML = `
        <td>${s.schId ?? "-"}</td>
        <td>${s.schNo ?? "-"}</td>
        <td><strong>${s.name || "-"}</strong></td>
        <td class="text-mono">${timeText}</td>
        <td>${daysText}</td>
        <td>${segSummary}</td>
        <td>
          <span class="schedule-status-label ${statusClass}">${statusText}</span>
        </td>
        <td>
          <div class="action-buttons">
            <button class="btn btn-small btn-edit" data-id="${s.schId}">수정</button>
            <button class="btn btn-small btn-err btn-delete" data-id="${s.schId}">삭제</button>
          </div>
        </td>
      `;
      tbody.appendChild(tr);
    });
  }

  // ======================= 6. 모달 표시/닫기 및 폼 채우기 =======================

  function resetPeriodDaysUI() {
    $$("#periodDays label").forEach((lab) => lab.classList.remove("checked"));
    $$("#periodDays input[type='checkbox']").forEach((el) => (el.checked = false));
  }

  function applyPeriodDaysUI(days) {
    resetPeriodDaysUI();
    if (!Array.isArray(days) || days.length !== 7) return;
    // days[index] 가 truthy면 체크
    $$("#periodDays input[type='checkbox']").forEach((input) => {
      const idx = Number(input.dataset.index);
      if (!Number.isNaN(idx) && days[idx]) {
        input.checked = true;
        const label = input.closest("label");
        if (label) label.classList.add("checked");
      }
    });
  }

  function renderSegmentsInModal(segments) {
    const tbody = $("#segmentListBody");
    if (!tbody) return;
    tbody.innerHTML = "";

    const segs = Array.isArray(segments) ? segments : [];

    segs.forEach((seg, idx) => {
      addSegmentRow(seg, idx === 0);
    });
  }

  function addSegmentRow(seg = null, appendToEnd = true) {
    const tbody = $("#segmentListBody");
    if (!tbody) return;

    const row = document.createElement("tr");
    row.className = "segment-row";

    const segId = seg?.segId ?? 0;
    const segNo = seg?.segNo ?? 10;
    const onMin = seg?.onMinutes ?? 10;
    const offMin = seg?.offMinutes ?? 0;
    const mode = seg?.mode || "PRESET";
    const presetCode = seg?.presetCode || "";
    const styleCode = seg?.styleCode || "";
    const fixedSpeed = seg?.fixed_speed ?? 0.0;

    const adj = seg?.adjust || {};
    const adjWind = adj.windIntensity ?? 0.0;
    const adjVar = adj.windVariability ?? 0.0;
    const adjGust = adj.gustFrequency ?? 0.0;
    const adjFanLimit = adj.fanLimit ?? 0.0;
    const adjMinFan = adj.minFan ?? 0.0;
    const adjTurbL = adj.turbulence_length_scale ?? 0.0;
    const adjTurbSigma = adj.turbulence_intensity_sigma ?? 0.0;

    const presetOptions = [
      `<option value="">(없음)</option>`,
      ...windPresets.map(
        (p) =>
          `<option value="${p.code}" ${
            p.code === presetCode ? "selected" : ""
          }>${p.code} - ${p.name}</option>`
      ),
    ].join("");

    const styleOptions = [
      `<option value="">(없음)</option>`,
      ...windStyles.map(
        (s) =>
          `<option value="${s.code}" ${
            s.code === styleCode ? "selected" : ""
          }>${s.code} - ${s.name}</option>`
      ),
    ].join("");

    row.innerHTML = `
      <td>
        <input type="number" class="seg-id" min="0" step="1" value="${segId}" />
      </td>
      <td>
        <input type="number" class="seg-no" min="0" step="1" value="${segNo}" />
      </td>
      <td>
        <input type="number" class="seg-on-min" min="0" step="1" value="${onMin}" />
      </td>
      <td>
        <input type="number" class="seg-off-min" min="0" step="1" value="${offMin}" />
      </td>
      <td>
        <select class="seg-mode">
          <option value="PRESET" ${mode === "PRESET" ? "selected" : ""}>PRESET</option>
          <option value="FIXED" ${mode === "FIXED" ? "selected" : ""}>FIXED</option>
        </select>
      </td>
      <td>
        <select class="seg-preset">
          ${presetOptions}
        </select>
      </td>
      <td>
        <select class="seg-style">
          ${styleOptions}
        </select>
      </td>
      <td>
        <input type="number" class="seg-fixed-speed" step="0.1" value="${fixedSpeed}" />
      </td>
      <td>
        <div class="grid grid-3">
          <input type="number" class="seg-adj-wind" step="0.1" placeholder="강도" value="${adjWind}" />
          <input type="number" class="seg-adj-var" step="0.1" placeholder="변동" value="${adjVar}" />
          <input type="number" class="seg-adj-gust" step="0.1" placeholder="돌풍" value="${adjGust}" />
        </div>
        <div class="grid grid-2">
          <input type="number" class="seg-adj-fanlimit" step="0.1" placeholder="제한" value="${adjFanLimit}" />
          <input type="number" class="seg-adj-minfan" step="0.1" placeholder="최소" value="${adjMinFan}" />
        </div>
      </td>
      <td>
        <div class="grid grid-2">
          <input type="number" class="seg-adj-turbl" step="0.1" placeholder="L" value="${adjTurbL}" />
          <input type="number" class="seg-adj-turbs" step="0.1" placeholder="σ" value="${adjTurbSigma}" />
        </div>
      </td>
      <td>
        <button type="button" class="btn btn-small btn-err btn-del-seg">삭제</button>
      </td>
    `;

    if (appendToEnd) {
      tbody.appendChild(row);
    } else {
      tbody.insertBefore(row, tbody.firstChild);
    }
  }

  function openModal(schedule = null) {
    const modal = $("#scheduleModal");
    const form = $("#scheduleForm");
    if (!modal || !form) return;

    form.reset();
    resetPeriodDaysUI();
    $("#segmentListBody").innerHTML = "";

    if (schedule) {
      $("#modalTitle").textContent = `스케줄 수정: ${schedule.name}`;
      $("#scheduleId").value = schedule.schId ?? "";
      $("#schNo").value = schedule.schNo ?? "";
      $("#scheduleName").value = schedule.name || "";
      $("#isEnabled").checked = !!schedule.enabled;
      $("#repeatSegments").checked = schedule.repeatSegments ?? true;
      $("#repeatCount").value = schedule.repeatCount ?? 1;

      const period = schedule.period || {};
      //// $("#periodEnabled").checked = period.enabled ?? true;
      $("#periodStart").value = period.startTime || "08:00";
      $("#periodEnd").value = period.endTime || "23:00";
      applyPeriodDaysUI(Array.isArray(period.days) ? period.days : [1, 1, 1, 1, 1, 1, 1]);

      const ao = schedule.autoOff || {};
      const timer = ao.timer || {};
      const offTime = ao.offTime || {};
      const offTemp = ao.offTemp || {};

      $("#autoOffTimerEnabled").checked = timer.enabled ?? false;
      $("#autoOffTimerMinutes").value = timer.minutes ?? 0;
      $("#autoOffTimeEnabled").checked = offTime.enabled ?? false;
      $("#autoOffTime").value = offTime.time || "00:00";
      $("#autoOffTempEnabled").checked = offTemp.enabled ?? false;
      $("#autoOffTemp").value = offTemp.temp ?? 0;

      const motion = schedule.motion || {};
      const pir = motion.pir || {};
      const ble = motion.ble || {};

      $("#pirEnabled").checked = pir.enabled ?? false;
      $("#pirHoldSec").value = pir.holdSec ?? 0;
      $("#bleEnabled").checked = ble.enabled ?? false;
      $("#bleRssiThreshold").value = ble.rssi_threshold ?? -70;
      $("#bleHoldSec").value = ble.holdSec ?? 0;

      renderSegmentsInModal(schedule.segments || []);
    } else {
      $("#modalTitle").textContent = "새 스케줄 생성";
      $("#scheduleId").value = "";
      $("#schNo").value = "";
      $("#scheduleName").value = "";
      $("#isEnabled").checked = true;
      $("#repeatSegments").checked = true;
      $("#repeatCount").value = 1;
      //// $("#periodEnabled").checked = true;
      $("#periodStart").value = "08:00";
      $("#periodEnd").value = "23:00";
      applyPeriodDaysUI([1, 1, 1, 1, 1, 1, 1]);

      $("#autoOffTimerEnabled").checked = false;
      $("#autoOffTimerMinutes").value = 0;
      $("#autoOffTimeEnabled").checked = false;
      $("#autoOffTime").value = "00:00";
      $("#autoOffTempEnabled").checked = false;
      $("#autoOffTemp").value = 0;

      $("#pirEnabled").checked = true;
      $("#pirHoldSec").value = 120;
      $("#bleEnabled").checked = true;
      $("#bleRssiThreshold").value = -70;
      $("#bleHoldSec").value = 120;

      // 기본 세그먼트 1개 정도 생성
      addSegmentRow(
        {
          segId: 1,
          segNo: 10,
          onMinutes: 20,
          offMinutes: 10,
          mode: "PRESET",
          presetCode: "",
          styleCode: "",
          fixed_speed: 0,
          adjust: {
            windIntensity: 0,
            windVariability: 0,
            gustFrequency: 0,
            fanLimit: 0,
            minFan: 0,
            turbulence_length_scale: 0,
            turbulence_intensity_sigma: 0,
          },
        },
        true
      );
    }

    modal.style.display = "flex";
  }

  function closeModal() {
    const modal = $("#scheduleModal");
    if (modal) modal.style.display = "none";
  }

  // ======================= 7. 폼 → 스케줄 객체 변환 =======================

  function buildDaysFromUI() {
    const days = [0, 0, 0, 0, 0, 0, 0];
    $$("#periodDays input[type='checkbox']").forEach((input) => {
      const idx = Number(input.dataset.index);
      if (!Number.isNaN(idx) && idx >= 0 && idx < 7) {
        days[idx] = input.checked ? 1 : 0;
      }
    });
    return days;
  }

  function buildSegmentsFromUI() {
    /** @type {Array<any>} */
    const segments = [];
    $$("#segmentListBody .segment-row").forEach((row, idx) => {
      const getVal = (sel) => row.querySelector(sel)?.value ?? "";

      const segId = Number(getVal(".seg-id")) || 0;
      const segNo = Number(getVal(".seg-no")) || (idx + 1) * 10;
      const onMin = Number(getVal(".seg-on-min")) || 0;
      const offMin = Number(getVal(".seg-off-min")) || 0;
      const mode = getVal(".seg-mode") || "PRESET";
      const presetCode = getVal(".seg-preset") || "";
      const styleCode = getVal(".seg-style") || "";
      const fixedSpeed = Number(getVal(".seg-fixed-speed")) || 0;

      const adjWind = Number(getVal(".seg-adj-wind")) || 0;
      const adjVar = Number(getVal(".seg-adj-var")) || 0;
      const adjGust = Number(getVal(".seg-adj-gust")) || 0;
      const adjFanLimit = Number(getVal(".seg-adj-fanlimit")) || 0;
      const adjMinFan = Number(getVal(".seg-adj-minfan")) || 0;
      const adjTurbL = Number(getVal(".seg-adj-turbl")) || 0;
      const adjTurbSigma = Number(getVal(".seg-adj-turbs")) || 0;

      segments.push({
        segId,
        segNo,
        onMinutes: onMin,
        offMinutes: offMin,
        mode,
        presetCode,
        styleCode,
        adjust: {
          windIntensity: adjWind,
          windVariability: adjVar,
          gustFrequency: adjGust,
          fanLimit: adjFanLimit,
          minFan: adjMinFan,
          turbulence_length_scale: adjTurbL,
          turbulence_intensity_sigma: adjTurbSigma,
        },
        fixed_speed: fixedSpeed,
      });
    });

    return segments;
  }

  function buildScheduleFromForm() {
    const schIdRaw = $("#scheduleId").value;
    const schId = schIdRaw ? Number(schIdRaw) : 0;

    const schNo = Number($("#schNo").value) || 0;
    const name = $("#scheduleName").value.trim();
    const enabled = $("#isEnabled").checked;
    const repeatSegments = $("#repeatSegments").checked;
    const repeatCount = Number($("#repeatCount").value) || 0;

    //// const periodEnabled = $("#periodEnabled").checked;
    const periodStart = $("#periodStart").value || "00:00";
    const periodEnd = $("#periodEnd").value || "23:59";
    const periodDays = buildDaysFromUI();

    const autoOffTimerEnabled = $("#autoOffTimerEnabled").checked;
    const autoOffTimerMinutes = Number($("#autoOffTimerMinutes").value) || 0;
    const autoOffTimeEnabled = $("#autoOffTimeEnabled").checked;
    const autoOffTime = $("#autoOffTime").value || "00:00";
    const autoOffTempEnabled = $("#autoOffTempEnabled").checked;
    const autoOffTemp = Number($("#autoOffTemp").value) || 0;

    const pirEnabled = $("#pirEnabled").checked;
    const pirHoldSec = Number($("#pirHoldSec").value) || 0;
    const bleEnabled = $("#bleEnabled").checked;
    const bleRssi = Number($("#bleRssiThreshold").value) || -70;
    const bleHoldSec = Number($("#bleHoldSec").value) || 0;

    const segments = buildSegmentsFromUI();

    return {
      schId,
      schNo,
      name,
      enabled,
      repeatSegments,
      repeatCount,
      period: {
        //// enabled: periodEnabled,
        days: periodDays,
        startTime: periodStart,
        endTime: periodEnd,
      },
      segments,
      autoOff: {
        timer: {
          enabled: autoOffTimerEnabled,
          minutes: autoOffTimerMinutes,
        },
        offTime: {
          enabled: autoOffTimeEnabled,
          time: autoOffTime,
        },
        offTemp: {
          enabled: autoOffTempEnabled,
          temp: autoOffTemp,
        },
      },
      motion: {
        pir: {
          enabled: pirEnabled,
          holdSec: pirHoldSec,
        },
        ble: {
          enabled: bleEnabled,
          rssi_threshold: bleRssi,
          holdSec: bleHoldSec,
        },
      },
    };
  }

  // ======================= 8. CRUD 핸들러 =======================

  async function saveSchedule(event) {
    event.preventDefault();

    const schedule = buildScheduleFromForm();

    if (!schedule.name) {
      toast("스케줄 이름을 입력해주세요.", "err");
      return;
    }

    if (!Array.isArray(schedule.segments) || schedule.segments.length === 0) {
      toast("최소 1개 이상의 세그먼트를 추가해주세요.", "err");
      return;
    }

    const isUpdate = !!schedule.schId;
    let url = API_SCHEDULES;
    let method = "POST";
    let desc = "새 스케줄 생성";

    if (isUpdate) {
      url = `${API_SCHEDULES}/${schedule.schId}`;
      method = "PUT";
      desc = `스케줄 ${schedule.schId} 수정`;
    }

    const payload = { schedule };

    const result = await fetchApi(url, method, payload, desc);
    if (result !== null) {
      setDirtyStatus(true);
      closeModal();
      await loadSchedules();
    }
  }

  async function deleteSchedule(schId, name) {
    const result = await fetchApi(
      `${API_SCHEDULES}/${schId}`,
      "DELETE",
      null,
      `스케줄 ${name} 삭제`
    );
    if (result !== null) {
      setDirtyStatus(true);
      await loadSchedules();
    }
  }

  async function handleScheduleActions(event) {
    const target = event.target;
    if (!(target instanceof HTMLElement)) return;
    const schId = target.dataset.id;
    if (!schId) return;

    const schedule = currentSchedules.find((s) => String(s.schId) === String(schId));
    if (!schedule) return;

    if (target.classList.contains("btn-edit")) {
      openModal(schedule);
    } else if (target.classList.contains("btn-delete")) {
      if (
        confirm(
          `정말로 스케줄 [${schedule.name} (ID: ${schedule.schId})] 을(를) 삭제하시겠습니까?`
        )
      ) {
        await deleteSchedule(schedule.schId, schedule.name);
      }
    }
  }

  // ======================= 9. 이벤트 바인딩 =======================

  function bindEvents() {
    $("#btnCreateNew")?.addEventListener("click", () => openModal(null));
    $("#btnRefreshList")?.addEventListener("click", loadSchedules);
    $("#btnSaveAllConfig")?.addEventListener("click", saveAllConfig);

    $("#btnCloseModal")?.addEventListener("click", closeModal);
    $("#btnCancelModal")?.addEventListener("click", closeModal);

    $("#scheduleForm")?.addEventListener("submit", saveSchedule);

    $("#scheduleListBody")?.addEventListener("click", handleScheduleActions);

    // period 요일 toggle
    const periodDays = $("#periodDays");
    if (periodDays) {
      periodDays.addEventListener("change", (e) => {
        const input = e.target.closest("input[type='checkbox']");
        if (!input) return;
        const label = input.closest("label");
        if (!label) return;
        label.classList.toggle("checked", input.checked);
      });
    }

    // 세그먼트 추가 버튼
    $("#btnAddSegment")?.addEventListener("click", () => addSegmentRow(null, true));

    // 세그먼트 삭제 버튼 (이벤트 위임)
    $("#segmentListBody")?.addEventListener("click", (e) => {
      const btn = e.target.closest(".btn-del-seg");
      if (!btn) return;
      const row = btn.closest(".segment-row");
      if (row && row.parentNode) {
        row.parentNode.removeChild(row);
      }
    });
  }

  // ======================= 10. 초기화 =======================

  document.addEventListener("DOMContentLoaded", async () => {
    // API Key 안내
    if (!getApiKey()) {
      toast(
        "API Key가 비어 있습니다. 메인 설정 페이지에서 API Key를 먼저 설정해 주세요.",
        "warn"
      );
    }

    bindEvents();
    await loadWindProfileDict();
    await loadSchedules();
    pollConfigDirty();
  });
})();
