// schedule_manager.js (JavaScript Implementation with Firebase and Gemini API)

import { initializeApp } from "https://www.gstatic.com/firebasejs/11.6.1/firebase-app.js";
import { getAuth, signInAnonymously, signInWithCustomToken, onAuthStateChanged, setPersistence, browserLocalPersistence } from "https://www.gstatic.com/firebasejs/11.6.1/firebase-auth.js";
import { getFirestore, doc, setDoc, onSnapshot, setLogLevel } from "https://www.gstatic.com/firebasejs/11.6.1/firebase-firestore.js";

// --- Gemini API Configuration ---
const GEMINI_MODEL = "gemini-2.5-flash-preview-09-2025";
const apiKey = ""; // API key is provided by the environment
const API_URL = `https://generativelanguage.googleapis.com/v1beta/models/${GEMINI_MODEL}:generateContent?key=${apiKey}`;

// --- Firebase Global Variables ---
let app, db, auth;
let userId = null;
let isAuthReady = false;

// Canvas 환경 변수 로드 (Guard Clause 추가)
const appId = typeof __app_id !== 'undefined' ? __app_id : 'default-app-id';
const firebaseConfig = typeof __firebase_config !== 'undefined' ? JSON.parse(__firebase_config) : {};
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
    
    // LLM 및 저장 버튼의 비활성화 상태 제어 (작동 오류 수정)
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
    div.className = `p-3 mt-2 rounded-lg text-white text-sm ${type === 'ok' ? 'bg-green-500' : type === 'warn' ? 'bg-amber-500' : 'bg-red-500'}`;
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
                // console.warn(`API 호출 제한. ${delay}ms 후 재시도...`);
                await new Promise(resolve => setTimeout(resolve, delay));
                continue; // 재시도
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
    // LLM 프롬프트에 현재 UI의 최신값을 반영하기 위해 DOM에서 데이터를 수집
    // saveScheduleDetail() 호출 전에 collectScheduleDetailData를 통해 최신 UI 상태를 반영합니다.
    const currentItem = collectScheduleDetailData(g_scheduleData[g_editingItemIndex]);
    
    const dayString = currentItem.period.days.map((d, i) => d === 1 ? DAY_NAMES[i] : '').filter(Boolean).join(', ');
    const segmentString = currentItem.segments.map(seg => 
        `${seg.on_minutes}분 작동 (${seg.mode === 'PRESET' ? seg.presetCode : seg.fixed_speed + '%'})`
    ).join(' -> ');

    const systemPrompt = "당신은 스마트 윈드 스케줄 시스템의 마케팅 전문가입니다. 제공된 설정 데이터를 기반으로 매력적이고, 직관적이며, 창의적인 스케줄 이름(4~10단어 이내)을 한국어로만 한 개 제안합니다. 다른 설명이나 인사말 없이 이름만 제공하세요.";
    const userQuery = `
        스케줄 설정을 분석하여 이름을 제안해 주세요.
        - 동작 시간대: ${currentItem.period.enabled ? `${currentItem.period.start_time} ~ ${currentItem.period.end_time}` : '24시간'}
        - 동작 요일: ${dayString || '매일'}
        - 자동 종료: ${currentItem.autoOff.timer.enabled ? `${currentItem.autoOff.timer.minutes}분 후 타이머 종료` : '비활성'}
        - 동작 단계 시퀀스: ${segmentString || '단계 없음'}
        
        이름을 제안하세요:
    `;
    
    const btn = $("#btnSuggestName");
    const input = $("#scheduleNameDetail");
    if (!btn || !input) return;

    setLoading(true); // 로딩 시작 (버튼 비활성화 포함)

    try {
        const payload = {
            contents: [{ parts: [{ text: userQuery }] }],
            systemInstruction: { parts: [{ text: systemPrompt }] },
        };
        
        const response = await fetchGemini(payload);
        // 응답 텍스트에서 따옴표, 공백 등 불필요한 문자 제거
        const suggestedName = response.text.trim().replace(/^['"“‘”’\s]+/, '').replace(/['"“‘”’\s]+$/, ''); 
        
        input.value = suggestedName;
        showToast(`새 이름: ${suggestedName}`, "ok");

    } catch (error) {
        showToast(`이름 추천 실패: ${error.message}`, "err");
        console.error("Name Suggestion Error:", error);
    } finally {
        btn.textContent = '✨ 이름 추천';
        setLoading(false); // 로딩 종료 (버튼 활성화 포함)
    }
}

// ======================= LLM Feature 2: Preset Adjustment Optimization =======================

async function handleOptimizeAdjust(button) {
    const index = Number(button.dataset.index);
    const card = button.closest('.segment-card');
    if (!isAuthReady || g_editingItemIndex < 0 || !card || isNaN(index)) return;
    
    const item = g_scheduleData[g_editingItemIndex];
    // 사용자가 입력한 프롬프트 텍스트를 읽음
    const promptTextarea = card.querySelector('[data-key="adjust_prompt"]');
    const userPrompt = promptTextarea?.value.trim();
    
    if (!userPrompt) {
        return showToast("원하는 바람의 느낌을 텍스트로 설명해주세요.", "warn");
    }

    // 현재 UI에서 데이터 수집 (최신 상태 반영)
    const currentSeg = collectSegmentData(card, item.segments[index]?.segNo || index + 1);

    if (currentSeg.mode !== 'PRESET') {
        return showToast("Preset 모드일 때만 조정값을 최적화할 수 있습니다.", "warn");
    }
    
    // 현재 UI에서 선택된 프리셋 코드를 읽어옴
    const currentPresetCode = currentSeg.presetCode || 'OCEAN';
    const currentPreset = g_presets.find(p => p.code === currentPresetCode)?.name || '기본 프리셋';

    const statusDiv = card.querySelector('.llm-adjust-status');
    if (!statusDiv) return;

    setLoading(true); // 로딩 시작
    statusDiv.classList.remove('hidden');

    try {
        const systemPrompt = `당신은 스마트 윈드 시스템의 바람 엔지니어입니다. 사용자가 묘사한 바람의 느낌을 현실화하기 위해 필요한 'wind_intensity' (강도)와 'wind_variability' (변동성)의 조정값(Adjustment Value)을 JSON 형태로만 정확히 계산해 제공합니다. 
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
                        "wind_intensity": { "type": "NUMBER" },
                        "wind_variability": { "type": "NUMBER" }
                    },
                    "propertyOrdering": ["wind_intensity", "wind_variability"]
                }
            }
        };
        
        const response = await fetchGemini(payload);
        const jsonText = response.text;
        const adjustedValues = JSON.parse(jsonText);
        
        if (adjustedValues.wind_intensity !== undefined && adjustedValues.wind_variability !== undefined) {
            // 소수점 첫째 자리로 반올림하여 시스템 규칙 준수 및 유효 범위 확인
            let intensity = Math.round(adjustedValues.wind_intensity * 10) / 10;
            let variability = Math.round(adjustedValues.wind_variability * 10) / 10;
            
            intensity = Math.max(-1.0, Math.min(1.0, intensity));
            variability = Math.max(-1.0, Math.min(1.0, variability));
            
            // UI 업데이트 (DOM 직접 수정) 및 슬라이더 연동
            const intensityInput = card.querySelector('[data-key="adjust.wind_intensity"]');
            const variabilityInput = card.querySelector('[data-key="adjust.wind_variability"]');
            
            if (intensityInput) intensityInput.value = intensity.toFixed(1);
            // Range input도 같이 업데이트
            const intensityRange = card.querySelector('[data-key="adjust.wind_intensity_range"]');
            if (intensityRange) intensityRange.value = intensity.toFixed(1);

            if (variabilityInput) variabilityInput.value = variability.toFixed(1);
            // Range input도 같이 업데이트
            const variabilityRange = card.querySelector('[data-key="adjust.wind_variability_range"]');
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
        setLoading(false); // 로딩 종료
    }
}


// ======================= Firebase Initialization =======================
async function initializeFirebase() {
    if (Object.keys(firebaseConfig).length === 0) {
        showToast("Firebase 설정이 정의되지 않았습니다.", "err");
        return;
    }

    try {
        app = initializeApp(firebaseConfig);
        db = getFirestore(app);
        auth = getAuth(app);
        setLogLevel('debug'); // 디버깅을 위해 로깅 활성화

        await setPersistence(auth, browserLocalPersistence);

        // 초기 인증 처리
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
                // 익명 인증으로 fallback 시도
                await signInAnonymously(auth);
            }
        };

        // onAuthStateChanged 리스너 설정
        onAuthStateChanged(auth, (user) => {
            const authStatus = $("#authStatus");
            if (!authStatus) return; 

            let statusText, dotClass;
            const btnAddNewSchedule = $("#btnAddNewSchedule");
            
            if (user) {
                userId = user.uid;
                isAuthReady = true;
                statusText = "✅ 인증 완료";
                dotClass = 'h-2 w-2 rounded-full bg-green-500 mr-1';
                if (btnAddNewSchedule) btnAddNewSchedule.disabled = false;
                loadDataListener(); // 인증 완료 후 데이터 로드 시작
            } else {
                // 익명 인증 실패 또는 로그아웃 시
                userId = 'unknown'; 
                isAuthReady = false;
                statusText = "❌ 인증 실패";
                dotClass = 'h-2 w-2 rounded-full bg-red-700 mr-1';
                if (btnAddNewSchedule) btnAddNewSchedule.disabled = true;
            }
            
            authStatus.innerHTML = `<span class="${dotClass}"></span> ${statusText}`;
            text($("#displayUserId"), userId);
        });
        
        // onAuthStateChanged가 즉시 호출되지 않을 경우를 대비해 초기 인증 시작
        await handleAuth(); 

    } catch (e) {
        showToast(`Firebase 초기화 오류: ${e.message}`, "err");
        console.error("Firebase Init Error:", e);
        const authStatus = $("#authStatus");
        if (authStatus) {
             const statusText = "❌ 초기화 오류";
             const dotClass = 'h-2 w-2 rounded-full bg-red-700 mr-1';
             authStatus.innerHTML = `<span class="${dotClass}"></span> ${statusText}`;
        }
    }
}

// ======================= Firestore Data Handlers =======================

// Firestore 데이터 저장 (전체 스케줄 배열 덮어쓰기)
async function saveSchedulesToFirestore() {
    if (!db || !isAuthReady || !userId || userId === 'unknown') {
        showToast("데이터베이스 연결 또는 인증이 불안정합니다.", "err");
        return;
    }

    setLoading(true);
    try {
        const docRef = doc(db, SCHEDULE_DOC_PATH);
        // JSON.parse(JSON.stringify())를 사용하여 데이터 구조를 검증하고 저장합니다.
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

// Firestore 데이터 실시간 로드
function loadDataListener() {
    if (!db || !isAuthReady || !userId || userId === 'unknown') return;
    
    const docRef = doc(db, SCHEDULE_DOC_PATH);
    const placeholder = $("#listPlaceholder");
    
    onSnapshot(docRef, (docSnap) => {
        if (docSnap.exists()) {
            const data = docSnap.data();
            // 데이터 유효성 검사 및 기본값 설정
            if (Array.isArray(data.schedules)) {
                g_scheduleData = data.schedules;
            } else {
                g_scheduleData = [];
            }
            renderScheduleList();
        } else {
            // 문서가 없으면 빈 배열로 초기화 및 생성 시도
            g_scheduleData = [];
            renderScheduleList();
            // 문서가 없는 경우, 새로 저장할 때 문서가 생성됩니다.
            showToast("스케줄 문서가 존재하지 않아 새로 생성됩니다.", "warn");
        }
        if (placeholder) placeholder.textContent = g_scheduleData.length === 0 ? "등록된 스케줄이 없습니다. 새로운 스케줄을 추가하세요." : "스케줄 목록이 로드되었습니다.";
    }, (error) => {
        showToast(`실시간 업데이트 오류: ${error.message}`, "err");
        if (placeholder) placeholder.textContent = "데이터 로드 중 오류 발생.";
        console.error("Snapshot Error:", error);
    });
}

// ======================= UI Logic: Input Toggling (Visibility) =======================

// Period Inputs 보이기/숨기기
function togglePeriodInputs() {
    const enabled = $("#periodEnabledDetail")?.checked;
    const container = $("#periodSettingsContainer");
    const dayCheckboxes = document.querySelectorAll('#daySelectorsDetail input[type="checkbox"]');

    if (!container) return;

    // 컨테이너 표시/숨기기
    container.classList.toggle('hidden', !enabled);
    
    // 입력 필드 활성화/비활성화 (가시성 제어 후에도 입력 필드는 disabled 처리를 유지해야함)
    const periodInputs = [
        $("#startTimeDetail"),
        $("#endTimeDetail")
    ].filter(el => el);

    [...periodInputs, ...Array.from(dayCheckboxes)].forEach(input => {
        input.disabled = !enabled;
        // 요일 버튼 비활성화 시 opacity 적용
        input.closest('.day-toggle')?.classList.toggle('opacity-50', !enabled);
        input.classList.toggle('input-disabled', !enabled);
    });
}

// AutoOff 관련항목 보이기/숨기기
function toggleAutoOffInputs() {
    // 타이머
    const timerEnabled = $("#autoOffTimerEnabledDetail")?.checked;
    const timerInput = $("#autoOffTimerMinutesDetail");
    const timerGroup = document.querySelector('.timer-group');
    if (timerInput && timerGroup) {
        timerInput.disabled = !timerEnabled;
        timerInput.classList.toggle('input-disabled', !timerEnabled);
        timerGroup.classList.toggle('hidden', !timerEnabled);
    }

    // 종료 시간
    const offTimeEnabled = $("#autoOffOffTimeEnabledDetail")?.checked;
    const offTimeInput = $("#autoOffOffTimeDetail");
    const offTimeGroup = document.querySelector('.offtime-group');
    if (offTimeInput && offTimeGroup) {
        offTimeInput.disabled = !offTimeEnabled;
        offTimeInput.classList.toggle('input-disabled', !offTimeEnabled);
        offTimeGroup.classList.toggle('hidden', !offTimeEnabled);
    }

    // 종료 온도
    const offTempEnabled = $("#autoOffOffTempEnabledDetail")?.checked;
    const offTempInput = $("#autoOffOffTempDetail");
    const offTempGroup = document.querySelector('.offtemp-group');
    if (offTempInput && offTempGroup) {
        offTempInput.disabled = !offTempEnabled;
        offTempInput.classList.toggle('input-disabled', !offTempEnabled);
        offTempGroup.classList.toggle('hidden', !offTempEnabled);
    }
}

// Motion 관련항목 보이기/숨기기
function toggleMotionInputs() {
    // PIR
    const pirEnabled = $("#motionPirEnabledDetail")?.checked;
    const pirInput = $("#motionPirHoldSecDetail");
    const pirGroup = document.querySelector('.pir-group');
    if (pirInput && pirGroup) {
        pirInput.disabled = !pirEnabled;
        pirInput.classList.toggle('input-disabled', !pirEnabled);
        pirGroup.classList.toggle('hidden', !pirEnabled);
    }

    // BLE
    const bleEnabled = $("#motionBleEnabledDetail")?.checked;
    const bleGroup = document.querySelector('.ble-group');
    document.querySelectorAll('.motion-input-ble').forEach(input => {
        input.disabled = !bleEnabled;
        input.classList.toggle('input-disabled', !bleEnabled);
    });
    if (bleGroup) {
        bleGroup.classList.toggle('hidden', !bleEnabled);
    }
}

// Segment Mode에 따라 Fixed/Preset 입력 필드 토글
function toggleSegmentInputs(selectElement) {
    const card = selectElement.closest('.segment-card');
    if (!card) return;
    
    const mode = selectElement.value;
    const isPreset = mode === 'PRESET';

    // 고정 속도 입력 그룹
    const fixedGroup = card.querySelector('.seg-fixed-group');
    if (fixedGroup) {
        fixedGroup.classList.toggle('hidden', isPreset); // Preset이면 숨김
        
        // 고정 속도 입력
        const fixedInput = card.querySelector('.seg-fixed-input');
        if (fixedInput) {
            fixedInput.disabled = isPreset; 
            fixedInput.classList.toggle('input-disabled', isPreset);
        }
        // 고정 속도 Range Input
        const fixedRange = card.querySelector('[data-key="fixed_speed_range"]');
        if (fixedRange) {
            fixedRange.disabled = isPreset; 
            fixedRange.classList.toggle('input-disabled', isPreset);
        }
    }


    // 프리셋/조정 입력 그룹
    const presetGroup = card.querySelector('.seg-preset-group');
    if (!presetGroup) return;
    
    // 프리셋 섹션 전체 표시/숨기기
    presetGroup.classList.toggle('hidden', !isPreset); // Fixed이면 숨김

    // 입력 필드 활성화/비활성화
    presetGroup.querySelectorAll('input:not([data-key="adjust_prompt"]), select').forEach(input => {
        input.disabled = !isPreset; 
        input.classList.toggle('input-disabled', !isPreset);
    });
    
    // LLM 버튼 활성화/비활성화 (PRESET 모드에서만 활성화)
    const llmButton = card.querySelector('.btnOptimizeAdjust');
    if(llmButton) llmButton.disabled = !isPreset;
}

// --- View Toggling ---
const showListView = () => {
    $("#scheduleListSection")?.classList.remove('hidden');
    $("#scheduleDetailSection")?.classList.add('hidden');
    g_editingItemIndex = -1;
};

const showDetailView = () => {
    $("#scheduleListSection")?.classList.add('hidden');
    $("#scheduleDetailSection")?.classList.remove('hidden');
};

// ======================= List View (목록) =======================

function renderScheduleList() {
    const container = $("#scheduleListContainer");
    const countSpan = $("#scheduleCount");
    if (!container || !countSpan) return;
    
    container.innerHTML = '';
    
    // schNo 오름차순 정렬
    g_scheduleData.sort((a, b) => a.schNo - b.schNo);

    text(countSpan, g_scheduleData.length);
    
    if (g_scheduleData.length === 0) {
        container.innerHTML = `<div id="listPlaceholder" class="p-8 text-center text-gray-500">등록된 스케줄이 없습니다. 새로운 스케줄을 추가하세요.</div>`;
        return;
    }

    g_scheduleData.forEach((item, index) => {
        container.appendChild(createScheduleItem(item, index));
    });
}

function createScheduleItem(item, index) {
    const div = document.createElement('div');
    div.className = 'schedule-item';
    div.dataset.index = index;
    
    // 요일 표시를 위한 HTML 생성
    const days = item.period.days.map((d, i) => 
        `<span class="${d === 1 ? (i >= 5 ? 'text-red-500 font-bold' : 'text-blue-500 font-bold') : 'text-gray-400'}">${DAY_NAMES[i]}</span>`
    ).join(' ');

    const segmentSummary = item.segments.length > 0 ? 
        `${item.segments.length} 단계 (${item.segments[0].mode === 'PRESET' ? item.segments[0].presetCode : item.segments[0].fixed_speed + '%'})` : 
        '단계 없음';

    div.innerHTML = `
        <div class="flex flex-col space-y-1 w-full" data-action="edit" data-index="${index}">
            <div class="flex items-center space-x-2">
                <span class="font-extrabold text-lg text-gray-800">${item.name} (#${item.schNo})</span>
                <span class="text-xs font-semibold px-2 py-0.5 rounded-full ${item.enabled ? 'bg-green-100 text-green-700' : 'bg-red-100 text-red-700'}">
                    ${item.enabled ? '사용 중' : '비활성'}
                </span>
            </div>
            <div class="text-sm text-gray-600">
                ${item.period.enabled ? 
                    `시간: ${item.period.start_time} ~ ${item.period.end_time} | 요일: ${days}` : 
                    '시간대 Rule 비활성'
                }
            </div>
            <div class="text-xs text-gray-500">
                동작: ${segmentSummary} | AutoOff: ${item.autoOff.timer.enabled ? `Timer ${item.autoOff.timer.minutes}분` : 'Off'} | Motion: ${item.motion.pir.enabled ? 'PIR On' : 'Off'}
            </div>
        </div>
        <button data-index="${index}" class="ml-4 bg-yellow-500 hover:bg-yellow-600 text-white font-bold py-1 px-3 rounded-lg text-sm transition btn-toggle-status">
            ${item.enabled ? '비활성화' : '활성화'}
        </button>
    `;
    
    // 클릭 이벤트 핸들러 추가
    div.querySelector('[data-action="edit"]').addEventListener('click', () => {
         startEdit(index);
    });
    
    div.querySelector('.btn-toggle-status').addEventListener('click', (e) => {
        e.stopPropagation();
        toggleScheduleStatus(index);
    });

    return div;
}

async function toggleScheduleStatus(index) {
    const item = g_scheduleData[index];
    if (!item) return;

    item.enabled = !item.enabled;
    
    await saveSchedulesToFirestore();
    showToast(`${item.name}이(가) ${item.enabled ? '활성화' : '비활성화'}되었습니다.`, "warn");
}

// ======================= Detail View (세부 편집) =======================

// 신규 스케줄 생성 시 초기 데이터를 제공하는 헬퍼 함수
function getNewScheduleTemplate(maxSchNo) {
    const newSchNo = maxSchNo > 0 ? maxSchNo + 10 : 10;
    return {
        schNo: newSchNo,
        name: "새 스케줄",
        enabled: true,
        period: { enabled: false, days: [1, 1, 1, 1, 1, 0, 0], start_time: "08:00", end_time: "18:00" },
        seg_count: 0,
        segments: [],
        autoOff: { 
            timer: { enabled: false, minutes: 0 }, 
            offTime: { enabled: false, time: "23:59" }, 
            offTemp: { enabled: false, temp: 0.0 } 
        },
        motion: { 
            pir: { enabled: false, hold_sec: 0 }, 
            ble: { enabled: false, rssi_threshold: -70, hold_sec: 0 } 
        }
    };
}

function startNewSchedule() {
    // 현재 목록에 있는 schNo 중 최대값을 찾아 +10
    const maxSchNo = g_scheduleData.reduce((max, item) => Math.max(max, item.schNo), 0);
    
    // 임시 항목을 생성하고 배열에 추가
    const newItem = getNewScheduleTemplate(maxSchNo);
    g_scheduleData.push(newItem);
    g_editingItemIndex = g_scheduleData.length - 1;
    
    renderScheduleDetail(newItem);
    
    $("#btnDeleteSchedule")?.classList.add('hidden');
    showDetailView();
}

function startEdit(index) {
    if (index < 0 || index >= g_scheduleData.length) return; // 유효성 검사 추가
    
    g_editingItemIndex = index;
    const item = g_scheduleData[index];
    renderScheduleDetail(item);
    $("#btnDeleteSchedule")?.classList.remove('hidden');
    showDetailView();
}

function renderScheduleDetail(item) {
    // UI에 데이터 바인딩
    text($("#detailTitle"), `편집: ${item.name}`);
    $("#schNoDetail").value = item.schNo;
    $("#scheduleNameDetail").value = item.name;
    $("#scheduleEnabledDetail").checked = item.enabled;

    // Period
    $("#periodEnabledDetail").checked = item.period.enabled;
    $("#startTimeDetail").value = item.period.start_time;
    $("#endTimeDetail").value = item.period.end_time;
    
    // Days
    document.querySelectorAll('#daySelectorsDetail input[type="checkbox"]').forEach((input, index) => {
         input.checked = (item.period.days[index] === 1);
    });
    
    // AutoOff
    $("#autoOffTimerEnabledDetail").checked = item.autoOff.timer.enabled;
    $("#autoOffTimerMinutesDetail").value = item.autoOff.timer.minutes;
    $("#autoOffOffTimeEnabledDetail").checked = item.autoOff.offTime.enabled;
    $("#autoOffOffTimeDetail").value = item.autoOff.offTime.time;
    $("#autoOffOffTempEnabledDetail").checked = item.autoOff.offTemp.enabled;
    $("#autoOffOffTempDetail").value = item.autoOff.offTemp.temp;
    
    // Motion
    $("#motionPirEnabledDetail").checked = item.motion.pir.enabled;
    $("#motionPirHoldSecDetail").value = item.motion.pir.hold_sec;
    $("#motionBleEnabledDetail").checked = item.motion.ble.enabled;
    $("#motionBleRssiThresholdDetail").value = item.motion.ble.rssi_threshold;
    $("#motionBleHoldSecDetail").value = item.motion.ble.hold_sec;

    // 중요: UI 업데이트 후 입력 상태 토글 (가시성/활성화 제어)
    togglePeriodInputs(); 
    toggleAutoOffInputs(); 
    toggleMotionInputs(); 

    renderSegmentList(item.segments);
}

function renderSegmentList(segments) {
    const container = $("#segmentListDetail");
    if (!container) return;

    container.innerHTML = '';
    
    if (segments.length === 0) {
         container.innerHTML = `<div class="p-4 text-center text-gray-500">직동 단계를 추가해주세요.</div>`;
        return;
    }

    // segNo 기준으로 렌더링 전에 정렬
    segments.sort((a, b) => a.segNo - b.segNo);
    
    segments.forEach((seg, index) => {
        const segElement = createSegmentElement(seg, index);
        container.appendChild(segElement);
    });
    
    // 렌더링 후 동적으로 생성된 요소에 대한 입력 상태 토글 적용
    document.querySelectorAll('.seg-mode-select').forEach(select => {
        toggleSegmentInputs(select);
    });
}

function createSegmentElement(seg, index) {
    const div = document.createElement('div');
    div.className = 'segment-card';
    
    // 현재 세그먼트의 presetCode가 g_presets에 없으면 기본값 ('OCEAN') 사용
    const selectedPresetCode = g_presets.some(p => p.code === seg.presetCode) ? seg.presetCode : (g_presets[0]?.code || "OCEAN");

    const presetOptions = g_presets.map(p => 
        `<option value="${p.code}" ${p.code === selectedPresetCode ? 'selected' : ''}>${p.name} (${p.code})</option>`
    ).join('');

    div.innerHTML = `
        <div class="flex justify-between items-center mb-3">
            <div class="flex items-center space-x-3">
                <h4 class="font-bold text-base text-gray-800">Step</h4>
                <input type="number" data-key="segNo" value="${seg.segNo}" min="1" class="input-text text-sm w-16 p-1 text-center">
            </div>
            <button data-index="${index}" class="btnDeleteSegment bg-red-500 hover:bg-red-600 text-white py-1 px-3 text-xs rounded transition">삭제</button>
        </div>
        
        <div class="grid grid-cols-2 md:grid-cols-4 gap-4 mb-4">
            <div>
                <label class="text-xs text-gray-600">작동 시간 (분)</label>
                <input type="number" data-key="on_minutes" value="${seg.on_minutes}" min="0" class="input-text text-sm">
            </div>
            <div>
                <label class="text-xs text-gray-600">대기 시간 (분)</label>
                <input type="number" data-key="off_minutes" value="${seg.off_minutes}" min="0" class="input-text text-sm">
            </div>
             <div class="col-span-2 md:col-span-1">
                <label class="text-xs text-gray-600">작동 모드</label>
                <select data-key="mode" class="input-select text-sm seg-mode-select">
                    <option value="PRESET" ${seg.mode === 'PRESET' ? 'selected' : ''}>Preset</option>
                    <option value="FIXED" ${seg.mode === 'FIXED' ? 'selected' : ''}>고정속도</option>
                </select>
            </div>
             
             <div class="col-span-2 md:col-span-1 seg-fixed-group">
                <label class="block text-xs text-gray-600">고정속도 (%)</label>
                <input type="number" data-key="fixed_speed" value="${seg.fixed_speed.toFixed(1)}" min="0" max="100" step="0.1" class="input-text text-sm seg-fixed-input mb-1">
                <input type="range" data-key="fixed_speed_range" value="${seg.fixed_speed.toFixed(1)}" min="0" max="100" step="0.1" class="w-full">
            </div>
        </div>

        <div class="preset-details space-y-3 mt-3 p-3 bg-gray-50 rounded-lg seg-preset-group">
            <h5 class="text-sm font-semibold mb-2">Preset 상세 조정 (Adjust)</h5>
            <div class="grid grid-cols-2 gap-4">
                <div>
                    <label class="text-xs text-gray-600">풍향 Preset</label>
                    <select data-key="presetCode" class="input-select text-sm seg-preset-input">
                        ${presetOptions}
                    </select>
                </div>
                <div>
                    <label class="text-xs text-gray-600">풍향 스타일</label>
                    <select data-key="styleCode" class="input-select text-sm seg-style-input">
                        <option value="BALANCE">BALANCE</option>
                    </select>
                </div>
            </div>
            
            <div class="grid grid-cols-2 gap-4">
                <div>
                    <label class="block text-xs text-gray-600">강도 조정(±1.0)</label>
                    <input type="number" data-key="adjust.wind_intensity" value="${seg.adjust.wind_intensity.toFixed(1)}" step="0.1" min="-1.0" max="1.0" class="input-text text-sm seg-adjust-input mb-1">
                    <input type="range" data-key="adjust.wind_intensity_range" value="${seg.adjust.wind_intensity.toFixed(1)}" step="0.1" min="-1.0" max="1.0" class="w-full">
                </div>
                <div>
                    <label class="block text-xs text-gray-600">변동성 조정(±1.0)</label>
                    <input type="number" data-key="adjust.wind_variability" value="${seg.adjust.wind_variability.toFixed(1)}" step="0.1" min="-1.0" max="1.0" class="input-text text-sm seg-adjust-input mb-1">
                    <input type="range" data-key="adjust.wind_variability_range" value="${seg.adjust.wind_variability.toFixed(1)}" step="0.1" min="-1.0" max="1.0" class="w-full">
                </div>
            </div>
             
             <div class="col-span-2 mt-4 p-3 bg-blue-100 rounded-lg border border-blue-200">
                <h5 class="text-sm font-bold text-blue-800 mb-2 flex items-center">
                    ✨ LLM 기반 조정값 최적화 (Preset 모드 전용)
                </h5>
                <textarea rows="2" data-key="adjust_prompt" class="input-text w-full text-sm mb-2" placeholder="예: '조금 더 부드럽고 무작위적인 새벽 바람처럼 바꿔줘'"></textarea>
                <div class="flex justify-between items-center">
                    <button data-index="${index}" class="btnOptimizeAdjust bg-blue-600 hover:bg-blue-700 text-white font-bold py-1 px-3 text-xs rounded transition disabled:bg-blue-400">
                        ✨ 조정값 최적화 요청
                    </button>
                    <div data-index="${index}" class="llm-adjust-status text-xs text-blue-700 flex items-center hidden">
                        <span class="animate-spin rounded-full h-4 w-4 border-t-2 border-b-2 border-blue-500 mr-2"></span>
                        처리 중...
                    </div>
                </div>
            </div>
        </div>
    `;
    
    // --- 이벤트 바인딩: Range/Number 동기화 ---
    const syncRangeAndNumber = (rangeInput, numberInput) => {
        numberInput.addEventListener('input', () => {
            rangeInput.value = numberInput.value;
        });
        rangeInput.addEventListener('input', () => {
            numberInput.value = rangeInput.value;
        });
    };
    
    // 고정 속도 동기화
    syncRangeAndNumber(
        div.querySelector('[data-key="fixed_speed_range"]'),
        div.querySelector('[data-key="fixed_speed"]')
    );
    // 강도 조정 동기화
    syncRangeAndNumber(
        div.querySelector('[data-key="adjust.wind_intensity_range"]'),
        div.querySelector('[data-key="adjust.wind_intensity"]')
    );
    // 변동성 조정 동기화
    syncRangeAndNumber(
        div.querySelector('[data-key="adjust.wind_variability_range"]'),
        div.querySelector('[data-key="adjust.wind_variability"]')
    );
    
    // 세그먼트 모드 변경 시 입력 필드 토글
    div.querySelector('.seg-mode-select')?.addEventListener('change', (e) => {
        toggleSegmentInputs(e.currentTarget);
    });
    
    return div;
}

// 개별 세그먼트 카드에서 데이터를 수집하는 헬퍼 함수
function collectSegmentData(card, fallbackSegNo) {
    const segNoInput = card.querySelector('[data-key="segNo"]');
    let segNo = Number(segNoInput?.value);
    
    // segNo 유효성 검사 (1 이상의 정수)
    if (segNo <= 0 || !Number.isInteger(segNo)) {
         segNo = fallbackSegNo;
         if (segNoInput) segNoInput.value = segNo; // UI 값도 수정
    }
    
    return {
        segNo: segNo, 
        on_minutes: Number(card.querySelector('[data-key="on_minutes"]')?.value) || 0,
        off_minutes: Number(card.querySelector('[data-key="off_minutes"]')?.value) || 0,
        mode: card.querySelector('.seg-mode-select')?.value || 'PRESET',
        // Range input이 아닌 Number input에서 값을 읽어야 합니다.
        fixed_speed: Number(card.querySelector('[data-key="fixed_speed"]')?.value) || 0.0,
        
        // Preset/Style/Adjust
        presetCode: card.querySelector('[data-key="presetCode"]')?.value || 'OCEAN',
        styleCode: card.querySelector('[data-key="styleCode"]')?.value || 'BALANCE',
        adjust: {
            wind_intensity: Number(card.querySelector('[data-key="adjust.wind_intensity"]')?.value) || 0.0,
            wind_variability: Number(card.querySelector('[data-key="adjust.wind_variability"]')?.value) || 0.0,
            gust_frequency: 0.0, 
            fan_limit: 0.0, 
            min_fan: 0.0, 
        }
    };
}

// --- Segment Management ---

function addSegment() {
    const item = g_scheduleData[g_editingItemIndex];
    if (!item) return showToast("스케줄 편집 모드가 아닙니다.", "err");

    // 현재 segments 중 최대 segNo를 찾거나, 없다면 0
    const maxSegNo = item.segments.reduce((max, seg) => Math.max(max, seg.segNo), 0);
    
    const newSegment = {
        segNo: maxSegNo > 0 ? maxSegNo + 1 : 1, // 최대값 + 1
        on_minutes: 15,
        off_minutes: 5,
        mode: "PRESET",
        presetCode: g_presets[0]?.code || "OCEAN",
        styleCode: "BALANCE",
        adjust: { wind_intensity: 0.0, wind_variability: 0.0, gust_frequency: 0.0, fan_limit: 0.0, min_fan: 0.0 },
        fixed_speed: 0.0
    };
    
    item.segments.push(newSegment);
    renderSegmentList(item.segments);
    showToast("작동 단계 추가 완료", "ok"); 
}

function deleteSegment(index) {
    const item = g_scheduleData[g_editingItemIndex];
    if (!item || index < 0 || index >= item.segments.length) return;

    item.segments.splice(index, 1);

    renderSegmentList(item.segments); // 재렌더링 시 자동 정렬
    showToast("작동 단계 삭제됨", "warn");
}

// --- Detail Data Collection and Save ---

function collectScheduleDetailData(item) {
    // 1. Basic/Period Data
    item.schNo = Number($("#schNoDetail")?.value) || item.schNo; // Read editable schNo
    item.name = $("#scheduleNameDetail")?.value || item.name;
    item.enabled = $("#scheduleEnabledDetail")?.checked ?? item.enabled;
    item.period.enabled = $("#periodEnabledDetail")?.checked ?? item.period.enabled;
    item.period.start_time = $("#startTimeDetail")?.value || "00:00";
    item.period.end_time = $("#endTimeDetail")?.value || "00:00";

    // 2. Days Data
    document.querySelectorAll('#daySelectorsDetail input[type="checkbox"]').forEach((input, index) => {
         item.period.days[index] = input.checked ? 1 : 0;
    });

    // 3. AutoOff Data (Number casting 및 기본값 처리 강화)
    item.autoOff.timer.enabled = $("#autoOffTimerEnabledDetail")?.checked ?? item.autoOff.timer.enabled;
    item.autoOff.timer.minutes = Number($("#autoOffTimerMinutesDetail")?.value) || 0;
    item.autoOff.offTime.enabled = $("#autoOffOffTimeEnabledDetail")?.checked ?? item.autoOff.offTime.enabled;
    item.autoOff.offTime.time = $("#autoOffOffTimeDetail")?.value || "23:59";
    item.autoOff.offTemp.enabled = $("#autoOffOffTempEnabledDetail")?.checked ?? item.autoOff.offTemp.enabled;
    item.autoOff.offTemp.temp = Number($("#autoOffOffTempDetail")?.value) || 0.0;

    // 4. Motion Data (Number casting 및 기본값 처리 강화)
    item.motion.pir.enabled = $("#motionPirEnabledDetail")?.checked ?? item.motion.pir.enabled;
    item.motion.pir.hold_sec = Number($("#motionPirHoldSecDetail")?.value) || 0;
    item.motion.ble.enabled = $("#motionBleEnabledDetail")?.checked ?? item.motion.ble.enabled;
    item.motion.ble.rssi_threshold = Number($("#motionBleRssiThresholdDetail")?.value) || -70; // RSSI는 음수
    item.motion.ble.hold_sec = Number($("#motionBleHoldSecDetail")?.value) || 0;

    // 5. Segment Data Collection (Rebuild Segments Array)
    const newSegments = [];
    document.querySelectorAll('#segmentListDetail .segment-card').forEach((card, index) => {
        // collectSegmentData를 사용하여 현재 UI 상태의 데이터를 수집
        const segmentData = collectSegmentData(card, index + 1);
        newSegments.push(segmentData);
    });

    // segNo 오름차순 정렬
    newSegments.sort((a, b) => a.segNo - b.segNo);
    
    // Update item segments
    item.seg_count = newSegments.length;
    item.segments = newSegments;

    return item;
}

async function saveScheduleDetail() {
    if (g_editingItemIndex < 0 || !isAuthReady) {
         return showToast("편집 중이 아니거나 인증이 완료되지 않았습니다.", "err");
    }
    
    const item = g_scheduleData[g_editingItemIndex];
    // 1. 데이터 수집 및 유효성 검사
    collectScheduleDetailData(item); 
    
    // --- schNo 유효성 및 중복 검사 ---
    const currentSchNo = Number(item.schNo);
    
    if (currentSchNo <= 0 || !Number.isInteger(currentSchNo)) {
         return showToast("스케줄 번호는 1 이상의 정수여야 합니다.", "err");
    }
    
    const schNoDuplicated = g_scheduleData.filter((sch, i) => 
        sch.schNo === currentSchNo && i !== g_editingItemIndex
    ).length > 0;
    
    if (schNoDuplicated) {
        return showToast(`스케줄 번호 ${currentSchNo}은(는) 이미 사용 중입니다. 다른 번호를 사용해 주세요.`, "err");
    }
    // ------------------------------------

    if (!item.name || item.name.trim() === '') {
        return showToast("스케줄 이름을 입력해주세요.", "err");
    }
    
    if (item.segments.length === 0) {
         return showToast("하나 이상의 동작 단계를 추가해야 합니다.", "err");
    }
    
    // 작동 오류 방지: FIXED 모드일 때 속도가 0인지 확인
    const invalidFixedSeg = item.segments.find(seg => seg.mode === 'FIXED' && seg.fixed_speed <= 0);
    if (invalidFixedSeg) {
        return showToast(`고정 속도 모드(Step ${invalidFixedSeg.segNo})는 속도(%)를 0보다 크게 설정해야 합니다.`, 'err');
    }

    // 2. 서버로 전체 스케줄 목록 전송 (Firestore)
    await saveSchedulesToFirestore();

    // 3. 목록 뷰로 돌아가기 
    showListView();
}

async function deleteSchedule() {
    if (g_editingItemIndex < 0) return;
    const item = g_scheduleData[g_editingItemIndex];

    g_scheduleData.splice(g_editingItemIndex, 1);
    
    await saveSchedulesToFirestore();
    showListView();
    showToast(`${item.name} 스케줄이 삭제되었습니다.`, 'err');
}

// ======================= Initialization and Event Binding =======================

function bindEvents() {
    $("#btnRefresh")?.addEventListener("click", () => {
        if (isAuthReady) {
            renderScheduleList();
            showToast("목록 UI 새로고침 완료", "ok");
        } else {
             showToast("인증이 완료되지 않았습니다.", "warn");
        }
    }); 
    $("#btnAddNewSchedule")?.addEventListener("click", startNewSchedule);
    $("#btnCancelEdit")?.addEventListener("click", showListView);
    $("#btnSaveDetail")?.addEventListener("click", saveScheduleDetail);
    $("#btnDeleteSchedule")?.addEventListener("click", deleteSchedule);
    
    // LLM 기능 1
    $("#btnSuggestName")?.addEventListener("click", handleSuggestName);
    
    // Segment Delete (이벤트 위임)
    $("#segmentListDetail")?.addEventListener('click', (e) => {
        if (e.target.classList.contains('btnDeleteSegment')) {
            const segIndex = Number(e.target.dataset.index);
            deleteSegment(segIndex);
        }
    });
    
    // Segment Add
    $("#btnAddSegmentDetail")?.addEventListener("click", addSegment);
    
    // --- Input Toggling Events (Change events) ---
    
    // Period / Days
    $("#periodEnabledDetail")?.addEventListener('change', togglePeriodInputs);

    // AutoOff
    document.querySelectorAll('.autooff-toggle').forEach(el => el.addEventListener('change', toggleAutoOffInputs));

    // Motion
    document.querySelectorAll('.motion-toggle-pir').forEach(el => el.addEventListener('change', toggleMotionInputs));
    document.querySelectorAll('.motion-toggle-ble').forEach(el => el.addEventListener('change', toggleMotionInputs));
    
    // 세그먼트 목록에 위임된 모드 토글 이벤트
    $("#segmentListDetail")?.addEventListener('change', (e) => {
        if (e.target.classList.contains('seg-mode-select')) {
            toggleSegmentInputs(e.target);
        }
    });
    
    // LLM 기능 2: Preset 조정 최적화 (이벤트 위임)
    $("#segmentListDetail")?.addEventListener('click', (e) => {
        if (e.target.classList.contains('btnOptimizeAdjust')) {
            handleOptimizeAdjust(e.target);
        }
    });

}

function renderDaySelectors() {
    const container = $("#daySelectorsDetail");
    if (!container) return;
    
    DAY_NAMES.forEach((name, index) => {
        const label = document.createElement('label');
        label.className = 'inline-flex items-center rounded-lg cursor-pointer transition day-toggle'; 
        label.innerHTML = `
            <input type="checkbox" id="day-${index}" data-day-index="${index}">
            <span class="text-sm font-medium ${index >= 5 ? 'text-red-500' : 'text-gray-700'}">${name}</span>
        `;
        container.appendChild(label);
    });
}

// 전역적으로 접근 가능하도록 노출
window.scheduleManager = {
    startEdit,
    toggleScheduleStatus 
};

document.addEventListener("DOMContentLoaded", () => {
    renderDaySelectors(); 
    bindEvents();
    initializeFirebase();
});
