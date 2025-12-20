
/**
 * ------------------------------------------------------
 * 소스명 : P000_common_006.js
 * 모듈명 : Smart Nature Wind UI 공통 스크립트 (v003+patched)
 * ------------------------------------------------------
 * 기능 요약:
 * 1. ONLINE / OFFLINE 모드 판별 및 페이지/메뉴 데이터 로드
 * 2. cfg_pages_030.json 구조 기반 동적 내비게이션 구성
 * 3. pages[].enable=false 페이지 자동 제외
 * 4. isMain = true 페이지는 메뉴에서 숨김
 * 5. 로고 링크는 isMain 페이지의 path 또는 uri로 자동 설정
 * 6. OFFLINE 모드는 path의 파일명을 기준으로 상대 경로를 구성
 * 7. 현재 페이지 active 자동 적용
 * 8. window.currentMode 노출
 * ------------------------------------------------------
 */

// -------------------------------
//  모드 상수
// -------------------------------
const G_MODE_ONLINE = "ONLINE";
const G_MODE_OFFLINE = "OFFLINE";

// -------------------------------
//  데이터 경로
// -------------------------------
const G_API_MENU_PATH = "/api/v1/menu";          // ONLINE
const G_LOCAL_JSON_PATH = "../json/cfg_pages_032.json"; // OFFLINE

let g_currentMode = G_MODE_OFFLINE;
window.currentMode = g_currentMode;

/**
 * ------------------------------------------------------
 * 토스트 메시지
 * ------------------------------------------------------
 */
function showToast(message, type = "info") {
    const toastContainer = document.getElementById("toastContainer");
    if (toastContainer) {
        const toast = document.createElement("div");
        const mapped =
            type === "ok" || type === "warn" || type === "err" ? type : "info";
        toast.className = "toast" + (mapped !== "info" ? " " + mapped : "");
        toast.textContent = message;
        toastContainer.appendChild(toast);
        setTimeout(() => toast.remove(), 4000);
    }
    console.log(`[Toast ${type.toUpperCase()}] (${g_currentMode}): ${message}`);
}

/**
 * ------------------------------------------------------
 * fetch 기반 JSON 로딩
 * ------------------------------------------------------
 */
async function fetchData(path, mode) {
    try {
        const response = await fetch(path);

        if (!response.ok) {
            console.error(
                `[MenuLoader] Failed to load data from ${mode} path: ${path}. Status: ${response.status}`
            );
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
 * ------------------------------------------------------
 * API/JSON에서 pages 배열 추출
 * ------------------------------------------------------
 */
function extractPagesArray(rawData) {
    if (!rawData) return null;

    if (Array.isArray(rawData)) {
        // API에서 배열 자체를 보내는 경우
        return rawData;
    }

    // 로컬 cfg_pages_030.json 구조: { pages: [...], assets: [...] }
    if (Array.isArray(rawData.pages)) {
        return rawData.pages;
    }

    console.warn("[MenuLoader] No pages array found in data.");
    return null;
}

/**
 * ------------------------------------------------------
 * 로고 링크 설정
 * ------------------------------------------------------
 */
function setLogoLink(pagesArray) {
    const logoLink = document.querySelector(".nav-logo");
    if (!logoLink || !Array.isArray(pagesArray)) return;

    const mainPageItem = pagesArray.find((item) => item.isMain === true);

    if (mainPageItem) {
        if (window.currentMode === G_MODE_OFFLINE) {
            // OFFLINE ⇒ path에서 파일명만 추출
            const targetFile = (mainPageItem.path || "").split("/").pop();
            const href = "./" + targetFile;
            logoLink.setAttribute("href", href);
            console.log(`[Logo] Logo link set to: ${href} (OFFLINE)`);
        } else {
            // ONLINE ⇒ uri 또는 path 사용
            const href = mainPageItem.uri || mainPageItem.path || "/";
            logoLink.setAttribute("href", href);
            console.log(`[Logo] Logo link set to: ${href} (ONLINE)`);
        }
    } else {
        console.warn("[Logo] Main page not found (isMain:true)");
    }
}

/**
 * ------------------------------------------------------
 * 메뉴 렌더링
 * ------------------------------------------------------
 */
function renderMenu(pagesArray) {
    const navMenu = document.getElementById("navMenu");
    if (!navMenu || !Array.isArray(pagesArray) || pagesArray.length === 0) {
        console.warn("[MenuLoader] Navigation menu element not found or pages empty.");
        return;
    }

    const currentPath = (window.location.pathname || "/").split("?")[0];
    const currentFile = currentPath.split("/").pop() || "";

    navMenu.innerHTML = "";

    pagesArray
        // 1. enable 필터 (메뉴 비활성 페이지 제거)
        .filter((item) => item.enable !== false)
        // 2. isMain=true는 메뉴에서 숨김
        .filter((item) => !item.isMain)
        // 3. order 기준 정렬
        .sort((a, b) => {
            const oa = typeof a.order === "number" ? a.order : 0;
            const ob = typeof b.order === "number" ? b.order : 0;
            return oa - ob;
        })
        // 4. 메뉴 항목 생성
        .forEach((item) => {
            const li = document.createElement("li");
            const a = document.createElement("a");

            let href = "#";
            let activeTarget = "";

            if (window.currentMode === G_MODE_OFFLINE) {
                const targetFile = (item.path || "").split("/").pop();
                href = "./" + targetFile;
                activeTarget = targetFile; // ACTIVE 판단 기준
            } else {
                href = item.uri || item.path || "#";
                activeTarget = href.split("?")[0];
            }

            a.href = href;
            a.textContent = item.label || item.path || "(no label)";

            // ACTIVE 클래스 적용
            if (window.currentMode === G_MODE_OFFLINE) {
                if (activeTarget === currentFile) {
                    a.classList.add("active");
                }
            } else {
                const candidates = [item.uri, item.path]
                    .filter(Boolean)
                    .map((p) => p.split("?")[0]);

                if (candidates.includes(currentPath)) {
                    a.classList.add("active");
                }
            }

            li.appendChild(a);
            navMenu.appendChild(li);
        });
}

/**
 * ------------------------------------------------------
 * ONLINE / OFFLINE 모드 판별 및 메뉴 로딩
 * ------------------------------------------------------
 */
async function loadMenuAndSetMode() {
    let pagesData = null;

    // 1) ONLINE 모드 우선시
    let onlineRaw = await fetchData(G_API_MENU_PATH, G_MODE_ONLINE);
    let onlinePages = extractPagesArray(onlineRaw);

    if (onlinePages && onlinePages.length > 0) {
        g_currentMode = G_MODE_ONLINE;
        window.currentMode = g_currentMode;
        pagesData = onlinePages;
        console.log(`[Mode] ONLINE`);
    } else {
        // 2) 실패 시 OFFLINE
        console.warn("[Mode] Online failed → OFFLINE fallback.");
        let offlineRaw = await fetchData(G_LOCAL_JSON_PATH, G_MODE_OFFLINE);
        let offlinePages = extractPagesArray(offlineRaw);

        if (offlinePages && offlinePages.length > 0) {
            g_currentMode = G_MODE_OFFLINE;
            window.currentMode = g_currentMode;
            pagesData = offlinePages;
            console.log(`[Mode] OFFLINE`);
            showToast("오프라인 모드로 동작합니다.", "info");
        } else {
            console.error("[Mode] Both ONLINE and OFFLINE menu load failed.");
            showToast("메뉴 로드 실패! 장치 연결 또는 cfg_pages_030.json 확인 필요.", "err");
            return;
        }
    }

    // 로고 링크 설정
    setLogoLink(pagesData);

    // 메뉴 렌더링
    renderMenu(pagesData);
}

// DOM 로드 후 실행
document.addEventListener("DOMContentLoaded", loadMenuAndSetMode);

