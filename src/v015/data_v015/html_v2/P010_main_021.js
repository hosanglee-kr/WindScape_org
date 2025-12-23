/* P010_main_021.js
 * ------------------------------------------------------
 * 모듈명 : Smart Nature Wind Main UI Logic (v021, Backend 029 기준)
 * ------------------------------------------------------
 * 기능 요약:
 *  - /api/config, /api/state 연동하여 초기 상태/설정 로딩
 *  - 풍속(Motion) 파라미터 메모리 패치
 *  - TIMING 파라미터 메모리 패치
 *  - Wi-Fi AP / STA / 스캔 / PWM 하드웨어 설정 메모리 패치
 *  - 전체 Config 저장 (/api/config/save) 및 Factory Reset (/api/config/init)
 *  - API Key 로컬 저장 및 모든 요청에 X-API-Key 포함
 *  - 파일 업로드 (/upload) 및 OTA 업데이트 (/update)
 *  - WebSocket 로그 (/ws/log) + 상태 (/ws/state) 연동
 * ------------------------------------------------------
 */

/* ==============================
 * 0. 상수 정의
 * ============================== */

// REST API 기본 prefix
const API_BASE = "/api/v1";


// 주요 엔드포인트 정의 (필요 시 여기만 고쳐 쓰면 됨)
const API_STATE          = `${API_BASE}/state`;
const API_CONFIG         = `${API_BASE}/config`;
const API_CONFIG_SAVE    = `${API_BASE}/config/save`;
const API_CONFIG_INIT    = `${API_BASE}/config/init`;     // factoryResetFromDefault와 매핑
const API_CONFIG_MOTION  = `${API_BASE}/config/motion`;   // 풍속/모션 메모리 패치
const API_CONFIG_TIMING  = `${API_BASE}/config/timing`;   // 타이밍 메모리 패치
const API_CONFIG_WIFI_AP = `${API_BASE}/config/wifi_ap`;  // Wi-Fi AP 설정 패치
const API_CONFIG_WIFI_STA= `${API_BASE}/config/wifi_sta`; // Wi-Fi STA 리스트 패치
const API_CONFIG_PWM     = `${API_BASE}/config/hw_pwm`;   // PWM 하드웨어 설정 패치
const API_WIFI_SCAN      = `${API_BASE}/wifi/scan`;       // Wi-Fi 스캔
const API_VERSION        = `${API_BASE}/version`;         // FW 버전 조회 (문자열 or JSON)

// 파일 업로드 / OTA
const API_UPLOAD_STATIC  = "/upload";
const API_UPLOAD_OTA     = "/update";

// WebSocket 엔드포인트 (필요시 "/ws/logs", "/ws/state" 로 수정)
const WS_LOG_URL   = () => {
	const protocol = window.location.protocol === "https:" ? "wss" : "ws";
	return `${protocol}://${window.location.host}/ws/logs`;
};
const WS_STATE_URL = () => {
	const protocol = window.location.protocol === "https:" ? "wss" : "ws";
	return `${protocol}://${window.location.host}/ws/state`;
};

// API Key 저장 키 이름
const API_KEY_STORAGE_KEY = "snw_api_key";

/* ==============================
 * 1. 유틸리티 함수 (API Key, Fetch 래퍼, 로딩/토스트)
 * ============================== */

/** 로컬 스토리지에서 API Key 가져오기 */
function getStoredApiKey() {
	try {
		return localStorage.getItem(API_KEY_STORAGE_KEY) || "";
	} catch (e) {
		console.warn("[Main021] Unable to read API key from localStorage:", e);
		return "";
	}
}

/** 로컬 스토리지에 API Key 저장 */
function storeApiKey(key) {
	try {
		if (key) {
			localStorage.setItem(API_KEY_STORAGE_KEY, key);
		} else {
			localStorage.removeItem(API_KEY_STORAGE_KEY);
		}
	} catch (e) {
		console.warn("[Main021] Unable to store API key:", e);
	}
}

/** 공통 fetch 래퍼 (X-API-Key 헤더 자동 부착) */
async function apiFetch(url, options = {}) {
	const apiKey = getStoredApiKey();
	const headers = new Headers(options.headers || {});
	headers.set("Accept", "application/json");
	headers.set("Content-Type", "application/json");
	if (apiKey) {
		headers.set("X-API-Key", apiKey);
	}

	const finalOptions = {
		...options,
		headers
	};

	const res = await fetch(url, finalOptions);
	if (!res.ok) {
		let msg = `HTTP ${res.status}`;
		try {
			const text = await res.text();
			msg += `: ${text}`;
		} catch (e) {
			// ignore
		}
		throw new Error(msg);
	}
	try {
		// 비어있는 응답일 수도 있으므로
		const text = await res.text();
		if (!text) return null;
		return JSON.parse(text);
	} catch (e) {
		// JSON이 아니면 그대로 무시
		return null;
	}
}

/** 로딩 오버레이 제어 */
function showLoading() {
	const el = document.getElementById("loadingOverlay");
	if (el) el.style.display = "flex";
}
function hideLoading() {
	const el = document.getElementById("loadingOverlay");
	if (el) el.style.display = "none";
}

/** 토스트: 공통 showToast가 있으면 그걸 사용, 없으면 fallback */
function notify(message, type = "info") {
	if (typeof showToast === "function") {
		showToast(message, type);
	} else {
		// fallback
		console.log(`[Toast ${type}]`, message);
	}
}

/* ==============================
 * 2. DOM 요소 참조
 * ============================== */

const elFwVer        = () => document.getElementById("fwVer");
const elSimActive    = () => document.getElementById("simActive");
const elPhase        = () => document.getElementById("phase");
const elWind         = () => document.getElementById("wind");
const elPwm          = () => document.getElementById("pwm");
const elWifiMode     = () => document.getElementById("wifiMode");
const elCurSsid      = () => document.getElementById("curSsid");
const elIp           = () => document.getElementById("ip");

const elPreset       = () => document.getElementById("preset");
const elIntensity    = () => document.getElementById("intensity");
const elGustFreq     = () => document.getElementById("gust_freq");
const elVariability  = () => document.getElementById("variability");
const elFanLimit     = () => document.getElementById("fanLimit");
const elMinFan       = () => document.getElementById("minFan");
const elTurbLen      = () => document.getElementById("turb_len");
const elTurbSig      = () => document.getElementById("turb_sig");
const elThermStr     = () => document.getElementById("therm_str");
const elThermRad     = () => document.getElementById("therm_rad");

const elSimInt       = () => document.getElementById("sim_int");
const elGustInt      = () => document.getElementById("gust_int");
const elThermalInt   = () => document.getElementById("thermal_int");

const elWifiModeSel  = () => document.getElementById("wifi_mode");
const elApSsid       = () => document.getElementById("ap_ssid");
const elApPass       = () => document.getElementById("ap_password");

const elStaList      = () => document.getElementById("staList");
const elScanList     = () => document.getElementById("scanList");
const elScanPass     = () => document.getElementById("scanPass");

const elPwmPin       = () => document.getElementById("pwm_pin");
const elPwmChannel   = () => document.getElementById("pwm_channel");
const elPwmFreq      = () => document.getElementById("pwm_freq");
const elPwmRes       = () => document.getElementById("pwm_res");

const elApiKeyInput  = () => document.getElementById("apiKeyInput");
const elUpload       = () => document.getElementById("fileUpload");
const elUploadMsg    = () => document.getElementById("uploadMsg");
const elOTA          = () => document.getElementById("fileOTA");
const elOtaMsg       = () => document.getElementById("otaMsg");
const elLogConsole   = () => document.getElementById("logConsole");

const elBtnSaveAll   = () => document.getElementById("btnSaveAllConfig");

/* ==============================
 * 3. 전역 상태 (Dirty 플래그, STA 목록 메모리)
 * ============================== */

let g_configDirty = false;
let g_staList = []; // [{ssid, pass}, ...]
let g_wsLog = null;
let g_wsState = null;

/** Dirty 플래그 UI 반영 */
function updateDirtyButton() {
	const btn = elBtnSaveAll();
	if (!btn) return;

	if (g_configDirty) {
		btn.textContent = "변경 있음 - 전체 Config 저장";
		btn.classList.add("warn");
		btn.style.background = "#eab308";
		btn.style.color = "#fff";
	} else {
		btn.textContent = "저장 완료";
		btn.classList.remove("warn");
		btn.style.background = "#2ecc71";
		btn.style.color = "#fff";
	}
}

/** 어떤 입력이라도 변경되면 Dirty 처리 */
function markDirty() {
	g_configDirty = true;
	updateDirtyButton();
}

/* ==============================
 * 4. 초기 데이터 로딩
 * ============================== */

/** FW 버전 로딩 */
async function loadFwVersion() {
	try {
		const data = await apiFetch(API_VERSION, { method: "GET" });
		let versionText = "…";
		if (typeof data === "string") {
			versionText = data;
		} else if (data && (data.version || data.fw || data.fw_version)) {
			versionText = data.version || data.fw || data.fw_version;
		}
		const el = elFwVer();
		if (el) el.textContent = versionText;
	} catch (e) {
		console.warn("[Main021] FW version load failed:", e.message);
	}
}

/** 상태(JSON) 로딩 → 상단 Status UI 갱신  */
async function loadStateOnce() {
	try {
		const data = await apiFetch(API_STATE, { method: "GET" });
		if (!data) return;

		// sim 정보 추정
		const sim = data.sim || data.motion || data.state || {};
		const wifi = (data.wifi && data.wifi.state) ? data.wifi.state : data.wifi || {};

		const simActive = sim.active !== undefined ? sim.active : sim.simActive;
		const phase     = sim.phase !== undefined ? sim.phase : sim.phaseName;
		const wind      = sim.wind !== undefined ? sim.wind : sim.wind_ms;
		const pwm       = sim.pwm !== undefined ? sim.pwm : sim.pwm_val;

		const elA = elSimActive();
		if (elA) {
			if (simActive === true || simActive === 1 || simActive === "on") {
				elA.textContent = "ACTIVE";
				elA.classList.remove("err");
				elA.classList.add("ok");
			} else {
				elA.textContent = "IDLE";
				elA.classList.remove("ok");
				elA.classList.add("err");
			}
		}
		const elP = elPhase();
		if (elP) elP.textContent = phase != null ? String(phase) : "-";

		const elW = elWind();
		if (elW) elW.textContent = wind != null ? String(wind) : "-";

		const elPw = elPwm();
		if (elPw) elPw.textContent = pwm != null ? String(pwm) : "-";

		const elWM = elWifiMode();
		if (elWM)
			elWM.textContent =
				(wifi.mode_name || wifi.mode || "-").toString();

		const elWS = elCurSsid();
		if (elWS) elWS.textContent = wifi.ssid || "-";

		const elIP = elIp();
		if (elIP) elIP.textContent = wifi.ip || "-";

	} catch (e) {
		console.warn("[Main021] loadStateOnce failed:", e.message);
	}
}

/** 전체 Config 로딩 → 각 입력값 채우기 */
async function loadConfig() {
	try {
		showLoading();
		const cfg = await apiFetch(API_CONFIG, { method: "GET" });
		if (!cfg) return;

		// ---- Wi-Fi ----
		if (cfg.wifi) {
			if (elWifiModeSel()) elWifiModeSel().value = cfg.wifi.wifiMode ?? 0;
			if (elApSsid()) elApSsid().value = cfg.wifi.ap ? cfg.wifi.ap.ssid || "" : "";
			if (elApPass()) elApPass().value = cfg.wifi.ap ? cfg.wifi.ap.password || "" : "";

			// STA 리스트
			g_staList = [];
			if (Array.isArray(cfg.wifi.sta)) {
				cfg.wifi.sta.forEach((item) => {
					if (item && item.ssid) {
						g_staList.push({
							ssid: item.ssid,
							pass: item.pass || ""
						});
					}
				});
			}
			renderStaList();
		}

		// ---- PWM HW ----
		if (cfg.hw && cfg.hw.fan_pwm) {
			if (elPwmPin())     elPwmPin().value     = cfg.hw.fan_pwm.pin      ?? "";
			if (elPwmChannel()) elPwmChannel().value = cfg.hw.fan_pwm.channel  ?? "";
			if (elPwmFreq())    elPwmFreq().value    = cfg.hw.fan_pwm.freq     ?? "";
			if (elPwmRes())     elPwmRes().value     = cfg.hw.fan_pwm.res      ?? "";
		}

		// ---- Motion / Wind ----
		//  Config 구조: cfg.motion.current 또는 cfg.motion.active 또는 cfg.control.wind 등
		let motion = null;
		if (cfg.motion && cfg.motion.current) {
			motion = cfg.motion.current;
		} else if (cfg.motion && cfg.motion.active) {
			motion = cfg.motion.active;
		} else if (cfg.control && cfg.control.wind) {
			motion = cfg.control.wind;
		}

		if (motion) {
			if (elIntensity())   elIntensity().value   = motion.intensity   ?? "";
			if (elGustFreq())    elGustFreq().value    = motion.gust_freq   ?? "";
			if (elVariability()) elVariability().value = motion.variability ?? "";
			if (elFanLimit())    elFanLimit().value    = motion.fanLimit   ?? "";
			if (elMinFan())      elMinFan().value      = motion.minFan     ?? "";
			if (elTurbLen())     elTurbLen().value     = motion.turb_len    ?? "";
			if (elTurbSig())     elTurbSig().value     = motion.turb_sig    ?? "";
			if (elThermStr())    elThermStr().value    = motion.therm_str   ?? "";
			if (elThermRad())    elThermRad().value    = motion.therm_rad   ?? "";
		}

		// ---- Timing ----
		if (cfg.timing) {
			if (elSimInt())     elSimInt().value     = cfg.timing.sim_int     ?? "";
			if (elGustInt())    elGustInt().value    = cfg.timing.gust_int    ?? "";
			if (elThermalInt()) elThermalInt().value = cfg.timing.thermal_int ?? "";
		}

		// ---- Preset 목록 (백엔드에서 motion.presets 또는 windProfiles 등) ----
		loadPresetsFromConfig(cfg);

		// ---- Security(API Key) ----
		if (cfg.security && cfg.security.api_key && !getStoredApiKey()) {
			// 펌웨어에 저장된 키가 있고, 브라우저에는 비어있다면 자동 세팅 (선택사항)
			storeApiKey(cfg.security.api_key);
			if (elApiKeyInput()) elApiKeyInput().value = cfg.security.api_key;
		}

		// 초기 로딩 직후에는 Dirty 아님
		g_configDirty = false;
		updateDirtyButton();
	} catch (e) {
		console.error("[Main021] loadConfig failed:", e.message);
		notify("Config 로딩 실패: " + e.message, "err");
	} finally {
		hideLoading();
	}
}

/** Config에서 Preset 목록을 추출해 셀렉트에 채운다 */
function loadPresetsFromConfig(cfg) {
	const sel = elPreset();
	if (!sel) return;
	sel.innerHTML = "";

	let presets = [];

	if (cfg.motion && Array.isArray(cfg.motion.presets)) {
		presets = cfg.motion.presets;
	} else if (Array.isArray(cfg.windProfiles)) {
		presets = cfg.windProfiles;
	}

	if (!Array.isArray(presets) || presets.length === 0) {
		const opt = document.createElement("option");
		opt.value = "";
		opt.textContent = "(프리셋 없음)";
		sel.appendChild(opt);
		return;
	}

	presets.forEach((p, idx) => {
		const opt = document.createElement("option");
		// {id, name, label} 형식을 가정
		opt.value = p.id != null ? p.id : p.name || String(idx);
		opt.textContent = p.label || p.name || `Preset ${idx + 1}`;
		sel.appendChild(opt);
	});
}

/* ==============================
 * 5. STA 리스트 / 스캔 렌더링
 * ============================== */

/** STA 목록 렌더링 (g_staList 기준) */
function renderStaList() {
	const container = elStaList();
	if (!container) return;

	container.innerHTML = "";

	if (!g_staList || g_staList.length === 0) {
		const div = document.createElement("div");
		div.className = "muted";
		div.textContent = "등록된 STA 네트워크가 없습니다.";
		container.appendChild(div);
		return;
	}

	const table = document.createElement("table");
	const thead = document.createElement("thead");
	const trh = document.createElement("tr");
	["SSID", "Password", "액션"].forEach((txt) => {
		const th = document.createElement("th");
		th.textContent = txt;
		trh.appendChild(th);
	});
	thead.appendChild(trh);
	table.appendChild(thead);

	const tbody = document.createElement("tbody");

	g_staList.forEach((item, idx) => {
		const tr = document.createElement("tr");

		const tdSsid = document.createElement("td");
		tdSsid.textContent = item.ssid || "";
		tr.appendChild(tdSsid);

		const tdPass = document.createElement("td");
		tdPass.textContent = item.pass ? "********" : "";
		tr.appendChild(tdPass);

		const tdAct = document.createElement("td");
		tdAct.style.textAlign = "right";

		const btnDel = document.createElement("button");
		btnDel.className = "btn btn-small err";
		btnDel.textContent = "삭제";
		btnDel.addEventListener("click", () => {
			g_staList.splice(idx, 1);
			renderStaList();
			markDirty();
		});

		tdAct.appendChild(btnDel);
		tr.appendChild(tdAct);

		tbody.appendChild(tr);
	});

	table.appendChild(tbody);
	container.appendChild(table);
}

/** Wi-Fi 스캔 결과를 셀렉트에 채우기 */
function renderScanList(networks) {
	const sel = elScanList();
	if (!sel) return;

	sel.innerHTML = "";

	if (!networks || networks.length === 0) {
		const opt = document.createElement("option");
		opt.value = "";
		opt.textContent = "검색된 네트워크가 없습니다.";
		sel.appendChild(opt);
		return;
	}

	networks.forEach((ap) => {
		const opt = document.createElement("option");
		opt.value = ap.ssid || "";
		const rssi = ap.rssi != null ? ` (RSSI ${ap.rssi})` : "";
		opt.textContent = (ap.ssid || "") + rssi;
		sel.appendChild(opt);
	});
}

/* ==============================
 * 6. 섹션별 메모리 패치 (PATCH)
 * ============================== */

/** 풍속/모션 메모리 패치 */
async function saveMotionPatch() {
	try {
		showLoading();
		const body = {
			motion: {
				intensity:   Number(elIntensity().value || 0),
				gust_freq:   Number(elGustFreq().value || 0),
				variability: Number(elVariability().value || 0),
				fanLimit:   Number(elFanLimit().value || 0),
				minFan:     Number(elMinFan().value || 0),
				turb_len:    Number(elTurbLen().value || 0),
				turb_sig:    Number(elTurbSig().value || 0),
				therm_str:   Number(elThermStr().value || 0),
				therm_rad:   Number(elThermRad().value || 0),
				preset_id:   elPreset().value || null
			}
		};

		await apiFetch(API_CONFIG_MOTION, {
			method: "PATCH",
			body: JSON.stringify(body)
		});

		notify("풍속 설정 메모리 패치 완료", "ok");
		markDirty(); // Config와 동기화가 필요하므로 Dirty 처리
	} catch (e) {
		console.error("[Main021] saveMotionPatch failed:", e.message);
		notify("풍속 설정 실패: " + e.message, "err");
	} finally {
		hideLoading();
	}
}

/** 타이밍 메모리 패치 */
async function saveTimingPatch() {
	try {
		showLoading();
		const body = {
			timing: {
				sim_int:     Number(elSimInt().value || 0),
				gust_int:    Number(elGustInt().value || 0),
				thermal_int: Number(elThermalInt().value || 0)
			}
		};

		await apiFetch(API_CONFIG_TIMING, {
			method: "PATCH",
			body: JSON.stringify(body)
		});

		notify("타이밍 설정 메모리 패치 완료", "ok");
		markDirty();
	} catch (e) {
		console.error("[Main021] saveTimingPatch failed:", e.message);
		notify("타이밍 설정 실패: " + e.message, "err");
	} finally {
		hideLoading();
	}
}

/** Wi-Fi AP 설정 메모리 패치 */
async function saveWifiApPatch() {
	try {
		showLoading();
		const body = {
			wifi: {
				wifiMode: Number(elWifiModeSel().value || 0),
				ap: {
					ssid:     elApSsid().value || "",
					password: elApPass().value || ""
				}
			}
		};

		await apiFetch(API_CONFIG_WIFI_AP, {
			method: "PATCH",
			body: JSON.stringify(body)
		});

		notify("Wi-Fi AP 설정 메모리 패치 완료", "ok");
		markDirty();
	} catch (e) {
		console.error("[Main021] saveWifiApPatch failed:", e.message);
		notify("Wi-Fi AP 설정 실패: " + e.message, "err");
	} finally {
		hideLoading();
	}
}

/** Wi-Fi STA 목록 메모리 패치 */
async function saveWifiStaPatch() {
	try {
		showLoading();
		const body = {
			wifi: {
				sta: g_staList.map((item) => ({
					ssid: item.ssid,
					pass: item.pass || ""
				}))
			}
		};

		await apiFetch(API_CONFIG_WIFI_STA, {
			method: "PATCH",
			body: JSON.stringify(body)
		});

		notify("Wi-Fi STA 목록 메모리 패치 완료", "ok");
		markDirty();
	} catch (e) {
		console.error("[Main021] saveWifiStaPatch failed:", e.message);
		notify("Wi-Fi STA 설정 실패: " + e.message, "err");
	} finally {
		hideLoading();
	}
}

/** PWM 하드웨어 설정 메모리 패치 */
async function savePwmPatch() {
	try {
		showLoading();
		const body = {
			hw: {
				fan_pwm: {
					pin:     Number(elPwmPin().value || 0),
					channel: Number(elPwmChannel().value || 0),
					freq:    Number(elPwmFreq().value || 0),
					res:     Number(elPwmRes().value || 0)
				}
			}
		};

		await apiFetch(API_CONFIG_PWM, {
			method: "PATCH",
			body: JSON.stringify(body)
		});

		notify("PWM 하드웨어 설정 메모리 패치 완료", "ok");
		markDirty();
	} catch (e) {
		console.error("[Main021] savePwmPatch failed:", e.message);
		notify("PWM 설정 실패: " + e.message, "err");
	} finally {
		hideLoading();
	}
}

/* ==============================
 * 7. Config 전체 저장 + Factory Reset
 * ============================== */

/** 전체 Config 저장 (Flash flush) */
async function saveAllConfig() {
	if (!g_configDirty) {
		notify("변경 사항이 없습니다.", "info");
		return;
	}

	if (!confirm("현재까지의 메모리 변경 내용을 모두 저장하시겠습니까?")) {
		return;
	}

	try {
		showLoading();
		await apiFetch(API_CONFIG_SAVE, {
			method: "POST",
			body: JSON.stringify({ save_all: true })
		});

		notify("전체 Config 저장 완료", "ok");
		g_configDirty = false;
		updateDirtyButton();
	} catch (e) {
		console.error("[Main021] saveAllConfig failed:", e.message);
		notify("Config 저장 실패: " + e.message, "err");
	} finally {
		hideLoading();
	}
}

/** Factory Reset (factoryResetFromDefault) */
async function factoryReset() {
	if (!confirm("⚠️ 모든 설정을 기본값으로 초기화합니다.\n진행하시겠습니까?")) {
		return;
	}

	try {
		showLoading();
		await apiFetch(API_CONFIG_INIT, {
			method: "POST",
			body: JSON.stringify({ factory: true })
		});

		notify("시스템 전체 설정이 기본값으로 초기화되었습니다. (재시작 필요 시 자동 적용)", "warn");
		// 초기화 후 다시 로딩
		await loadConfig();
		await loadStateOnce();
	} catch (e) {
		console.error("[Main021] factoryReset failed:", e.message);
		notify("시스템 초기화 실패: " + e.message, "err");
	} finally {
		hideLoading();
	}
}

/* ==============================
 * 8. Wi-Fi 스캔 및 STA 추가
 * ============================== */

/** Wi-Fi 스캔 */
async function scanWifi() {
	try {
		showLoading();
		const data = await apiFetch(API_WIFI_SCAN, {
			method: "POST",
			body: JSON.stringify({ scan: true })
		});

		const list =
			(data && data.wifi && data.wifi.scan) ? data.wifi.scan : data || [];
		renderScanList(list);
		notify("Wi-Fi 스캔 완료", "ok");
	} catch (e) {
		console.error("[Main021] scanWifi failed:", e.message);
		notify("Wi-Fi 스캔 실패: " + e.message, "err");
	} finally {
		hideLoading();
	}
}

/** 스캔 결과에서 선택한 AP를 STA 목록에 추가 */
function addStaFromScan() {
	const sel = elScanList();
	const passInput = elScanPass();
	if (!sel) return;

	const ssid = sel.value || "";
	if (!ssid) {
		notify("추가할 SSID를 선택하세요.", "warn");
		return;
	}
	const pass = passInput ? passInput.value : "";

	// 중복 체크
	if (g_staList.some((s) => s.ssid === ssid)) {
		notify("이미 등록된 SSID입니다.", "warn");
		return;
	}

	g_staList.push({ ssid, pass });
	renderStaList();
	markDirty();

	if (passInput) passInput.value = "";
}

/* ==============================
 * 9. API Key 저장
 * ============================== */

function applyApiKeyFromInput() {
	const input = elApiKeyInput();
	if (!input) return;
	const key = input.value.trim();
	storeApiKey(key);
	notify("API Key가 브라우저에 저장되었습니다.", "ok");
}

/* ==============================
 * 10. 파일 업로드 / OTA
 * ============================== */

/** 공통 업로드 함수 */
async function uploadFile(endpoint, file, msgEl, successMsg, errorMsg) {
	if (!file) {
		notify("파일을 선택하세요.", "warn");
		return;
	}

	const apiKey = getStoredApiKey();
	const formData = new FormData();
	formData.append("file", file, file.name);

	try {
		showLoading();
		const res = await fetch(endpoint, {
			method: "POST",
			headers: apiKey ? { "X-API-Key": apiKey } : {},
			body: formData
		});

		const text = await res.text();
		if (!res.ok) {
			throw new Error(`HTTP ${res.status} / ${text}`);
		}

		if (msgEl) msgEl.textContent = text || successMsg;
		notify(successMsg, "ok");
	} catch (e) {
		console.error("[Main021] uploadFile failed:", e.message);
		if (msgEl) msgEl.textContent = e.message;
		notify(errorMsg + ": " + e.message, "err");
	} finally {
		hideLoading();
	}
}

/** 정적 파일 업로드 */
function handleStaticUpload() {
	const f = elUpload() ? elUpload().files[0] : null;
	uploadFile(API_UPLOAD_STATIC, f, elUploadMsg(), "정적 파일 업로드 완료", "정적 파일 업로드 실패");
}

/** 펌웨어 OTA 업로드 */
function handleOtaUpload() {
	const f = elOTA() ? elOTA().files[0] : null;
	uploadFile(API_UPLOAD_OTA, f, elOtaMsg(), "OTA 업데이트 전송 완료 (장치 재시작 대기)", "OTA 업데이트 실패");
}

/* ==============================
 * 11. WebSocket 로그 / 상태
 * ============================== */

function initWebSocketLog() {
	try {
		const url = WS_LOG_URL();
		const apiKey = getStoredApiKey();
		const fullUrl = apiKey ? `${url}?apiKey=${encodeURIComponent(apiKey)}` : url;
		const ws = new WebSocket(fullUrl);
		g_wsLog = ws;

		ws.onopen = () => {
			console.log("[WS-LOG] connected");
			const el = elLogConsole();
			if (el) el.textContent = "WebSocket 로그 연결됨.\n";
		};

		ws.onmessage = (ev) => {
			const el = elLogConsole();
			if (!el) return;
			el.textContent += ev.data + "\n";
			el.scrollTop = el.scrollHeight;
		};

		ws.onclose = () => {
			console.log("[WS-LOG] disconnected");
		};

		ws.onerror = (err) => {
			console.error("[WS-LOG] error:", err);
		};
	} catch (e) {
		console.error("[WS-LOG] init failed:", e.message);
	}
}

function clearLogConsole() {
	const el = elLogConsole();
	if (el) el.textContent = "";
}

/** WebSocket 상태 (/ws/state) → 실시간 상태 업데이트 */
function initWebSocketState() {
	try {
		const url = WS_STATE_URL();
		const apiKey = getStoredApiKey();
		const fullUrl = apiKey ? `${url}?apiKey=${encodeURIComponent(apiKey)}` : url;
		const ws = new WebSocket(fullUrl);
		g_wsState = ws;

		ws.onopen = () => {
			console.log("[WS-STATE] connected");
		};

		ws.onmessage = (ev) => {
			try {
				const data = JSON.parse(ev.data);
				// 상태 JSON이 오면 loadStateOnce와 동일한 방식으로 반영
				const fake = { ...data };
				// API와 형식을 대충 맞추기 위해 래퍼
				handleStateUpdateFromWs(fake);
			} catch (e) {
				console.warn("[WS-STATE] invalid JSON:", ev.data);
			}
		};

		ws.onclose = () => {
			console.log("[WS-STATE] disconnected");
		};

		ws.onerror = (err) => {
			console.error("[WS-STATE] error:", err);
		};
	} catch (e) {
		console.error("[WS-STATE] init failed:", e.message);
	}
}

/** WS에서 받은 상태 JSON을 UI에 반영 (loadStateOnce 로직과 동일/유사) */
function handleStateUpdateFromWs(data) {
	if (!data) return;

	const sim = data.sim || data.motion || data.state || {};
	const wifi = (data.wifi && data.wifi.state) ? data.wifi.state : data.wifi || {};

	const simActive = sim.active !== undefined ? sim.active : sim.simActive;
	const phase     = sim.phase !== undefined ? sim.phase : sim.phaseName;
	const wind      = sim.wind !== undefined ? sim.wind : sim.wind_ms;
	const pwm       = sim.pwm !== undefined ? sim.pwm : sim.pwm_val;

	const elA = elSimActive();
	if (elA) {
		if (simActive === true || simActive === 1 || simActive === "on") {
			elA.textContent = "ACTIVE";
			elA.classList.remove("err");
			elA.classList.add("ok");
		} else {
			elA.textContent = "IDLE";
			elA.classList.remove("ok");
			elA.classList.add("err");
		}
	}
	const elP = elPhase();
	if (elP) elP.textContent = phase != null ? String(phase) : "-";

	const elW = elWind();
	if (elW) elW.textContent = wind != null ? String(wind) : "-";

	const elPw = elPwm();
	if (elPw) elPw.textContent = pwm != null ? String(pwm) : "-";

	const elWM = elWifiMode();
	if (elWM)
		elWM.textContent = (wifi.mode_name || wifi.mode || "-").toString();

	const elWS = elCurSsid();
	if (elWS) elWS.textContent = wifi.ssid || "-";

	const elIP = elIp();
	if (elIP) elIP.textContent = wifi.ip || "-";
}

/* ==============================
 * 12. 이벤트 바인딩
 * ============================== */

function bindEvents() {
	// 상단 상태 새로고침
	const btnRefresh = document.getElementById("btnRefresh");
	if (btnRefresh) {
		btnRefresh.addEventListener("click", () => {
			loadStateOnce();
		});
	}

	// 풍속 설정
	const btnSaveSim = document.getElementById("btnSaveSim");
	if (btnSaveSim) {
		btnSaveSim.addEventListener("click", saveMotionPatch);
	}

	// 전체 Config 저장
	if (elBtnSaveAll()) {
		elBtnSaveAll().addEventListener("click", saveAllConfig);
	}

	// Factory Reset
	const btnConfigInit = document.getElementById("btnConfigInit");
	if (btnConfigInit) {
		btnConfigInit.addEventListener("click", factoryReset);
	}

	// 타이밍
	const btnSaveTiming = document.getElementById("btnSaveTiming");
	if (btnSaveTiming) {
		btnSaveTiming.addEventListener("click", saveTimingPatch);
	}

	// Wi-Fi AP
	const btnSaveWifiAP = document.getElementById("btnSaveWifiAP");
	if (btnSaveWifiAP) {
		btnSaveWifiAP.addEventListener("click", saveWifiApPatch);
	}

	// Wi-Fi STA
	const btnSaveWifiSTA = document.getElementById("btnSaveWifiSTA");
	if (btnSaveWifiSTA) {
		btnSaveWifiSTA.addEventListener("click", saveWifiStaPatch);
	}

	// Wi-Fi 스캔
	const btnScan = document.getElementById("btnScan");
	if (btnScan) {
		btnScan.addEventListener("click", scanWifi);
	}

	// 스캔 결과에서 STA 추가
	const btnUseScan = document.getElementById("btnUseScan");
	if (btnUseScan) {
		btnUseScan.addEventListener("click", addStaFromScan);
	}

	// PWM 저장
	const btnSavePWM = document.getElementById("btnSavePWM");
	if (btnSavePWM) {
		btnSavePWM.addEventListener("click", savePwmPatch);
	}

	// API Key 저장
	const btnSaveApiKey = document.getElementById("btnSaveApiKey");
	if (btnSaveApiKey) {
		btnSaveApiKey.addEventListener("click", applyApiKeyFromInput);
	}

	// 정적 파일 업로드
	const btnUploadStatic = document.getElementById("btnUploadStatic");
	if (btnUploadStatic) {
		btnUploadStatic.addEventListener("click", handleStaticUpload);
	}

	// OTA 업로드
	const btnUploadOTA = document.getElementById("btnUploadOTA");
	if (btnUploadOTA) {
		btnUploadOTA.addEventListener("click", handleOtaUpload);
	}

	// 로그 지우기
	const btnClearLog = document.getElementById("btnClearLog");
	if (btnClearLog) {
		btnClearLog.addEventListener("click", clearLogConsole);
	}

	// 입력 변경 시 Dirty 표시
	const inputSelectors = [
		"#intensity",
		"#gust_freq",
		"#variability",
		"#fanLimit",
		"#minFan",
		"#turb_len",
		"#turb_sig",
		"#therm_str",
		"#therm_rad",
		"#sim_int",
		"#gust_int",
		"#thermal_int",
		"#wifi_mode",
		"#ap_ssid",
		"#ap_password",
		"#pwm_pin",
		"#pwm_channel",
		"#pwm_freq",
		"#pwm_res"
	];

	inputSelectors.forEach((sel) => {
		const el = document.querySelector(sel);
		if (el) {
			el.addEventListener("change", markDirty);
			el.addEventListener("input", markDirty);
		}
	});
}

/* ==============================
 * 13. 초기화
 * ============================== */

document.addEventListener("DOMContentLoaded", async () => {
	// API Key 인풋 초기화
	const key = getStoredApiKey();
	if (elApiKeyInput()) elApiKeyInput().value = key;

	updateDirtyButton();
	bindEvents();
	initWebSocketLog();
	initWebSocketState();

	await loadFwVersion();
	await loadConfig();
	await loadStateOnce();
});

