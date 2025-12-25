/*
 * ------------------------------------------------------
 * 소스명 : P050_chart_t2_008.js
 * 모듈명 : Smart Nature Wind Chart Monitor Controller (v008)
 * ------------------------------------------------------
 * 기능 요약:
 * - 🎯 /ws/chart WebSocket을 통한 실시간 차트 데이터 모니터링 (T1과 동일 스키마)
 * - 6개 Chart.js 차트에 풍속/파라미터/난류/이벤트/프리셋/타이밍 실시간 반영
 * - 일시정지/재개/줌 초기화 + 차트 접기 토글
 * - ⚠️ 설정 변경/저장 기능은 전혀 없음 (순수 모니터 페이지)
 * ------------------------------------------------------
 */

(() => {
  "use strict";

  const $ = (s, r = document) => r.querySelector(s);
  const getWSUrl = () => `ws://${window.location.host}/ws/chart`;

  const refreshLabel = $("#refreshInfo");

  let isPaused = false;
  const charts = [];

  // ======================= Chart.js 공통 옵션 =======================

  const baseOptions = {
    responsive: true,
    maintainAspectRatio: false,
    animation: false,
    scales: {
      x: {
        type: "time",
        time: {
          unit: "second"
        }
      }
    },
    plugins: {
      legend: { position: "bottom" },
      zoom: {
        zoom: {
          wheel: { enabled: true },
          pinch: { enabled: true },
          mode: "x"
        },
        pan: {
          enabled: true,
          mode: "x"
        }
      }
    }
  };

  const initChart = (ctx, config) => {
    const chart = new Chart(ctx, config);
    charts.push(chart);
    return chart;
  };

  // ======================= 차트 인스턴스 초기화 =======================

  let chartWind, chartParam, chartTurbThermSig, chartEvent, chartPreset, chartTiming;

  function initCharts() {
    const ctxWind = $("#chartWind");
    const ctxParam = $("#chartParams");
    const ctxTurbThermSig = $("#chartTurbThermSig");
    const ctxEvent = $("#chartEvents");
    const ctxPreset = $("#chartPreset");
    const ctxTiming = $("#chartTiming");

    if (!ctxWind || !ctxParam || !ctxTurbThermSig || !ctxEvent || !ctxPreset || !ctxTiming) {
      console.error("[ChartT2] Canvas 요소가 일부 없습니다.");
      return;
    }

    chartWind = initChart(ctxWind, {
      type: "line",
      data: {
        datasets: [
          {
            label: "풍속 (m/s)",
            yAxisID: "yWind",
            borderColor: "#2196f3",
            data: [],
            tension: 0.3
          },
          {
            label: "PWM Duty (%)",
            yAxisID: "yPWM",
            borderColor: "#ff6384",
            data: [],
            tension: 0.3
          }
        ]
      },
      options: {
        ...baseOptions,
        scales: {
          ...baseOptions.scales,
          yWind: { position: "left", min: 0, max: 20 },
          yPWM: {
            position: "right",
            min: 0,
            max: 100,
            grid: { drawOnChartArea: false }
          }
        }
      }
    });

    chartParam = initChart(ctxParam, {
      type: "line",
      data: {
        datasets: [
          { label: "강도(Intensity %)", borderColor: "#4caf50", data: [] },
          { label: "가변성(Variability %)", borderColor: "#ff9800", data: [] },
          { label: "팬 최대(Fan Limit %)", borderColor: "#00bcd4", data: [] },
          { label: "팬 최소(Min Fan %)", borderColor: "#e91e63", data: [] }
        ]
      },
      options: {
        ...baseOptions,
        scales: {
          ...baseOptions.scales,
          y: { min: 0, max: 200 }
        }
      }
    });

    chartTurbThermSig = initChart(ctxTurbThermSig, {
      type: "line",
      data: {
        datasets: [
          {
            label: "난류 시그마(Turb Sig)",
            yAxisID: "ySig",
            borderColor: "#9c27b0",
            data: [],
            tension: 0.3
          },
          {
            label: "난류 길이 스케일(Turb Len)",
            yAxisID: "yLen",
            borderColor: "#795548",
            data: [],
            tension: 0.3
          },
          {
            label: "열기포 세기(Therm Str)",
            yAxisID: "ySig",
            borderColor: "#8bc34a",
            data: [],
            tension: 0.3,
            borderDash: [5, 5]
          },
          {
            label: "열기포 반경(Therm Rad)",
            yAxisID: "yLen",
            borderColor: "#ffc107",
            data: [],
            tension: 0.3,
            borderDash: [5, 5]
          }
        ]
      },
      options: {
        ...baseOptions,
        scales: {
          ...baseOptions.scales,
          ySig: { position: "left", min: 0, max: 5 },
          yLen: {
            position: "right",
            min: 0,
            max: 200,
            grid: { drawOnChartArea: false }
          }
        }
      }
    });

    chartEvent = initChart(ctxEvent, {
      type: "line",
      data: {
        datasets: [
          {
            label: "돌풍(Gust)",
            borderColor: "#f44336",
            data: [],
            stepped: true
          },
          {
            label: "열기포(Thermal)",
            borderColor: "#03a9f4",
            data: [],
            stepped: true
          }
        ]
      },
      options: {
        ...baseOptions,
        scales: {
          ...baseOptions.scales,
          y: { min: 0, max: 1 }
        }
      }
    });

    chartPreset = initChart(ctxPreset, {
      type: "line",
      data: {
        datasets: [
          {
            label: "Preset Index",
            borderColor: "#607d8b",
            data: [],
            stepped: true
          }
        ]
      },
      options: {
        ...baseOptions,
        scales: {
          ...baseOptions.scales,
          y: { min: 0, max: 10 }
        }
      }
    });

    chartTiming = initChart(ctxTiming, {
      type: "line",
      data: {
        datasets: [
          {
            label: "Sim Interval (ms)",
            borderColor: "#9e9e9e",
            data: [],
            tension: 0.3
          },
          {
            label: "Gust Interval (ms)",
            borderColor: "#bdbdbd",
            data: [],
            tension: 0.3
          },
          {
            label: "Thermal Interval (ms)",
            borderColor: "#e0e0e0",
            data: [],
            tension: 0.3
          }
        ]
      },
      options: {
        ...baseOptions,
        scales: {
          ...baseOptions.scales,
          y: { min: 0 }
        }
      }
    });
  }

  // ======================= WS 데이터 → 차트 반영 =======================

  function processChartRecords(recs) {
    if (!Array.isArray(recs) || recs.length === 0) return;

    const toXY = (key) =>
      recs.map((r) => ({
        x: new Date(r.t),
        y: r[key]
      }));

    // 1) 풍속 / PWM
    chartWind.data.datasets[0].data = toXY("wind");
    chartWind.data.datasets[1].data = toXY("pwm");

    // 2) 핵심 파라미터
    chartParam.data.datasets[0].data = toXY("intensity");
    chartParam.data.datasets[1].data = toXY("variability");
    chartParam.data.datasets[2].data = toXY("fanLimit");
    chartParam.data.datasets[3].data = toXY("minFan");

    // 3) 난류/열기포
    chartTurbThermSig.data.datasets[0].data = toXY("turb_sig");
    chartTurbThermSig.data.datasets[1].data = toXY("turb_len");
    chartTurbThermSig.data.datasets[2].data = toXY("therm_str");
    chartTurbThermSig.data.datasets[3].data = toXY("therm_rad");

    // 4) 이벤트 (0/1)
    chartEvent.data.datasets[0].data = toXY("gust").map((p) => ({ x: p.x, y: p.y ? 1 : 0 }));
    chartEvent.data.datasets[1].data = toXY("thermal").map((p) => ({ x: p.x, y: p.y ? 1 : 0 }));

    // 5) 프리셋 인덱스
    chartPreset.data.datasets[0].data = toXY("preset");

    // 6) 타이밍
    chartTiming.data.datasets[0].data = toXY("sim_int");
    chartTiming.data.datasets[1].data = toXY("gust_int");
    chartTiming.data.datasets[2].data = toXY("thermal_int");

    charts.forEach((c) => c.update("none"));

    const last = recs[recs.length - 1];
    if (refreshLabel && last?.t) {
      const ts = new Date(last.t).toLocaleTimeString();
      refreshLabel.textContent = `🕒 WS 업데이트: ${ts} (샘플 ${recs.length}개)`;
    }
  }

  // ======================= WebSocket 초기화 =======================

  function initWebSocket() {
    const ws = new WebSocket(getWSUrl());

    ws.onopen = () => {
      if (refreshLabel) refreshLabel.textContent = "✅ 실시간 차트 데이터 수신 중...";
      if (window.showToast) window.showToast("/ws/chart 연결 성공", "ok");
    };

    ws.onmessage = (event) => {
      if (isPaused) return;
      try {
        const data = JSON.parse(event.data);
        if (Array.isArray(data.chart)) {
          processChartRecords(data.chart);
        }
      } catch (e) {
        console.error("[ChartT2] WS 데이터 파싱 오류:", e);
        if (window.showToast) window.showToast("WS 데이터 파싱 오류", "err");
      }
    };

    ws.onclose = () => {
      if (refreshLabel)
        refreshLabel.textContent = "❌ WS 연결 끊김. 5초 후 재연결 시도...";
      if (window.showToast) window.showToast("/ws/chart 연결 끊김", "warn");
      setTimeout(initWebSocket, 5000);
    };

    ws.onerror = (e) => {
      console.error("[ChartT2] WebSocket 오류:", e);
      if (refreshLabel) refreshLabel.textContent = "⚠️ WS 오류 발생";
    };
  }

  // ======================= 이벤트 바인딩 =======================

  function bindEvents() {
    $("#btnPause")?.addEventListener("click", () => {
      isPaused = true;
      if (refreshLabel) refreshLabel.textContent = "⏸ 갱신 일시정지됨";
      if (window.showToast) window.showToast("차트 갱신 일시정지", "warn");
    });

    $("#btnResume")?.addEventListener("click", () => {
      isPaused = false;
      if (window.showToast) window.showToast("차트 갱신 재개", "ok");
    });

    $("#btnResetZoomAll")?.addEventListener("click", () => {
      charts.forEach((c) => c.resetZoom && c.resetZoom());
      if (window.showToast) window.showToast("모든 차트 줌 초기화", "ok");
    });

    // 차트 카드 접기/펼치기
    document.querySelectorAll(".chart-container").forEach((container) => {
      const header = container.querySelector(".chart-header");
      const content = container.querySelector(".chart-content");
      const btnToggle = container.querySelector(".btn-toggle");

      if (!header || !content || !btnToggle) return;

      header.addEventListener("click", (e) => {
        // 토글 버튼 자체를 클릭한 경우도 동일 동작
        if (content.style.display === "none") {
          content.style.display = "block";
          btnToggle.textContent = "▲";
        } else {
          content.style.display = "none";
          btnToggle.textContent = "▼";
        }
      });
    });
  }

  // ======================= 초기화 =======================

  document.addEventListener("DOMContentLoaded", () => {
    initCharts();
    bindEvents();
    initWebSocket();
  });
})();
