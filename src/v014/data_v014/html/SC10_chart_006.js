/*
 * ------------------------------------------------------
 * 소스명 : SC10_chart_006.js
 * 모듈명 : Smart Nature Wind Chart/Simulation UI Controller (v025)
 * ------------------------------------------------------
 * 기능 요약:
 * - ✅ /ws/chart WebSocket을 통한 실시간 차트 데이터 모니터링 (v025 핵심)
 * - 풍속 관련 상세 매개변수 및 타이밍 설정 (메모리 패치)
 * - ✅ /api/config/save 명시적 저장 기능 및 Dirty 상태 체크 통합
 * ------------------------------------------------------
 */

(() => {
	"use strict";

	let g_config = {}; 
	let g_presets = [];
	let paused = false;
	let configDirty = false; // ✅ 설정 변경 상태 플래그

	const refreshLabel = document.getElementById("refreshInfo");
	
	// API Key (localStorage)
	const KEY_API = 'sc10_api_key';
	const getKey = () => localStorage.getItem(KEY_API) || '';
	const getWSHost = () => `ws://${window.location.host}/ws/chart`; // ✅ WS 경로

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
	
	// ✅ HTML에 토스트/로딩이 없으므로 콘솔 출력으로 대체
	const setLoading = (flag) => { /* console.log(flag ? "Loading..." : "Loaded."); */ };
	const showToast = (msg, type = "ok") => { console.log(`[${type.toUpperCase()}] ${msg}`); };

	// ✅ 설정 Dirty 상태 UI 업데이트 함수
	const setDirtyStatus = (isDirty) => {
		configDirty = isDirty;
		// HTML에 이 버튼 ID가 존재해야 합니다. (SC10_chart_005.html 수정 요청 사항 반영)
		const btnSaveAll = $("#btnSaveAllConfig"); 
		if (btnSaveAll) {
			if (isDirty) {
				btnSaveAll.style.backgroundColor = "#dc2626"; // 빨간색
				btnSaveAll.textContent = "⚠️ 전체 설정 저장 (미저장)";
			} else {
				btnSaveAll.style.backgroundColor = "#2196f3"; // 파란색
				btnSaveAll.textContent = "✅ 전체 설정 저장";
			}
		}
	};
	
	// ✅ Dirty 상태 체크 함수 추가
	async function checkConfigDirtyState() {
		try {
			// Main 페이지와 동일하게 Dirty 상태 API 사용
			const r = await fetch("/api/config/dirty", { headers: { "X-API-Key": getKey() } });
			const j = await r.json();
			setDirtyStatus(j.dirty || false); 
		} catch (e) {
			// API 호출 오류 시 경고 표시
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

	// ======================= 초기화 및 상태 갱신 =======================
	document.addEventListener("DOMContentLoaded", () => {
		bindEvents();
		refreshState(false); // 초기 상태 및 프리셋 로드 (REST API 사용)
		initChartWebSocket(); // ✅ WS 연결 시작 (차트 데이터용)
		checkConfigDirtyState(); // ✅ Dirty 상태 주기적 체크 시작
	});

	function bindEvents() {
		$("#btnPreviewPreset")?.addEventListener("click", previewPreset);
		// ✅ 개별 저장 버튼은 메모리 패치 후 Dirty 상태만 업데이트
		$("#btnSaveSim")?.addEventListener("click", saveSim);
		$("#btnConfigInit")?.addEventListener("click", saveConfigInit); 
		$("#btnSaveTiming")?.addEventListener("click", saveTiming);
		
		// ✅ 전체 저장 버튼 이벤트 바인딩 (HTML에 이 버튼이 추가되어야 함)
		$("#btnSaveAllConfig")?.addEventListener("click", saveAllConfig); 

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
			// API 경로 변경: /api/control/summary (Sim 설정이 포함된 Summary API 사용)
			const r = await fetch("/api/control/summary");
			const j = await r.json();
			g_config = j; 
			
			// j.windProfile에서 프리셋 목록 로드
			g_presets = j.windProfile?.presets || []; 

			// Sim 설정 로드: j.simulation.sim 경로 사용
			const simConfig = j.simulation?.sim || {};
			
			// 프리셋 채우기
			const sel = $("#preset");
			sel.innerHTML = "";
			g_presets.forEach(p => {
				const o = document.createElement("option");
				o.value = p.code; // code 필드 사용 가정
				o.textContent = displayPresetName(p.code);
				sel.appendChild(o);
			});
			sel.value = simConfig.preset || "";

			// Sim 값 반영
			Object.entries(simConfig).forEach(([k, v]) => { const el = $(`#${k}`); if (el) el.value = v; });

			// Timing 반영: j.motion.timing 경로 사용
			const timingConfig = j.motion?.timing || {};
			Object.entries(timingConfig).forEach(([k, v]) => { const el = $(`#${k}`); if (el) el.value = v; });

			if (showToastMsg) showToast("설정 상태 갱신 완료", "ok");
		} catch (e) {
			showToast(`설정 상태 불러오기 실패: ${e.message}`, "err");
		} finally {
			setLoading(false);
		}
	}

	// ======================= 그룹별 저장 및 전체 저장 (v025 핵심 변경) =======================
	
	function previewPreset() {
		const preset = $("#preset").value;
		showToast(`"${displayPresetName(preset)}" 미리보기 적용 (장치에 반영되지 않음)`, "ok");
	}
	
	// ✅ saveAllConfig 함수 추가 (실제 파일 저장)
	async function saveAllConfig() {
		if (configDirty) {
			await fetchApi("/api/config/save", "POST", {}, "전체 설정 파일 저장");
			// 저장 후 장치에 적용된 최신 설정값을 다시 로드
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
		// 메모리에 패치
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
		// API 변경: /api/config -> /api/motion (타이밍 설정은 Motion 객체 내부에 포함됨)
		const body = {
			timing: {
				sim_int: Number($("#sim_int").value),
				gust_int: Number($("#gust_int").value),
				thermal_int: Number($("#thermal_int").value)
			}
		};
		// 메모리에 패치
		await fetchApi("/api/motion", "POST", body, "타이밍 설정 메모리 패치");
		// ✅ refreshState 제거 및 Dirty 상태 업데이트
		setDirtyStatus(true);
	}

	// ======================= 차트 토글 기능 =======================
	function toggleChartContent(e) {
		const btn = e.currentTarget;
		const content = btn.closest(".chart-container").querySelector(".chart-content");
		// CSS display 속성을 사용한 토글
		if (content.style.display === "none") {
			content.style.display = "block";
			btn.textContent = "▲"; // 열림
		} else {
			content.style.display = "none";
			btn.textContent = "▼"; // 닫힘
		}
	}


	// ======================= 차트 초기화 =======================
	
	const charts = [];
	const initChart = (ctx, config) => {
		const chart = new Chart(ctx, config);
		charts.push(chart);
		return chart;
	};

	const ctxWind = $("#chartWind");
	const ctxParam = $("#chartParams");
	const ctxTurbThermSig = $("#chartTurbThermSig"); 
	const ctxEvent = $("#chartEvents");
	const ctxPreset = $("#chartPreset");
	const ctxTiming = $("#chartTiming"); 

	const chartOptions = {
		animation: false,
		plugins: {
			legend: { position: "bottom" },
			zoom: { zoom: { wheel: { enabled: true }, mode: "x" }, pan: { enabled: true, mode: "x" } },
		},
		scales: { x: { type: "time", time: { unit: "second" } } }
	};
	
	// 각 차트 인스턴스를 initChart 함수로 생성하여 charts 배열에 추가
	const chartWind = initChart(ctxWind, {
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

	const chartParam = initChart(ctxParam, {
		type: "line",
		data: {
			datasets: [
				{ label: "강도(Intensity)", borderColor: "#4caf50", data: [] },
				{ label: "가변성(Variability)", borderColor: "#ff9800", data: [] },
				{ label: "팬 최대(Fan Limit)", borderColor: "#00bcd4", data: [] }, 
				{ label: "팬 최소(Min Fan)", borderColor: "#e91e63", data: [] }, 
			]
		},
		options: { ...chartOptions, plugins: { legend: { position: "bottom" } } },
	});

	const chartTurbThermSig = initChart(ctxTurbThermSig, {
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
				ySig: { position: "left", min: 0, max: 5 }, 
				yLen: { position: "right", min: 0, max: 200, grid: { drawOnChartArea: false } }, 
			},
		},
	});

	const chartEvent = initChart(ctxEvent, {
		type: "line",
		data: {
			datasets: [
				{ label: "돌풍(Gust)", borderColor: "#f44336", data: [], stepped: true },
				{ label: "열기포(Thermal)", borderColor: "#03a9f4", data: [], stepped: true },
			]
		},
		options: { ...chartOptions, scales: { ...chartOptions.scales, y: { min: 0, max: 1 } } },
	});

	const chartPreset = initChart(ctxPreset, {
		type: "line",
		data: {
			datasets: [
				{ label: "Preset Index", borderColor: "#607d8b", data: [], stepped: true },
			]
		},
		options: { ...chartOptions, scales: { ...chartOptions.scales, y: { min: 0, max: 10 } } },
	});

	const chartTiming = initChart(ctxTiming, {
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
		charts.forEach((c) => c.resetZoom());
	}


	// ======================= WS 데이터 수신 및 차트 갱신 (v025 핵심) =======================
	function processChartData(recs) {
		const toXY = (arr, key) => arr.map((e) => ({ x: new Date(e.t), y: e[key] }));

		// Chart Wind/PWM
		chartWind.data.datasets[0].data = toXY(recs, "wind");
		chartWind.data.datasets[1].data = toXY(recs, "pwm");

		// Chart Params
		chartParam.data.datasets[0].data = toXY(recs, "intensity");
		chartParam.data.datasets[1].data = toXY(recs, "variability");
		chartParam.data.datasets[2].data = toXY(recs, "fan_limit");
		chartParam.data.datasets[3].data = toXY(recs, "min_fan");

		// Chart TurbThermSig
		chartTurbThermSig.data.datasets[0].data = toXY(recs, "turb_sig");
		chartTurbThermSig.data.datasets[1].data = toXY(recs, "turb_len");
		chartTurbThermSig.data.datasets[2].data = toXY(recs, "therm_str");
		chartTurbThermSig.data.datasets[3].data = toXY(recs, "therm_rad");

		// Chart Events
		chartEvent.data.datasets[0].data = toXY(recs, "gust").map((v) => ({ x: v.x, y: v.y ? 1 : 0 }));
		chartEvent.data.datasets[1].data = toXY(recs, "thermal").map((v) => ({ x: v.x, y: v.y ? 1 : 0 }));

		// Chart Preset
		chartPreset.data.datasets[0].data = toXY(recs, "preset");

		// Chart Timing
		chartTiming.data.datasets[0].data = toXY(recs, "sim_int");
		chartTiming.data.datasets[1].data = toXY(recs, "gust_int");
		chartTiming.data.datasets[2].data = toXY(recs, "thermal_int");

		charts.forEach((c) => c.update("none"));

		const last = recs.length ? new Date(recs[recs.length - 1].t).toLocaleTimeString() : "-";
		refreshLabel.textContent = `🕒 WS 업데이트: ${last} (데이터 ${recs.length}개)`;
	}

	// ✅ WebSocket 연결 로직 추가
	function initChartWebSocket() {
		const ws = new WebSocket(getWSHost());
		ws.onopen = () => {
			showToast("WebSocket /ws/chart 연결 성공", "ok");
			refreshLabel.textContent = "✅ 실시간 차트 데이터 수신 중...";
		};

		ws.onmessage = (event) => {
			if (paused) return;
			try {
				const data = JSON.parse(event.data);
				// WS 메시지는 /api/sim/chart와 동일하게 'chart' 필드에 배열이 담겨온다고 가정
				if (data.chart && Array.isArray(data.chart)) {
					processChartData(data.chart);
				}
			} catch (e) {
				showToast("WS 데이터 파싱 오류", "err");
			}
		};

		ws.onclose = () => {
			showToast("WebSocket /ws/chart 연결 끊김, 5초 후 재연결 시도", "warn");
			refreshLabel.textContent = "❌ WS 연결 끊김. 재연결 시도 중...";
			setTimeout(initChartWebSocket, 5000); 
		};

		ws.onerror = (e) => {
			showToast(`WebSocket 오류: ${e.message}`, "err");
		};
	}

})();
