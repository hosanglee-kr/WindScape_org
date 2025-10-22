/*
 * ------------------------------------------------------
 * 소스명 : SC10_chart_001.js
 * 모듈명 : WindScape 실시간 차트 Controller (확장판)
 * ------------------------------------------------------
 * 기능 요약:
 *  - /api/chart_data 수집 → 4개 차트 표시
 *    ① 풍속/PWM Duty (주요 출력)
 *    ② Intensity/Variability/Turbulence (설정 변화)
 *    ③ Gust/Thermal 이벤트 상태
 *    ④ Preset 변화 이력
 *  - Chart.js + zoom/pan 플러그인 지원
 * ------------------------------------------------------
 */

(() => {
"use strict";

let paused = false;
const refreshLabel = document.getElementById("refreshInfo");

// ---------- 공통 옵션 ----------
const commonOptions = (title, yMin, yMax) => ({
  animation: false,
  scales: {
    x: {
      type: "time",
      time: { unit: "second", tooltipFormat: "HH:mm:ss" },
      ticks: { source: "auto", autoSkip: true, maxRotation: 0 }
    },
    y: {
      min: yMin,
      max: yMax,
      title: { display: true, text: title }
    }
  },
  plugins: {
    legend: { position: "bottom" },
    zoom: {
      zoom: { wheel: { enabled: true }, pinch: { enabled: true }, mode: "x" },
      pan: { enabled: true, mode: "x" }
    }
  }
});

// ---------- Chart #1: 풍속 / PWM ----------
const ctx1 = document.getElementById("chartWind");
const chart1 = new Chart(ctx1, {
  type: "line",
  data: {
    datasets: [
      {
        label: "풍속 (m/s)",
        borderColor: "rgba(54,162,235,0.9)",
        backgroundColor: "rgba(54,162,235,0.1)",
        yAxisID: "yWind",
        data: [],
        tension: 0.3
      },
      {
        label: "PWM Duty (%)",
        borderColor: "rgba(255,99,132,0.9)",
        backgroundColor: "rgba(255,99,132,0.1)",
        yAxisID: "yPWM",
        data: [],
        tension: 0.3
      }
    ]
  },
  options: {
    animation: false,
    scales: {
      x: {
        type: "time",
        time: { unit: "second", tooltipFormat: "HH:mm:ss" },
        ticks: { source: "auto", autoSkip: true }
      },
      yWind: {
        type: "linear",
        position: "left",
        suggestedMin: 0,
        suggestedMax: 10,
        title: { display: true, text: "풍속 (m/s)" }
      },
      yPWM: {
        type: "linear",
        position: "right",
        suggestedMin: 0,
        suggestedMax: 100,
        grid: { drawOnChartArea: false },
        title: { display: true, text: "PWM Duty (%)" }
      }
    },
    plugins: { legend: { position: "bottom" } }
  }
});

// ---------- Chart #2: 시뮬레이션 파라미터 ----------
const ctx2 = document.getElementById("chartParams");
const chart2 = new Chart(ctx2, {
  type: "line",
  data: {
    datasets: [
      { label: "Intensity (%)", borderColor: "#4caf50", data: [], tension: 0.3 },
      { label: "Variability (%)", borderColor: "#ff9800", data: [], tension: 0.3 },
      { label: "Turbulence σ", borderColor: "#9c27b0", data: [], tension: 0.3 }
    ]
  },
  options: commonOptions("시뮬레이션 파라미터", 0, 100)
});

// ---------- Chart #3: 이벤트 상태 ----------
const ctx3 = document.getElementById("chartEvents");
const chart3 = new Chart(ctx3, {
  type: "line",
  data: {
    datasets: [
      { label: "Gust Active", borderColor: "#e91e63", data: [], stepped: true },
      { label: "Thermal Active", borderColor: "#03a9f4", data: [], stepped: true }
    ]
  },
  options: commonOptions("이벤트 상태(1=활성)", -0.1, 1.1)
});

// ---------- Chart #4: 프리셋 변화 ----------
const ctx4 = document.getElementById("chartPreset");
const chart4 = new Chart(ctx4, {
  type: "line",
  data: {
    datasets: [
      {
        label: "Preset ID",
        borderColor: "#607d8b",
        backgroundColor: "rgba(96,125,139,0.2)",
        data: [],
        stepped: true
      }
    ]
  },
  options: commonOptions("프리셋 변화", 0, 10)
});

// ---------- 제어 버튼 ----------
document.getElementById("btnPause").addEventListener("click", () => paused = true);
document.getElementById("btnResume").addEventListener("click", () => paused = false);
document.getElementById("btnResetZoomAll").addEventListener("click", () => {
  [chart1, chart2, chart3, chart4].forEach(c => c.resetZoom());
});

// ---------- 데이터 갱신 ----------
async function updateCharts() {
  if (paused) {
    refreshLabel.textContent = "⏸ 일시정지 중...";
    return;
  }

  try {
    const resp = await fetch("/api/chart_data");
    const json = await resp.json();
    const recs = json.records || [];

    if (!recs.length) {
      refreshLabel.textContent = "데이터 없음";
      return;
    }

    // 변환
    const toXY = (field) => recs.map(e => ({ x: new Date(e.t), y: e[field] }));

    chart1.data.datasets[0].data = toXY("wind");
    chart1.data.datasets[1].data = toXY("pwm");

    chart2.data.datasets[0].data = toXY("intensity");
    chart2.data.datasets[1].data = toXY("variability");
    chart2.data.datasets[2].data = toXY("turbulence");

    chart3.data.datasets[0].data = recs.map(e => ({ x: new Date(e.t), y: e.gust ? 1 : 0 }));
    chart3.data.datasets[1].data = recs.map(e => ({ x: new Date(e.t), y: e.thermal ? 1 : 0 }));

    chart4.data.datasets[0].data = toXY("preset");

    [chart1, chart2, chart3, chart4].forEach(c => c.update("none"));

    const last = new Date(recs[recs.length - 1].t).toLocaleTimeString();
    refreshLabel.textContent = `업데이트: ${last} (${recs.length}개)`;
  } catch (err) {
    refreshLabel.textContent = `❌ 데이터 오류: ${err.message}`;
  }
}

setInterval(updateCharts, 3000);
updateCharts();

})();
