

// P030_schedules_t1_006.js (JavaScript Implementation with Firebase and Gemini API)

// --- Configuration (ESP32 환경에서는 C++ 코드에서 주입되어야 합니다) ---
const GEMINI_MODEL = "gemini-2.5-flash-preview-09-2025";
const apiKey = ""; // API key is provided by the environment
const API_URL = `https://generativelanguage.googleapis.com/v1beta/models/${GEMINI_MODEL}:generateContent?key=${apiKey}`;

// 모듈 로드 (브라우저 환경 유지)
import { initializeApp } from "https://www.gstatic.com/firebasejs/12.6.0/firebase-app.js";
//import { initializeApp } from "https://www.gstatic.com/firebasejs/11.6.1/firebase-app.js";

import {
    getAuth,
    signInAnonymously,
    signInWithCustomToken,
    onAuthStateChanged,
    setPersistence,
    browserLocalPersistence
} from "https://www.gstatic.com/firebasejs/12.6.0/firebase-auth.js";

import {
    getFirestore,
    doc,
    setDoc,
    onSnapshot,
    setLogLevel
} from "https://www.gstatic.com/firebasejs/12.6.0/firebase-firestore.js";
// --- Firebase Global Variables ---

let app, db, auth;
let userId = null;
let isAuthReady = false;


// 🚩 테스트용 임시 수정: 실제 Firebase 설정을 여기에 직접 삽입
// const firebaseConfig = typeof __firebase_config !== 'undefined' ? JSON.parse(__firebase_config) : {};
const firebaseConfig = {
    apiKey: "AIzaSyB680oE-OfVzkjHnvmCDD2dWlEjBlOZ-Nc",
    authDomain: "smartwind-esp32.firebaseapp.com",
    projectId: "smartwind-esp32",
    storageBucket: "smartwind-esp32.firebasestorage.app",
    messagingSenderId: "65871128852",
    appId: "1:65871128852:web:953510ad4dbbd54f343a01"
};


// Canvas 환경 변수 로드 (Guard Clause 추가)
const appId = typeof __app_id !== 'undefined' ? __app_id : 'default-app-id';
//// const firebaseConfig = typeof __firebase_config !== 'undefined' ? JSON.parse(__firebase_config) : {};

const initialAuthToken = typeof __initial_auth_token !== 'undefined' ? __initial_auth_token : null;

// Constants and State
let g_scheduleData = []; // 로드된 전체 스케줄 데이터
let g_editingItemIndex = -1; // 현재 편집 중인 스케줄의 인덱스

// 모의 풍향 프리셋 목록 (실제 API 대신 사용)
const g_presets = [
    { code: "OCEAN", name: "바다의 숨결" },
    { code: "MOUNTAIN", name: "산들바람" },
    { code: "FOREST", name: "숲의 아침" },
    { code: "TURBULENCE", name: "강풍" }
];

const DAY_NAMES = ['월', '화', '수', '목', '금', '토', '일'];
const SCHEDULE_DOC_PATH = `/artifacts/${appId}/public/data/scheduleConfig/windSchedules`; // Firestore 문서 경로

const $ = (s, r = document) => r.querySelector(s);
const text = (el, v) => el && (el.textContent = v);

// UI Helpers
const setLoading = (flag) => {
    const ov = $("#loadingOverlay");
    if (ov) ov.style.display = flag ? "flex" : "none";

    const btnSuggestName = $("#btnSuggestName");
    if (btnSuggestName) btnSuggestName.disabled = flag;

    const btnSaveDetail = $("#btnSaveDetail");
    if (btnSaveDetail) btnSaveDetail.disabled = flag;

    document.querySelectorAll('.btnOptimizeAdjust').forEach(btn => btn.disabled = flag);
};

const showToast = (msg, type = "ok") => {
    const cont = $("#toastContainer");
    if (!cont) return;
    const div = document.createElement("div");
    div.className = `toast toast-${type}`;
    div.textContent = msg;
    cont.appendChild(div);
    setTimeout(() => div.remove(), 3000);
};

// ======================= Gemini API Fetch Wrapper =======================

async function fetchGemini(payload, maxRetries = 3) {
    for (let attempt = 0; attempt < maxRetries; attempt++) {
        try {
            const response = await fetch(API_URL, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(payload)
            });

            if (response.ok) {
                const result = await response.json();
                const text = result?.candidates?.[0]?.content?.parts?.[0]?.text;
                if (text) {
                    return { text, result };
                }
                throw new Error("Gemini 응답 내용이 비어 있습니다.");
            } else if (response.status === 429 && attempt < maxRetries - 1) {
                const delay = Math.pow(2, attempt) * 1000 + Math.random() * 1000;
                await new Promise(resolve => setTimeout(resolve, delay));
                continue;
            } else {
                const errorBody = await response.json();
                throw new Error(`API 오류: ${response.status} - ${errorBody.error?.message || response.statusText}`);
            }
        } catch (error) {
            if (attempt === maxRetries - 1) {
                throw error;
            }
        }
    }
    return null;
}

// ======================= LLM Feature 1: Schedule Name Suggestion =======================

async function handleSuggestName() {
    if (!isAuthReady || g_editingItemIndex < 0) return;
    const currentItem = collectScheduleDetailData(g_scheduleData[g_editingItemIndex]);

    const dayString = currentItem.period.days.map((d, i) => d === 1 ? DAY_NAMES[i] : '').filter(Boolean).join(', ');
    const segmentString = currentItem.segments.map(seg =>
        `${seg.onMinutes}분 작동 (${seg.mode === 'PRESET' ? seg.presetCode : seg.fixed_speed + '%'})`
    ).join(' -> ');

    const systemPrompt = "당신은 스마트 윈드 스케줄 시스템의 마케팅 전문가입니다. 제공된 설정 데이터를 기반으로 매력적이고, 직관적이며, 창의적인 스케줄 이름(4~10단어 이내)을 한국어로만 한 개 제안합니다. 다른 설명이나 인사말 없이 이름만 제공하세요.";
    const userQuery = `
        스케줄 설정을 분석하여 이름을 제안해 주세요.
        - 동작 시간대: ${currentItem.period.enabled ? `${currentItem.period.startTime} ~ ${currentItem.period.endTime}` : '24시간'}
        - 동작 요일: ${dayString || '매일'}
        - 자동 종료: ${currentItem.autoOff.timer.enabled ? `${currentItem.autoOff.timer.minutes}분 후 타이머 종료` : '비활성'}
        - 동작 단계 시퀀스: ${segmentString || '단계 없음'}

        이름을 제안하세요:
    `;

    const btn = $("#btnSuggestName");
    const input = $("#scheduleNameDetail");
    if (!btn || !input) return;

    setLoading(true);

    try {
        const payload = {
            contents: [{ parts: [{ text: userQuery }] }],
            systemInstruction: { parts: [{ text: systemPrompt }] },
        };

        const response = await fetchGemini(payload);
        const suggestedName = response.text.trim().replace(/^['"“‘”’\s]+/, '').replace(/['"“‘”’\s]+$/, '');

        input.value = suggestedName;
        showToast(`새 이름: ${suggestedName}`, "ok");

    } catch (error) {
        showToast(`이름 추천 실패: ${error.message}`, "err");
        console.error("Name Suggestion Error:", error);
    } finally {
        setLoading(false);
    }
}

// ======================= LLM Feature 2: Preset Adjustment Optimization =======================

async function handleOptimizeAdjust(button) {
    const index = Number(button.dataset.index);
    const card = button.closest('.segment-card');
    if (!isAuthReady || g_editingItemIndex < 0 || !card || isNaN(index)) return;

    const item = g_scheduleData[g_editingItemIndex];
    const promptTextarea = card.querySelector('[data-key="adjust_prompt"]');
    const userPrompt = promptTextarea?.value.trim();

    if (!userPrompt) {
        return showToast("원하는 바람의 느낌을 텍스트로 설명해주세요.", "warn");
    }

    const currentSeg = collectSegmentData(card, item.segments[index]?.segNo || index + 1);

    if (currentSeg.mode !== 'PRESET') {
        return showToast("Preset 모드일 때만 조정값을 최적화할 수 있습니다.", "warn");
    }

    const currentPresetCode = currentSeg.presetCode || 'OCEAN';
    const currentPreset = g_presets.find(p => p.code === currentPresetCode)?.name || '기본 프리셋';

    const statusDiv = card.querySelector('.llm-adjust-status');
    if (!statusDiv) return;

    setLoading(true);
    statusDiv.classList.remove('hidden');

    try {
        const systemPrompt = `당신은 스마트 윈드 시스템의 바람 엔지니어입니다. 사용자가 묘사한 바람의 느낌을 현실화하기 위해 필요한 'windIntensity' (강도)와 'windVariability' (변동성)의 조정값(Adjustment Value)을 JSON 형태로만 정확히 계산해 제공합니다.
        조정값은 -1.0에서 +1.0 사이의 float(소수점 첫째 자리까지) 값이어야 합니다.
        사용자 설명에 따라 이 두 값만 변경하며, 다른 필드를 추가하거나 변경하지 마십시오.`;

        const userQuery = `
            현재 프리셋: ${currentPreset} (${currentPresetCode})
            사용자 요구사항 (어떻게 조정하고 싶나요?): "${userPrompt}"

            요구사항을 충족시키기 위해 wind_intensity와 wind_variability를 조정(Adjustment)하여 JSON으로 출력하십시오.
        `;

        const payload = {
            contents: [{ parts: [{ text: userQuery }] }],
            systemInstruction: { parts: [{ text: systemPrompt }] },
            generationConfig: {
                responseMimeType: "application/json",
                responseSchema: {
                    type: "OBJECT",
                    properties: {
                        "windIntensity": { "type": "NUMBER" },
                        "windVariability": { "type": "NUMBER" }
                    },
                    "propertyOrdering": ["windIntensity", "windVariability"]
                }
            }
        };

        const response = await fetchGemini(payload);
        const jsonText = response.text;
        const adjustedValues = JSON.parse(jsonText);

        if (adjustedValues.windIntensity !== undefined && adjustedValues.windVariability !== undefined) {
            let intensity = Math.round(adjustedValues.windIntensity * 10) / 10;
            let variability = Math.round(adjustedValues.windVariability * 10) / 10;

            intensity = Math.max(-1.0, Math.min(1.0, intensity));
            variability = Math.max(-1.0, Math.min(1.0, variability));

            const intensityInput = card.querySelector('[data-key="adjust.windIntensity"]');
            const variabilityInput = card.querySelector('[data-key="adjust.windVariability"]');
            const intensityRange = card.querySelector('[data-key="adjust.wind_intensity_range"]');
            const variabilityRange = card.querySelector('[data-key="adjust.wind_variability_range"]');

            if (intensityInput) intensityInput.value = intensity.toFixed(1);
            if (intensityRange) intensityRange.value = intensity.toFixed(1);

            if (variabilityInput) variabilityInput.value = variability.toFixed(1);
            if (variabilityRange) variabilityRange.value = variability.toFixed(1);

            showToast(`조정값 최적화 완료! 강도: ${intensity.toFixed(1)}, 변동성: ${variability.toFixed(1)}`, "ok");
        } else {
            throw new Error("LLM이 필요한 조정값을 반환하지 않았습니다.");
        }

    } catch (error) {
        showToast(`조정값 최적화 실패: ${error.message}`, "err");
        console.error("Adjustment Optimization Error:", error);
    } finally {
        statusDiv.classList.add('hidden');
        setLoading(false);
    }
}

// ======================= Firebase Initialization =======================
async function initializeFirebase() {
    if (Object.keys(firebaseConfig).length === 0) {
        showToast("Firebase 설정이 정의되지 않았습니다.", "err");
        loadMockData();
        return;
    }

    try {
        app = initializeApp(firebaseConfig);
        db = getFirestore(app);
        auth = getAuth(app);
        setLogLevel('debug');

        await setPersistence(auth, browserLocalPersistence);

        const handleAuth = async () => {
            try {
                if (initialAuthToken) {
                    await signInWithCustomToken(auth, initialAuthToken);
                } else {
                    await signInAnonymously(auth);
                }
            } catch (e) {
                console.error("Initial Auth Error:", e);
                showToast(`초기 인증 실패: ${e.message}`, "err");
                await signInAnonymously(auth);
            }
        };

        onAuthStateChanged(auth, (user) => {
            const authStatus = $("#authStatus");
            if (!authStatus) return;

            let statusText, dotStyle;
            const btnAddNewSchedule = $("#btnAddNewSchedule");
            const authDot = $(".auth-dot");

            if (user) {
                userId = user.uid;
                isAuthReady = true;
                statusText = "✅ 인증 완료";
                if(authDot) {
                    authDot.style.backgroundColor = '#10b981';
                    authDot.classList.remove('pulse');
                }

                if (btnAddNewSchedule) btnAddNewSchedule.disabled = false;
                loadDataListener();
            } else {
                userId = 'unknown';
                isAuthReady = false;
                statusText = "❌ 인증 실패";
                if(authDot) {
                    authDot.style.backgroundColor = '#dc2626';
                    authDot.classList.remove('pulse');
                }
                if (btnAddNewSchedule) btnAddNewSchedule.disabled = true;
            }

            // HTML 구조 변경에 맞춰 업데이트
            authStatus.querySelector('span').nextSibling.textContent = ` ${statusText}`;
            text($("#displayUserId"), userId);
        });

        await handleAuth();

    } catch (e) {
        showToast(`Firebase 초기화 오류: ${e.message}`, "err");
        console.error("Firebase Init Error:", e);
        const authStatus = $("#authStatus");
        const authDot = $(".auth-dot");

        if (authStatus) {
            const statusText = "❌ 초기화 오류";
            if(authDot) {
                authDot.style.backgroundColor = '#dc2626';
                authDot.classList.remove('pulse');
            }
            authStatus.querySelector('span').nextSibling.textContent = ` ${statusText}`;
        }
        loadMockData();
    }
}

function loadMockData() {
    showToast("Mock 데이터로 로드 중...", "warn");
    g_scheduleData = [
        { schNo: 1, name: "오피스 주간 기본", enabled: true, period: { enabled: true, startTime: "09:00", endTime: "18:00", days: [1, 1, 1, 1, 1, 0, 0] }, segments: [{ segNo: 1, onMinutes: 60, mode: "PRESET", presetCode: "FOREST", adjust: { windIntensity: 0.5, windVariability: 0.2 }, fixed_speed: 0 }], autoOff: { timer: { enabled: false, minutes: 0 }, offtime: { enabled: false, time: "23:59" }, offtemp: { enabled: true, temp: 18.0 } }, motion: { pir: { enabled: true, holdSec: 120 }, ble: { enabled: false, rssi_threshold: -70, holdSec: 0 } } },
        { schNo: 2, name: "새벽 청정", enabled: false, period: { enabled: false, startTime: "00:00", endTime: "23:59", days: [1, 1, 1, 1, 1, 1, 1] }, segments: [{ segNo: 1, onMinutes: 180, mode: "PRESET", presetCode: "OCEAN", adjust: { windIntensity: -0.8, windVariability: 0.0 }, fixed_speed: 0 }], autoOff: { timer: { enabled: true, minutes: 30 }, offtime: { enabled: false, time: "23:59" }, offtemp: { enabled: false, temp: 0.0 } }, motion: { pir: { enabled: false, holdSec: 0 }, ble: { enabled: true, rssi_threshold: -65, holdSec: 300 } } }
    ];
    text($("#displayUserId"), "MOCK_USER");

    // HTML 구조 변경에 맞춰 업데이트
    const authStatus = $("#authStatus");
    const authDot = $(".auth-dot");

    if (authStatus) {
        if (authDot) {
            authDot.style.backgroundColor = '#f59e0b';
            authDot.classList.remove('pulse');
        }
        authStatus.querySelector('span').nextSibling.textContent = ` MOCK 데이터 로드`;
    }

    isAuthReady = true;
    if ($("#btnAddNewSchedule")) $("#btnAddNewSchedule").disabled = false;
    renderScheduleList();
}

// ======================= Firestore Data Handlers =======================

async function saveSchedulesToFirestore() {
    if (!db || !isAuthReady || !userId || userId === 'unknown') {
        showToast("데이터베이스 연결 또는 인증이 불안정합니다. (Mock 저장)", "warn");
        renderScheduleList();
        return;
    }

    setLoading(true);
    try {
        const docRef = doc(db, SCHEDULE_DOC_PATH);
        const dataToSave = JSON.parse(JSON.stringify({ schedules: g_scheduleData }));
        await setDoc(docRef, dataToSave);
        showToast("스케줄 데이터 저장 완료", "ok");
    } catch (e) {
        showToast(`스케줄 저장 실패: ${e.message}`, "err");
        console.error("Firestore Save Error:", e);
    } finally {
        setLoading(false);
    }
}

function loadDataListener() {
    if (!db || !isAuthReady || !userId || userId === 'unknown') return;

    const docRef = doc(db, SCHEDULE_DOC_PATH);

    // 🚩 중요 수정: 데이터 유무와 관계없이 리스너 초기 응답 후 로딩 해제
    setLoading(false);

    const placeholder = $("#listPlaceholder");

    onSnapshot(docRef, (docSnap) => {
        if (docSnap.exists()) {
            const data = docSnap.data();
            if (Array.isArray(data.schedules)) {
                g_scheduleData = data.schedules;
            } else {
                g_scheduleData = [];
            }
            renderScheduleList();
        } else {
            g_scheduleData = [];
            renderScheduleList();
            showToast("스케줄 문서가 존재하지 않아 새로 생성됩니다.", "warn");
        }
        if (placeholder) placeholder.textContent = g_scheduleData.length === 0 ? "등록된 스케줄이 없습니다. 새로운 스케줄을 추가하세요." : "스케줄 목록이 로드되었습니다.";
    }, (error) => {
        showToast(`실시간 업데이트 오류: ${error.message}`, "err");
        if (placeholder) placeholder.textContent = "데이터 로드 중 오류 발생.";
        console.error("Snapshot Error:", error);
    });
}

// ======================= Rendering Functions =======================

function renderScheduleList() {
    const container = $("#scheduleListContainer");
    const placeholder = $("#listPlaceholder");
    if (!container) return;

    // 1. 컨테이너를 비웁니다.
    container.innerHTML = '';

    // 2. 플레이스홀더의 표시 여부를 결정합니다.
    if (placeholder) {
        // 🚩 오류 수정 핵심: DOM에 요소를 추가하지 않고 display 속성만 제어
        placeholder.style.display = g_scheduleData.length === 0 ? 'block' : 'none';
    }

    // 3. 데이터 항목을 렌더링합니다.
    g_scheduleData.forEach((item, index) => {
        const daysActive = item.period.days.map((d, i) => d === 1 ? DAY_NAMES[i] : '').filter(Boolean).join(', ');
        const statusClass = item.enabled ? 'text-green-500 font-bold' : 'text-gray-500';
        const statusText = item.enabled ? '활성' : '비활성';

        const div = document.createElement('div');
        div.className = 'schedule-item';
        div.dataset.index = index;
        div.innerHTML = `
            <div style="flex-grow: 1;">
                <span class="schedule-item-title">[${item.schNo}] ${item.name}</span>
                <div class="schedule-item-detail">
                    ${item.period.enabled ?
                        `🕒 ${item.period.startTime} ~ ${item.period.endTime} | 🗓️ ${daysActive || '매일'}` :
                        '🕒 24시간 동작'
                    }
                </div>
            </div>
            <div class="${statusClass}">
                ${statusText}
            </div>
        `;
        div.addEventListener('click', () => editSchedule(index));
        container.appendChild(div);
    });

    text($("#scheduleCount"), g_scheduleData.length);
}

function renderDaySelectors(days) {
    const container = $("#daySelectorsDetail");
    if (!container) return;
    container.innerHTML = '';

    DAY_NAMES.forEach((day, index) => {
        const isChecked = days[index] === 1;
        const dayCode = index;

        const label = document.createElement('label');
        label.innerHTML = `
            <input type="checkbox" data-day-code="${dayCode}" ${isChecked ? 'checked' : ''}>
            <span>${day}</span>
        `;
        // 클래스를 'day-toggle'이 아닌 개별 label에 맞춰 삽입 (CSS 구조 변경)
        const div = document.createElement('div');
        div.className = 'day-toggle-item'; // 새로운 래퍼 클래스
        div.appendChild(label);
        container.appendChild(div);
    });
}

function renderSegment(segment, index, container, totalSegments) {
    // Segment 렌더링 로직 (CSS 클래스로 대체)
    const card = document.createElement('div');
    card.className = 'segment-card';
    card.dataset.index = index;

    const isPreset = segment.mode === 'PRESET';
    const presetOptions = g_presets.map(p =>
        `<option value="${p.code}" ${segment.presetCode === p.code ? 'selected' : ''}>${p.name}</option>`
    ).join('');

    card.innerHTML = `
        <div class="segment-control-buttons">
            <button type="button" class="btn btn-gray" style="padding: 0.25rem 0.5rem; font-size: 0.75rem;" data-action="up" ${index === 0 ? 'disabled' : ''}>▲</button>
            <button type="button" class="btn btn-gray" style="padding: 0.25rem 0.5rem; font-size: 0.75rem;" data-action="down" ${index === totalSegments - 1 ? 'disabled' : ''}>▼</button>
            <button type="button" class="btn btn-red" style="padding: 0.25rem 0.5rem; font-size: 0.75rem;" data-action="delete">삭제</button>
        </div>

        <h4 class="segment-step-title">Step ${segment.segNo}</h4>

        <div class="grid-container md-grid-cols-4 gap-4" style="margin-top: 1rem;">
            <div class="md-col-span-1">
                <label class="form-label">작동 시간 (분)</label>
                <input type="number" data-key="onMinutes" min="1" class="input-style" value="${segment.onMinutes}">
            </div>
            <div class="md-col-span-1">
                <label class="form-label">동작 모드</label>
                <select data-key="mode" class="input-style">
                    <option value="PRESET" ${isPreset ? 'selected' : ''}>프리셋</option>
                    <option value="FIXED" ${!isPreset ? 'selected' : ''}>고정 속도</option>
                </select>
            </div>

            <div class="md-col-span-2">
                <div data-mode="PRESET" style="display: ${isPreset ? 'block' : 'none'};">
                    <label class="form-label">프리셋 선택</label>
                    <select data-key="presetCode" class="input-style">
                        ${presetOptions}
                    </select>
                    <div class="segment-preset-group">
                        <h5 class="segment-preset-h5">AI 미세 조정</h5>
                        <textarea data-key="adjust_prompt" class="segment-adjust-textarea" rows="2" placeholder="예: 좀 더 부드럽고 약하게 불어오도록 조정해 줘."></textarea>
                        <div class="flex justify-between items-center" style="margin-top: 0.5rem;">
                            <button type="button" class="btn btn-purple btnOptimizeAdjust" data-index="${index}" style="padding: 0.5rem 0.75rem; font-size: 0.875rem;">
                                🚀 AI 최적화
                            </button>
                            <div class="llm-adjust-status hidden">최적화 중...</div>
                        </div>

                        <div class="segment-adjust-group">
                            <label class="form-label segment-adjust-label">강도 조정 (${segment.adjust.windIntensity.toFixed(1)})</label>
                            <input type="range" data-key="adjust.wind_intensity_range" min="-1.0" max="1.0" step="0.1" value="${segment.adjust.windIntensity.toFixed(1)}">
                            <input type="number" data-key="adjust.windIntensity" min="-1.0" max="1.0" step="0.1" value="${segment.adjust.windIntensity.toFixed(1)}" class="input-style segment-adjust-input">
                        </div>
                        <div class="segment-adjust-group mt-075">
                            <label class="form-label segment-adjust-label">변동성 조정 (${segment.adjust.windVariability.toFixed(1)})</label>
                            <input type="range" data-key="adjust.wind_variability_range" min="-1.0" max="1.0" step="0.1" value="${segment.adjust.windVariability.toFixed(1)}">
                            <input type="number" data-key="adjust.windVariability" min="-1.0" max="1.0" step="0.1" value="${segment.adjust.windVariability.toFixed(1)}" class="input-style segment-adjust-input">
                        </div>
                    </div>
                </div>

                <div data-mode="FIXED" style="display: ${!isPreset ? 'block' : 'none'};">
                    <label class="form-label">고정 속도 (%)</label>
                    <input type="number" data-key="fixed_speed" min="0" max="100" class="input-style" value="${segment.fixed_speed}">
                </div>
            </div>
        </div>
    `;

    // 이벤트 리스너 추가
    card.querySelector('[data-key="mode"]').addEventListener('change', (e) => {
        const isPreset = e.target.value === 'PRESET';
        card.querySelector('[data-mode="PRESET"]').style.display = isPreset ? 'block' : 'none';
        card.querySelector('[data-mode="FIXED"]').style.display = !isPreset ? 'block' : 'none';
    });

    // 슬라이더와 숫자 입력 필드 연동
    card.querySelectorAll('input[type="range"]').forEach(range => {
        const key = range.dataset.key;
        const numericKey = key.replace('_range', '');
        const numericInput = card.querySelector(`[data-key="${numericKey}"]`);

        range.addEventListener('input', () => {
            if (numericInput) numericInput.value = range.value;
            const label = range.previousElementSibling;
            if (label) label.textContent = label.textContent.split('(')[0].trim() + ` (${Number(range.value).toFixed(1)})`;
        });

        if (numericInput) {
             numericInput.addEventListener('input', () => {
                const value = Number(numericInput.value);
                if (!isNaN(value)) {
                    range.value = value;
                    const label = range.previousElementSibling.previousElementSibling;
                    if (label) label.textContent = label.textContent.split('(')[0].trim() + ` (${value.toFixed(1)})`;
                }
            });
        }
    });

    card.querySelector('.btnOptimizeAdjust').addEventListener('click', (e) => handleOptimizeAdjust(e.target));

    container.appendChild(card);
}

function renderSegmentList(segments) {
    const container = $("#segmentListDetail");
    if (!container) return;
    container.innerHTML = '';

    const totalSegments = segments.length; // 💡 총 길이를 계산
    segments.forEach((seg, index) => renderSegment(seg, index, container, totalSegments));

    container.querySelectorAll('.segment-card').forEach((card, index) => {
        const upBtn = card.querySelector('[data-action="up"]');
        const downBtn = card.querySelector('[data-action="down"]');
        if (upBtn) upBtn.disabled = index === 0;
        if (downBtn) downBtn.disabled = segments.length === 0 || index === segments.length - 1; // 💡 segments.length === 0 방어
    });
}

// ======================= Data Collection / UI Control =======================

function collectScheduleDetailData(initialItem) {
    const item = JSON.parse(JSON.stringify(initialItem));

    // 🚩 [수정] 데이터 수집 전에 필수 하위 객체가 존재하는지 확인하고 초기화
    // 이 코드를 추가하여 `item.period`, `item.autoOff`, `item.motion`이
    // 최소한의 객체 구조를 갖도록 보장합니다.
    if (!item.period) item.period = { enabled: true, startTime: "00:00", endTime: "23:59", days: [1, 1, 1, 1, 1, 1, 1] };
    if (!item.autoOff) item.autoOff = { timer: { enabled: false, minutes: 0 }, offtime: { enabled: false, time: "23:59" }, offtemp: { enabled: false, temp: 0.0 } };
    if (!item.motion) item.motion = { pir: { enabled: false, holdSec: 0 }, ble: { enabled: false, rssi_threshold: -70, holdSec: 0 } };

    // 추가적으로, autoOff 및 motion 내의 세부 객체들도 초기화해야 합니다.
    // (addNewSchedule에서 완벽하게 초기화하지 않았을 경우를 대비)
    if (!item.autoOff.timer) item.autoOff.timer = { enabled: false, minutes: 0 };
    if (!item.autoOff.offtime) item.autoOff.offtime = { enabled: false, time: "23:59" };
    if (!item.autoOff.offtemp) item.autoOff.offtemp = { enabled: false, temp: 0.0 };
    if (!item.motion.pir) item.motion.pir = { enabled: false, holdSec: 0 };
    if (!item.motion.ble) item.motion.ble = { enabled: false, rssi_threshold: -70, holdSec: 0 };


    // A. 기본 정보
    item.schNo = Number($("#schNoDetail").value) || 1;
    item.name = $("#scheduleNameDetail").value.trim();
    item.enabled = $("#scheduleEnabledDetail").checked;

    // B. 기간 설정
    item.period.enabled = $("#periodEnabledDetail").checked;

    if (item.period.enabled) {
        item.period.startTime = $("#startTimeDetail").value;
        item.period.endTime = $("#endTimeDetail").value;

        const dayInputs = $("#daySelectorsDetail").querySelectorAll('input[type="checkbox"]');
        item.period.days = Array.from(dayInputs).map(input => input.checked ? 1 : 0);
    } else {
        item.period.startTime = "00:00";
        item.period.endTime = "23:59";
        item.period.days = [1, 1, 1, 1, 1, 1, 1];
    }

    // C. 세그먼트 (Step)
    item.segments = Array.from($("#segmentListDetail").querySelectorAll('.segment-card')).map((card, index) => {
        return collectSegmentData(card, index + 1);
    });

    // D. AutoOff 및 Motion
    item.autoOff.timer.enabled = $("#autoOffTimerEnabledDetail").checked;
    item.autoOff.timer.minutes = item.autoOff.timer.enabled ? Number($("#autoOffTimerMinutesDetail").value) : 0;

    item.autoOff.offtime.enabled = $("#autoOffOffTimeEnabledDetail").checked;
    item.autoOff.offtime.time = item.autoOff.offtime.enabled ? $("#autoOffOffTimeDetail").value : "23:59";

    item.autoOff.offtemp.enabled = $("#autoOffOffTempEnabledDetail").checked;
    item.autoOff.offtemp.temp = item.autoOff.offtemp.enabled ? Number($("#autoOffOffTempDetail").value) : 0.0;

    item.motion.pir.enabled = $("#motionPirEnabledDetail").checked;
    item.motion.pir.holdSec = item.motion.pir.enabled ? Number($("#motionPirHoldSecDetail").value) : 0;

    item.motion.ble.enabled = $("#motionBleEnabledDetail").checked;
    item.motion.ble.rssi_threshold = item.motion.ble.enabled ? Number($("#motionBleRssiThresholdDetail").value) : -70;
    item.motion.ble.holdSec = item.motion.ble.enabled ? Number($("#motionBleHoldSecDetail").value) : 0;

    return item;
}

function collectSegmentData(card, segNo) {
    const mode = card.querySelector('[data-key="mode"]').value;
    const segment = {
        segNo: segNo,
        onMinutes: Number(card.querySelector('[data-key="onMinutes"]').value) || 1,
        mode: mode,
        presetCode: mode === 'PRESET' ? card.querySelector('[data-key="presetCode"]').value : "OCEAN",
        fixed_speed: mode === 'FIXED' ? Number(card.querySelector('[data-key="fixed_speed"]').value) : 0,
        adjust: {
            windIntensity: Number(card.querySelector('[data-key="adjust.windIntensity"]').value) || 0.0,
            windVariability: Number(card.querySelector('[data-key="adjust.windVariability"]').value) || 0.0
        }
    };
    segment.adjust.windIntensity = Math.max(-1.0, Math.min(1.0, segment.adjust.windIntensity));
    segment.adjust.windVariability = Math.max(-1.0, Math.min(1.0, segment.adjust.windVariability));

    return segment;
}

function toggleVisibility() {
    const periodEnabled = $("#periodEnabledDetail").checked;
    // CSS 클래스로 변경: periodSettingsContainer에 hidden 클래스 토글
    $("#periodSettingsContainer").classList.toggle('hidden', !periodEnabled);

    document.querySelectorAll('.autooff-group').forEach(group => group.style.display = 'none');
    if ($("#autoOffTimerEnabledDetail").checked) { $(".timer-group").style.display = 'flex'; } // grid-container라서 flex로 변경
    if ($("#autoOffOffTimeEnabledDetail").checked) { $(".offtime-group").style.display = 'flex'; }
    if ($("#autoOffOffTempEnabledDetail").checked) { $(".offtemp-group").style.display = 'flex'; }

    document.querySelectorAll('.motion-group.pir-group').forEach(group => group.style.display = $("#motionPirEnabledDetail").checked ? 'block' : 'none');
    document.querySelectorAll('.motion-group.ble-group').forEach(group => group.style.display = $("#motionBleEnabledDetail").checked ? 'block' : 'none');
}

// ======================= Core Logic =======================

function showDetailView(initialItem, isNew) {
    $("#scheduleListSection").classList.add('hidden');
    $("#scheduleDetailSection").classList.remove('hidden');

    text($("#detailTitle"), isNew ? '새 항목' : initialItem.schNo);
    $("#btnDeleteSchedule").classList.toggle('hidden', isNew);

    $("#schNoDetail").value = initialItem.schNo;
    $("#scheduleNameDetail").value = initialItem.name;
    $("#scheduleEnabledDetail").checked = initialItem.enabled;

    $("#periodEnabledDetail").checked = initialItem.period.enabled;
    $("#startTimeDetail").value = initialItem.period.startTime;
    $("#endTimeDetail").value = initialItem.period.endTime;
    renderDaySelectors(initialItem.period.days);

    renderSegmentList(initialItem.segments);

    $("#autoOffTimerEnabledDetail").checked = initialItem.autoOff.timer.enabled;
    $("#autoOffTimerMinutesDetail").value = initialItem.autoOff.timer.minutes;
    $("#autoOffOffTimeEnabledDetail").checked = initialItem.autoOff.offtime.enabled;
    $("#autoOffOffTimeDetail").value = initialItem.autoOff.offtime.time;
    $("#autoOffOffTempEnabledDetail").checked = initialItem.autoOff.offtemp.enabled;
    $("#autoOffOffTempDetail").value = initialItem.autoOff.offtemp.temp;

    $("#motionPirEnabledDetail").checked = initialItem.motion.pir.enabled;
    $("#motionPirHoldSecDetail").value = initialItem.motion.pir.holdSec;
    $("#motionBleEnabledDetail").checked = initialItem.motion.ble.enabled;
    $("#motionBleRssiThresholdDetail").value = initialItem.motion.ble.rssi_threshold;
    $("#motionBleHoldSecDetail").value = initialItem.motion.ble.holdSec;

    toggleVisibility();
}

function editSchedule(index) {
    g_editingItemIndex = index;
    const item = g_scheduleData[index];
    showDetailView(item, false);
}

function addNewSchedule() {
    g_editingItemIndex = -1;
    const schNos = g_scheduleData.map(s => s.schNo).filter(n => !isNaN(n));
    const newSchNo = schNos.length > 0 ? Math.max(...schNos) + 1 : 1;

    const newItem = {
        schNo: newSchNo,
        name: `새 스케줄 ${newSchNo}`,
        enabled: true,
        period: { enabled: true, startTime: "08:00", endTime: "18:00", days: [1, 1, 1, 1, 1, 0, 0] },
        segments: [
            { segNo: 1, onMinutes: 60, mode: "PRESET", presetCode: "OCEAN", adjust: { windIntensity: 0.0, windVariability: 0.0 }, fixed_speed: 0 }
        ],
        autoOff: { timer: { enabled: false, minutes: 0 }, offtime: { enabled: false, time: "23:59" }, offtemp: { enabled: false, temp: 0.0 } },
        motion: { pir: { enabled: true, holdSec: 120 }, ble: { enabled: false, rssi_threshold: -70, holdSec: 0 } }
    };

    showDetailView(newItem, true);
}

function saveScheduleDetail() {
    const currentItem = collectScheduleDetailData(g_editingItemIndex === -1 ? {} : g_scheduleData[g_editingItemIndex]);

    const isDuplicate = g_scheduleData.some((item, idx) =>
        item.schNo === currentItem.schNo && idx !== g_editingItemIndex
    );
    if (isDuplicate) {
        return showToast("스케줄 번호(schNo)가 중복됩니다.", "err");
    }

    if (currentItem.segments.length === 0) {
         return showToast("최소한 1개의 동작 단계(Step)를 추가해야 합니다.", "err");
    }

    if (g_editingItemIndex === -1) {
        g_scheduleData.push(currentItem);
    } else {
        g_scheduleData[g_editingItemIndex] = currentItem;
    }

    g_scheduleData.sort((a, b) => a.schNo - b.schNo);

    saveSchedulesToFirestore();

    cancelEdit();
}

function deleteSchedule() {
    if (g_editingItemIndex === -1 || !confirm("정말로 이 스케줄을 삭제하시겠습니까?")) return;

    g_scheduleData.splice(g_editingItemIndex, 1);
    saveSchedulesToFirestore();
    showToast("스케줄이 삭제되었습니다.", "ok");
    cancelEdit();
}

function cancelEdit() {
    g_editingItemIndex = -1;
    $("#scheduleDetailSection").classList.add('hidden');
    $("#scheduleListSection").classList.remove('hidden');
    renderScheduleList();
}

// ======================= Event Listeners =======================

function setupEventListeners() {
    $("#btnAddNewSchedule").addEventListener('click', addNewSchedule);
    $("#btnRefresh").addEventListener('click', () => loadDataListener() || loadMockData());

    $("#btnCancelEdit").addEventListener('click', cancelEdit);
    $("#btnSaveDetail").addEventListener('click', saveScheduleDetail);
    $("#btnDeleteSchedule").addEventListener('click', deleteSchedule);

    $("#btnSuggestName").addEventListener('click', handleSuggestName);

    $("#periodEnabledDetail").addEventListener('change', toggleVisibility);
    $("#autoOffTimerEnabledDetail").addEventListener('change', toggleVisibility);
    $("#autoOffOffTimeEnabledDetail").addEventListener('change', toggleVisibility);
    $("#autoOffOffTempEnabledDetail").addEventListener('change', toggleVisibility);
    $("#motionPirEnabledDetail").addEventListener('change', toggleVisibility);
    $("#motionBleEnabledDetail").addEventListener('change', toggleVisibility);

    $("#btnAddSegmentDetail").addEventListener('click', () => {
        const container = $("#segmentListDetail");

        // 현재 UI 상태를 데이터에 반영하여 segments 배열을 가져옵니다.
        const segmentsFromUI = Array.from(container.querySelectorAll('.segment-card')).map((c, i) => collectSegmentData(c, i + 1));
        const newSegNo = segmentsFromUI.length + 1;

        const newSegment = {
            segNo: newSegNo,
            onMinutes: 60,
            mode: "PRESET",
            presetCode: "OCEAN",
            adjust: { windIntensity: 0.0, windVariability: 0.0 },
            fixed_speed: 0
        };

        segmentsFromUI.push(newSegment);

        // 편집 중인 데이터의 segments를 업데이트합니다.
        if (g_editingItemIndex !== -1) {
            g_scheduleData[g_editingItemIndex].segments = segmentsFromUI;
        }
        // 새 항목 편집 중일 경우, 임시로 데이터를 구성하는 작업은 collectScheduleDetailData에서 처리됩니다.

        renderSegmentList(segmentsFromUI);
    });

    // Segment 이동/삭제 (이벤트 위임)
    $("#scheduleDetailSection").addEventListener('click', (e) => {
        const target = e.target;
        if (target.dataset.action === 'up' || target.dataset.action === 'down' || target.dataset.action === 'delete') {
            const card = target.closest('.segment-card');
            const index = Number(card.dataset.index);
            if (isNaN(index)) return;

            // 1. UI의 최신 상태를 반영한 segments 배열을 가져옵니다.
            const segments = Array.from($("#segmentListDetail").querySelectorAll('.segment-card')).map((c, i) => collectSegmentData(c, i + 1));

            let shouldUpdate = false;

            if (target.dataset.action === 'delete') {
                if (segments.length <= 1) return showToast("최소 1개의 단계는 유지해야 합니다.", "err");
                segments.splice(index, 1);
                shouldUpdate = true;
            } else if (target.dataset.action === 'up' && index > 0) {
                [segments[index], segments[index - 1]] = [segments[index - 1], segments[index]];
                shouldUpdate = true;
            } else if (target.dataset.action === 'down' && index < segments.length - 1) {
                [segments[index], segments[index + 1]] = [segments[index + 1], segments[index]];
                shouldUpdate = true;
            }

            if (shouldUpdate) {
                // segNo 재할당
                segments.forEach((seg, i) => seg.segNo = i + 1);

                // 2. 편집 중인 데이터의 segments를 업데이트합니다.
                if (g_editingItemIndex !== -1) {
                    g_scheduleData[g_editingItemIndex].segments = segments;
                }

                // 3. UI를 다시 렌더링합니다.
                renderSegmentList(segments);
            }
        }
    });
}

// ======================= Main Initialization =======================
initializeFirebase();
setupEventListeners();

