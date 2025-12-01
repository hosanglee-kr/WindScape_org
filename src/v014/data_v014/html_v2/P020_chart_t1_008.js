/*  
 * ------------------------------------------------------  
 * 소스명 : P020_chart_t1_008.js  
 * 모듈명 : Smart Nature Wind Chart/Simulation UI Controller (v029, Backend 029 정합)  
 * ------------------------------------------------------  
 * 기능 요약:  
 * - /ws/chart WebSocket을 통한 실시간 차트 데이터 모니터링  
 * - /api/config 기반 초기 설정/프리셋 로딩 (Main 페이지와 동일 구조)  
 * - /api/config/motion, /api/config/timing 메모리 패치  
 * - /api/config/save 전체 저장 (Flash Flush) + Dirty 상태 버튼 표시  
 * - /api/config/init Factory Reset  
 * - API Key: Main 페이지와 동일 키("snw_api_key") 사용  
 * ------------------------------------------------------  
 */

(() => {
	"use strict";

	/* ==============================
	 * 0. 상수 / 공용 유틸
	 * ============================== */

	const API_BASE          = "/api";
	const API_CONFIG        = `${API_BASE}/config`;
	const API_CONFIG_SAVE   = `${API_BASE}/config/save`;
	const API_CONFIG_INIT   = `${API_BASE}/config/init`;
	const API_CONFIG_MOTION = `${API_BASE}/config/motion`;   // P010_main_021.js와 동일
	const API_CONFIG_TIMING = `${API_BASE}/config/timing`;   // P010_main_021.js와 동일

	const API_KEY_STORAGE_KEY = "snw_api_key"; // ✅ Main 페이지와 동일

	// DOM 헬퍼
	const $ = (s, root = document) => root.querySelector(s);
	const text = (el, v) => el && (el.textContent = v);

	// API Key 읽기
	function getStoredApiKey() {
		try {
			return localStorage.getItem(API_KEY_STORAGE_KEY) || "";
		} catch (e) {
			console.warn("[ChartT1] Unable to read API key:", e);
			return "";
		}
	}

	// WebSocket URL (http/https → ws/wss + apiKey 쿼리)
	function buildWsUrl() {
		const proto = window.location.protocol === "https:" ? "wss" : "ws";
		let url = `${proto}://${window.location.host}/ws/chart`; // v029에서 /ws/chart 사용할 예정
		const apiKey = getStoredApiKey();
		if (apiKey) {
			url += `?apiKey=${encodeURIComponent(apiKey)}`;
		}
		return url;
	}

	// 토스트: 공통 showToast 있으면 그걸 쓰고, 없으면 console
	function showLocalToast(msg, type = "info") {
		if (typeof window.showToast === "function") {
			window.showToast(msg, type);
		} else {
			console.log(`[Toast ${type.toUpperCase()}] ${msg}`);
		}
	}

	// 로딩 오버레이
	function setLoading(flag) {
		const overlay = $("#loadingOverlay");
		if (!overlay) return;
		overlay.style.display = flag ? "flex" : "none";
	}

	/* ==============================
	 * 1. 전역 상태
	 * ============================== */

	let g_config  = {};   // /api/config 전체 구조 캐시
	let g_presets = [];   // 모션 프리셋 목록
	let paused    = false;
	let configDirty = false; // 메모리와 Flash 사이의 Dirty 상태

	const refreshLabel = document.getElementById("refreshInfo");

	// 프리셋 한글 매핑 (백엔드 code/name과 무관하게 표시용)
	const presetNameMap = {
		OFF           : "고정풍속",
		COUNTRY       : "들판",
		MEDITERRANEAN : "지중해",
		OCEAN         : "바다",
		MOUNTAIN      : "산바람",
		PLAINS        : "평야",
		FOREST_CANOPY : "숲속",
		HARBOR_BREEZE : "항구바람",
		URBAN_SUNSET  : "도심석양",
		TROPICAL_RAIN : "열대우림",
		DESERT_NIGHT  : "사막밤"
	};
	const displayPresetName = (n) => presetNameMap[n] || n;

	// Dirty 상태 버튼 UI 반영 (메인과 역할 유사)
	function setDirtyStatus(isDirty) {
		configDirty = isDirty;
		const btnSaveAll = $("#btnSaveAllConfig");
		if (!btnSaveAll) return;

		if (isDirty) {
			btnSaveAll.classList.add("warn");
			btnSaveAll.style.backgroundColor = "#dc2626";
			btnSaveAll.style.color = "#fff";
			btnSaveAll.textContent = "⚠️ 전체 Config 저장 (미저장)";
		} else {
			btnSaveAll.classList.remove("warn");
			btnSaveAll.style.backgroundColor = "#2ecc71";
			btnSaveAll.style.color = "#fff";
			btnSaveAll.textContent = "✅ 전체 Config 저장 완료";
		}
	}

	// 공통 fetch 래퍼 (POST/PATCH/초기화/저장용)
	async function fetchApi(url, method = "GET", body = null, desc = "작업") {
		setLoading(true);
		try {
			const opt = { method, headers: {} };

			if (body != null) {
				opt.body = JSON.stringify(body);
				opt.headers["Content-Type"] = "application/json";
			}

			const k = getStoredApiKey();
			if (k) {
				opt.headers["X-API-Key"] = k;
			}

			const resp = await fetch(url, opt);
			const txt  = await resp.text();

			if (resp.status === 401) {
				showLocalToast(`[401] ${desc} 실패: 인증 필요`, "err");
				throw new Error("Unauthorized");
			}
			if (!resp.ok) {
				showLocalToast(`${desc} 실패: ${txt || resp.status}`, "err");
				throw new Error(txt || resp.status);
			}

			// 성공 토스트 (GET용은 별도 사용, 여기선 주로 저장/초기화)
			if (desc) {
				showLocalToast(`${desc} 성공`, "ok");
			}

			return txt;
		} catch (e) {
			if (e.message !== "Unauthorized") {
				showLocalToast(`${desc} 실패: ${e.message}`, "err");
			}
			throw e;
		} finally {
			setLoading(false);
		}
	}

	/* ==============================
	 * 2. 초기화 (DOMContentLoaded)
	 * ============================== */

	document.addEventListener("DOMContentLoaded", () => {
		bindEvents();
		loadConfigAndFillUI();   // /api/config 로딩 + 프리셋/UI 반영
		initChartWebSocket();    // /ws/chart 연결
	});

	function bindEvents() {
		// 시뮬/타이밍/전체 저장/초기화
		$("#btnPreviewPreset")?.addEventListener("click", previewPreset);
		$("#btnSaveSim")?.addEventListener("click", saveSim);
		$("#btnSaveTiming")?.addEventListener("click", saveTiming);
		$("#btnConfigInit")?.addEventListener("click", saveConfigInit);
		$("#btnSaveAllConfig")?.addEventListener("click", saveAllConfig);

		// 차트 제어
		$("#btnPause")?.addEventListener("click", () => (paused = true));
		$("#btnResume")?.addEventListener("click", () => (paused = false));
		$("#btnResetZoomAll")?.addEventListener("click", resetAllChartsZoom);

		// 차트 토글 버튼
		document.querySelectorAll(".btn-toggle").forEach((btn) => {
			btn.addEventListener("click", toggleChartContent);
		});
	}

	/* ==============================
	 * 3. /api/config 로딩 → UI 반영 (v029 백엔드 정합)
	 * ============================== */

	async function loadConfigAndFillUI() {
		setLoading(true);
		try {
			const headers = { Accept: "application/json" };
			const apiKey  = getStoredApiKey();
			if (apiKey) headers["X-API-Key"] = apiKey;

			const resp = await fetch(API_CONFIG, { method: "GET", headers });
			if (!resp.ok) {
				throw new Error(`HTTP ${resp.status}`);
			}
			const cfg = await resp.json();
			g_config = cfg;

			// ---- 프리셋 목록 추출 (Main과 동일 로직) ----
			let presets = [];
			if (cfg.motion && Array.isArray(cfg.motion.presets)) {
				presets = cfg.motion.presets;
			} else if (Array.isArray(cfg.windProfiles)) {
				presets = cfg.windProfiles;
			}
			g_presets = presets;

			const selPreset = $("#preset");
			if (selPreset) {
				selPreset.innerHTML = "";
				if (!presets || presets.length === 0) {
					const opt = document.createElement("option");
					opt.value = "";
					opt.textContent = "(프리셋 없음)";
					selPreset.appendChild(opt);
				} else {
					presets.forEach((p, idx) => {
						const opt = document.createElement("option");
						const value =
							p.id != null ? p.id :
							p.code    ?? p.name ?? String(idx);
						const label =
							p.label || p.name || displayPresetName(p.code || value);

						opt.value = value;
						opt.textContent = label;
						selPreset.appendChild(opt);
					});
				}
			}

			// ---- Motion / Wind ----
			let motion = null;
			if (cfg.motion && cfg.motion.current) {
				motion = cfg.motion.current;
			} else if (cfg.motion && cfg.motion.active) {
				motion = cfg.motion.active;
			} else if (cfg.control && cfg.control.wind) {
				motion = cfg.control.wind;
			}

			if (motion) {
				if ($("#intensity"))   $("#intensity").value   = motion.intensity   ?? "";
				if ($("#gust_freq"))   $("#gust_freq").value   = motion.gust_freq   ?? "";
				if ($("#variability")) $("#variability").value = motion.variability ?? "";
				if ($("#fan_limit"))   $("#fan_limit").value   = motion.fan_limit   ?? "";
				if ($("#min_fan"))     $("#min_fan").value     = motion.min_fan     ?? "";
				if ($("#turb_len"))    $("#turb_len").value    = motion.turb_len    ?? "";
				if ($("#turb_sig"))    $("#turb_sig").value    = motion.turb_sig    ?? "";
				if ($("#therm_str"))   $("#therm_str").value   = motion.therm_str   ?? "";
				if ($("#therm_rad"))   $("#therm_rad").value   = motion.therm_rad   ?? "";

				// 프리셋 선택값 (백엔드에서 preset_id 등 관리하는 경우)
				if ($("#preset")) {
					const id = motion.preset_id ?? motion.preset ?? "";
					if (id !== "" && $("#preset").querySelector(`option[value="${id}"]`)) {
						$("#preset").value = id;
					}
				}
			}

			// ---- Timing ----
			const timing = cfg.timing || {};
			if ($("#sim_int"))     $("#sim_int").value     = timing.sim_int     ?? "";
			if ($("#gust_int"))    $("#gust_int").value    = timing.gust_int    ?? "";
			if ($("#thermal_int")) $("#thermal_int").value = timing.thermal_int ?? "";

			// 초기 로딩 직후 Dirty 아님
			setDirtyStatus(false);

			showLocalToast("설정 상태 로딩 완료 (/api/config)", "ok");
		} catch (e) {
			console.error("[ChartT1] loadConfigAndFillUI failed:", e);
			showLocalToast("설정 상태 로딩 실패: " + e.message, "err");
		} finally {
			setLoading(false);
		}
	}

	/* ==============================
	 * 4. 그룹별 저장 및 전체 저장 (v029 REST 연동)
	 * ============================== */

	function previewPreset() {
		const sel = $("#preset");
		if (!sel) return;
		const code = sel.value;
		showLocalToast(`"${displayPresetName(code)}" 미리보기 적용 (장치에 아직 저장되진 않음)`, "info");
	}

	// 풍속/모션 메모리 패치 → /api/config/motion (PATCH)
	async function saveSim() {
		try {
			const body = {
				motion: {
					intensity:   Number($("#intensity")?.value   || 0),
					gust_freq:   Number($("#gust_freq")?.value   || 0),
					variability: Number($("#variability")?.value || 0),
					fan_limit:   Number($("#fan_limit")?.value   || 0),
					min_fan:     Number($("#min_fan")?.value     || 0),
					turb_len:    Number($("#turb_len")?.value    || 0),
					turb_sig:    Number($("#turb_sig")?.value    || 0),
					therm_str:   Number($("#therm_str")?.value   || 0),
					therm_rad:   Number($("#therm_rad")?.value   || 0),
					preset_id:   $("#preset") ? ($("#preset").value || null) : null
				}
			};

			await fetchApi(API_CONFIG_MOTION, "PATCH", body, "시뮬(모션) 설정 메모리 패치");
			// 메모리와 Flash가 달라졌으므로 Dirty
			setDirtyStatus(true);
		} catch {
			// fetchApi 내부에서 처리
		}
	}

	// 타이밍 메모리 패치 → /api/config/timing (PATCH)
	async function saveTiming() {
		try {
			const body = {
				timing: {
					sim_int:     Number($("#sim_int")?.value     || 0),
					gust_int:    Number($("#gust_int")?.value    || 0),
					thermal_int: Number($("#thermal_int")?.value || 0)
				}
			};

			await fetchApi(API_CONFIG_TIMING, "PATCH", body, "타이밍 설정 메모리 패치");
			setDirtyStatus(true);
		} catch {
			// fetchApi 내부에서 처리
		}
	}

	// Factory Reset → /api/config/init (POST)
	async function saveConfigInit() {
		if (!confirm("⚠️ 모든 설정을 기본값으로 초기화합니다.\n진행하시겠습니까?")) {
			return;
		}
		try {
			await fetchApi(API_CONFIG_INIT, "POST", { factory: true }, "시스템 전체 초기화");
			// 초기화 후 다시 로딩
			await loadConfigAndFillUI();
		} catch {
			// fetchApi 내부에서 처리
		}
	}

	// 전체 Config 저장 → /api/config/save (POST {save_all:true})
	async function saveAllConfig() {
		if (!configDirty) {
			showLocalToast("저장할 변경 사항이 없습니다.", "info");
			return;
		}
		if (!confirm("현재까지의 메모리 변경 내용을 모두 Flash에 저장하시겠습니까?")) {
			return;
		}
		try {
			await fetchApi(API_CONFIG_SAVE, "POST", { save_all: true }, "전체 Config 저장");
			setDirtyStatus(false);
		} catch {
			// fetchApi 내부에서 처리
		}
	}

	/* ==============================
	 * 5. 차트 토글 / 초기화
	 * ============================== */

	function toggleChartContent(e) {
		const btn = e.currentTarget;
		const container = btn.closest(".chart-container");
		if (!container) return;
		const content = container.querySelector(".chart-content");
		if (!content) return;

		if (content.style.display === "none") {
			content.style.display = "block";
			btn.textContent = "▲";
		} else {
			content.style.display = "none";
			btn.textContent = "▼";
		}
	}

	const charts = [];

	function initChart(ctx, config) {
		if (!ctx) return null;
		const chart = new Chart(ctx, config);
		charts.push(chart);
		return chart;
	}

	const chartOptionsBase = {
		animation: false,
		plugins: {
			legend: { position: "bottom" },
			zoom: {
				zoom: {
					wheel: { enabled: true },
					mode: "x"
				},
				pan: { enabled: true, mode: "x" }
			}
		},
		scales: {
			x: { type: "time", time: { unit: "second" } }
		}
	};

	// 캔버스 참조
	const ctxWind          = $("#chartWind");
	const ctxParam         = $("#chartParams");
	const ctxTurbThermSig  = $("#chartTurbThermSig");
	const ctxEvent         = $("#chartEvents");
	const ctxPreset        = $("#chartPreset");
	const ctxTiming        = $("#chartTiming");

	// 풍속 / PWM
	const chartWind = initChart(ctxWind, {
		type: "line",
		data: {
			datasets: [
				{ label: "풍속 (m/s)",     yAxisID: "yWind", borderColor: "#2196f3", data: [], tension: 0.3 },
				{ label: "PWM Duty (%)",  yAxisID: "yPWM",  borderColor: "#ff6384", data: [], tension: 0.3 }
			]
		},
		options: {
			...chartOptionsBase,
			scales: {
				...chartOptionsBase.scales,
				yWind: { position: "left",  min: 0, max: 20 },
				yPWM:  {
					position: "right",
					min: 0,
					max: 100,
					grid: { drawOnChartArea: false }
				}
			}
		}
	});

	// 핵심 매개변수
	const chartParam = initChart(ctxParam, {
		type: "line",
		data: {
			datasets: [
				{ label: "강도(Intensity)",           borderColor: "#4caf50", data: [] },
				{ label: "가변성(Variability)",       borderColor: "#ff9800", data: [] },
				{ label: "팬 최대(Fan Limit)",        borderColor: "#00bcd4", data: [] },
				{ label: "팬 최소(Min Fan)",          borderColor: "#e91e63", data: [] }
			]
		},
		options: {
			...chartOptionsBase,
			plugins: { ...chartOptionsBase.plugins, legend: { position: "bottom" } }
		}
	});

	// 난류/열기포
	const chartTurbThermSig = initChart(ctxTurbThermSig, {
		type: "line",
		data: {
			datasets: [
				{ label: "난류 시그마(Turb Sig)",   yAxisID: "ySig", borderColor: "#9c27b0", data: [], tension: 0.3 },
				{ label: "난류 길이(Turb Len)",     yAxisID: "yLen", borderColor: "#795548", data: [], tension: 0.3 },
				{ label: "열기포 세기(Therm Str)",  yAxisID: "ySig", borderColor: "#8bc34a", data: [], tension: 0.3, borderDash: [5, 5] },
				{ label: "열기포 반경(Therm Rad)",  yAxisID: "yLen", borderColor: "#ffc107", data: [], tension: 0.3, borderDash: [5, 5] }
			]
		},
		options: {
			...chartOptionsBase,
			scales: {
				...chartOptionsBase.scales,
				ySig: { position: "left",  min: 0, max: 5 },
				yLen: { position: "right", min: 0, max: 200, grid: { drawOnChartArea: false } }
			}
		}
	});

	// 이벤트 (Gust / Thermal)
	const chartEvent = initChart(ctxEvent, {
		type: "line",
		data: {
			datasets: [
				{ label: "돌풍(Gust)",    borderColor: "#f44336", data: [], stepped: true },
				{ label: "열기포(Thermal)", borderColor: "#03a9f4", data: [], stepped: true }
			]
		},
		options: {
			...chartOptionsBase,
			scales: {
				...chartOptionsBase.scales,
				y: { min: 0, max: 1 }
			}
		}
	});

	// 프리셋 인덱스
	const chartPreset = initChart(ctxPreset, {
		type: "line",
		data: {
			datasets: [
				{ label: "Preset Index", borderColor: "#607d8b", data: [], stepped: true }
			]
		},
		options: {
			...chartOptionsBase,
			scales: {
				...chartOptionsBase.scales,
				y: { min: 0 }
			}
		}
	});

	// 타이밍
	const chartTiming = initChart(ctxTiming, {
		type: "line",
		data: {
			datasets: [
				{ label: "Sim Interval (ms)",                   borderColor: "#9e9e9e", data: [], tension: 0.3 },
				{ label: "돌풍 간격(Gust Interval) (ms)",       borderColor: "#e0e0e0", data: [], tension: 0.3 },
				{ label: "열기포 체크 간격(Thermal Interval)", borderColor: "#bdbdbd", data: [], tension: 0.3 }
			]
		},
		options: {
			...chartOptionsBase,
			scales: {
				...chartOptionsBase.scales,
				y: { min: 0 }
			}
		}
	});

	function resetAllChartsZoom() {
		charts.forEach((c) => c && c.resetZoom && c.resetZoom());
	}

	/* ==============================
	 * 6. WebSocket /ws/chart → 차트 갱신
	 * ============================== */

	function processChartData(recs) {
		if (!recs || !recs.length) {
			if (refreshLabel) {
				refreshLabel.textContent = "WS 데이터 없음";
			}
			return;
		}

		const toXY = (arr, key) =>
			arr.map((e) => ({ x: new Date(e.t), y: e[key] }));

		// Wind / PWM
		if (chartWind) {
			chartWind.data.datasets[0].data = toXY(recs, "wind");
			chartWind.data.datasets[1].data = toXY(recs, "pwm");
		}

		// Params
		if (chartParam) {
			chartParam.data.datasets[0].data = toXY(recs, "intensity");
			chartParam.data.datasets[1].data = toXY(recs, "variability");
			chartParam.data.datasets[2].data = toXY(recs, "fan_limit");
			chartParam.data.datasets[3].data = toXY(recs, "min_fan");
		}

		// Turb / Therm
		if (chartTurbThermSig) {
			chartTurbThermSig.data.datasets[0].data = toXY(recs, "turb_sig");
			chartTurbThermSig.data.datasets[1].data = toXY(recs, "turb_len");
			chartTurbThermSig.data.datasets[2].data = toXY(recs, "therm_str");
			chartTurbThermSig.data.datasets[3].data = toXY(recs, "therm_rad");
		}

		// Events
		if (chartEvent) {
			chartEvent.data.datasets[0].data = toXY(recs, "gust").map((v) => ({ x: v.x, y: v.y ? 1 : 0 }));
			chartEvent.data.datasets[1].data = toXY(recs, "thermal").map((v) => ({ x: v.x, y: v.y ? 1 : 0 }));
		}

		// Preset index
		if (chartPreset) {
			chartPreset.data.datasets[0].data = toXY(recs, "preset");
		}

		// Timing
		if (chartTiming) {
			chartTiming.data.datasets[0].data = toXY(recs, "sim_int");
			chartTiming.data.datasets[1].data = toXY(recs, "gust_int");
			chartTiming.data.datasets[2].data = toXY(recs, "thermal_int");
		}

		charts.forEach((c) => c && c.update("none"));

		const last = new Date(recs[recs.length - 1].t);
		if (refreshLabel) {
			refreshLabel.textContent = `🕒 WS 업데이트: ${last.toLocaleTimeString()} (데이터 ${recs.length}개)`;
		}
	}

	function initChartWebSocket() {
		let ws = null;

		function connect() {
			const url = buildWsUrl();
			ws = new WebSocket(url);

			ws.onopen = () => {
				showLocalToast("WebSocket /ws/chart 연결 성공", "ok");
				if (refreshLabel) {
					refreshLabel.textContent = "✅ 실시간 차트 데이터 수신 중...";
				}
			};

			ws.onmessage = (event) => {
				if (paused) return;
				try {
					const data = JSON.parse(event.data);
					// 백엔드에서 { chart: [...] } 구조로 보낸다고 가정
					if (data.chart && Array.isArray(data.chart)) {
						processChartData(data.chart);
					}
				} catch (e) {
					console.warn("[ChartT1] WS 데이터 파싱 오류:", e);
					showLocalToast("WS 데이터 파싱 오류", "err");
				}
			};

			ws.onclose = () => {
				showLocalToast("WebSocket /ws/chart 연결 끊김, 5초 후 재연결 시도", "warn");
				if (refreshLabel) {
					refreshLabel.textContent = "❌ WS 연결 끊김. 재연결 시도 중...";
				}
				setTimeout(connect, 5000);
			};

			ws.onerror = (e) => {
				console.error("[ChartT1] WebSocket 오류:", e);
			};
		}

		connect();
	}

})();
