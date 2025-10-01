// SC10_main_003.js

const API_STATE_URL = '/api/state';
const API_CONFIG_URL = '/api/config';
const configForm = document.getElementById('config-form');

// 설정 슬라이더와 값 표시 영역 매핑
// HTML에 모든 슬라이더가 추가되었으므로, 이 맵은 레이블 정보로만 사용됩니다.
const sliderMap = {
    'intensity'     : '전체 풍속 강도',
    'gust_freq'     : '돌풍 빈도',
    'variability'   : '바람 변화율',
    'fan_limit'     : '최대 팬 속도',
    'min_fan'       : '최소 팬 속도',
    'turb_len'      : '난류 길이 스케일',
    'turb_sig'      : '난류 강도 시그마',
    'therm_str'     : '열 상승 기류 강도',
    'therm_rad'     : '열 상승 기류 반경'
};

// 특정 설정값에 대한 단위/접미사 정의 (필요에 따라 사용)
const unitMap = {
    'fan_limit': '', // 팬 속도는 정수 값 (0~255)
    'min_fan': ''   // 팬 속도는 정수 값 (0~255)
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
        // sim_active는 C++에서 bool -> "true" / "false" 문자열로 보내는 경우를 가정
        const isActive = data.status.sim_active === true || data.status.sim_active === "true";
        document.getElementById('status-sim-active').textContent = isActive ? '활성 (Dynamic)' : '비활성 (Steady)';
        document.getElementById('status-wind-speed').textContent = parseFloat(data.status.wind_speed).toFixed(2); // 소수점 두 자리까지 표시
        document.getElementById('status-phase-name').textContent = data.status.phase_name;
        document.getElementById('status-fan-pwm').textContent = data.status.fan_pwm;
        
        // 2. 프리셋 드롭다운 생성 및 선택
        const presetSelect = document.getElementById('preset');
        if (presetSelect.options.length === 0 && data.presets && Array.isArray(data.presets)) {
            data.presets.forEach(presetName => {
                const option = document.createElement('option');
                option.value = presetName;
                option.textContent = presetName;
                presetSelect.appendChild(option);
            });
        }
        // 현재 프리셋 선택 (data.config가 존재하는 경우에만)
        if (data.config && data.config.preset) {
            presetSelect.value = data.config.preset; 
        }

        // 3. 슬라이더(설정값) 업데이트
        for (const key in sliderMap) {
            const slider = document.getElementById(key);
            const display = document.getElementById(`${key}-value`);
            const unit = unitMap[key] || ''; // 설정된 단위 가져오기

            // DOM에 해당 요소가 없거나, 설정 데이터가 없는 경우는 건너뜁니다.
            if (!slider || !data.config || !data.config.hasOwnProperty(key)) continue; 

            const configValue = data.config[key];

            // 3-1. 설정값 반영
            // C++에서 보낸 실제 값을 슬라이더에 반영합니다.
            // HTML input[type="range"]의 min/max/step은 이 값에 맞게 설정해야 합니다.
            slider.value = configValue; 
            
            // 3-2. 실시간 값 표시 업데이트 함수 정의
            const updateDisplay = (value) => {
                // 부동 소수점 값을 보기 좋게 소수점 두 자리까지 표시 (팬 속도(정수) 제외)
                const displayValue = key === 'fan_limit' || key === 'min_fan' ? 
                                     parseInt(value) : 
                                     parseFloat(value).toFixed(2);
                display.textContent = `${displayValue}${unit}`;
            };
            
            // 초기 값 표시
            updateDisplay(configValue);

            // 3-3. 슬라이더 조작 시 실시간 값 표시 업데이트 이벤트 리스너
            slider.oninput = function() {
                updateDisplay(this.value);
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
        } else if (key === 'fan_limit' || key === 'min_fan') {
             // 팬 속도는 정수로 변환
            payload[key] = parseInt(value); 
        } else {
            // 나머지 값은 float/number로 변환 (0.0~1.0, 0.0~5.0 등)
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
            // alert(`설정 저장 성공: ${result.message}`);
            console.log(`설정 저장 성공: ${result.message}`); // 사용자 경험을 위해 alert 대신 console.log 사용
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