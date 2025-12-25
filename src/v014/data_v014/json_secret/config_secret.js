// app.js

let apiKeys = {};

/**
 * 로컬 config_secret.json 파일을 불러와서 설정값을 저장합니다.
 */
async function loadSecretConfig() {
    const configPath = './config_secret.json'; // 파일 경로
    
    try {
        // 1. fetch 요청 (웹 서버 환경 필수)
        const response = await fetch(configPath);
        
        if (!response.ok) {
            throw new Error(`파일 로드 실패: ${response.status} ${response.statusText}`);
        }
        
        // 2. JSON 객체로 파싱
        apiKeys = await response.json();
        
        console.log("✅ 설정 로드 완료.");
        
        // 3. 로드된 키 사용 (예시)
        useKeys(apiKeys);
        
    } catch (error) {
        console.error("❌ config_secret.json 로드 중 오류 발생:", error);
    }
}

function useKeys(keys) {
    const firebaseKey = keys.FIREBASE_API_KEY;
    const geminiKey = keys.GEMINI_API_KEY;
    
    // 이 시점에서 키가 브라우저 메모리에 로드되어 있습니다.
    document.getElementById('result').textContent = 
        `Firebase Key Start: ${firebaseKey.substring(0, 8)}...`;
        
    // ⚠️ 경고: 민감한 키를 브라우저에서 직접 사용하는 것은 보안 위험이 큽니다.
    // fetch(..., { headers: { 'Authorization': `Bearer ${geminiKey}` } });
}

// 애플리케이션 시작
loadSecretConfig();
