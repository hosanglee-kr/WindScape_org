/* P000_common_004.js */
/**
 * ------------------------------------------------------
 * 소스명 : P000_common_004.js
 * 모듈명 : Smart Nature Wind UI 공통 스크립트 (v003)
 * ------------------------------------------------------
 * 기능 요약:
 * 1. ONLINE / OFFLINE 모드 판별 및 메뉴 데이터 로드
 * 2. cfg_pages_029.json 구조(pages[], assets[]) 기반 동적 내비게이션 렌더링
 * 3. 현재 페이지에 active 클래스 적용
 * 4. 현재 동작 모드(MODE_ONLINE / MODE_OFFLINE)를 window.currentMode로 노출
 * ------------------------------------------------------
 */

// 모드 상수
const MODE_ONLINE = "ONLINE";
const MODE_OFFLINE = "OFFLINE";

// 데이터 경로
// - ONLINE: C++ 백엔드 W10_getMenuJson() → /api/v1/menu
// - OFFLINE: LittleFS cfg_pages_029.json → /config/cfg_pages_029.json
const API_MENU_PATH = "/api/v1/menu";
const LOCAL_JSON_PATH = "../json/cfg_pages_030.json";

let currentMode = MODE_OFFLINE;
window.currentMode = currentMode;

/**
 * @brief 토스트 메시지를 화면에 표시합니다.
 * @param {string} message 
 * @param {'info'|'ok'|'warn'|'err'} type 
 */
function showToast(message, type = "info") {
	const toastContainer = document.getElementById("toastContainer");
	if (toastContainer) {
		const toast = document.createElement("div");
		// CSS: .toast, .toast.ok, .toast.warn, .toast.err 사용
		const mapped =
			type === "ok" || type === "warn" || type === "err" ? type : "info";
		toast.className = "toast" + (mapped !== "info" ? " " + mapped : "");
		toast.textContent = message;
		toastContainer.appendChild(toast);
		setTimeout(() => toast.remove(), 4000);
	}
	console.log(`[Toast ${type.toUpperCase()}] (${currentMode}): ${message}`);
}

/**
 * @brief 네트워크를 통해 JSON 데이터를 가져옵니다.
 * @param {string} path 
 * @param {string} mode 디버깅용 모드 표시
 * @returns {Promise<any|null>}
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
 * @brief cfg_pages_029.json / API 응답에서 pages 배열만 추출합니다.
 * @param {any} rawData API 또는 로컬 JSON 파싱 결과
 * @returns {Array|null}
 */
function extractPagesArray(rawData) {
	if (!rawData) return null;

	// 1) API가 "정렬된 메뉴 배열"만 직접 주는 경우
	if (Array.isArray(rawData)) {
		return rawData;
	}

	// 2) 로컬 cfg_pages_029.json 처럼 { pages: [...], assets: [...] } 인 경우
	if (Array.isArray(rawData.pages)) {
		return rawData.pages;
	}

	console.warn("[MenuLoader] No pages array found in data.");
	return null;
}

// --------------------------------------------------
// 🚨 추가된 기능: 로고 링크 설정
// --------------------------------------------------
/**
 * @brief nav-logo의 href를 isMain:true인 페이지 경로로 설정합니다.
 * @param {Array} pagesArray 
 */
function setLogoLink(pagesArray) {
  const logoLink = document.querySelector(".nav-logo");
  if (!logoLink || !Array.isArray(pagesArray)) return;

  // isMain: true인 첫 페이지 찾기
  const mainPageItem = pagesArray.find((item) => item.isMain === true);

  if (mainPageItem) {
    const href = mainPageItem.uri || mainPageItem.path || "/";
    logoLink.setAttribute("href", href);
    console.log(`[Logo] Logo link set to: ${href}`);
  } else {
    console.warn("[Logo] Main page item (isMain:true) not found. Keeping default href.");
  }
}
/*
function setLogoLink(pagesArray) {
    const logoLink = document.querySelector(".nav-logo");
    if (!logoLink || !pagesArray) return;

    // isMain: true인 항목을 찾습니다.
    const mainPageItem = pagesArray.find(item => item.isMain === true);

    if (mainPageItem && mainPageItem.path) {
        const newPath = mainPageItem.path;
        logoLink.href = newPath;
        console.log(`[Logo] Logo link set to: ${newPath}`);
    } else {
        console.warn("[Logo] Main page item (isMain: true) not found or path is missing. Using default link.");
    }
}
*/

/**
 * @brief 메뉴 데이터를 기반으로 내비게이션 메뉴를 생성합니다.
 * @param {Array} pagesArray cfg_pages_029.json의 pages 항목과 동일한 배열
 */
function renderMenu(pagesArray) {
  const navMenu = document.getElementById("navMenu");
  if (!navMenu || !Array.isArray(pagesArray) || pagesArray.length === 0) {
    console.warn("[MenuLoader] Navigation menu element not found or pages data is empty.");
    return;
  }

  const currentPath = (window.location.pathname || "/").split("?")[0];

  navMenu.innerHTML = "";

  pagesArray
    // 메뉴에서는 isMain(true) 페이지는 제외
    .filter((item) => !item.isMain)
    // order 기준 정렬 (API/JSON 혼용 대비)
    .sort((a, b) => {
      const oa = typeof a.order === "number" ? a.order : 0;
      const ob = typeof b.order === "number" ? b.order : 0;
      return oa - ob;
    })
    .forEach((item) => {
      const li = document.createElement("li");
      const a = document.createElement("a");

      const href = item.uri || item.path || "#";   // ← 우선 uri 사용
      a.href = href;
      a.textContent = item.label || item.path || "(no label)";

      // 현재 페이지와 일치하는지 검사 (uri/path 둘 다 후보로)
      const candidates = [item.uri, item.path]
        .filter(Boolean)
        .map((p) => p.split("?")[0]);

      if (candidates.includes(currentPath)) {
        a.classList.add("active");
      }

      li.appendChild(a);
      navMenu.appendChild(li);
    });
}
/*
function renderMenu(pagesArray) {
	const navMenu = document.getElementById("navMenu");
	if (!navMenu || !pagesArray || pagesArray.length === 0) {
		console.warn(
			"[MenuLoader] Navigation menu element not found or pages data is empty."
		);
		return;
	}

	// 현재 HTML 파일 이름 (예: /html_v2/P010_main_021.html → P010_main_021.html)
	const currentPath = window.location.pathname || "";
	const currentFile = currentPath.split("/").pop() || "P010_main_021.html";

	navMenu.innerHTML = "";

	pagesArray
		// isMain=true(메인 페이지)는 메뉴에서 제외
		.filter((item) => !item.isMain)
		// order 기준 정렬 (혹시 API가 정렬 안해줄 경우 대비)
		.sort((a, b) => {
			const oa = typeof a.order === "number" ? a.order : 0;
			const ob = typeof b.order === "number" ? b.order : 0;
			return oa - ob;
		})
		.forEach((item) => {
			const li = document.createElement("li");
			const a = document.createElement("a");

			// const v_uri = item.uri || "";
			const fullPath = item.path || "";
			// path에서 파일명만 추출해 active 판별
			const targetFile = fullPath.split("/").pop();

			// href는 cfg_pages_029.json에 정의된 path 그대로 사용
			// a.href = v_uri	|| "#";		
			a.href = "./" + targetFile || "#";

			a.textContent = item.label || fullPath || "(no label)";

			if (targetFile === currentFile) {
				a.classList.add("active");
			}

			li.appendChild(a);
			navMenu.appendChild(li);
		});
}
*/

/**
 * @brief ONLINE / OFFLINE 모드 판별 및 메뉴 로딩
 */
async function loadMenuAndSetMode() {
	let pagesData = null;

	// 1. ONLINE(API) 우선 시도
	let onlineRaw = await fetchData(API_MENU_PATH, MODE_ONLINE);
	let onlinePages = extractPagesArray(onlineRaw);

	if (onlinePages && onlinePages.length > 0) {
		currentMode = MODE_ONLINE;
		pagesData = onlinePages;
		console.log(`[Mode] Set to ${currentMode} (API)`);
	} else {
		// 2. 실패 시 OFFLINE(로컬 cfg_pages_029.json)
		console.warn(
			"[Mode] Online API failed or returned invalid data. Trying OFFLINE mode."
		);
		let offlineRaw = await fetchData(LOCAL_JSON_PATH, MODE_OFFLINE);
		let offlinePages = extractPagesArray(offlineRaw);

		if (offlinePages && offlinePages.length > 0) {
			currentMode = MODE_OFFLINE;
			pagesData = offlinePages;
			console.log(`[Mode] Set to ${currentMode} (LOCAL JSON)`);
			showToast(
				"온라인 API 응답 실패. 오프라인(로컬) 모드로 동작합니다.",
				"info"
			);
		} else {
			currentMode = MODE_OFFLINE;
			console.error("[Mode] OFFLINE mode also failed. Menu cannot be loaded.");
			showToast(
				"메뉴 로드 실패. 장치 연결 상태 또는 cfg_pages_029.json을 확인하세요.",
				"err"
			);
			return;
		}
	}

	window.currentMode = currentMode;

	// 로고 링크 설정
    setLogoLink(pagesData);

	renderMenu(pagesData);
}

// DOM 로드 후 메뉴 초기화
document.addEventListener("DOMContentLoaded", loadMenuAndSetMode);

