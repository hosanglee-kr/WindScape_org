//

// SC10_main_002.js

const API_STATE_URL = '/api/state';
const API_CONFIG_URL = '/api/config';
const configForm = document.getElementById('config-form');

// 설정 슬라이더와 값 표시 영역 매핑
const sliderMap = {
    'intensity': '전체 풍속 강도',
    'gust_freq': '돌풍 빈도',
    'variability': '바람 변화율',
    'fan_limit': '최대 팬 속도',
    'min_fan': '최소 팬 속도',
    'turb_len': '난류 길이 스케일',
    'turb_sig': '난류 강도 시그마',
    'therm_str': '열 상승 기류 강도',
    'therm_rad': '열 상승 기류 반경'
};

/**
 * @brief 현재 상태 및 설정 데이터를 API로부터 가져와 페이지를 갱신
 */
async function fetchAndRenderState() {
    try {
        const response = await fetch(API_STATE_URL);
        if (!response.ok) throw new Error('Network response was not ok');
        const data = await response.json();

        // 1. 상태 표시 업데이트
        document.getElementById('status-sim-active').textContent = data.status.sim_active ? '활성 (Dynamic)' : '비활성 (Steady)';
        document.getElementById('status-wind-speed').textContent = data.status.wind_speed;
        document.getElementById('status-phase-name').textContent = data.status.phase_name;
        document.getElementById('status-fan-pwm').textContent = data.status.fan_pwm;
        
        // 2. 프리셋 드롭다운 생성 및 선택
        const presetSelect = document.getElementById('preset');
        if (presetSelect.options.length === 0) {
            data.presets.forEach(presetName => {
                const option = document.createElement('option');
                option.value = presetName;
                option.textContent = presetName;
                presetSelect.appendChild(option);
            });
        }
        presetSelect.value = data.config.preset; // 현재 프리셋 선택

        // 3. 슬라이더(설정값) 업데이트
        for (const [key, label] of Object.entries(sliderMap)) {
            const slider = document.getElementById(key);
            const display = document.getElementById(`${key}-value`);
            
            // 아직 DOM에 없는 슬라이더는 건너뜁니다.
            if (!slider) continue; 

            // 설정값 반영
            if (data.config.hasOwnProperty(key)) {
                slider.value = data.config[key];
                display.textContent = `${data.config[key]}${key.includes('freq') || key.includes('limit') || key.includes('intensity') ? '%' : ''}`;
            }

            // 슬라이더 조작 시 실시간 값 표시 업데이트
            slider.oninput = function() {
                display.textContent = `${this.value}${key.includes('freq') || key.includes('limit') || key.includes('intensity') ? '%' : ''}`;
            };
        }

    } catch (error) {
        console.error('Error fetching state:', error);
        document.getElementById('status-sim-active').textContent = 'API 오류';
    }
}

/**
 * @brief 폼 데이터 수집 및 API로 전송
 */
async function handleSubmit(event) {
    event.preventDefault();

    const formData = new FormData(configForm);
    const payload = {};
    
    // 폼 데이터를 JSON payload로 변환
    for (const [key, value] of formData.entries()) {
        if (key === 'preset') {
            payload[key] = value; // 프리셋은 문자열
        } else {
            // 나머지 값은 float/number로 변환 (C++에서 알아서 처리)
            payload[key] = parseFloat(value); 
        }
    }

    try {
        const response = await fetch(API_CONFIG_URL, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify(payload)
        });

        const result = await response.json();
        
        if (response.ok) {
            alert(`설정 저장 성공: ${result.message}`);
            // 설정 저장 후 상태 갱신
            fetchAndRenderState();
        } else {
            alert(`설정 저장 실패: ${result.message || '서버 오류'}`);
        }

    } catch (error) {
        console.error('Error submitting config:', error);
        alert('API 통신 오류');
    }
}

// 폼 이벤트 리스너 등록
configForm.addEventListener('submit', handleSubmit);

// 초기 로드 시 1회 호출
fetchAndRenderState();

// 2초마다 상태를 갱신
setInterval(fetchAndRenderState, 2000); 

// 초기 로드 시 나머지 슬라이더 동적 추가 (Optional: HTML에 모두 넣는 것이 더 간단)
// *추가 개선: 나머지 설정 슬라이더도 HTML에 직접 추가하고 script.js에서 관리하면 코드가 더 단순해집니다.

