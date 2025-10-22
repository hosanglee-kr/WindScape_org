/*
 * ------------------------------------------------------
 * 소스명 : SC10_chart_001.js
 * 모듈명 : WindScape 실시간 차트 Controller
 * ------------------------------------------------------
 * 기능 요약:
 *  - /api/chart_data 로 풍속/PWM 실시간 수집
 *  - Chart.js 기반 2축 그래프 업데이트
 *  - 줌/팬/정지/재시작 지원
 * ------------------------------------------------------
 */

(() => {
"use strict";

let paused = false;
const ctx = document.getElementById('windChart');
const refreshLabel = document.getElementById('refreshInfo');

const chart = new Chart(ctx, {
  type: 'line',
  data: {
    datasets: [
      {
        label: '풍속 (m/s)',
        yAxisID: 'yWind',
        borderColor: 'rgba(54,162,235,0.9)',
        backgroundColor: 'rgba(54,162,235,0.1)',
        data: [],
        tension: 0.3,
      },
      {
        label: 'PWM Duty (%)',
        yAxisID: 'yPWM',
        borderColor: 'rgba(255,99,132,0.9)',
        backgroundColor: 'rgba(255,99,132,0.1)',
        data: [],
        tension: 0.3,
      }
    ]
  },
  options: {
    animation: false,
    scales: {
      x: {
        type: 'time',
        time: { unit: 'second', tooltipFormat: 'HH:mm:ss' },
        ticks: { source: 'auto', autoSkip: true, maxRotation: 0 }
      },
      yWind: {
        type: 'linear',
        position: 'left',
        title: { display: true, text: '풍속 (m/s)' },
        suggestedMin: 0, suggestedMax: 10
      },
      yPWM: {
        type: 'linear',
        position: 'right',
        title: { display: true, text: 'PWM Duty (%)' },
        grid: { drawOnChartArea: false },
        suggestedMin: 0, suggestedMax: 100
      }
    },
    plugins: {
      legend: { position: 'bottom' },
      zoom: {
        zoom: { wheel: { enabled: true }, pinch: { enabled: true }, mode: 'x' },
        pan: { enabled: true, mode: 'x' },
        limits: { x: { min: 'original', max: 'original' } }
      }
    }
  }
});

document.getElementById('btnResetZoom').addEventListener('click', () => chart.resetZoom());
document.getElementById('btnPause').addEventListener('click', () => paused = true);
document.getElementById('btnResume').addEventListener('click', () => paused = false);

async function updateChart() {
  if (paused) {
    refreshLabel.textContent = "⏸ 일시정지 중...";
    return;
  }

  try {
    const resp = await fetch('/api/chart_data');
    const json = await resp.json();
    const recs = json.records || [];

    const wind = recs.map(e => ({ x: new Date(e.t), y: e.wind }));
    const pwm  = recs.map(e => ({ x: new Date(e.t), y: e.pwm }));

    chart.data.datasets[0].data = wind;
    chart.data.datasets[1].data = pwm;
    chart.update('none');

    const last = recs.length ? new Date(recs[recs.length-1].t).toLocaleTimeString() : '-';
    refreshLabel.textContent = `업데이트: ${last} (데이터 ${recs.length}개)`;
  } catch (err) {
    refreshLabel.textContent = `❌ 데이터 수신 실패: ${err.message}`;
  }
}

setInterval(updateChart, 3000);
updateChart();

})();

