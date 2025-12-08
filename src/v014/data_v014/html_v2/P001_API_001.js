/**
 * Smart Nature Wind - REST API Constants (v030)
 * 백엔드 W10_Web_Const_030.h와 동기화됨
 */

// 1. REST API 기본 Prefix 및 버전 제어
const API_HTTP_BASE      = "/api/v001";
const API_WS_BASE        = "/ws";

// 2. 시스템 정보 및 상태 조회
const API_HTTP_VERSION        = `${API_HTTP_BASE}/version`;
const API_HTTP_STATE          = `${API_HTTP_BASE}/state`;
const API_HTTP_SYSTEM         = `${API_HTTP_BASE}/system`;
const API_HTTP_WIFI           = `${API_HTTP_BASE}/wifi`;
const API_HTTP_DIAG           = `${API_HTTP_BASE}/diag`;
const API_HTTP_SCAN           = `${API_HTTP_BASE}/scan`;
const API_HTTP_AUTH_TEST      = `${API_HTTP_BASE}/auth/test`;
const API_HTTP_WIFI_CONFIG    = `${API_HTTP_BASE}/network/wifi/config`;
const API_HTTP_TIME_SET       = `${API_HTTP_BASE}/system/time/set`;
const API_HTTP_FW_CHECK       = `${API_HTTP_BASE}/system/firmware/check`;

// 3. 설정 및 실시간 제어
const API_HTTP_MOTION         = `${API_HTTP_BASE}/motion`;
const API_HTTP_SIMULATION     = `${API_HTTP_BASE}/simulation`;
const API_HTTP_SIM_STATE      = `${API_HTTP_BASE}/sim/state`;
const API_HTTP_CONTROL_SUMMARY= `${API_HTTP_BASE}/control/summary`;

// 4. 설정 관리 (Configuration Life Cycle)
const API_HTTP_CONFIG_SAVE    = `${API_HTTP_BASE}/config/save`;
const API_HTTP_CONFIG_DIRTY   = `${API_HTTP_BASE}/config/dirty`;
const API_HTTP_CONFIG_INIT    = `${API_HTTP_BASE}/config/init`;
const API_HTTP_RELOAD         = `${API_HTTP_BASE}/reload`;

// 5. CRUD 엔드포인트
const API_HTTP_WIND_PROFILE   = `${API_HTTP_BASE}/windProfile`;
const API_HTTP_SCHEDULES      = `${API_HTTP_BASE}/schedules`;
const API_HTTP_USER_PROFILES  = `${API_HTTP_BASE}/user_profiles`;
const API_HTTP_USER_PATCH     = `${API_HTTP_BASE}/user_profiles/patch`;

// 6. 하드웨어 직접 제어
const API_HTTP_CTL_REBOOT     = `${API_HTTP_BASE}/control/reboot`;
const API_HTTP_CTL_FACTORY    = `${API_HTTP_BASE}/control/factoryReset`;
const API_HTTP_CTL_PROF_SEL   = `${API_HTTP_BASE}/control/profile/select`;
const API_HTTP_CTL_PROF_STOP  = `${API_HTTP_BASE}/control/profile/stop`;
const API_HTTP_CTL_OVR_FIXED  = `${API_HTTP_BASE}/control/override/fixed`;
const API_HTTP_CTL_OVR_PRESET = `${API_HTTP_BASE}/control/override/preset`;
const API_HTTP_CTL_OVR_CLEAR  = `${API_HTTP_BASE}/control/override/clear`;

// 7. 데이터 피드 및 메트릭스
const API_HTTP_FEED_PIR       = `${API_HTTP_BASE}/motion/pir/feed`;
const API_HTTP_FEED_BLE       = `${API_HTTP_BASE}/motion/ble/feed`;
const API_HTTP_METRICS        = `${API_HTTP_BASE}/metrics`;
const API_HTTP_LOGS           = `${API_HTTP_BASE}/logs`;

// 8. 파일 및 업데이트 (Upload & OTA)
const API_HTTP_FILE_UPLOAD    = `${API_HTTP_BASE}/fileUpload`;
const API_HTTP_FW_UPDATE      = `${API_HTTP_BASE}/fwUpdate`;

// 9. 웹 인터페이스 및 메뉴
const API_HTTP_MENU           = `${API_HTTP_BASE}/menu`;

// 10. WebSocket 엔드포인트
const WS_API_LOG              = `${API_WS_BASE}/log`;
const WS_API_STATE            = `${API_WS_BASE}/state`;
const WS_API_CHART            = `${API_WS_BASE}/chart`;
const WS_API_METRICS          = `${API_WS_BASE}/metrics`;

