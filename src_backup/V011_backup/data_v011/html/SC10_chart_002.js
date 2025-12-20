// SC10_chart_002.js

(() => {
"use strict";

let paused = false;
const refreshLabel = document.getElementById("refreshInfo");

// ======================= 슬라이더 값 표시 =======================
const sliders = [
  { id: "intensity", unit: "%", label: "intensityVal" },
  { id: "gust_freq", unit: "", label: "gustVal" },
  { id: "variability", unit: "%", label: "varVal" },
];
sliders.forEach(({ id, unit, label }) => {
  const el = document.getElementById(id);
  const lbl = document.getElementById(label);
  el.addEventListener("input", () => (lbl.textContent = `${el.value}${unit}`));
});

// ======================= 설정 저장 =======================
document.getElementById("btnSaveSim").addEventListener("click", async () => {
  const body = {
    preset_mode_index: document.getElementById("preset").selectedIndex,
    wind_intensity: Number(document.getElementById("intensity").value),
    gust_frequency: Number(document.getElementById("gust_freq").value * 10),
    wind_variability: Number(document.getElementById("variability").value),
  };

  const resp = await fetch("/api/config", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body),
  });

  if (resp.ok) {
    alert("✅ 시뮬레이션 설정이 적용되었습니다!");
  } else {
    alert("❌ 설정 적용 실패");
  }
});

// ======================= 차트 초기화 =======================
const ctxWind = document.getElementById("chartWind");
const ctxParam = document.getElementById("chartParams");
const ctxEvent = document.getElementById("chartEvents");
const ctxPreset = document.getElementById("chartPreset");

const chartWind = new Chart(ctxWind, {
  type: "line",
  data: { datasets: [
    { label: "풍속 (m/s)", yAxisID: "yWind", borderColor: "#2196f3", data: [], tension: 0.3 },
    { label: "PWM Duty (%)", yAxisID: "yPWM", borderColor: "#ff6384", data: [], tension: 0.3 },
  ]},
  options: {
    animation: false,
    scales: {
      x: { type: "time", time: { unit: "second" } },
      yWind: { position: "left", min: 0, max: 10 },
      yPWM: { position: "right", min: 0, max: 100, grid: { drawOnChartArea: false } },
    },
    plugins: {
      legend: { position: "bottom" },
      zoom: { zoom: { wheel: { enabled: true }, mode: "x" }, pan: { enabled: true, mode: "x" } },
    },
  },
});

const chartParam = new Chart(ctxParam, {
  type: "line",
  data: { datasets: [
    { label: "Intensity", borderColor: "#4caf50", data: [] },
    { label: "Variability", borderColor: "#ff9800", data: [] },
    { label: "Turbulence", borderColor: "#9c27b0", data: [] },
  ]},
  options: { animation: false, scales: { x: { type: "time" } }, plugins: { legend: { position: "bottom" } } },
});

const chartEvent = new Chart(ctxEvent, {
  type: "line",
  data: { datasets: [
    { label: "Gust", borderColor: "#f44336", data: [] },
    { label: "Thermal", borderColor: "#03a9f4", data: [] },
  ]},
  options: { animation: false, scales: { x: { type: "time" }, y: { min: 0, max: 1 } } },
});

const chartPreset = new Chart(ctxPreset, {
  type: "line",
  data: { datasets: [
    { label: "Preset Index", borderColor: "#607d8b", data: [] },
  ]},
  options: { animation: false, scales: { x: { type: "time" }, y: { min: 0, max: 10 } } },
});

// ======================= 컨트롤 버튼 =======================
document.getElementById("btnPause").addEventListener("click", () => (paused = true));
document.getElementById("btnResume").addEventListener("click", () => (paused = false));
document.getElementById("btnResetZoomAll").addEventListener("click", () => {
  [chartWind, chartParam, chartEvent, chartPreset].forEach((c) => c.resetZoom());
});

// ======================= 데이터 갱신 =======================
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

    chartWind.data.datasets[0].data = toXY(recs, "wind");
    chartWind.data.datasets[1].data = toXY(recs, "pwm");
    chartParam.data.datasets[0].data = toXY(recs, "intensity");
    chartParam.data.datasets[1].data = toXY(recs, "variability");
    chartParam.data.datasets[2].data = toXY(recs, "turbulence");
    chartEvent.data.datasets[0].data = toXY(recs, "gust").map((v) => ({ x: v.x, y: v.y ? 1 : 0 }));
    chartEvent.data.datasets[1].data = toXY(recs, "thermal").map((v) => ({ x: v.x, y: v.y ? 1 : 0 }));
    chartPreset.data.datasets[0].data = toXY(recs, "preset");

    [chartWind, chartParam, chartEvent, chartPreset].forEach((c) => c.update("none"));

    const last = recs.length ? new Date(recs[recs.length - 1].t).toLocaleTimeString() : "-";
    refreshLabel.textContent = `🕒 업데이트: ${last} (데이터 ${recs.length}개)`;
  } catch (err) {
    refreshLabel.textContent = `❌ 데이터 수신 실패: ${err.message}`;
  }
}

setInterval(updateCharts, 3000);
updateCharts();
})();
