#ifndef W10_CONST_H
#define W10_CONST_H

namespace W10_Const {

    // --------------------------------------------------
    // 0. REST API 기본 Prefix 및 버전 제어
    // --------------------------------------------------
    constexpr char API_HTTP_BASE[]      = "/api/v001";
    constexpr char API_WS_BASE[]        = "/ws";

    // --------------------------------------------------
    // 1. 시스템 정보 및 상태 조회 (System & Status)
    // --------------------------------------------------
    constexpr char API_VERSION[]        = API_HTTP_BASE "/version";
    constexpr char API_STATE[]          = API_HTTP_BASE "/state";
    constexpr char API_SYSTEM[]         = API_HTTP_BASE "/system";
    constexpr char API_WIFI[]           = API_HTTP_BASE "/wifi";
    constexpr char API_DIAG[]           = API_HTTP_BASE "/diag";
    constexpr char API_SCAN[]           = API_HTTP_BASE "/scan";
    constexpr char API_AUTH_TEST[]      = API_HTTP_BASE "/auth/test";
    constexpr char API_WIFI_CONFIG[]    = API_HTTP_BASE "/network/wifi/config";
    constexpr char API_TIME_SET[]       = API_HTTP_BASE "/system/time/set";
    constexpr char API_FW_CHECK[]       = API_HTTP_BASE "/system/firmware/check";

    // --------------------------------------------------
    // 2. 설정 및 실시간 제어 (Control & Simulation)
    // --------------------------------------------------
    constexpr char API_MOTION[]         = API_HTTP_BASE "/motion";
    constexpr char API_SIMULATION[]     = API_HTTP_BASE "/simulation";
    constexpr char API_SIM_STATE[]      = API_HTTP_BASE "/sim/state";
    constexpr char API_CONTROL_SUMMARY[]= API_HTTP_BASE "/control/summary";

    // --------------------------------------------------
    // 3. 설정 관리 (Configuration Life Cycle)
    // --------------------------------------------------
    constexpr char API_CONFIG_SAVE[]    = API_HTTP_BASE "/config/save";
    constexpr char API_CONFIG_DIRTY[]   = API_HTTP_BASE "/config/dirty";
    constexpr char API_CONFIG_INIT[]    = API_HTTP_BASE "/config/init";
    constexpr char API_RELOAD[]         = API_HTTP_BASE "/reload";

    // --------------------------------------------------
    // 4. 리소스 CRUD (Profile, Schedule, User)
    // --------------------------------------------------
    constexpr char API_WIND_PROFILE[]   = API_HTTP_BASE "/windProfile";
    constexpr char API_SCHEDULES[]      = API_HTTP_BASE "/schedules";
    constexpr char API_USER_PROFILES[]  = API_HTTP_BASE "/user_profiles";
    constexpr char API_USER_PATCH[]     = API_HTTP_BASE "/user_profiles/patch";

    // --------------------------------------------------
    // 5. 하드웨어 직접 제어 (Direct HW Control)
    // --------------------------------------------------
    constexpr char API_CTL_REBOOT[]     = API_HTTP_BASE "/control/reboot";
    constexpr char API_CTL_FACTORY[]    = API_HTTP_BASE "/control/factoryReset";
    constexpr char API_CTL_PROF_SEL[]   = API_HTTP_BASE "/control/profile/select";
    constexpr char API_CTL_PROF_STOP[]  = API_HTTP_BASE "/control/profile/stop";
    constexpr char API_CTL_OVR_FIXED[]  = API_HTTP_BASE "/control/override/fixed";
    constexpr char API_CTL_OVR_PRESET[] = API_HTTP_BASE "/control/override/preset";
    constexpr char API_CTL_OVR_CLEAR[]  = API_HTTP_BASE "/control/override/clear";

    // --------------------------------------------------
    // 6. 데이터 피드 및 메트릭스 (Feed & Metrics)
    // --------------------------------------------------
    constexpr char API_FEED_PIR[]       = API_HTTP_BASE "/motion/pir/feed";
    constexpr char API_FEED_BLE[]       = API_HTTP_BASE "/motion/ble/feed";
    constexpr char API_METRICS[]        = API_HTTP_BASE "/metrics";
    constexpr char API_LOGS[]           = API_HTTP_BASE "/logs";

    // --------------------------------------------------
    // 7. 파일 및 업데이트 (Upload & OTA)
    // --------------------------------------------------
    constexpr char API_FILE_UPLOAD[]    = API_HTTP_BASE "/fileUpload";
    constexpr char API_FW_UPDATE[]      = API_HTTP_BASE "/fwUpdate";
    
    // --------------------------------------------------
    // 8. 웹 인터페이스 및 메뉴 (UI & Static Assets)
    // --------------------------------------------------
    constexpr char API_MENU[]           = API_HTTP_BASE "/menu";
    constexpr char PATH_STATIC_ROOT[]   = "/html_v2";
    constexpr char FOLDER_JSON[]        = "/json/";
    constexpr char FOLDER_HTML[]        = "/html_v2/";
    constexpr char FILE_PAGES_JSON[]    = "/json/cfg_pages_032.json";

    // --------------------------------------------------
    // 9. WebSocket 엔드포인트 (v029 반영)
    // --------------------------------------------------
    constexpr char API_WS_LOG[]         = API_WS_BASE "/log";     
    constexpr char API_WS_STATE[]       = API_WS_BASE "/state";   
    constexpr char API_WS_CHART[]       = API_WS_BASE "/chart";   
    constexpr char API_WS_METRICS[]     = API_WS_BASE "/metrics"; 

} // namespace W10_Const

#endif // W10_CONST_H

