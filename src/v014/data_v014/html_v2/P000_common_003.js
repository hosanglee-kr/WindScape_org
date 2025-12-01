/* P000_common_003.js
 * ------------------------------------------------------
 * 모듈명 : Smart Nature Wind UI 공통 스크립트 (v002)
 * ------------------------------------------------------
 * 기능 요약:
 * 1. 온라인/오프라인 모드 판별 및 메뉴 데이터 로드.
 * 2. 로드된 'pages' 데이터를 기반으로 내비게이션 메뉴 동적 생성.
 * 3. 현재 페이지에 'active' 클래스 적용.
 * 4. 현재 동작 모드 (ONLINE/OFFLINE)를 전역에 노출.
 * ------------------------------------------------------
 */

// [모드 상수]
const MODE_ONLINE  = "ONLINE";
const MODE_OFFLINE = "OFFLINE";

// [데이터 경로]
//  - ONLINE: C++ 백엔드 W10_getMenuJson → 정렬된 pages 배열 반환
//  - OFFLINE: LittleFS /config/pages.json → { pages:[...], assets:[...] }
const API_MENU_PATH   = "/api/v1/menu";
const LOCAL_JSON_PATH = "/config/pages.json";

let currentMode = MODE_OFFLINE;
window.currentMode = currentMode; // 전역 노출

/**
 * @brief 토스트 메시지를 화면에 표시합니다.
 * @param {string} message 메시지
 * @param {('info'|'ok'|'warn'|'err')} type 메시지 타입
 */
function showToast(message, type = "info") {
	const toastContainer = document.getElementById("toastContainer");
	if (toastContainer) {
		const toast = document.createElement("div");
		toast.classList.add("toast");
		// type에 따라 추가 클래스 부여 (CSS: .toast.ok, .toast.warn, .toast.err, .toast.info 등)
		if (type) {
			toast.classList.add(type);
		}
		toast.textContent = message;
		toastContainer.appendChild(toast);
		setTimeout(() => toast.remove(), 4000);
	}
	console.log(`[Toast ${type.toUpperCase()}] (${currentMode}): ${message}`);
}

/**
 * @brief 네트워크를 통해 JSON 데이터를 가져옵니다.
 * @param {string} path 요청 경로
 * @param {string} mode 현재 모드 (로그용)
 * @returns {Promise<any|null>} 파싱된 JSON 또는 null
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
 * @brief pages 항목의 path/uri를 정규화합니다.
 *        - path: "/html/SC10_xxx_001.html" → "SC10_xxx_001.html"
 *        - uri는 그대로 유지 ("/dashboard" 등)
 * @param {Array<object>} pages 원본 pages 배열
 * @returns {Array<object>} 정규화된 pages 배열
 */
function normalizePages(pages) {
	if (!Array.isArray(pages)) return [];

	return pages.map((item) => {
		const normalized = { ...item };

		// path가 "/html/SC10_xxx_001.html" 형식일 경우 파일명만 추출
		if (normalized.path && typeof normalized.path === "string") {
			const fileName = normalized.path.split("/").pop();
			normalized.path = fileName || normalized.path;
		}

		return normalized;
	});
}

/**
 * @brief 메뉴 데이터를 기반으로 내비게이션 메뉴를 동적으로 생성합니다.
 * @param {Array<object>} pagesArray 메뉴 항목 배열
 *   - 예상 필드: { uri, path, label, ... }
 *   - uri: "/dashboard" 등 (우선적으로 href에 사용)
 *   - path: "SC10_dashboard_001.html" (파일명 기준 active 판별에 사용)
 */
function renderMenu(pagesArray) {
	const navMenu = document.getElementById("navMenu");
	if (!navMenu || !Array.isArray(pagesArray) || pagesArray.length === 0) {
		console.warn(
			"[MenuLoader] Navigation menu element not found or pages data is empty."
		);
		return;
	}

	const pathname = window.location.pathname; // 예: "/", "/dashboard", "/html/SC10_main_021.html"
	const currentFile =
		pathname.split("/").pop() || "SC10_main_021.html"; // 루트 접근 시 기본 메인 파일 가정

	navMenu.innerHTML = "";

	pagesArray.forEach((item) => {
		const li = document.createElement("li");
		const a = document.createElement("a");

		// 1) href 결정
		//   - 1순위: item.uri ("/dashboard")
		//   - 2순위: ./파일명 (예: ./SC10_dashboard_001.html)
		const targetFile = (item.path || "").split("/").pop();
		const href = item.uri || (targetFile ? `./${targetFile}` : "#");

		a.href = href;
		a.textContent = item.label || href;

		// 2) active 상태 판별
		//   - 장치에서 URI 기반 접근: pathname === item.uri
		//   - HTML 파일 직접 열기: currentFile === targetFile
		if (
			(item.uri && pathname === item.uri) ||
			(targetFile && currentFile === targetFile)
		) {
			a.classList.add("active");
		}

		li.appendChild(a);
		navMenu.appendChild(li);
	});
}

/**
 * @brief 온라인/오프라인 모드를 판별하고 메뉴를 로드하는 메인 함수입니다.
 */
async function loadMenuAndSetMode() {
	let pagesData = null;

	// ====================== 1. ONLINE 모드 시도 (API) ======================
	const onlineData = await fetchData(API_MENU_PATH, MODE_ONLINE);

	// ONLINE: API는 이미 정렬된 pages 배열을 직접 반환한다고 가정
	if (onlineData && Array.isArray(onlineData)) {
		currentMode = MODE_ONLINE;
		window.currentMode = currentMode;
		pagesData = normalizePages(onlineData);
		console.log(`[Mode] Set to ${currentMode} (API_MENU_PATH)`);
	} else {
		// ====================== 2. OFFLINE 모드 시도 (pages.json) ======================
		console.warn(
			"[Mode] Online API failed or invalid. Attempting OFFLINE (pages.json) mode."
		);
		const offlineData = await fetchData(LOCAL_JSON_PATH, MODE_OFFLINE);

		if (offlineData && Array.isArray(offlineData.pages)) {
			currentMode = MODE_OFFLINE;
			window.currentMode = currentMode;

			// pages.json 구조:
			// {
			//   "pages": [ { uri, path, label, isMain, order, css, js }, ... ],
			//   "assets": [ ... ]
			// }
			const filtered = offlineData.pages
				// 메인 페이지(isMain=true)는 메뉴에서 제외
				.filter((item) => !item.isMain);

			pagesData = normalizePages(filtered);

			console.log(`[Mode] Set to ${currentMode} (LOCAL_JSON_PATH)`);
			showToast(
				"온라인 API 응답 실패. 오프라인(로컬) 메뉴 구성으로 동작합니다.",
				"info"
			);
		} else {
			// ====================== 3. 모든 로딩 실패 ======================
			currentMode = MODE_OFFLINE;
			window.currentMode = currentMode;
			console.error(
				"[Mode] OFFLINE mode failed as well. Menu cannot be loaded."
			);
			showToast("메뉴 로드 실패. 장치 연결 상태를 확인하세요.", "err");
			return;
		}
	}

	// ====================== 4. 메뉴 렌더링 ======================
	renderMenu(pagesData);
}

// 문서 로드 완료 시 메뉴 로드 시작
document.addEventListener("DOMContentLoaded", loadMenuAndSetMode);

