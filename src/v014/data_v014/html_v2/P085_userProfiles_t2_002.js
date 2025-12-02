/*
 * ------------------------------------------------------
 * 소스명 : P085_userProfiles_t2_002.js
 * 모듈명 : Smart Nature Wind User Profile Manager Controller (v002)
 * ------------------------------------------------------
 * 기능 요약:
 * - 🎯 /api/user_profiles (GET, POST, PUT, DELETE) CRUD (v029 스펙)
 * - 🎯 /api/windProfile (GET) → basePreset 선택 옵션 동적 로드
 * - 목록 테이블 렌더링 및 모달을 통한 생성/수정 관리
 * - 🎯 /api/config/dirty · /api/config/save 와 연동 (userProfiles dirty 상태)
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

  // P000_common_005.js 쪽에 showToast가 있으면 우선 사용
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

  // ======================= 2. 상태 변수 =======================

  let currentProfiles = [];   // [{ id, name, basePreset, minFan, maxFan, ... }]
  let windProfiles = [];      // /api/windProfile 결과
  let configDirty = false;    // userProfiles dirty 상태

  // preset code → 한글명 매핑 (있으면 사용, 없으면 name/code 그대로)
  const presetNameMap = {
    OFF: "고정풍",
    COUNTRY: "들판",
    MEDITERRANEAN: "지중해",
    OCEAN: "바다",
    MOUNTAIN: "산바람",
    PLAINS: "평야",
    FOREST_CANOPY: "숲속",
    HARBOR_BREEZE: "항구바람",
    URBAN_SUNSET: "도심석양",
    TROPICAL_RAIN: "열대우림",
    DESERT_NIGHT: "사막밤"
  };

  const displayPreset = (profile) => {
    if (!profile) return "-";
    const code = profile.basePreset || profile.base_preset || profile.code;
    if (!code) return profile.name || "-";

    // windProfile에서 찾아보기
    const wp =
      windProfiles.find((w) => w.code === code) ||
      windProfiles.find((w) => w.id === profile.basePresetId);

    if (wp) {
      const label = wp.name || presetNameMap[wp.code] || wp.code;
      return `${label} (${wp.code})`;
    }
    const label = presetNameMap[code] || code;
    return label;
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
      const res = await fetch("/api/config/dirty", {
        headers: { "X-API-Key": getKey() }
      });
      if (!res.ok) throw new Error(res.statusText);
      const j = await res.json();
      setDirtyStatus(!!j.userProfiles);
    } catch (e) {
      console.warn("[UserProfiles] Dirty 상태 조회 실패:", e.message);
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
      await loadUserProfiles();
    }
  }

  // ======================= 4. 데이터 로드 =======================

  async function loadWindProfiles() {
    const data = await fetchApi("/api/windProfile", "GET", null, "Wind Profile 목록 로드");
    if (data && Array.isArray(data.windProfiles)) {
      windProfiles = data.windProfiles;
      const selectEl = $("#basePreset");
      if (selectEl) {
        selectEl.innerHTML = '<option value="">-- 기본 프리셋 선택 --</option>';
        windProfiles.forEach((p) => {
          const option = document.createElement("option");
          option.value = p.code; // user_profile 에서는 basePreset 코드 문자열 사용
          const label = p.name || presetNameMap[p.code] || p.code;
          option.textContent = `${label} (${p.code})`;
          selectEl.appendChild(option);
        });
      }
    } else {
      windProfiles = [];
    }
  }

  async function loadUserProfiles() {
    const data = await fetchApi("/api/user_profiles", "GET", null, "유저 프로파일 목록 불러오기");
    const noMsg = $("#noProfileMessage");
    if (!$("#profileListBody")) return;

    if (Array.isArray(data)) {
      currentProfiles = data;
      renderProfileList(currentProfiles);
      if (noMsg) noMsg.style.display = currentProfiles.length === 0 ? "block" : "none";
    } else {
      currentProfiles = [];
      renderProfileList([]);
      if (noMsg) noMsg.style.display = "block";
    }
  }

  function renderProfileList(profiles) {
    const tbody = $("#profileListBody");
    if (!tbody) return;
    tbody.innerHTML = "";

    profiles.forEach((p) => {
      const tr = document.createElement("tr");
      tr.dataset.profileId = p.id;

      const basePresetText = displayPreset(p);

      const rowHtml = `
        <td>${p.id}</td>
        <td><strong>${p.name}</strong></td>
        <td>${basePresetText}</td>
        <td>${p.minFan != null ? p.minFan : "-"}</td>
        <td>${p.maxFan != null ? p.maxFan : "-"}</td>
        <td>
          <div class="action-buttons">
            <button class="btn btn-small btn-edit" data-id="${p.id}">수정</button>
            <button class="btn btn-small btn-err btn-delete" data-id="${p.id}">삭제</button>
          </div>
        </td>
      `;
      tr.innerHTML = rowHtml;
      tbody.appendChild(tr);
    });
  }

  // ======================= 5. 모달 처리 =======================

  function openModal(profile = null) {
    const modal = $("#profileModal");
    const form = $("#profileForm");
    if (!modal || !form) return;

    form.reset();

    if (profile) {
      $("#modalTitle").textContent = `유저 프로파일 수정: ${profile.name}`;
      $("#profileId").value = profile.id;
      $("#profileName").value = profile.name || "";

      const basePresetCode = profile.basePreset || profile.base_preset || "";
      $("#basePreset").value = basePresetCode;

      $("#minFan").value =
        profile.minFan != null
          ? profile.minFan
          : profile.min_fan != null
          ? profile.min_fan
          : 0;
      $("#maxFan").value =
        profile.maxFan != null
          ? profile.maxFan
          : profile.max_fan != null
          ? profile.max_fan
          : 100;
    } else {
      $("#modalTitle").textContent = "새 유저 프로파일 생성";
      $("#profileId").value = "";
      $("#minFan").value = 0;
      $("#maxFan").value = 100;
    }

    modal.style.display = "flex";
  }

  function closeModal() {
    const modal = $("#profileModal");
    if (modal) modal.style.display = "none";
  }

  async function saveProfile(event) {
    event.preventDefault();

    const id = $("#profileId").value;
    const isUpdate = !!id;

    const name = $("#profileName").value.trim();
    const basePreset = $("#basePreset").value;
    const minFan = Number($("#minFan").value);
    const maxFan = Number($("#maxFan").value);

    if (!name || !basePreset) {
      showToast("이름과 기본 프리셋은 필수 항목입니다.", "err");
      return;
    }

    if (isNaN(minFan) || isNaN(maxFan)) {
      showToast("팬 속도는 숫자로 입력해 주세요.", "err");
      return;
    }
    if (minFan < 0 || maxFan > 100 || minFan > maxFan) {
      showToast("팬 최소/최대 값 범위(0~100) 및 관계(min <= max)를 확인해 주세요.", "err");
      return;
    }

    const body = {
      name,
      basePreset,
      minFan,
      maxFan
    };

    let result;
    if (isUpdate) {
      result = await fetchApi(`/api/user_profiles/${id}`, "PUT", body, `유저 프로파일 ${id} 수정`);
    } else {
      result = await fetchApi("/api/user_profiles", "POST", body, "새 유저 프로파일 생성");
    }

    if (result) {
      setDirtyStatus(true);
      closeModal();
      await loadUserProfiles();
    }
  }

  async function deleteProfile(id, name) {
    const result = await fetchApi(
      `/api/user_profiles/${id}`,
      "DELETE",
      null,
      `유저 프로파일 ${name} 삭제`
    );
    if (result) {
      setDirtyStatus(true);
      await loadUserProfiles();
    }
  }

  async function handleProfileActions(event) {
    const target = event.target;
    const id = target.dataset.id;
    if (!id) return;

    const profile = currentProfiles.find((p) => String(p.id) === id);
    if (!profile) return;

    if (target.classList.contains("btn-edit")) {
      openModal(profile);
    } else if (target.classList.contains("btn-delete")) {
      if (confirm(`정말로 유저 프로파일 [${profile.name} (ID: ${id})] 을(를) 삭제하시겠습니까?`)) {
        await deleteProfile(id, profile.name);
      }
    }
  }

  // ======================= 6. 이벤트 바인딩 & 초기화 =======================

  function bindEvents() {
    $("#btnCreateNewProfile")?.addEventListener("click", () => openModal(null));
    $("#btnRefreshList")?.addEventListener("click", loadUserProfiles);
    $("#btnSaveAllConfig")?.addEventListener("click", saveAllConfig);

    $("#btnCloseModal")?.addEventListener("click", closeModal);
    $("#btnCancelModal")?.addEventListener("click", closeModal);

    $("#profileForm")?.addEventListener("submit", saveProfile);

    $("#profileListBody")?.addEventListener("click", handleProfileActions);
  }

  document.addEventListener("DOMContentLoaded", async () => {
    bindEvents();
    await loadWindProfiles();
    await loadUserProfiles();
    checkConfigDirtyState();
  });
})();

