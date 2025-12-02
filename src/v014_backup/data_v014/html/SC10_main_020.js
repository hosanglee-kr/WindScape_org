/*
 * ------------------------------------------------------
 * 소스명 : SC10_main_020.js
 * 모듈명 : Smart Nature Wind Web UI Controller (v020)
 * ------------------------------------------------------
 * 기능 요약:
 * - 설정 그룹별 저장 (Wi-Fi / HW / Timing / Sim / API Key) (메모리 패치 역할로 변경)
 * - /api/control/summary 상태 반영
 * - ✅ /api/config/save 명시적 저장 기능 및 Dirty 상태 체크 (v025 핵심)
 * - ✅ /ws/state 및 /ws/log WebSocket을 통한 실시간 상태 및 로그 반영 (v025 핵심)
 * - 정적 파일 & 펌웨어 업로드 분리
 * ------------------------------------------------------
 */

(() => {
	"use strict";

	// DOM Helper
	const $ = (s, r = document) => r.querySelector(s);
	const text = (el, v) => el && (el.textContent = v);

	// API Key (localStorage)
	const KEY_API = 'sc10_api_key';
	const getKey = () => localStorage.getItem(KEY_API) || '';
	const setKey = (k) => localStorage.setItem(KEY_API, k);

	// 상태 캐시 및 플래그
	let g_config = {};
	let g_presets = [];
	let configDirty = false; // ✅ 설정 변경 상태 플래그

	// WebSocket 경로
	const getWSHost = (endpoint) => `ws://${window.location.host}/ws/${endpoint}`;

	// 프리셋 한글 매핑
	const presetNameMap = {
		OFF				: "고정풍속",
		COUNTRY      	: "들판",
		MEDITERRANEAN	: "지중해",
		OCEAN        	: "바다",
		MOUNTAIN     	: "산바람",
		PLAINS       	: "평야",
		FOREST_CANOPY	: "숲속",
		HARBOR_BREEZE	: "항구바람",
		URBAN_SUNSET 	: "도심석양",
		TROPICAL_RAIN	: "열대우림",
		DESERT_NIGHT 	: "사막밤"
	};
	const displayPresetName = (n) => presetNameMap[n] || n;

	// 초기화
	document.addEventListener("DOMContentLoaded", () => {
		const k = getKey();
		if ($("#apiKeyInput")) $("#apiKeyInput").value = k;
		bindEvents();
		refreshVersion();
		refreshState(true); // 초기 설정 로드
		initWebSockets(); // ✅ WebSocket 연결 시작
		checkConfigDirtyState(); // ✅ Dirty 상태 주기적 체크 시작
	});

	// ======================= 유틸 UI & Dirty 상태 관리 =======================
	function showToast(msg, type = "ok") {
		const cont = $("#toastContainer");
		if (!cont) { console.log(`[${type.toUpperCase()}] ${msg}`); return; }
		const div = document.createElement("div");
		div.className = `toast ${type}`;
		div.textContent = msg;
		cont.appendChild(div);
		setTimeout(() => div.remove(), 3000);
	}
	function setLoading(flag) {
		const ov = $("#loadingOverlay");
		if (ov) ov.style.display = flag ? "flex" : "none";
	}

	// ✅ 설정 Dirty 상태 UI 업데이트 함수
	const setDirtyStatus = (isDirty) => {
		configDirty = isDirty;
		const btnSaveAll = $("#btnSaveAllConfig"); 
		if (btnSaveAll) {
			if (isDirty) {
				btnSaveAll.style.backgroundColor = "#dc2626"; // 빨간색
				btnSaveAll.textContent = "⚠️ 전체 설정 저장 (미저장)";
			} else {
				btnSaveAll.style.backgroundColor = "#2196f3"; // 파란색
				btnSaveAll.textContent = "✅ 전체 설정 저장 (저장 완료)";
			}
		}
	};
	
	// ✅ Dirty 상태 체크 함수 추가
	async function checkConfigDirtyState() {
		try {
			const r = await fetch("/api/config/dirty", { headers: { "X-API-Key": getKey() } });
			const j = await r.json();
			setDirtyStatus(j.dirty || false); 
		} catch (e) {
			// API 호출 오류 시 경고 표시
			// showToast("Dirty 상태 체크 실패", "warn");
		}
		// 5초마다 체크
		setTimeout(checkConfigDirtyState, 5000); 
	}

	// ======================= 공통 API =======================
	async function fetchApi(url, method = "GET", body = null, desc = "작업") {
		setLoading(true);
		try {
			const opt = { method, headers: {} };
			if (body) {
				opt.body = JSON.stringify(body);
				opt.headers["Content-Type"] = "application/json";
			}
			const k = getKey();
			if (k) opt.headers["X-API-Key"] = k;

			const resp = await fetch(url, opt);
			const txt = await resp.text();

			if (resp.status === 401) {
				showToast(`[401] ${desc} 실패: 인증 필요`, "err");
				throw new Error("Unauthorized");
			}
			if (!resp.ok) {
				showToast(`${desc} 실패: ${txt || resp.status}`, "err");
				throw new Error(txt || resp.status);
			}
			showToast(`${desc} 성공`, "ok");
			
			// ✅ 파일 저장 성공 시 Dirty 상태 초기화
			if (url === "/api/config/save") setDirtyStatus(false);
			
			return txt;
		} catch (e) {
			if (e.message !== "Unauthorized") showToast(`${desc} 실패: ${e.message}`, "err");
		} finally {
			setLoading(false);
		}
	}

	// ======================= 이벤트 바인딩 =======================
	function bindEvents() {
		$("#btnRefresh")?.addEventListener("click", () => refreshState(true));
		$("#btnPreviewPreset")?.addEventListener("click", previewPreset);

		// ✅ 개별 저장 버튼은 메모리 패치 후 Dirty 상태만 업데이트
		$("#btnSaveSim")?.addEventListener("click", saveSim);
		$("#btnSaveTiming")?.addEventListener("click", saveTiming);
		$("#btnSaveWifiAP")?.addEventListener("click", saveWifiAP);
		$("#btnSaveWifiSTA")?.addEventListener("click", saveWifiSTA);
		$("#btnSavePWM")?.addEventListener("click", savePWMConfig);
		$("#btnSaveApiKey")?.addEventListener("click", saveApiKey);
		
		// ✅ 전체 저장 버튼 (파일 저장)
		$("#btnSaveAllConfig")?.addEventListener("click", saveAllConfig); 
		$("#btnConfigInit")?.addEventListener("click", saveConfigInit); 

		$("#btnScan")?.addEventListener("click", scanNetworks);
		$("#btnUseScan")?.addEventListener("click", useScanResult);

		$("#btnUploadStatic")?.addEventListener("click", uploadStatic);
		$("#btnUploadOTA")?.addEventListener("click", uploadOTA);
		
		// ✅ 로그 콘솔 이벤트
		$("#btnClearLog")?.addEventListener("click", () => {
			const consoleEl = $("#logConsole");
			if (consoleEl) consoleEl.textContent = "로그를 지웠습니다.\n";
		});
	}

	// ======================= WebSocket 초기화 (v025 핵심) =======================

	function initWebSockets() {
		initWS("/ws/state", processStateWS);
		initWS("/ws/log", processLogWS);
		// Metrics, Chart는 Chart 페이지에서 담당 (여기서는 생략)
	}

	function initWS(endpoint, processor) {
		const wsUrl = getWSHost(endpoint.substring(4)); // endpoint: /ws/state -> substring(4): state
		const ws = new WebSocket(wsUrl);

		ws.onopen = () => {
			showToast(`WebSocket ${endpoint} 연결 성공`, "ok");
			if (endpoint === "/ws/state") {
				// 최초 연결 시 상태 로딩을 기다리지 않고 UI를 활성화
				// (첫 상태는 REST로 로드하고, 그 이후부터 WS로 갱신)
			}
		};

		ws.onmessage = (event) => {
			try {
				const data = JSON.parse(event.data);
				processor(data);
			} catch (e) {
				showToast(`WS ${endpoint} 데이터 파싱 오류: ${e.message}`, "err");
			}
		};

		ws.onclose = () => {
			showToast(`WebSocket ${endpoint} 연결 끊김, 5초 후 재연결 시도`, "warn");
			setTimeout(() => initWS(endpoint, processor), 5000); 
		};

		ws.onerror = (e) => {
			showToast(`WebSocket ${endpoint} 오류 발생`, "err");
		};
	}

	// ✅ WS 상태 처리 (/ws/state)
	function processStateWS(j) {
		// j는 /api/control/summary와 유사한 status 필드를 가집니다.
		if (!j.status) return;

		text($("#simActive"), j.status.sim_active ? "Active" : "Idle");
		text($("#phase"), j.status.phase_name);
		text($("#wind"), `${Number(j.status.wind_speed || 0).toFixed(2)} m/s`);
		text($("#pwm"), `${Number(j.status.fan_pwm_percent || 0).toFixed(1)}%`);
		text($("#wifiMode"), j.status.wifi_mode || "-"); 

		const ipStr = (j.status.ip_ap || j.status.ip_sta)
			? [j.status.ip_ap && `AP:${j.status.ip_ap}`, j.status.ip_sta && `STA:${j.status.ip_sta}`].filter(Boolean).join(" / ")
			: (j.status.ip_addr || "-");
		text($("#ip"), ipStr);

		text($("#curSsid"), j.status.ssid || j.status.ap_ssid || "-");
		
		// 펌웨어 버전도 상태에 포함되면 갱신 (선택 사항)
		if (j.fw_version) text($("#fwVer"), j.fw_version);
	}

	// ✅ WS 로그 처리 (/ws/log)
	function processLogWS(data) {
		const consoleEl = $("#logConsole");
		if (!consoleEl || !data.log) return;
		
		// 로그 메시지 처리 (data.log는 배열 또는 단일 문자열 가정)
		const logs = Array.isArray(data.log) ? data.log : [data.log];
		const now = new Date().toLocaleTimeString();
		
		logs.forEach(msg => {
			consoleEl.textContent += `[${now}] ${msg}\n`;
		});

		// 스크롤 맨 아래로 이동
		consoleEl.scrollTop = consoleEl.scrollHeight;
		
		// 최대 로그 라인 수 제한 (예: 500라인)
		const maxLines = 500;
		const lines = consoleEl.textContent.split('\n');
		if (lines.length > maxLines) {
			consoleEl.textContent = lines.slice(lines.length - maxLines).join('\n');
		}
	}


	// ======================= 상태/버전 (API 경로/BODY 구조 변경) =======================
	async function refreshVersion() {
		try {
			// ✅ /api/version 대신 summary에 포함되지만, 초기 버전 로드를 위해 유지
			const r = await fetch("/api/version");
			const j = await r.json();
			text($("#fwVer"), j.fw_version);
		} catch {
			text($("#fwVer"), "version?");
		}
	}

	async function refreshState(showToastMsg = false) {
		setLoading(true);
		try {
			// API 변경: /api/state -> /api/control/summary
			const r = await fetch("/api/control/summary");
			const j = await r.json();
			g_config = j;
			
			// 상태는 processStateWS가 담당하지만, 초기 설정 로드를 위해 config 관련만 사용
			
			// Preset 목록은 windProfile에서 로드 가정
			g_presets = j.windProfile?.presets?.map(p => p.code) || [];

			// Status (초기 로딩 시만 반영)
			if (j.status) processStateWS(j);

			// Presets
			const sel = $("#preset");
			sel.innerHTML = "";
			g_presets.forEach(p => {
				const o = document.createElement("option");
				o.value = p; // code 사용 가정
				o.textContent = displayPresetName(p);
				sel.appendChild(o);
			});
			
			// Sim 설정 로드: j.simulation.sim 경로 사용
			const simConfig = j.simulation?.sim || {};
			sel.value = simConfig.preset || "";
			$("#btnSaveAllConfig").textContent = `프리셋 미리보기: ${displayPresetName(sel.value)}`; // 임시로 프리뷰 텍스트 사용

			// Sim 값 반영
			Object.entries(simConfig).forEach(([k, v]) => { const el = $(`#${k}`); if (el) el.value = v; });
			
			// Timing 설정 로드: j.motion.timing 경로 사용
			const timingConfig = j.motion?.timing || {};
			Object.entries(timingConfig).forEach(([k, v]) => { const el = $(`#${k}`); if (el) el.value = v; });
			
			// Wi-Fi 설정 로드: j.network.wifi 경로 사용
			const wifiConfig = j.network?.wifi || {};
			$("#wifi_mode").value = wifiConfig.wifi_mode || 0;
			$("#ap_ssid").value = wifiConfig.ap_network?.ap_ssid || "";
			$("#ap_password").value = wifiConfig.ap_network?.ap_password || "";
			displayStaNetworks(wifiConfig.sta_networks || []);

			// HW 설정 로드: j.motion.hw 경로 사용
			const hwConfig = j.motion?.hw || {};
			$("#pwm_pin").value = hwConfig.pwm_pin;
			$("#pwm_channel").value = hwConfig.pwm_channel;
			$("#pwm_freq").value = hwConfig.pwm_freq;
			$("#pwm_res").value = hwConfig.pwm_res;

			if (showToastMsg) showToast("상태 갱신 완료", "ok");
		} catch (e) {
			showToast(`상태 불러오기 실패: ${e.message}`, "err");
		} finally {
			setLoading(false);
		}
	}


	// ======================= 그룹별 저장 (메모리 패치 & Dirty 상태 변경) =======================
	function previewPreset() {
		const preset = $("#preset").value;
		showToast(`"${displayPresetName(preset)}" 미리보기 적용 (장치에 반영되지 않음)`, "ok");
	}

	// ✅ saveAllConfig 함수 추가 (실제 파일 저장)
	async function saveAllConfig() {
		if (configDirty) {
			await fetchApi("/api/config/save", "POST", {}, "전체 설정 파일 저장");
			// 저장 후 장치에 적용된 최신 설정값을 다시 로드 (확실성 위해)
			refreshState(); 
		} else {
			showToast("저장할 변경 사항이 없습니다.", "warn");
		}
	}
	
	async function saveSim() {
		// API 변경: /api/config -> /api/simulation
		const body = {
			sim: {
				preset: $("#preset").value,
				intensity: Number($("#intensity").value),
				gust_freq: Number($("#gust_freq").value),
				variability: Number($("#variability").value),
				fan_limit: Number($("#fan_limit").value),
				min_fan: Number($("#min_fan").value),
				turb_len: Number($("#turb_len").value),
				turb_sig: Number($("#turb_sig").value),
				therm_str: Number($("#therm_str").value),
				therm_rad: Number($("#therm_rad").value)
			}
		};
		await fetchApi("/api/simulation", "POST", body, "시뮬 설정 메모리 패치");
		// ✅ refreshState 제거 및 Dirty 상태 업데이트
		setDirtyStatus(true);
	}

	async function saveConfigInit() {
		if (confirm("경고: 모든 설정을 초기화하고 장치를 재부팅합니다. 계속하시겠습니까?")) {
			// API 유지: /api/config/init
			await fetchApi("/api/config/init", "POST", {}, "시스템 전체 초기화");
		}
	}
	
	async function saveTiming() {
		// API 변경: /api/config -> /api/motion
		const body = {
			timing: {
				sim_int: Number($("#sim_int").value),
				gust_int: Number($("#gust_int").value),
				thermal_int: Number($("#thermal_int").value)
			}
		};
		await fetchApi("/api/motion", "POST", body, "타이밍 설정 메모리 패치");
		// ✅ refreshState 제거 및 Dirty 상태 업데이트
		setDirtyStatus(true);
	}

	async function saveWifiAP() {
		// API 변경: /api/config -> /api/network
		const body = {
			wifi: {
				wifi_mode: Number($("#wifi_mode").value),
				ap_network: {
					ap_ssid: $("#ap_ssid").value,
					ap_password: $("#ap_password").value
				}
			}
		};
		await fetchApi("/api/network", "POST", body, "AP 설정 메모리 패치");
		// ✅ refreshState 제거 및 Dirty 상태 업데이트
		setDirtyStatus(true);
	}

	async function saveWifiSTA() {
		// API 변경: /api/config -> /api/network
		const sta = g_config.network?.wifi?.sta_networks || []; 
		await fetchApi("/api/network", "POST", { wifi: { sta_networks: sta } }, "STA 목록 메모리 패치");
		// ✅ refreshState 제거 및 Dirty 상태 업데이트
		setDirtyStatus(true);
	}

	async function savePWMConfig() {
		// API 변경: /api/config -> /api/motion
		const body = {
			hw: {
				pwm_pin: Number($("#pwm_pin").value),
				pwm_channel: Number($("#pwm_channel").value),
				pwm_freq: Number($("#pwm_freq").value),
				pwm_res: Number($("#pwm_res").value)
			}
		};
		// GPIO 체크 로직 유지
		if (body.hw.pwm_pin >= 6 && body.hw.pwm_pin <= 11)
			return showToast("GPIO6~11은 Flash용 핀으로 PWM 불가", "warn");
		if (body.hw.pwm_pin >= 34)
			return showToast("GPIO34 이상은 입력전용으로 PWM 불가", "warn");
		if (body.hw.pwm_channel < 0 || body.hw.pwm_channel > 7)
			return showToast("PWM 채널은 0~7 범위", "warn");
			
		await fetchApi("/api/motion", "POST", body, "PWM 설정 메모리 패치");
		// ✅ refreshState 제거 및 Dirty 상태 업데이트
		setDirtyStatus(true);
	}

	async function saveApiKey() {
		// API 변경: /api/config -> /api/security
		const newKey = $("#apiKeyInput").value.trim();
		if (!newKey) return showToast("API Key를 입력하세요.", "warn");
		setKey(newKey);
		await fetchApi("/api/security", "POST", { security: { api_key: newKey } }, "API Key 저장");
		// API Key 저장은 로컬 및 장치에 즉시 저장되므로 Dirty 상태 변경 불필요.
		showToast("API Key 저장 완료", "ok");
	}

	// ======================= Wi-Fi STA/Scan =======================
	
	// STA 리스트
	function displayStaNetworks(networks) {
		const list = $("#staList");
		list.innerHTML = "";
		if (!networks.length) {
			list.innerHTML = `<div class="muted" style="text-align:center;">저장된 STA 네트워크 없음</div>`;
			return;
		}
		const table = document.createElement("table");
		table.innerHTML = `<thead><tr><th>SSID</th><th>PW</th><th></th></tr></thead><tbody></tbody>`;
		const tbody = table.querySelector("tbody");
		networks.forEach((n, idx) => {
			const row = tbody.insertRow();
			row.innerHTML = `
      <td>${n.ssid}</td>
      <td>${n.pass ? "********" : "OPEN"}</td>
      <td class="right tight"><button class="btn err btn-remove-sta" data-index="${idx}">삭제</button></td>
    `;
		});
		list.appendChild(table);
		list.querySelectorAll(".btn-remove-sta").forEach(btn => {
			btn.addEventListener("click", e => {
				const idx = Number(e.target.dataset.index);
				const removed = networks[idx]?.ssid || "";
				networks.splice(idx, 1);
				// g_config의 경로 수정
				if (g_config.network?.wifi) g_config.network.wifi.sta_networks = networks; 
				showToast(`${removed} 삭제됨 (STA 저장 필요)`, "warn");
				displayStaNetworks(networks);
				setDirtyStatus(true); // ✅ STA 목록 변경 시 Dirty 상태 업데이트
			});
		});
	}

	// Wi-Fi 스캔
	async function scanNetworks() {
		await fetchApi("/api/scan?async=true", "GET", null, "Wi-Fi 스캔 시작");
		showToast("스캔 중... 1.5초 후 결과 수집", "warn");
		await new Promise(r => setTimeout(r, 1500));
		const txt = await fetchApi("/api/scan", "GET", null, "Wi-Fi 스캔 결과");
		if (!txt) return;
		try {
			const arr = JSON.parse(txt);
			const sel = $("#scanList");
			sel.innerHTML = '<option value="">-- 선택하세요 --</option>';
			if (!arr.length) {
				sel.innerHTML += '<option value="" disabled>스캔된 네트워크 없음</option>';
				showToast("스캔 결과 없음", "warn");
				return;
			}
			arr.sort((a, b) => b.rssi - a.rssi);
			arr.forEach(n => {
				const o = document.createElement("option");
				o.value = n.ssid;
				o.textContent = `${n.ssid} [${n.rssi} dBm, ${n.enc}]`;
				sel.appendChild(o);
			});
			showToast(`총 ${arr.length}개 네트워크 스캔 완료`, "ok");
		} catch (e) {
			showToast(`스캔 결과 파싱 실패: ${e.message}`, "err");
		}
	}

	function useScanResult() {
		const ssid = $("#scanList").value;
		const pass = $("#scanPass").value;
		if (!ssid) return showToast("SSID를 선택하세요.", "warn");
		if (pass.length > 0 && pass.length < 8) return showToast("비밀번호는 8자 이상 (OPEN 제외)", "err");

		// g_config의 경로 수정 및 초기화
		let arr = g_config.network?.wifi?.sta_networks || [];
		if (!g_config.network) g_config.network = {};
		if (!g_config.network.wifi) g_config.network.wifi = {};

		if (arr.some(n => n.ssid === ssid)) return showToast(`"${ssid}"는 이미 등록됨`, "warn");

		arr.push({ ssid, pass });
		g_config.network.wifi.sta_networks = arr;
		displayStaNetworks(arr);
		$("#scanPass").value = "";
		showToast(`${ssid} 추가됨 (STA 저장 필요)`, "ok");
		setDirtyStatus(true); // ✅ STA 목록 변경 시 Dirty 상태 업데이트
	}

	// ======================= 파일 업로드 =======================
	
	async function uploadFile(inputSel, url, msgSel, desc, isOTA = false) {
		const fileInput = $(inputSel);
		if (!fileInput || fileInput.files.length === 0)
			return showToast("업로드할 파일을 선택하세요.", "warn");
		if (isOTA && !confirm("펌웨어 OTA를 진행하시겠습니까? (장치가 재부팅됩니다)")) return;

		setLoading(true);
		const file = fileInput.files[0];
		const form = new FormData();
		form.append("file", file, file.name);
		text($(msgSel), `업로드 중... (${file.size} bytes)`);

		try {
			// API Key 헤더를 수동으로 추가해야 함 (fetchApi가 아닌 일반 fetch 사용 시)
			const headers = {};
			const k = getKey();
			if (k) headers["X-API-Key"] = k;

			const r = await fetch(url, { method: "POST", body: form, headers });
			const txt = await r.text();
			if (!r.ok) throw new Error(`[${r.status}] ${txt}`);
			if (isOTA) {
				showToast(`${desc} 완료, 재부팅 중`, "ok");
				text($(msgSel), "업데이트 성공! 재시작 중...");
				// 재부팅 대기
				setTimeout(() => location.reload(), 8000); 
			} else {
				showToast(`${desc} 성공`, "ok");
				text($(msgSel), `업로드 완료: ${txt}`);
			}
		} catch (e) {
			showToast(`${desc} 실패: ${e.message}`, "err");
			text($(msgSel), `업로드 실패: ${e.message}`);
		} finally {
			if (!isOTA) setLoading(false);
		}
	}
	async function uploadStatic() { await uploadFile("#fileUpload", "/upload", "#uploadMsg", "정적 파일 업로드"); }
	async function uploadOTA() { await uploadFile("#fileOTA", "/update", "#otaMsg", "펌웨어 OTA", true); }

})();

