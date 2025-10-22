// SC10_main_003.js


const API_STATE_URL = '/api/state';
const API_CONFIG_URL = '/api/config';
const configForm = document.getElementById('config-form');

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

const unitMap = {
    'fan_limit': '',
    'min_fan': ''
};

const MAX_STA_CONFIGS = 5;

/**
 * @brief 비밀번호 입력 필드의 타입(password <-> text)을 토글하여 비밀번호를 보이거나 숨깁니다.
 */
function setupPasswordToggle() {
    const toggleButtons = document.querySelectorAll('.toggle-password');
    
    toggleButtons.forEach(button => {
        button.addEventListener('click', function() {
            const targetId = this.getAttribute('data-target');
            const passwordInput = document.getElementById(targetId);
            
            if (!passwordInput) return;

            // 타입 토글
            const type = passwordInput.getAttribute('type') === 'password' ? 'text' : 'password';
            passwordInput.setAttribute('type', type);
            
            // 버튼 텍스트(이모지) 토글
            this.textContent = (type === 'text' ? '🔒' : '👀');
        });
    });
}

/**
 * @brief 현재 상태 및 설정 데이터를 API로부터 가져와 페이지를 갱신
 */
async function fetchAndRenderState() {
    try {
        const response = await fetch(API_STATE_URL);
        if (!response.ok) throw new Error('Network response was not ok');
        const data = await response.json();

        // 1. 상태 표시 업데이트 (기존과 동일)
        const isActive = data.status.sim_active === true || data.status.sim_active === "true";
        document.getElementById('status-sim-active').textContent = isActive ? '활성 (Dynamic)' : '비활성 (Steady)';
        document.getElementById('status-wind-speed').textContent = data.status.wind_speed ? parseFloat(data.status.wind_speed).toFixed(2) : 'N/A';
        document.getElementById('status-phase-name').textContent = data.status.phase_name || 'N/A';
        document.getElementById('status-fan-pwm').textContent = data.status.fan_pwm || 'N/A';
        
        if (data.status && data.status.wifi) {
            document.getElementById('status-wifi-status').textContent = data.status.wifi.status || '연결 정보 없음';
            document.getElementById('status-wifi-ssid').textContent = data.status.wifi.ssid || 'N/A';
        }

        // 2. 프리셋 드롭다운 생성 및 선택 (기존과 동일)
        const presetSelect = document.getElementById('preset');
        if (presetSelect.options.length === 0 && data.presets && Array.isArray(data.presets)) {
            data.presets.forEach(presetName => {
                const option = document.createElement('option');
                option.value = presetName;
                option.textContent = presetName;
                presetSelect.appendChild(option);
            });
        }
        if (data.config && data.config.preset) {
            presetSelect.value = data.config.preset; 
        }

        // 3. Wi-Fi 설정값 로딩
        if (data.config && data.config.wifi) {
            const wifiConfig = data.config.wifi;

            // 3-1. Wi-Fi 모드 및 AP 설정
            document.getElementById('wifi_mode').value = wifiConfig.mode || 'STA';
            document.getElementById('ap_ssid').value = wifiConfig.ap_ssid || '';
            document.getElementById('ap_password').value = ''; // 비밀번호는 로딩 시 항상 비움

            // 3-2. STA 설정 (최대 5개)
            if (wifiConfig.sta_configs && Array.isArray(wifiConfig.sta_configs)) {
                for (let i = 1; i <= MAX_STA_CONFIGS; i++) {
                    const staData = wifiConfig.sta_configs[i - 1] || {};
                    
                    const ssidElement = document.getElementById(`sta_ssid_${i}`);
                    const passElement = document.getElementById(`sta_pass_${i}`);
                    
                    if (ssidElement) ssidElement.value = staData.ssid || '';
                    if (passElement) passElement.value = ''; // 비밀번호는 로딩 시 항상 비움
                }
            }
        }

        // 4. 슬라이더(설정값) 업데이트 (기존과 동일)
        for (const key in sliderMap) {
            const slider = document.getElementById(key);
            const display = document.getElementById(`${key}-value`);
            const unit = unitMap[key] || '';

            if (!slider || !data.config || !data.config.hasOwnProperty(key)) continue; 

            const configValue = data.config[key];

            const updateDisplay = (value) => {
                const displayValue = key === 'fan_limit' || key === 'min_fan' ? 
                                     parseInt(value) : 
                                     parseFloat(value).toFixed(2);
                display.textContent = `${displayValue}${unit}`;
            };
            
            slider.value = configValue; 
            updateDisplay(configValue);

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
 * @brief 폼 데이터 수집 및 API로 전송 (기존과 동일)
 */
async function handleSubmit(event) {
    event.preventDefault();

    const formData = new FormData(configForm);
    const payload = {
        wifi: {
            sta_configs: []
        } 
    };
    
    // 폼 데이터를 JSON payload로 변환
    for (const [key, value] of formData.entries()) {
        if (key === 'preset') {
            payload[key] = value;
        } else if (key === 'wifi_mode' || key === 'ap_ssid' || key === 'ap_password') {
            const wifiKey = key.substring(5);
            payload.wifi[wifiKey] = value;
        } else if (key.startsWith('sta_ssid_') || key.startsWith('sta_pass_')) {
            const parts = key.split('_');
            const type = parts[1];
            const index = parseInt(parts[2]) - 1;

            if (!payload.wifi.sta_configs[index]) {
                payload.wifi.sta_configs[index] = {};
            }
            if (type === 'ssid') {
                payload.wifi.sta_configs[index].ssid = value;
            } else if (type === 'pass') {
                if (value) {
                    payload.wifi.sta_configs[index].password = value;
                }
            }
        } else if (key === 'fan_limit' || key === 'min_fan') {
            payload[key] = parseInt(value); 
        } else {
            payload[key] = parseFloat(value); 
        }
    }

    // 최종적으로 STA 설정 배열을 정리합니다.
    for (let i = 0; i < MAX_STA_CONFIGS; i++) {
        if (!payload.wifi.sta_configs[i]) {
            payload.wifi.sta_configs[i] = { ssid: "", password: "" };
        }
        if (!payload.wifi.sta_configs[i].ssid) {
            delete payload.wifi.sta_configs[i].password; 
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
            console.log(`설정 저장 성공: ${result.message}`);
            alert(`설정 저장 성공: ${result.message}\n(Wi-Fi 설정이 변경된 경우 장치가 재시작될 수 있습니다.)`);
            // 비밀번호 필드는 전송 후 비웁니다.
            for (let i = 1; i <= MAX_STA_CONFIGS; i++) {
                document.getElementById(`sta_pass_${i}`).value = '';
            }
            document.getElementById('ap_password').value = '';
            
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

// 비밀번호 토글 기능 설정
setupPasswordToggle();

// 2초마다 상태를 갱신
setInterval(fetchAndRenderState, 2000);