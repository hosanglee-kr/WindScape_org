/* P000_common_002.js */

/**
 * @file P000_common_002.js
 * @brief 웹 UI 공통 기능 및 메뉴 로딩 처리
 * * * 기능 요약:
 * 1. 온라인/오프라인 모드 판별 및 메뉴 데이터 로드 (JSON 구조 변경 반영).
 * 2. 로드된 'pages' 데이터를 기반으로 내비게이션 메뉴를 동적으로 생성.
 * 3. 현재 페이지에 'active' 클래스 적용.
 * 4. 현재 동작 모드 (ONLINE/OFFLINE)를 전역에 노출.
 */

// **[모드 상수]**
const MODE_ONLINE = "ONLINE";
const MODE_OFFLINE = "OFFLINE";

// **[데이터 경로]**
// C++ 백엔드의 W10_getMenuJson 함수가 반환하는 메뉴 배열
const API_MENU_PATH = "/api/v1/menu"; 
// LittleFS에 저장된 전체 JSON 객체
const LOCAL_JSON_PATH = "/config/pages.json"; 


let currentMode = MODE_OFFLINE; // 초기 모드는 OFFLINE으로 설정
window.currentMode = currentMode; // 전역 노출

/**
 * @brief 토스트 메시지를 화면에 표시합니다. (생략)
 */
function showToast(message, type = 'info') {
    // 실제 토스트 구현 로직 (생략)
    const toastContainer = document.getElementById('toastContainer');
    if (toastContainer) {
        const toast = document.createElement('div');
        toast.className = `toast toast-${type}`;
        toast.textContent = message;
        toastContainer.appendChild(toast);
        setTimeout(() => toast.remove(), 4000);
    }
    console.log(`[Toast ${type.toUpperCase()}] (${currentMode}): ${message}`);
}

/**
 * @brief 네트워크를 통해 데이터를 가져옵니다.
 * @param path 데이터를 요청할 경로
 * @param mode 현재 설정된 모드
 * @returns {Promise<any|null>} 데이터 또는 null
 */
async function fetchData(path, mode) {
    try {
        const response = await fetch(path);
        
        if (!response.ok) {
            console.error(`[MenuLoader] Failed to load data from ${mode} path: ${path}. Status: ${response.status}`);
            return null;
        }

        const data = await response.json();
        
        if (!data) {
             console.error(`[MenuLoader] Empty data received from ${mode} path.`);
             return null;
        }
        
        console.log(`[MenuLoader] Data loaded successfully from ${mode} path.`);
        return data; 

    } catch (error) {
        console.error(`[MenuLoader] Error fetching data from ${mode} path:`, error);
        return null;
    }
}


/**
 * @brief 메뉴 데이터를 기반으로 내비게이션 메뉴를 동적으로 생성합니다.
 * @param pagesArray 메뉴 항목 배열 (W10_getMenuJson에서 반환하는 형태와 동일)
 */
function renderMenu(pagesArray) {
    const navMenu = document.getElementById('navMenu');
    if (!navMenu || !pagesArray || pagesArray.length === 0) {
        console.warn("[MenuLoader] Navigation menu element not found or pages data is empty.");
        return;
    }

    // 1. 현재 HTML 파일 이름을 추출 (예: /html/SC10_dashboard_001.html -> SC10_dashboard_001.html)
    const currentPath = window.location.pathname.split('/').pop(); 
    // root("/") 요청 시 파일명은 빈 문자열이므로, SC10_main_019.html을 기본 메인 파일로 가정
    const currentFile = currentPath.length > 0 ? currentPath : "SC10_main_019.html"; 

    navMenu.innerHTML = ''; // 기존 메뉴 항목 초기화
    
    pagesArray.forEach(item => {
        // 백엔드 API에서 isMain 항목은 이미 제외됨.
        
        const li = document.createElement('li');
        const a = document.createElement('a');
        
        // item.path는 백엔드에서 "/html/" 접두사가 제거된 상태임 (예: SC10_dashboard_001.html)
        const targetPath = item.path; 
        
        a.href = `./${targetPath}`;
        a.textContent = item.label;

        // 2. 현재 페이지 경로와 메뉴 항목 경로가 일치하면 active 클래스 적용
        if (targetPath === currentFile) {
            a.classList.add('active');
        }

        li.appendChild(a);
        navMenu.appendChild(li);
    });
}


/**
 * @brief 온라인/오프라인 모드를 판별하고 메뉴를 로드하는 메인 함수입니다.
 */
async function loadMenuAndSetMode() {
    
    let pagesData = null; // 렌더링에 사용할 최종 메뉴 배열

    // 1. 온라인 모드 (API) 시도
    let onlineData = await fetchData(API_MENU_PATH, MODE_ONLINE);

    if (onlineData && Array.isArray(onlineData)) {
        // API는 메뉴에 필요한 정렬된 배열을 직접 반환해야 함
        currentMode = MODE_ONLINE;
        pagesData = onlineData;
        console.log(`[Mode] Set to ${currentMode}`);
        
    } else {
        // 2. 온라인 모드 실패 시 오프라인 모드 (로컬 JSON) 시도
        console.warn("[Mode] Online API failed or invalid. Attempting OFFLINE mode.");
        let offlineData = await fetchData(LOCAL_JSON_PATH, MODE_OFFLINE);
        
        if (offlineData && Array.isArray(offlineData.pages)) {
             currentMode = MODE_OFFLINE;
             // JSON 객체에서 'pages' 배열만 추출
             pagesData = offlineData.pages.filter(item => !(item.isMain)); // isMain은 수동으로 제외
             console.log(`[Mode] Set to ${currentMode}`);
             showToast("온라인 API 응답 실패. 오프라인(로컬) 모드로 동작합니다.", 'info');
        } else {
             // 3. 모든 로딩 실패
             currentMode = MODE_OFFLINE;
             console.error("[Mode] OFFLINE mode failed. Menu cannot be loaded.");
             showToast("메뉴 로드 실패. 장치 연결 상태를 확인하세요.", 'error');
             return;
        }
    }

    // 최종 모드 및 데이터 반영
    window.currentMode = currentMode;
    
    // 4. 메뉴 렌더링
    renderMenu(pagesData);
}

// 문서 로드 완료 시 메뉴 로드 시작
document.addEventListener('DOMContentLoaded', loadMenuAndSetMode);
