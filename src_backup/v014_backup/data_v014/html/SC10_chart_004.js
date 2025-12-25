// SC10_chart_004.js

(() => {
	"use strict";

	let g_config = {}; // 설정 상태를 저장하기 위한 전역 변수
	let g_presets = [];
	let paused = false;
	const refreshLabel = document.getElementById("refreshInfo");

	// API Key (localStorage) - Main 페이지와 동일 로직
	const KEY_API = 'sc10_api_key';
	const getKey = () => localStorage.getItem(KEY_API) || '';

	// 프리셋 한글 매핑
	const presetNameMap = {
		OFF          	: "고정풍속",
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

	// DOM Helper
	const $ = (s, r = document) => r.querySelector(s);
	const text = (el, v) => el && (el.textContent = v);
	const setLoading = (flag) => {
		const ov = $("#loadingOverlay");
		if (ov) ov.style.display = flag ? "flex" : "none";
	};
	const showToast = (msg, type = "ok") => {
		const cont = $("#toastContainer");
		if (!cont) return;
		const div = document.createElement("div");
		div.className = `toast ${type}`;
		div.textContent = msg;
		cont.appendChild(div);
		setTimeout(() => div.remove(), 3000);
	};

	// ======================= 공통 API (SC10_main_017.js 유사) =======================
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
			return txt;
		} catch (e) {
			if (e.message !== "Unauthorized") showToast(`${desc} 실패: ${e.message}`, "err");
		} finally {
			setLoading(false);
		}
	}

	// ======================= 초기화 및 상태 갱신 (Main 페이지 로직 통합) =======================
	document.addEventListener("DOMContentLoaded", () => {
		bindEvents();
		refreshState(); // 초기 상태 및 프리셋 로드
		setInterval(updateCharts, 2000); // 3초마다 차트 데이터 갱신
		updateCharts();
	});

	function bindEvents() {
		$("#btnPreviewPreset")?.addEventListener("click", previewPreset);
		$("#btnSaveSim")?.addEventListener("click", saveSim);
		$("#btnSaveSimInit")?.addEventListener("click", saveSimInit);
		$("#btnSaveTiming")?.addEventListener("click", saveTiming);

		$("#btnPause")?.addEventListener("click", () => (paused = true));
		$("#btnResume")?.addEventListener("click", () => (paused = false));
		$("#btnResetZoomAll")?.addEventListener("click", resetAllChartsZoom);

		// 차트 토글 이벤트 바인딩
		document.querySelectorAll(".btn-toggle").forEach(btn => {
			btn.addEventListener("click", toggleChartContent);
		});
	}

	async function refreshState(showToastMsg = false) {
		setLoading(true);
		try {
			const r = await fetch("/api/state");
			const j = await r.json();
			g_config = j;
			g_presets = j.presets || [];

			// 프리셋 채우기
			const sel = $("#preset");
			sel.innerHTML = "";
			g_presets.forEach(p => {
				const o = document.createElement("option");
				o.value = p;
				o.textContent = displayPresetName(p);
				sel.appendChild(o);
			});
			sel.value = j.config.sim.preset || "";
			$("#presetPreview").textContent = `프리셋 미리보기: ${displayPresetName(sel.value)}`;

			// Sim 값 반영
			Object.entries(j.config.sim || {}).forEach(([k, v]) => { const el = $(`#${k}`); if (el) el.value = v; });

			// Timing 반영
			Object.entries(j.config.timing || {}).forEach(([k, v]) => { const el = $(`#${k}`); if (el) el.value = v; });

			if (showToastMsg) showToast("설정 상태 갱신 완료", "ok");
		} catch (e) {
			showToast(`설정 상태 불러오기 실패: ${e.message}`, "err");
		} finally {
			setLoading(false);
		}
	}

	// ======================= 그룹별 저장 로직 =======================
	function previewPreset() {
		const preset = $("#preset").value;
		$("#presetPreview").textContent = `프리셋 미리보기: ${displayPresetName(preset)}`;
		showToast(`"${displayPresetName(preset)}" 미리보기 적용`, "ok");
	}

	async function saveSim() {
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
		await fetchApi("/api/config", "POST", body, "시뮬 설정 저장");
		refreshState();
	}

	async function saveSimInit() {
		const body = {
		};
		await fetchApi("/api/config/init", "POST", body, "시뮬 설정 초기화");
		refreshState();
	}

	async function saveTiming() {
		const body = {
			timing: {
				sim_int: Number($("#sim_int").value),
				gust_int: Number($("#gust_int").value),
				thermal_int: Number($("#thermal_int").value)
			}
		};
		await fetchApi("/api/config", "POST", body, "타이밍 설정 저장");
		refreshState();
	}

	// ======================= 차트 토글 기능 =======================
	function toggleChartContent(e) {
		const btn = e.currentTarget;
		const content = btn.closest(".chart-container").querySelector(".chart-content");
		if (content.style.display === "none") {
			content.style.display = "block";
			btn.textContent = "▲"; // 열림
		} else {
			content.style.display = "none";
			btn.textContent = "▼"; // 닫힘
		}
	}


	// ======================= 차트 초기화 (신규 차트 추가) =======================
	const ctxWind = $("#chartWind");
	const ctxParam = $("#chartParams");
	const ctxTurbThermSig = $("#chartTurbThermSig"); // 신규
	const ctxEvent = $("#chartEvents");
	const ctxPreset = $("#chartPreset");
	const ctxTiming = $("#chartTiming"); // 신규

	const chartOptions = {
		animation: false,
		plugins: {
			legend: { position: "bottom" },
			zoom: { zoom: { wheel: { enabled: true }, mode: "x" }, pan: { enabled: true, mode: "x" } },
		},
		scales: { x: { type: "time", time: { unit: "second" } } }
	};

	const chartWind = new Chart(ctxWind, {
		type: "line",
		data: {
			datasets: [
				{ label: "풍속 (m/s)", yAxisID: "yWind", borderColor: "#2196f3", data: [], tension: 0.3 },
				{ label: "PWM Duty (%)", yAxisID: "yPWM", borderColor: "#ff6384", data: [], tension: 0.3 },
			]
		},
		options: {
			...chartOptions,
			scales: {
				...chartOptions.scales,
				yWind: { position: "left", min: 0, max: 20 },
				yPWM: { position: "right", min: 0, max: 100, grid: { drawOnChartArea: false } },
			},
		},
	});

	const chartParam = new Chart(ctxParam, {
		type: "line",
		data: {
			datasets: [
				{ label: "강도(Intensity)", borderColor: "#4caf50", data: [] },
				{ label: "가변성(Variability)", borderColor: "#ff9800", data: [] },
				{ label: "팬 최대(Fan Limit)", borderColor: "#00bcd4", data: [] }, // 차트 파라미터 확장
				{ label: "팬 최소(Min Fan)", borderColor: "#e91e63", data: [] }, // 차트 파라미터 확장
			]
		},
		options: { ...chartOptions, plugins: { legend: { position: "bottom" } } },
	});

	// 신규 차트: 난류/열기포 시그마 및 길이
	const chartTurbThermSig = new Chart(ctxTurbThermSig, {
		type: "line",
		data: {
			datasets: [
				{ label: "난류 시그마(Turb Sig)", yAxisID: "ySig", borderColor: "#9c27b0", data: [], tension: 0.3 },
				{ label: "난류 길이 스케일(Turb Len)", yAxisID: "yLen", borderColor: "#795548", data: [], tension: 0.3 },
				{ label: "열기포 세기(Therm Str)", yAxisID: "ySig", borderColor: "#8bc34a", data: [], tension: 0.3, borderDash: [5, 5] },
				{ label: "열기포 반경(Therm Rad)", yAxisID: "yLen", borderColor: "#ffc107", data: [], tension: 0.3, borderDash: [5, 5] },
			]
		},
		options: {
			...chartOptions,
			scales: {
				...chartOptions.scales,
				ySig: { position: "left", min: 0, max: 5 }, // Sigma/Strength 범위
				yLen: { position: "right", min: 0, max: 200, grid: { drawOnChartArea: false } }, // Length/Radius 범위
			},
		},
	});

	const chartEvent = new Chart(ctxEvent, {
		type: "line",
		data: {
			datasets: [
				{ label: "돌풍(Gust)", borderColor: "#f44336", data: [], stepped: true },
				{ label: "열기포(Thermal)", borderColor: "#03a9f4", data: [], stepped: true },
			]
		},
		options: { ...chartOptions, scales: { ...chartOptions.scales, y: { min: 0, max: 1 } } },
	});

	const chartPreset = new Chart(ctxPreset, {
		type: "line",
		data: {
			datasets: [
				{ label: "Preset Index", borderColor: "#607d8b", data: [], stepped: true },
			]
		},
		options: { ...chartOptions, scales: { ...chartOptions.scales, y: { min: 0, max: 10 } } },
	});

	// 신규 차트: 타이밍 설정
	const chartTiming = new Chart(ctxTiming, {
		type: "line",
		data: {
			datasets: [
				{ label: "Sim Interval (ms)", borderColor: "#9e9e9e", data: [], tension: 0.3 },
				{ label: "돌풍 간격(Gust Interval) (ms)", borderColor: "#e0e0e0", data: [], tension: 0.3 },
				{ label: "열기포체크 간격(Thermal Interval) (ms)", borderColor: "#bdbdbd", data: [], tension: 0.3 },
			]
		},
		options: { ...chartOptions, scales: { ...chartOptions.scales, y: { min: 0 } } },
	});


	function resetAllChartsZoom() {
		[chartWind, chartParam, chartTurbThermSig, chartEvent, chartPreset, chartTiming].forEach((c) => c.resetZoom());
	}


	// ======================= 데이터 갱신 로직 =======================
	async function updateCharts() {
		if (paused) {
			refreshLabel.textContent = "⏸ 일시정지 중...";
			return;
		}
		try {
			const resp = await fetch("/api/chart_data");
			const json = await resp.json();
			const recs = json.records || [];

			const toXY = (arr, key) => arr.map((e) => ({ x: new Date(e.t), y: e[key] }));

			// Chart Wind/PWM
			chartWind.data.datasets[0].data = toXY(recs, "wind");
			chartWind.data.datasets[1].data = toXY(recs, "pwm");

			// Chart Params (Intensity, Variability, Fan Limits)
			chartParam.data.datasets[0].data = toXY(recs, "intensity");
			chartParam.data.datasets[1].data = toXY(recs, "variability");
			chartParam.data.datasets[2].data = toXY(recs, "fan_limit");
			chartParam.data.datasets[3].data = toXY(recs, "min_fan");

			// Chart TurbThermSig (난류/열기포 시그마 및 길이) - 신규
			chartTurbThermSig.data.datasets[0].data = toXY(recs, "turb_sig");
			chartTurbThermSig.data.datasets[1].data = toXY(recs, "turb_len");
			chartTurbThermSig.data.datasets[2].data = toXY(recs, "therm_str");
			chartTurbThermSig.data.datasets[3].data = toXY(recs, "therm_rad");

			// Chart Events
			chartEvent.data.datasets[0].data = toXY(recs, "gust").map((v) => ({ x: v.x, y: v.y ? 1 : 0 }));
			chartEvent.data.datasets[1].data = toXY(recs, "thermal").map((v) => ({ x: v.x, y: v.y ? 1 : 0 }));

			// Chart Preset
			chartPreset.data.datasets[0].data = toXY(recs, "preset");

			// Chart Timing - 신규
			chartTiming.data.datasets[0].data = toXY(recs, "sim_int");
			chartTiming.data.datasets[1].data = toXY(recs, "gust_int");
			chartTiming.data.datasets[2].data = toXY(recs, "thermal_int");


			[chartWind, chartParam, chartTurbThermSig, chartEvent, chartPreset, chartTiming].forEach((c) => c.update("none"));

			const last = recs.length ? new Date(recs[recs.length - 1].t).toLocaleTimeString() : "-";
			refreshLabel.textContent = `🕒 업데이트: ${last} (데이터 ${recs.length}개)`;
		} catch (err) {
			refreshLabel.textContent = `❌ 데이터 수신 실패: ${err.message}`;
		}
	}
})();



