/*
 * ------------------------------------------------------
 * 소스명 : SC10_chart_t2_007.js
 * 모듈명 : Smart Nature Wind Chart Monitor Controller (v007)
 * ------------------------------------------------------
 * 기능 요약:
 * - 🎯 /ws/chart WebSocket 연결 및 6개 차트 실시간 갱신
 * - Chart.js 및 Zoom Plugin 초기화
 * - 차트 데이터 일시정지/재개/줌 초기화 기능
 * - 설정 관련 기능(프리셋, 저장 버튼 핸들러) 모두 제거
 * ------------------------------------------------------
 */

(() => {
    "use strict";

    // ======================= 1. 공통 헬퍼 함수 및 변수 =======================
    // SC10_dashboard_001.js에 정의된 공통 함수들을 사용한다고 가정합니다.
    // 여기서는 Chart 페이지에 필요한 핵심 헬퍼만 재정의합니다.
    
    const $ = (s, r = document) => r.querySelector(s);
    const getWSHost = (path) => `ws://${window.location.host}${path}`;

    // ************* 공통 기능 대체 (SC10_common_001.js에 있어야 함) *************
    const showToast = (msg, type = "ok") => { console.log(`[TOAST] ${type}: ${msg}`); };
    // *************************************************************************

    let isPaused = false;
    let chartInstances = {};
    let wsChart;

    // ======================= 2. 차트 데이터 구조 정의 =======================
    const MAX_DATA_POINTS = 300; // 차트에 표시할 최대 데이터 개수

    const dataStructure = {
        chartWind: {
            labels: [], datasets: [
                { label: '풍속 (m/s)', data: [], borderColor: 'rgb(54, 162, 235)', yAxisID: 'y' },
                { label: 'PWM Duty (%)', data: [], borderColor: 'rgb(255, 99, 132)', yAxisID: 'y1' }
            ]
        },
        chartParams: {
            labels: [], datasets: [
                { label: '강도 (%)', data: [], borderColor: 'rgb(75, 192, 192)', yAxisID: 'y' },
                { label: '가변성 (%)', data: [], borderColor: 'rgb(153, 102, 255)', yAxisID: 'y' },
                { label: '팬 최대 (%)', data: [], borderColor: 'rgb(255, 159, 64)', yAxisID: 'y' },
            ]
        },
        chartTurbThermSig: {
            labels: [], datasets: [
                { label: '난류 시그마', data: [], borderColor: 'rgb(255, 205, 86)', yAxisID: 'y' },
                { label: '난류 길이', data: [], borderColor: 'rgb(201, 203, 207)', yAxisID: 'y1' },
                { label: '열기포 세기', data: [], borderColor: 'rgb(40, 153, 102)', yAxisID: 'y' },
            ]
        },
        chartEvents: {
            labels: [], datasets: [
                { label: '돌풍 이벤트', data: [], borderColor: 'rgb(192, 75, 192)', type: 'line', pointRadius: 3, fill: false },
                { label: '열기포 이벤트', data: [], borderColor: 'rgb(54, 162, 235)', type: 'line', pointRadius: 3, fill: false },
            ]
        },
        chartPreset: {
            labels: [], datasets: [
                { label: '프리셋 인덱스', data: [], borderColor: 'rgb(255, 99, 132)', stepped: true }
            ]
        },
        chartTiming: {
            labels: [], datasets: [
                { label: 'Sim Interval (ms)', data: [], borderColor: 'rgb(75, 192, 192)', stepped: true },
                { label: 'Gust Interval (ms)', data: [], borderColor: 'rgb(153, 102, 255)', stepped: true },
                { label: 'Thermal Interval (ms)', data: [], borderColor: 'rgb(255, 159, 64)', stepped: true },
            ]
        }
    };


    // ======================= 3. Chart.js 옵션 및 초기화 =======================

    // 공통 차트 옵션 (타임스케일 및 줌 플러그인)
    const commonOptions = {
        responsive: true,
        maintainAspectRatio: false,
        animation: false,
        scales: {
            x: {
                type: 'time',
                time: { 
                    unit: 'second',
                    tooltipFormat: 'yyyy-MM-dd HH:mm:ss.SSS'
                },
                
                ticks: { source: 'auto' }
            },
        },
        plugins: {
            legend: { display: true },
            zoom: {
                zoom: {
                    wheel: { enabled: true },
                    pinch: { enabled: true },
                    mode: 'x',
                },
                pan: { enabled: true, mode: 'x' }
            }
        }
    };

    function initChart(id, data, options) {
        const ctx = $(`#${id}`).getContext('2d');
        const instance = new Chart(ctx, {
            type: 'line',
            data: data,
            options: Chart.helpers.merge(commonOptions, options)
        });
        chartInstances[id] = instance;
    }

    function initAllCharts() {
        initChart('chartWind', dataStructure.chartWind, {
            scales: {
                y: { beginAtZero: true, title: { display: true, text: '풍속 (m/s)' } },
                y1: { beginAtZero: true, position: 'right', grid: { drawOnChartArea: false }, title: { display: true, text: 'PWM (%)' } }
            }
        });
        
        initChart('chartParams', dataStructure.chartParams, {
            scales: { y: { beginAtZero: true, max: 100, title: { display: true, text: '백분율 (%)' } } }
        });

        initChart('chartTurbThermSig', dataStructure.chartTurbThermSig, {
            scales: {
                y: { beginAtZero: true, title: { display: true, text: '시그마 / 세기' } },
                y1: { beginAtZero: true, position: 'right', grid: { drawOnChartArea: false }, title: { display: true, text: '길이' } }
            }
        });
        
        initChart('chartEvents', dataStructure.chartEvents, {
            scales: { y: { min: 0, max: 1, title: { display: true, text: '발생 여부 (0/1)' } } }
        });
        
        initChart('chartPreset', dataStructure.chartPreset, {
             scales: { y: { title: { display: true, text: '프리셋 코드' } } }
        });

        initChart('chartTiming', dataStructure.chartTiming, {
            scales: { y: { title: { display: true, text: '간격 (ms)' } } }
        });

    }


    // ======================= 4. WebSocket 데이터 처리 =======================

    function handleWsChartData(data) {
        if (isPaused) return;

        const time = new Date(data.time_ms).getTime(); 

        // 1. Wind / PWM
        addData('chartWind', time, [
            data.wind_speed,
            data.pwm_duty
        ]);

        // 2. Params
        addData('chartParams', time, [
            data.params.wind_intensity,
            data.params.wind_variability,
            data.params.fan_limit
        ]);

        // 3. Turbulence / Thermal Sig/Len
        addData('chartTurbThermSig', time, [
            data.params.turbulence_intensity_sigma,
            data.params.turbulence_length_scale,
            data.params.thermal_bubble_strength
        ]);

        // 4. Events
        addData('chartEvents', time, [
            data.event_flags.gust ? 1 : 0,
            data.event_flags.thermal ? 1 : 0
        ]);

        // 5. Preset Index (code)
        addData('chartPreset', time, [
            data.preset_code // Y축 값으로 사용
        ]);

        // 6. Timing
        addData('chartTiming', time, [
            data.timing.sim_interval,
            data.timing.gust_interval,
            data.timing.thermal_interval
        ]);

        // 모든 차트 갱신
        Object.values(chartInstances).forEach(chart => chart.update('quiet'));
    }

    // 단일 차트 데이터 추가 로직
    function addData(chartId, time, values) {
        const chartData = dataStructure[chartId];

        // 레이블 추가/제한
        chartData.labels.push(time);
        if (chartData.labels.length > MAX_DATA_POINTS) {
            chartData.labels.shift();
        }

        // 데이터셋에 값 추가/제한
        chartData.datasets.forEach((dataset, index) => {
            dataset.data.push({ x: time, y: values[index] });
            if (dataset.data.length > MAX_DATA_POINTS) {
                dataset.data.shift();
            }
        });
    }

    // ======================= 5. WebSocket 초기화 =======================

    function initWebSocket() {
        const wsUrl = getWSHost("/ws/chart");
        wsChart = new WebSocket(wsUrl);

        wsChart.onopen = () => {
            $("#refreshInfo").textContent = "🟢 실시간 연결됨";
            showToast("차트 데이터 연결 성공", "ok");
        };

        wsChart.onmessage = (event) => {
            try {
                const data = JSON.parse(event.data);
                handleWsChartData(data);
                $("#refreshInfo").textContent = `🕒 ${new Date().toLocaleTimeString()} 업데이트`;
            } catch (e) {
                console.error("WS Chart Data Error:", e);
            }
        };

        wsChart.onclose = () => {
            $("#refreshInfo").textContent = "🔴 연결 끊김 (5초 후 재시도)";
            showToast("차트 데이터 연결 끊김", "warn");
            setTimeout(initWebSocket, 5000); // 5초 후 재시도
        };

        wsChart.onerror = (e) => {
            console.error("WS Chart Error:", e);
            $("#refreshInfo").textContent = "❌ 연결 오류";
        };
    }


    // ======================= 6. 이벤트 바인딩 =======================

    function bindEvents() {
        // 일시정지 버튼
        $("#btnPause")?.addEventListener('click', () => {
            isPaused = true;
            $("#refreshInfo").textContent = "⏸ 일시정지됨";
            showToast("차트 갱신 일시정지", "warn");
        });

        // 재개 버튼
        $("#btnResume")?.addEventListener('click', () => {
            isPaused = false;
            showToast("차트 갱신 재개", "ok");
        });

        // 줌 초기화 버튼
        $("#btnResetZoomAll")?.addEventListener('click', () => {
            Object.values(chartInstances).forEach(chart => chart.resetZoom());
            showToast("모든 차트 줌 초기화", "ok");
        });
        
        // 차트 토글 (헤더 클릭 시 내용 숨기기/보이기)
        document.querySelectorAll('.chart-header').forEach(header => {
            header.addEventListener('click', (e) => {
                // 버튼 자체 클릭 시 토글 방지
                if (e.target.classList.contains('btn-toggle')) return;
                
                const container = header.closest('.chart-container');
                const content = container.querySelector('.chart-content');
                const toggleBtn = container.querySelector('.btn-toggle');
                
                if (content.style.display === 'none') {
                    content.style.display = 'block';
                    toggleBtn.textContent = '▲';
                } else {
                    content.style.display = 'none';
                    toggleBtn.textContent = '▼';
                }
            });
        });
    }


    // ======================= 7. 초기화 =======================

    document.addEventListener("DOMContentLoaded", () => {
        // DOM 로드 후 차트 초기화
        initAllCharts();
        // 이벤트 바인딩
        bindEvents();
        // 웹소켓 연결 시작
        initWebSocket();
    });

})();
