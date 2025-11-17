// main.js

let globalSettings = {};

/**
 * config.json 파일을 불러와서 설정값을 전역 변수에 저장하는 함수
 */
async function loadSettings() {
    try {
        // 1. fetch를 사용하여 JSON 파일을 비동기적으로 요청합니다.
        const response = await fetch('./config_secret.json');
        
        // HTTP 응답 상태가 성공(200-299)인지 확인합니다.
        if (!response.ok) {
            throw new Error(`파일 로드 실패! 상태 코드: ${response.status}`);
        }
        
        // 2. 응답 본문(Body)을 JSON 객체로 파싱합니다.
        globalSettings = await response.json();
        
        console.log("✅ 설정 로드 완료:", globalSettings);
        
        // 3. 설정값을 사용하는 애플리케이션의 핵심 로직을 호출합니다.
        applySettings(globalSettings);
        
    } catch (error) {
        console.error("❌ 설정 파일 로드 중 오류 발생:", error);
        document.getElementById('status').textContent = "설정값 로드 실패!";
    }
}

function applySettings(settings) {
    // 로드된 설정값을 사용합니다.
    const url = settings.service_url;
    const theme = settings.theme;
    
    document.getElementById('status').textContent = `로드된 테마: ${theme}`;
    console.log(`사용할 서비스 URL: ${url}`);
    
    // ... 이후 애플리케이션 초기화 및 실행 로직 ...
}

// 애플리케이션 시작 (가장 먼저 실행)
loadSettings();
