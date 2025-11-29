/* SC10_common_001.js */

/**
 * @file SC10_common_001.js
 * @brief 웹 UI 공통 기능 및 메뉴 로딩 처리
 * * 기능 요약:
 * 1. 온라인/오프라인 모드 판별 및 메뉴 데이터 로드.
 * 2. 로드된 데이터를 기반으로 내비게이션 메뉴를 동적으로 생성.
 * 3. 현재 페이지에 'active' 클래스 적용.
 */

// **[모드 상수]**
const MODE_ONLINE = "ONLINE";
const MODE_OFFLINE = "OFFLINE";

// **[데이터 경로]**
const API_MENU_PATH = "/api/v1/menu";
const LOCAL_MENU_PATH = "/config/pages.json"; 
const DASHBOARD_HTML_PATH = "SC10_dashboard_001.html";


let currentMode = MODE_OFFLINE; // 초기 모드는 OFFLINE으로 설정

/**
 * @brief 토스트 메시지를 화면에 표시합니다. (생략)
 * @param message 표시할 메시지
 * @param type 'success', 'error', 'info' 중 하나
 */
function showToast(message, type = 'info') {
    // 실제 토스트 구현 로직 (생략)
    console.log(`[Toast ${type.toUpperCase()}]: ${message}`);
}

/**
 * @brief 네트워크를 통해 메뉴 데이터를 가져옵니다.
 * @param path 데이터를 요청할 경로 (API 또는 로컬 JSON)
 * @param mode 현재 설정된 모드
 * @returns {Promise<Array|null>} 메뉴 데이터 배열 또는 null
 */
async function fetchMenuData(path, mode) {
    try {
        const response = await fetch(path);
        
        if (!response.ok) {
            console.error(`[MenuLoader] Failed to load menu from ${mode} path: ${path}. Status: ${response.status}`);
            return null;
        }

        const data = await response.json();
        
        if (!Array.isArray(data)) {
             console.error(`[MenuLoader] Invalid menu data format: not an array from ${mode} path.`);
             return null;
        }

        console.log(`[MenuLoader] Menu loaded successfully from ${mode} path.`);
        return data;

    } catch (error) {
        console.error(`[MenuLoader] Error fetching menu from ${mode} path:`, error);
        return null;
    }
}


/**
 * @brief 메뉴 데이터를 기반으로 내비게이션 메뉴를 동적으로 생성합니다.
 * @param menuData 메뉴 항목 배열
 */
function renderMenu(menuData) {
    const navMenu = document.getElementById('navMenu');
    if (!navMenu || !menuData || menuData.length === 0) {
        console.warn("[MenuLoader] Navigation menu element not found or menu data is empty.");
        return;
    }

    // 1. 현재 HTML 파일 이름을 추출 (예: /html/SC10_dashboard_001.html -> SC10_dashboard_001.html)
    // path.substring(1)는 경로의 첫 '/'를 제거합니다.
    const currentPath = window.location.pathname.split('/').pop() || DASHBOARD_HTML_PATH; 

    navMenu.innerHTML = ''; // 기존 메뉴 항목 초기화
    
    menuData.forEach(item => {
        const li = document.createElement('li');
        const a = document.createElement('a');
        
        // item.path는 메뉴 API에서 /html/ 접두사가 제거된 상태임 (예: dashboard.html)
        const targetPath = item.path; 
        
        a.href = `./${targetPath}`;
        a.textContent = item.label;

        // 2. 현재 페이지 경로와 메뉴 항목 경로가 일치하면 active 클래스 적용
        // 비교 시 URL의 상대 경로 형식을 사용
        if (targetPath === currentPath) {
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
    
    // 1. 온라인 모드 (API) 시도
    let menuData = await fetchMenuData(API_MENU_PATH, MODE_ONLINE);

    if (menuData) {
        currentMode = MODE_ONLINE;
        console.log(`[Mode] Set to ${currentMode}`);
    } else {
        // 2. 온라인 모드 실패 시 오프라인 모드 (로컬 JSON) 시도
        console.warn("[Mode] Online API failed. Attempting OFFLINE mode.");
        menuData = await fetchMenuData(LOCAL_MENU_PATH, MODE_OFFLINE);
        
        if (menuData) {
             currentMode = MODE_OFFLINE;
             console.log(`[Mode] Set to ${currentMode}`);
             showToast("온라인 API 응답 실패. 오프라인(로컬) 모드로 동작합니다.", 'info');
        } else {
             // 3. 모든 로딩 실패
             currentMode = MODE_OFFLINE;
             console.error("[Mode] OFFLINE mode failed. Menu cannot be loaded.");
             showToast("메뉴 로드 실패. 장치 연결 상태를 확인하세요.", 'error');
             // 기본 메뉴를 표시하지 않고 종료
             return;
        }
    }

    // 4. 메뉴 렌더링
    renderMenu(menuData);
}

// 문서 로드 완료 시 메뉴 로드 시작
document.addEventListener('DOMContentLoaded', loadMenuAndSetMode);

// 현재 모드를 다른 JS 파일에서 사용할 수 있도록 노출
// (예: if (window.currentMode === 'ONLINE') { /* API 호출 */ })
window.currentMode = currentMode;
