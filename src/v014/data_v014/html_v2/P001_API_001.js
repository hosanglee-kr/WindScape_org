/**
 * Smart Nature Wind - REST API Constants (v030)
 * 백엔드 W10_Web_Const_030.h와 동기화됨
 */

// 1. REST API 기본 Prefix 및 버전 제어
export const API_HTTP_BASE      = "/api/v001";
export const API_WS_BASE        = "/ws";

// 2. 시스템 정보 및 상태 조회 (System & Status)
export const API_HTTP_VERSION         = `${API_HTTP_BASE}/version`;
export const API_HTTP_STATE           = `${API_HTTP_BASE}/state`;
export const API_HTTP_SYSTEM          = `${API_HTTP_BASE}/system`;

// W10_Web_Const_030 기준: 별도 /wifi 엔드포인트 없음
// export const API_HTTP_WIFI        = `${API_HTTP_BASE}/wifi`;

export const API_HTTP_WIFI_SCAN       = `${API_HTTP_BASE}/wifi/scan`;
export const API_HTTP_WIFI_CONFIG     = `${API_HTTP_BASE}/wifi/config`;

export const API_HTTP_DIAG            = `${API_HTTP_BASE}/diag`;
export const API_HTTP_AUTH_TEST       = `${API_HTTP_BASE}/auth/test`;
export const API_HTTP_TIME_SET        = `${API_HTTP_BASE}/system/time/set`;
export const API_HTTP_FW_CHECK        = `${API_HTTP_BASE}/system/firmware/check`;

// 3. 설정 및 실시간 제어 (Control & Simulation)
export const API_HTTP_MOTION          = `${API_HTTP_BASE}/motion`;
export const API_HTTP_SIMULATION      = `${API_HTTP_BASE}/simulation`;
export const API_HTTP_SIM_STATE       = `${API_HTTP_BASE}/sim/state`;
export const API_HTTP_CONTROL_SUMMARY = `${API_HTTP_BASE}/control/summary`;

// 4. 설정 관리 (Configuration Life Cycle)
export const API_HTTP_CONFIG_SAVE     = `${API_HTTP_BASE}/config/save`;
export const API_HTTP_CONFIG_DIRTY    = `${API_HTTP_BASE}/config/dirty`;
export const API_HTTP_CONFIG_INIT     = `${API_HTTP_BASE}/config/init`;
export const API_HTTP_RELOAD          = `${API_HTTP_BASE}/reload`;

// 5. CRUD 엔드포인트 (Profile, Schedule, User)
export const API_HTTP_WIND_PROFILE    = `${API_HTTP_BASE}/windProfile`;
export const API_HTTP_SCHEDULES       = `${API_HTTP_BASE}/schedules`;
export const API_HTTP_USER_PROFILES   = `${API_HTTP_BASE}/user_profiles`;
export const API_HTTP_USER_PROFILES_PATCH = `${API_HTTP_BASE}/user_profiles/patch`;

// 6. 하드웨어 직접 제어 (Direct HW Control)
export const API_HTTP_CTL_REBOOT      = `${API_HTTP_BASE}/control/reboot`;
export const API_HTTP_CTL_FACTORY     = `${API_HTTP_BASE}/control/factoryReset`;
export const API_HTTP_CTL_PROF_SEL    = `${API_HTTP_BASE}/control/profile/select`;
export const API_HTTP_CTL_PROF_STOP   = `${API_HTTP_BASE}/control/profile/stop`;
export const API_HTTP_CTL_OVR_FIXED   = `${API_HTTP_BASE}/control/override/fixed`;
export const API_HTTP_CTL_OVR_PRESET  = `${API_HTTP_BASE}/control/override/preset`;
export const API_HTTP_CTL_OVR_CLEAR   = `${API_HTTP_BASE}/control/override/clear`;

// 7. 데이터 피드 및 메트릭스 (Feed & Metrics)
export const API_HTTP_FEED_PIR        = `${API_HTTP_BASE}/motion/pir/feed`;
export const API_HTTP_FEED_BLE        = `${API_HTTP_BASE}/motion/ble/feed`;
export const API_HTTP_METRICS         = `${API_HTTP_BASE}/metrics`;
export const API_HTTP_LOGS            = `${API_HTTP_BASE}/logs`;

// 8. 파일 및 업데이트 (Upload & OTA)
export const API_HTTP_FILE_UPLOAD     = `${API_HTTP_BASE}/fileUpload`;
export const API_HTTP_FW_UPDATE       = `${API_HTTP_BASE}/fwUpdate`;

// 9. 웹 인터페이스 및 메뉴 (UI)
export const API_HTTP_MENU            = `${API_HTTP_BASE}/menu`;

// 10. WebSocket 엔드포인트
export const WS_API_LOG               = `${API_WS_BASE}/log`;
export const WS_API_STATE             = `${API_WS_BASE}/state`;
export const WS_API_CHART             = `${API_WS_BASE}/chart`;
export const WS_API_METRICS           = `${API_WS_BASE}/metrics`;

