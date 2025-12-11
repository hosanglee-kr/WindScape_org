/*
 * ------------------------------------------------------
 * 소스명 : P085_userProfiles_t2_003.js
 * 모듈명 : Smart Nature Wind User Profile Manager Controller (v003)
 * ------------------------------------------------------
 * 기능 요약:
 *  - 🎯 /api/user_profiles (GET, POST, PUT, DELETE) CRUD (C10 UserProfiles 풀 구조)
 *  - 🎯 /api/windProfile (GET) → presets/styles 기반 presetCode/styleCode 선택 옵션 로드
 *  - Profile → segments / autoOff / motion 전체 JSON 매핑
 *  - 세그먼트 segId 자동 증가, segNo 기본값 10단위 (수동 수정 가능)
 *  - 🎯 /api/config/dirty, /api/config/save 연동 (userProfiles dirty 상태 표시/저장)
 * ------------------------------------------------------
 */

(() => {
  "use strict";

  // ======================= 1. 공통 헬퍼 =======================

  const $ = (s, r = document) => r.querySelector(s);
  const $$ = (s, r = document) => Array.from(r.querySelectorAll(s));

  const KEY_API = "sc10_api_key";
  const getKey = () => localStorage.getItem(KEY_API) || "";

  const setLoading = (flag) => {
    const el = $("#loadingOverlay");
    if (el) el.style.display = flag ? "flex" : "none";
  };

  // P000_common_006.js 에 showToast 정의되어 있으면 우선 사용
  const showToast = (msg, type = "ok") => {
    if (typeof window.showToast === "function" && window.showToast !== showToast) {
      window.showToast(msg, type);
      return;
    }
    console.log(`[TOAST][${type}] ${msg}`);
  };

  async function fetchApi(url, method = "GET", body = null, desc = "작업") {
    setLoading(true);
    const opt = { method, headers: {} };
    const k = getKey();
    if (k) opt.headers["X-API-Key"] = k;

    if (body) {
      opt.headers["Content-Type"] = "application/json";
      opt.body = JSON.stringify(body);
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
      if (method !== "GET") {
        showToast(`${desc} 성공`, "ok");
      }

      const text = await resp.text();
      if (!text) return null;
      try {
        return JSON.parse(text);
      } catch {
        return text;
      }
    } catch (e) {
      if (e.message !== "Unauthorized") console.error("[fetchApi]", e);
      return null;
    } finally {
      setLoading(false);
    }
  }

  // ======================= 2. 상태 변수 =======================

  /** @type {Array<any>} */
  let currentProfiles = [];
  /** @type {Array<any>} */
  let windPresets = []; // {code,name,base:...}
  /** @type {Array<any>} */
  let windStyles = [];  // {code,name,factors:...}

  let configDirty = false;
  let nextSegmentId = 1; // 모달 내에서 segId 자동 증가용

  // preset code → 한글 표시용 (fallback)
  const presetNameMap = {
    OFF: "고정풍",
    COUNTRY: "들판",
    MEDITERRANEAN: "지중해",
    OCEAN: "바다",
    MOUNTAIN: "산바람",
    PLAINS: "평야",
    HARBOR_BREEZE: "항구바람",
    FOREST_CANOPY: "숲속",
    URBAN_SUNSET: "도심 석양",
    TROPICAL_RAIN: "열대우림",
    DESERT_NIGHT: "사막의 밤"
  };

  const styleNameMap = {
    BALANCE: "밸런스",
    RELAX: "휴식",
    SLEEP: "수면",
    FOCUS: "집중",
    ACTIVE: "활동"
  };

  const displayPresetLabel = (code) => {
    if (!code) return "-";
    const p = windPresets.find((x) => x.code === code);
    if (p) return `${p.name || presetNameMap[p.code] || p.code} (${p.code})`;
    return presetNameMap[code] || code;
  };

  const displayStyleLabel = (code) => {
    if (!code) return "-";
    const s = windStyles.find((x) => x.code === code);
    if (s) return `${s.name || styleNameMap[s.code] || s.code} (${s.code})`;
    return styleNameMap[code] || code;
  };

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
      const res = await fetchApi("/api/config/dirty", "GET", null, "Dirty 상태 조회");
      if (!res) return;

      // 형태 유연 처리: { userProfiles: true } 또는 { sections: { userProfiles: true } } 등
      let dirty = false;
      if (typeof res.userProfiles === "boolean") {
        dirty = res.userProfiles;
      } else if (res.sections && typeof res.sections.userProfiles === "boolean") {
        dirty = res.sections.userProfiles;
      }
      setDirtyStatus(dirty);
    } catch (e) {
      console.warn("[UserProfiles] Dirty 상태 조회 실패:", e?.message);
    } finally {
      // 주기적 폴링
      setTimeout(checkConfigDirtyState, 8000);
    }
  }

  async function saveAllConfig() {
    if (!configDirty) {
      showToast("저장할 변경 사항이 없습니다.", "warn");
      return;
    }
    const res = await fetchApi("/api/config/save", "POST", {}, "전체 설정 파일 저장");
    if (res !== null) {
      setDirtyStatus(false);
      await loadUserProfiles();
    }
  }

  // ======================= 4. 데이터 로드 =======================

  async function loadWindProfileDict() {
    const data = await fetchApi("/api/windProfile", "GET", null, "Wind Profile 사전 로드");

    let root = data || {};
    if (root.windProfile) root = root.windProfile;

    const presets = Array.isArray(root.presets) ? root.presets : [];
    const styles = Array.isArray(root.styles) ? root.styles : [];

    windPresets = presets;
    windStyles = styles;

    // 모달 내 preset/style select 채우기
    const presetSelAll = $$(".seg-preset-select");
    if (presetSelAll.length === 0) {
      // 아직 모달이 안 떠있을 수도 있으니, openModal 시점에 한 번 더 채움
      return;
    }
    presetSelAll.forEach((sel) => fillPresetSelectOptions(sel));

    const styleSelAll = $$(".seg-style-select");
    styleSelAll.forEach((sel) => fillStyleSelectOptions(sel));
  }

  async function loadUserProfiles() {
    const data = await fetchApi("/api/user_profiles", "GET", null, "유저 프로파일 목록 로드");
    const noMsg = $("#noProfileMessage");

    let profiles = [];
    if (Array.isArray(data)) {
      profiles = data;
    } else if (data && Array.isArray(data.profiles)) {
      profiles = data.profiles;
    } else if (data && data.userProfiles && Array.isArray(data.userProfiles.profiles)) {
      profiles = data.userProfiles.profiles;
    }

    currentProfiles = profiles || [];
    renderProfileList(currentProfiles);

    if (noMsg) {
      noMsg.style.display = currentProfiles.length === 0 ? "block" : "none";
    }
  }

  // ======================= 5. 리스트 렌더링 =======================

  function summarizeSegments(profile) {
    if (!profile || !Array.isArray(profile.segments) || profile.segments.length === 0)
      return "세그먼트 없음";

    const segCount = profile.segments.length;
    const totalOnMin = profile.segments.reduce(
      (acc, s) => acc + (Number(s.on_minutes) || 0),
      0
    );
    const modes = new Set(
      profile.segments.map((s) => (s.mode || "PRESET").toUpperCase())
    );
    const modeText = Array.from(modes).join("/");

    return `${segCount}개, 합계 ${totalOnMin}분, 모드 ${modeText}`;
  }

  function summarizeAutoOff(profile) {
    const ao = profile.autoOff || {};
    const t = ao.timer || {};
    const ot = ao.offTime || {};
    const tt = ao.offTemp || {};
    const parts = [];

    if (t.enabled) parts.push(`타이머 ${t.minutes || 0}분`);
    if (ot.enabled) parts.push(`시간 ${ot.time || "??:??"}`);
    if (tt.enabled) parts.push(`온도 ${tt.temp || 0}℃`);

    if (parts.length === 0) return "사용 안 함";
    return parts.join(" / ");
  }

  function summarizeMotion(profile) {
    const m = profile.motion || {};
    const pir = m.pir || {};
    const ble = m.ble || {};
    const parts = [];
    if (pir.enabled) parts.push(`PIR(${pir.hold_sec || 0}s)`);
    if (ble.enabled) parts.push(`BLE(${ble.rssi_threshold || -70}dBm, ${ble.hold_sec || 0}s)`);
    if (parts.length === 0) return "사용 안 함";
    return parts.join(" / ");
  }

  function renderProfileList(list) {
    const tbody = $("#profileListBody");
    if (!tbody) return;
    tbody.innerHTML = "";

    list.forEach((p) => {
      const tr = document.createElement("tr");
      tr.dataset.profileId = String(p.profileId ?? p.profile_id ?? "");

      const enabled = p.enabled !== false;
      const repeatSegments = p.repeatSegments !== false;
      const repeatCount = Number(p.repeatCount ?? 0) || 0;

      const rowHtml = `
        <td>${p.profileId ?? "-"}</td>
        <td>
          <strong>${p.name || "-"}</strong><br>
          <span class="muted">No: ${p.profileNo ?? "-"}</span>
        </td>
        <td>
          <span class="badge ${enabled ? "badge-on" : "badge-off"}">
            ${enabled ? "사용" : "OFF"}
          </span>
        </td>
        <td>
          ${repeatSegments ? "세그먼트 반복" : "1회 실행"}<br>
          <span class="muted">반복 횟수: ${repeatCount}</span>
        </td>
        <td>${summarizeSegments(p)}</td>
        <td>${summarizeAutoOff(p)}</td>
        <td>${summarizeMotion(p)}</td>
        <td>
          <div class="action-buttons">
            <button class="btn btn-small btn-edit" data-id="${p.profileId}">수정</button>
            <button class="btn btn-small btn-err btn-delete" data-id="${p.profileId}">삭제</button>
          </div>
        </td>
      `;
      tr.innerHTML = rowHtml;
      tbody.appendChild(tr);
    });
  }

  // ======================= 6. 모달 열기/닫기 =======================

  function openModal(profile = null) {
    const modal = $("#profileModal");
    const form = $("#profileForm");
    if (!modal || !form) return;

    form.reset();
    $("#segmentsBody").innerHTML = "";
    nextSegmentId = 1;

    // preset/style select는 모달 열릴 때도 한 번 채움
    // (windProfileDict 로드가 끝난 이후에 열릴 수도 있기 때문)
    // → 세그먼트 행 생성시마다 options 세팅

    if (profile) {
      fillModalWithProfile(profile);
      $("#modalTitle").textContent = `유저 프로파일 수정: ${profile.name || profile.profileId}`;
    } else {
      // 신규
      $("#profileId").value = "";
      $("#profileNo").value = "";
      $("#profileName").value = "";
      const enabledEl = $("#profileEnabled");
      if (enabledEl) enabledEl.checked = true;

      const repSegEl = $("#repeatSegments");
      if (repSegEl) repSegEl.checked = true;
      const repCntEl = $("#repeatCount");
      if (repCntEl) repCntEl.value = 1;

      // 기본 세그먼트 한 개 정도 생성
      addSegmentRow();
      $("#modalTitle").textContent = "새 유저 프로파일 생성";
    }

    modal.style.display = "flex";
  }

  function closeModal() {
    const modal = $("#profileModal");
    if (modal) modal.style.display = "none";
  }

  // ======================= 7. 모달 채우기 / 값 읽기 =======================

  function fillPresetSelectOptions(selectEl) {
    if (!selectEl) return;
    selectEl.innerHTML = '<option value="">-- Preset 선택 --</option>';
    windPresets.forEach((p) => {
      const opt = document.createElement("option");
      opt.value = p.code;
      opt.textContent = `${p.name || presetNameMap[p.code] || p.code} (${p.code})`;
      selectEl.appendChild(opt);
    });
  }

  function fillStyleSelectOptions(selectEl) {
    if (!selectEl) return;
    selectEl.innerHTML = '<option value="">-- Style 선택 --</option>';
    windStyles.forEach((s) => {
      const opt = document.createElement("option");
      opt.value = s.code;
      opt.textContent = `${s.name || styleNameMap[s.code] || s.code} (${s.code})`;
      selectEl.appendChild(opt);
    });
  }

  // 세그먼트 행 HTML 템플릿 생성
  function createSegmentRow(seg = null) {
    const tr = document.createElement("tr");
    tr.classList.add("segment-row");

    const segId = seg?.segId ?? seg?.seqId ?? nextSegmentId++;
    const segNoDefault = segId * 10;
    const segNo = seg?.segNo ?? segNoDefault;

    const onMin = seg?.on_minutes ?? 10;
    const offMin = seg?.off_minutes ?? 0;
    const mode = (seg?.mode || "PRESET").toUpperCase();

    const presetCode = seg?.presetCode || "";
    const styleCode = seg?.styleCode || "";

    const fixedSpeed = seg?.fixed_speed ?? 0;

    const adj = seg?.adjust || {};
    const adj_wi = adj.wind_intensity ?? 0;
    const adj_wv = adj.wind_variability ?? 0;
    const adj_gf = adj.gust_frequency ?? 0;
    const adj_fl = adj.fan_limit ?? 0;
    const adj_mf = adj.min_fan ?? 0;
    const adj_tls = adj.turbulence_length_scale ?? 0;
    const adj_tis = adj.turbulence_intensity_sigma ?? 0;

    tr.innerHTML = `
      <td>
        <input type="number" class="seg-id" value="${segId}" min="1" step="1" readonly>
      </td>
      <td>
        <input type="number" class="seg-no" value="${segNo}" min="0" step="10">
      </td>
      <td>
        <input type="number" class="seg-on-min" value="${onMin}" min="1" max="600" step="1">
      </td>
      <td>
        <input type="number" class="seg-off-min" value="${offMin}" min="0" max="600" step="1">
      </td>
      <td>
        <select class="seg-mode">
          <option value="PRESET"${mode === "PRESET" ? " selected" : ""}>PRESET</option>
          <option value="FIXED"${mode === "FIXED" ? " selected" : ""}>FIXED</option>
        </select>
      </td>
      <td>
        <select class="seg-preset-select"></select>
        <select class="seg-style-select"></select>
      </td>
      <td>
        <input type="number" class="seg-fixed-speed" value="${fixedSpeed}" min="0" max="100" step="1">
      </td>
      <td>
        <button type="button" class="btn btn-small seg-adjust-toggle">조정</button>
        <button type="button" class="btn btn-small btn-err seg-delete">삭제</button>
        <div class="seg-adjust-panel" style="display:none; margin-top:6px;">
          <div class="seg-adjust-grid">
            <label>ΔIntensity
              <input type="number" class="seg-adj-wi" step="1" value="${adj_wi}">
            </label>
            <label>ΔVariability
              <input type="number" class="seg-adj-wv" step="1" value="${adj_wv}">
            </label>
            <label>ΔGust
              <input type="number" class="seg-adj-gf" step="1" value="${adj_gf}">
            </label>
            <label>ΔFanLimit
              <input type="number" class="seg-adj-fl" step="1" value="${adj_fl}">
            </label>
            <label>ΔMinFan
              <input type="number" class="seg-adj-mf" step="1" value="${adj_mf}">
            </label>
            <label>ΔT.L
              <input type="number" class="seg-adj-tls" step="0.1" value="${adj_tls}">
            </label>
            <label>ΔT.Sigma
              <input type="number" class="seg-adj-tis" step="0.1" value="${adj_tis}">
            </label>
          </div>
        </div>
      </td>
    `;

    // preset/style options 채우기
    const presetSel = tr.querySelector(".seg-preset-select");
    const styleSel = tr.querySelector(".seg-style-select");
    fillPresetSelectOptions(presetSel);
    fillStyleSelectOptions(styleSel);
    if (presetCode) presetSel.value = presetCode;
    if (styleCode) styleSel.value = styleCode;

    // mode에 따라 preset/fixed 필드 활성화 제어
    applyModeVisibility(tr, mode);

    return tr;
  }

  function applyModeVisibility(tr, mode) {
    const isFixed = mode === "FIXED";
    const presetSel = tr.querySelector(".seg-preset-select");
    const styleSel = tr.querySelector(".seg-style-select");
    const fixedInput = tr.querySelector(".seg-fixed-speed");

    if (presetSel) presetSel.disabled = isFixed;
    if (styleSel) styleSel.disabled = isFixed;
    if (fixedInput) fixedInput.disabled = !isFixed;
  }

  function addSegmentRow(seg = null) {
    const tbody = $("#segmentsBody");
    if (!tbody) return;

    const row = createSegmentRow(seg);
    tbody.appendChild(row);
  }

  function fillModalWithProfile(profile) {
    $("#profileId").value = profile.profileId ?? "";
    $("#profileNo").value = profile.profileNo ?? "";
    $("#profileName").value = profile.name ?? "";

    const enabledEl = $("#profileEnabled");
    if (enabledEl) enabledEl.checked = profile.enabled !== false;

    const repSegEl = $("#repeatSegments");
    if (repSegEl) repSegEl.checked = profile.repeatSegments !== false;

    const repCntEl = $("#repeatCount");
    if (repCntEl) repCntEl.value = profile.repeatCount ?? 0;

    // 세그먼트
    const segs = Array.isArray(profile.segments) ? profile.segments : [];
    const tbody = $("#segmentsBody");
    tbody.innerHTML = "";

    // nextSegmentId 계산: 기존 segId/seqId 최대값 + 1
    let maxSegId = 0;
    segs.forEach((s) => {
      const sid = Number(s.segId ?? s.seqId ?? 0);
      if (sid > maxSegId) maxSegId = sid;
    });
    nextSegmentId = maxSegId + 1 || 1;

    if (segs.length === 0) {
      addSegmentRow();
    } else {
      segs.forEach((s) => addSegmentRow(s));
    }

    // AutoOff
    const ao = profile.autoOff || {};
    const t = ao.timer || {};
    const ot = ao.offTime || {};
    const tt = ao.offTemp || {};

    const tEnEl = $("#autoOffTimerEnabled");
    const tMinEl = $("#autoOffTimerMinutes");
    const otEnEl = $("#autoOffOffTimeEnabled");
    const otTimeEl = $("#autoOffOffTimeTime");
    const ttEnEl = $("#autoOffOffTempEnabled");
    const ttTempEl = $("#autoOffOffTempTemp");

    if (tEnEl) tEnEl.checked = !!t.enabled;
    if (tMinEl) tMinEl.value = t.minutes ?? 0;
    if (otEnEl) otEnEl.checked = !!ot.enabled;
    if (otTimeEl) otTimeEl.value = ot.time ?? "";
    if (ttEnEl) ttEnEl.checked = !!tt.enabled;
    if (ttTempEl) ttTempEl.value = tt.temp ?? 0;

    // Motion
    const m = profile.motion || {};
    const pir = m.pir || {};
    const ble = m.ble || {};

    const pirEnEl = $("#motionPirEnabled");
    const pirHoldEl = $("#motionPirHold");
    const bleEnEl = $("#motionBleEnabled");
    const bleRssiEl = $("#motionBleRssi");
    const bleHoldEl = $("#motionBleHold");

    if (pirEnEl) pirEnEl.checked = !!pir.enabled;
    if (pirHoldEl) pirHoldEl.value = pir.hold_sec ?? 0;
    if (bleEnEl) bleEnEl.checked = !!ble.enabled;
    if (bleRssiEl) bleRssiEl.value = ble.rssi_threshold ?? -70;
    if (bleHoldEl) bleHoldEl.value = ble.hold_sec ?? 0;
  }

  function readProfileFromModal() {
    const idVal = $("#profileId").value.trim();
    const profileId = idVal ? Number(idVal) : 0;

    const profileNo = Number($("#profileNo").value || 0) || 0;
    const name = $("#profileName").value.trim();

    if (!name) {
      showToast("프로파일 이름은 필수입니다.", "err");
      return null;
    }

    const enabled = $("#profileEnabled")?.checked ?? true;
    const repeatSegments = $("#repeatSegments")?.checked ?? true;
    const repeatCount = Number($("#repeatCount")?.value || 0) || 0;

    // 세그먼트 수집
    const segRows = $$("#segmentsBody .segment-row");
    if (segRows.length === 0) {
      showToast("최소 1개 이상의 세그먼트를 등록해야 합니다.", "err");
      return null;
    }

    const segments = [];
    for (const row of segRows) {
      const sid = Number(row.querySelector(".seg-id")?.value || 0) || 0;
      const sno = Number(row.querySelector(".seg-no")?.value || 0) || 0;
      const onMin = Number(row.querySelector(".seg-on-min")?.value || 0) || 0;
      const offMin = Number(row.querySelector(".seg-off-min")?.value || 0) || 0;
      const mode = row.querySelector(".seg-mode")?.value || "PRESET";
      const presetCode = row.querySelector(".seg-preset-select")?.value || "";
      const styleCode = row.querySelector(".seg-style-select")?.value || "";
      const fixedSpeed = Number(row.querySelector(".seg-fixed-speed")?.value || 0) || 0;

      if (onMin <= 0) {
        showToast("세그먼트 ON 시간은 1분 이상이어야 합니다.", "err");
        return null;
      }

      if (mode === "PRESET" && !presetCode) {
        showToast("PRESET 모드 세그먼트에는 presetCode를 선택해야 합니다.", "err");
        return null;
      }

      const adjPanel = row.querySelector(".seg-adjust-panel");
      const adj = {};
      if (adjPanel) {
        const numVal = (cls) =>
          Number(adjPanel.querySelector(cls)?.value || 0) || 0;

        adj.wind_intensity = numVal(".seg-adj-wi");
        adj.wind_variability = numVal(".seg-adj-wv");
        adj.gust_frequency = numVal(".seg-adj-gf");
        adj.fan_limit = numVal(".seg-adj-fl");
        adj.min_fan = numVal(".seg-adj-mf");
        adj.turbulence_length_scale = numVal(".seg-adj-tls");
        adj.turbulence_intensity_sigma = numVal(".seg-adj-tis");
      }

      const segObj = {
        segId: sid,
        segNo: sno,
        on_minutes: onMin,
        off_minutes: offMin,
        mode,
        presetCode: mode === "PRESET" ? presetCode : "",
        styleCode: mode === "PRESET" ? styleCode : "",
        adjust: adj,
        fixed_speed: mode === "FIXED" ? fixedSpeed : 0
      };
      segments.push(segObj);
    }

    // AutoOff
    const ao = {
      timer: {
        enabled: $("#autoOffTimerEnabled")?.checked ?? false,
        minutes: Number($("#autoOffTimerMinutes")?.value || 0) || 0
      },
      offTime: {
        enabled: $("#autoOffOffTimeEnabled")?.checked ?? false,
        time: $("#autoOffOffTimeTime")?.value || ""
      },
      offTemp: {
        enabled: $("#autoOffOffTempEnabled")?.checked ?? false,
        temp: Number($("#autoOffOffTempTemp")?.value || 0) || 0
      }
    };

    // Motion
    const motion = {
      pir: {
        enabled: $("#motionPirEnabled")?.checked ?? false,
        hold_sec: Number($("#motionPirHold")?.value || 0) || 0
      },
      ble: {
        enabled: $("#motionBleEnabled")?.checked ?? false,
        rssi_threshold: Number($("#motionBleRssi")?.value || -70) || -70,
        hold_sec: Number($("#motionBleHold")?.value || 0) || 0
      }
    };

    const profile = {
      profileId,
      profileNo,
      name,
      enabled,
      repeatSegments,
      repeatCount,
      segments,
      autoOff: ao,
      motion
    };

    return profile;
  }

  // ======================= 8. 저장 / 삭제 =======================

  async function saveProfile(event) {
    event.preventDefault();
    const profile = readProfileFromModal();
    if (!profile) return;

    const isUpdate = !!profile.profileId;

    let result = null;
    if (isUpdate) {
      result = await fetchApi(
        `/api/user_profiles/${profile.profileId}`,
        "PUT",
        { profile },
        `유저 프로파일 ${profile.profileId} 수정`
      );
    } else {
      // 새 ID는 백엔드에서 할당하도록 profileId=0 또는 미포함으로 전송
      delete profile.profileId;
      result = await fetchApi(
        "/api/user_profiles",
        "POST",
        { profile },
        "새 유저 프로파일 생성"
      );
    }

    if (result !== null) {
      setDirtyStatus(true);
      closeModal();
      await loadUserProfiles();
    }
  }

  async function deleteProfile(id, name) {
    const ok = window.confirm(
      `정말로 유저 프로파일 [${name ?? id} (ID: ${id})] 을(를) 삭제하시겠습니까?`
    );
    if (!ok) return;

    const res = await fetchApi(
      `/api/user_profiles/${id}`,
      "DELETE",
      null,
      `유저 프로파일 ${id} 삭제`
    );
    if (res !== null) {
      setDirtyStatus(true);
      await loadUserProfiles();
    }
  }

  // ======================= 9. 이벤트 바인딩 =======================

  function bindEvents() {
    $("#btnCreateNewProfile")?.addEventListener("click", () => openModal(null));
    $("#btnRefreshList")?.addEventListener("click", loadUserProfiles);
    $("#btnSaveAllConfig")?.addEventListener("click", saveAllConfig);

    $("#btnCloseModal")?.addEventListener("click", closeModal);
    $("#btnCancelModal")?.addEventListener("click", closeModal);

    $("#profileForm")?.addEventListener("submit", saveProfile);

    // 리스트 액션(수정/삭제)
    $("#profileListBody")?.addEventListener("click", (ev) => {
      const t = ev.target;
      if (!(t instanceof HTMLElement)) return;
      const id = t.dataset.id;
      if (!id) return;

      const profile = currentProfiles.find(
        (p) => String(p.profileId ?? p.profile_id) === String(id)
      );
      if (!profile) return;

      if (t.classList.contains("btn-edit")) {
        openModal(profile);
      } else if (t.classList.contains("btn-delete")) {
        deleteProfile(id, profile.name);
      }
    });

    // 세그먼트 추가
    $("#btnAddSegment")?.addEventListener("click", () => addSegmentRow());

    // 세그먼트 내부(모드 전환, 조정패널 토글, 삭제) - 이벤트 위임
    $("#segmentsBody")?.addEventListener("click", (ev) => {
      const t = ev.target;
      if (!(t instanceof HTMLElement)) return;

      // 삭제
      if (t.classList.contains("seg-delete")) {
        const row = t.closest(".segment-row");
        if (row && row.parentElement) {
          row.parentElement.removeChild(row);
        }
        return;
      }

      // 조정 패널 토글
      if (t.classList.contains("seg-adjust-toggle")) {
        const row = t.closest(".segment-row");
        if (!row) return;
        const panel = row.querySelector(".seg-adjust-panel");
        if (!panel) return;
        panel.style.display = panel.style.display === "none" ? "block" : "none";
        return;
      }
    });

    // 세그먼트 모드 변경시 preset/fixed 필드 활성화
    $("#segmentsBody")?.addEventListener("change", (ev) => {
      const t = ev.target;
      if (!(t instanceof HTMLSelectElement)) return;
      if (!t.classList.contains("seg-mode")) return;

      const row = t.closest(".segment-row");
      if (!row) return;
      const mode = t.value || "PRESET";
      applyModeVisibility(row, mode);
    });
  }

  // ======================= 10. 초기화 =======================

  document.addEventListener("DOMContentLoaded", async () => {
    bindEvents();
    await loadWindProfileDict();
    await loadUserProfiles();
    checkConfigDirtyState();
  });
})();
