#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : N10_NVSManager_015.h
 * 모듈약어 : N10
 * 모듈명 : Smart Nature Wind NVS Persistence Manager (v015)
 * ------------------------------------------------------
 * 기능 요약
 *  - ESP32 NVS(Preferences) 기반 안정 영속화 계층
 *  - Lazy-Save Queue (디바운스/버스트 병합)로 불필요한 쓰기 최소화
 *  - CRC32 무결성 검증 + .bak 백업/복구
 *  - 섹션별 Factory Reset 지원 (SYSTEM/WIFI/SCHEDULES/USERPROFILES/MOTION/RUNSTATE/WINDDICT)
 *  - JSON 전용 헬퍼(loadJson/saveJson/queueSaveJson) + Raw 바이트 저장/복구
 *  - 상태 통계 toJson()
 * ------------------------------------------------------
 * [구현 규칙]
 *  - 항상 소스 시작 주석 체계 유지
 *  - ArduinoJson v7.x.x 사용 (v6 이하 사용 금지)
 *  - JsonDocument 단일 타입만 사용
 *  - createNestedArray/Object/containsKey 사용 금지
 *  - memset + strlcpy 기반 안전 초기화
 *  - 주석/필드명은 JSON 구조와 동일하게 유지
 *  - 모듈별 단일 헤더(h)파일로만 구성 (cpp 없음)
 * ------------------------------------------------------
 * [코드 네이밍 규칙]
 *   - 전역 상수,매크로      : G_모듈약어_ 접두사
 *   - 전역 변수             : g_모듈약어_ 접두사
 *   - 전역 함수             : 모듈약어_ 접두사
 *   - type                  : T_모듈약어_ 접두사
 *   - typedef               : _t  접미사
 *   - enum 상수             : EN_모듈약어_ 접두사
 *   - 구조체                : ST_모듈약어_ 접두사
 *   - 클래스명              : CL_모듈약어_ 접두사
 *   - 클래스 private 멤버   : _ 접두사
 *   - 클래스 멤버(함수/변수) : 모듈약어 접두사 미사용
 *   - 클래스 정적 멤버      : s_ 접두사
 *   - 함수 로컬 변수        : v_ 접두사
 *   - 함수 인자             : p_ 접두사
 * ------------------------------------------------------
 */

#include <Arduino.h>
#include <ArduinoJson.h>   // v7.x
#include <Preferences.h>
#include <vector>

/* ======================================================
 * 전역 상수/타입
 * ====================================================== */
namespace N10_Const {
    // NVS 네임스페이스 기본값
    static constexpr const char* G_N10_NVS_NS_DEFAULT = "SNW_NVS";

    // 헤더/버전/매직
    static constexpr uint32_t G_N10_MAGIC     = 0x4E31304E; // 'N10N'
    static constexpr uint16_t G_N10_FMT_VER   = 0x0015;     // v015 포맷
    static constexpr size_t   G_N10_MAX_KEY   = 24;         // NVS 키 최대 길이 권장

    // 디바운스/배치 저장 간격(ms)
    static constexpr uint32_t G_N10_DEBOUNCE_DEFAULT_MS = 3000;
    static constexpr uint32_t G_N10_FLUSH_SLICE_BYTES   = 4096;  // tick 1회당 최대 기록 바이트(소프트 제한)

    // .bak 키 접미사
    static constexpr const char* G_N10_BAK_SUFFIX = ".bak";

    // 알려진 섹션 키 (JSON blob 보관 키 이름)
    static constexpr const char* G_N10_KEY_SYSTEM      = "cfg_system";
    static constexpr const char* G_N10_KEY_WIFI        = "cfg_wifi";
    static constexpr const char* G_N10_KEY_SCHEDULES   = "cfg_schedules";
    static constexpr const char* G_N10_KEY_USERPROF    = "cfg_userprof";
    static constexpr const char* G_N10_KEY_MOTION      = "cfg_motion";
    static constexpr const char* G_N10_KEY_RUNSTATE    = "run_state";
    static constexpr const char* G_N10_KEY_WINDDICT    = "cfg_winddict"; // windProfile dict
}

// 섹션 플래그 (factoryReset에서 사용)
typedef enum : uint32_t {
    EN_N10_SEC_NONE        = 0,
    EN_N10_SEC_SYSTEM      = 1u << 0,
    EN_N10_SEC_WIFI        = 1u << 1,
    EN_N10_SEC_SCHEDULES   = 1u << 2,
    EN_N10_SEC_USERPROF    = 1u << 3,
    EN_N10_SEC_MOTION      = 1u << 4,
    EN_N10_SEC_RUNSTATE    = 1u << 5,
    EN_N10_SEC_WINDDICT    = 1u << 6,
    EN_N10_SEC_ALL         = 0x7FFFFFFFu
} EN_N10_section_flags_t;

// NVS 저장 헤더
typedef struct {
    uint32_t magic;     // G_N10_MAGIC
    uint16_t fmtVer;    // G_N10_FMT_VER
    uint16_t rsv;       // 정렬용
    uint32_t dataCrc;   // payload CRC32
    uint32_t dataLen;   // payload length
} ST_N10_blobHeader_t;

// 내부 큐 엔트리
typedef struct {
    char     key[N10_Const::G_N10_MAX_KEY + 1];
    uint32_t debounceMs;
    uint32_t firstQueuedMs;
    uint32_t lastQueuedMs;
    std::vector<uint8_t> payload; // JSON 텍스트 등 Raw 데이터
} ST_N10_queueItem_t;

/* ======================================================
 * 전역 함수 (모듈약어 접두사)
 * ====================================================== */
static inline uint32_t N10_crc32(const uint8_t* p_data, size_t p_len) {
    // 표준 CRC-32(Poly 0xEDB88320, seed 0xFFFFFFFF)
    uint32_t v_crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < p_len; ++i) {
        v_crc ^= p_data[i];
        for (int k = 0; k < 8; ++k) {
            uint32_t mask = -(v_crc & 1u);
            v_crc = (v_crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~v_crc;
}

/* ======================================================
 * NVS Manager 클래스
 *  - 멤버 함수/변수는 모듈약어 접두사 사용하지 않음
 * ====================================================== */
class CL_N10_NVSManager {
public:
    // 공개 상태
    bool     ready = false;
    uint32_t writesTotal = 0;
    uint32_t writesFailed = 0;
    uint32_t recoveredFromBak = 0;

public:
    /* ----------------------------------------------
     * 초기화
     * ---------------------------------------------- */
    bool begin(const char* p_namespace = N10_Const::G_N10_NVS_NS_DEFAULT, bool p_rw = true) {
        memset(_ns, 0, sizeof(_ns));
        strlcpy(_ns, p_namespace ? p_namespace : N10_Const::G_N10_NVS_NS_DEFAULT, sizeof(_ns));
        ready = _prefs.begin(_ns, !p_rw);
        return ready;
    }

    void end() {
        _prefs.end();
        ready = false;
        _queue.clear();
    }

    /* ----------------------------------------------
     * 주기 처리 (Lazy Save Flush)
     * ---------------------------------------------- */
    void tick() {
        if (!ready || _queue.empty()) return;

        const uint32_t v_now = millis();
        size_t v_writtenThisTick = 0;

        // 간단한 선형 스캔으로 디바운스 만료된 항목만 기록
        for (size_t i = 0; i < _queue.size();) {
            ST_N10_queueItem_t& it = _queue[i];
            if (v_now - it.lastQueuedMs < it.debounceMs) { ++i; continue; }

            if (writeRawWithBak(it.key, it.payload.data(), it.payload.size())) {
                v_writtenThisTick += sizeof(ST_N10_blobHeader_t) + it.payload.size();
                _eraseFromQueueIndex(i);
            } else {
                ++writesFailed;
                // 실패했어도 무한 재시도를 방지: 다음 tick 재시도 (인덱스 증가)
                ++i;
            }

            if (v_writtenThisTick >= N10_Const::G_N10_FLUSH_SLICE_BYTES) break;
        }
    }

    /* ----------------------------------------------
     * JSON 저장/로드 (즉시)
     * ---------------------------------------------- */
    bool saveJson(const char* p_key, const JsonDocument& p_doc) {
        if (!ready || !p_key) return false;
        std::string v;
        serializeJson(p_doc, v);
        return writeRawWithBak(p_key, reinterpret_cast<const uint8_t*>(v.data()), v.size());
    }

    bool loadJson(const char* p_key, JsonDocument& p_doc) {
        if (!ready || !p_key) return false;

        std::vector<uint8_t> buf;
        if (!readRawWithBakRecovery(p_key, buf)) return false;

        DeserializationError e = deserializeJson(p_doc, buf.data(), buf.size());
        if (e) return false;
        return true;
    }

    /* ----------------------------------------------
     * JSON Lazy 저장 (디바운스)
     * ---------------------------------------------- */
    bool queueSaveJson(const char* p_key, const JsonDocument& p_doc, uint32_t p_debounceMs = N10_Const::G_N10_DEBOUNCE_DEFAULT_MS) {
        if (!ready || !p_key) return false;

        std::string v; v.reserve(512);
        serializeJson(p_doc, v);
        return queueSaveRaw(p_key, reinterpret_cast<const uint8_t*>(v.data()), v.size(), p_debounceMs);
    }

    /* ----------------------------------------------
     * Raw 저장/로드 (즉시) — 헤더+CRC+bak 포함
     * ---------------------------------------------- */
    bool writeRawWithBak(const char* p_key, const uint8_t* p_data, size_t p_len) {
        if (!p_key) return false;
        if (!_writeOne(p_key, p_data, p_len)) {
            ++writesFailed;
            return false;
        }
        ++writesTotal;

        // .bak 갱신: 최신본을 복제 저장
        char bakKey[N10_Const::G_N10_MAX_KEY + 1] = {0};
        _makeBakKey(p_key, bakKey, sizeof(bakKey));
        (void)_writeOne(bakKey, p_data, p_len); // 실패해도 본문이 이미 저장되었으므로 치명적 아님
        return true;
    }

    bool readRawWithBakRecovery(const char* p_key, std::vector<uint8_t>& p_out) {
        p_out.clear();
        if (!p_key) return false;

        if (_readOneChecked(p_key, p_out)) return true;

        // 본문 손상 → .bak에서 복구 시도
        char bakKey[N10_Const::G_N10_MAX_KEY + 1] = {0};
        _makeBakKey(p_key, bakKey, sizeof(bakKey));
        std::vector<uint8_t> bak;
        if (_readOneChecked(bakKey, bak)) {
            // 복구: .bak 내용을 본키로 재기록
            if (_writeOne(p_key, bak.data(), bak.size())) {
                ++recoveredFromBak;
                p_out.swap(bak);
                return true;
            }
        }
        return false;
    }

    /* ----------------------------------------------
     * 큐 조작 / 키삭제 / 네임스페이스 초기화
     * ---------------------------------------------- */
    bool queueSaveRaw(const char* p_key, const uint8_t* p_data, size_t p_len, uint32_t p_debounceMs) {
        if (!ready || !p_key || !p_data) return false;

        // 동일 키 존재 시 payload 교체 + lastQueuedMs 갱신(버스트 병합)
        for (auto& it : _queue) {
            if (strncmp(it.key, p_key, sizeof(it.key)) == 0) {
                it.payload.assign(p_data, p_data + p_len);
                it.lastQueuedMs = millis();
                return true;
            }
        }

        ST_N10_queueItem_t it{};
        memset(it.key, 0, sizeof(it.key));
        strlcpy(it.key, p_key, sizeof(it.key));
        it.debounceMs    = p_debounceMs ? p_debounceMs : N10_Const::G_N10_DEBOUNCE_DEFAULT_MS;
        it.firstQueuedMs = millis();
        it.lastQueuedMs  = it.firstQueuedMs;
        it.payload.assign(p_data, p_data + p_len);
        _queue.push_back(std::move(it));
        return true;
    }

    bool eraseKey(const char* p_key) {
        if (!ready || !p_key) return false;
        bool ok = _prefs.remove(p_key);
        // bak도 삭제
        char bakKey[N10_Const::G_N10_MAX_KEY + 1] = {0};
        _makeBakKey(p_key, bakKey, sizeof(bakKey));
        _prefs.remove(bakKey);
        return ok;
    }

    bool factoryReset(uint32_t p_sectionFlags) {
        if (!ready) return false;
        bool ok = true;
        if (p_sectionFlags & EN_N10_SEC_SYSTEM)      ok &= eraseKey(N10_Const::G_N10_KEY_SYSTEM);
        if (p_sectionFlags & EN_N10_SEC_WIFI)        ok &= eraseKey(N10_Const::G_N10_KEY_WIFI);
        if (p_sectionFlags & EN_N10_SEC_SCHEDULES)   ok &= eraseKey(N10_Const::G_N10_KEY_SCHEDULES);
        if (p_sectionFlags & EN_N10_SEC_USERPROF)    ok &= eraseKey(N10_Const::G_N10_KEY_USERPROF);
        if (p_sectionFlags & EN_N10_SEC_MOTION)      ok &= eraseKey(N10_Const::G_N10_KEY_MOTION);
        if (p_sectionFlags & EN_N10_SEC_RUNSTATE)    ok &= eraseKey(N10_Const::G_N10_KEY_RUNSTATE);
        if (p_sectionFlags & EN_N10_SEC_WINDDICT)    ok &= eraseKey(N10_Const::G_N10_KEY_WINDDICT);
        return ok;
    }

    /* ----------------------------------------------
     * 상태 통계 JSON
     * ---------------------------------------------- */
    void toJson(JsonDocument& p_doc) {
        JsonObject o = p_doc["nvs"].to<JsonObject>();
        o["ready"]             = ready;
        o["ns"]                = _ns;
        o["queueSize"]         = (uint32_t)_queue.size();
        o["writesTotal"]       = writesTotal;
        o["writesFailed"]      = writesFailed;
        o["recoveredFromBak"]  = recoveredFromBak;
        o["debounceDefaultMs"] = (uint32_t)N10_Const::G_N10_DEBOUNCE_DEFAULT_MS;

        // 큐 인벤토리(키만)
        JsonArray arr = o["pendingKeys"].to<JsonArray>();
        for (auto& it : _queue) {
            JsonObject e = arr.add<JsonObject>();
            e["key"]          = it.key;
            e["ageMs"]        = (uint32_t)(millis() - it.firstQueuedMs);
            e["sinceLastMs"]  = (uint32_t)(millis() - it.lastQueuedMs);
            e["debounceMs"]   = it.debounceMs;
            e["payloadBytes"] = (uint32_t)it.payload.size();
        }
    }

private:
    Preferences _prefs;
    char        _ns[16] = {0};

    std::vector<ST_N10_queueItem_t> _queue;

    /* ----------------------------------------------
     * 내부 유틸
     * ---------------------------------------------- */
    void _makeBakKey(const char* p_key, char* p_out, size_t p_outLen) {
        // p_outLen 충분하다고 가정(G_N10_MAX_KEY + 1)
        memset(p_out, 0, p_outLen);
        size_t v_len = strnlen(p_key, N10_Const::G_N10_MAX_KEY);
        size_t v_suf = strnlen(N10_Const::G_N10_BAK_SUFFIX, 8);
        size_t v_copy = min(v_len, (size_t)N10_Const::G_N10_MAX_KEY - v_suf);
        memcpy(p_out, p_key, v_copy);
        memcpy(p_out + v_copy, N10_Const::G_N10_BAK_SUFFIX, v_suf);
        p_out[v_copy + v_suf] = '\0';
    }

    void _eraseFromQueueIndex(size_t idx) {
        if (idx >= _queue.size()) return;
        _queue.erase(_queue.begin() + idx);
    }

    bool _writeOne(const char* p_key, const uint8_t* p_data, size_t p_len) {
        if (!p_key || !p_data) return false;

        // 헤더 + payload 패킹
        ST_N10_blobHeader_t hdr{};
        hdr.magic   = N10_Const::G_N10_MAGIC;
        hdr.fmtVer  = N10_Const::G_N10_FMT_VER;
        hdr.rsv     = 0;
        hdr.dataCrc = N10_crc32(p_data, p_len);
        hdr.dataLen = (uint32_t)p_len;

        const size_t v_total = sizeof(hdr) + p_len;
        std::vector<uint8_t> buf;
        buf.resize(v_total);
        memcpy(buf.data(), &hdr, sizeof(hdr));
        memcpy(buf.data() + sizeof(hdr), p_data, p_len);

        // Preferences putBytes
        size_t written = _prefs.putBytes(p_key, buf.data(), buf.size());
        return (written == buf.size());
    }

    bool _readOneChecked(const char* p_key, std::vector<uint8_t>& p_out) {
        if (!p_key) return false;

        size_t sz = _prefs.getBytesLength(p_key);
        if (sz < sizeof(ST_N10_blobHeader_t)) return false;

        p_out.resize(sz);
        size_t rd = _prefs.getBytes(p_key, p_out.data(), sz);
        if (rd != sz) { p_out.clear(); return false; }

        // 헤더 검증
        if (sz < sizeof(ST_N10_blobHeader_t)) { p_out.clear(); return false; }
        const ST_N10_blobHeader_t* hdr = reinterpret_cast<const ST_N10_blobHeader_t*>(p_out.data());
        if (hdr->magic != N10_Const::G_N10_MAGIC) { p_out.clear(); return false; }
        if (hdr->fmtVer != N10_Const::G_N10_FMT_VER) {
            // 버전이 다른 경우: 여기서는 호환성 처리 생략(실서비스라면 변환 루틴 필요)
            p_out.clear(); return false;
        }
        if (sz != sizeof(ST_N10_blobHeader_t) + hdr->dataLen) { p_out.clear(); return false; }

        const uint8_t* payload = p_out.data() + sizeof(ST_N10_blobHeader_t);
        uint32_t crc = N10_crc32(payload, hdr->dataLen);
        if (crc != hdr->dataCrc) { p_out.clear(); return false; }

        // payload만 반환하도록 재배치
        std::vector<uint8_t> only(payload, payload + hdr->dataLen);
        p_out.swap(only);
        return true;
    }
};

/* ======================================================
 * 섹션별 Key 헬퍼 (선택사항)
 *  - 전역 함수로 노출(모듈 접두사)
 * ====================================================== */
static inline const char* N10_key_system()      { return N10_Const::G_N10_KEY_SYSTEM; }
static inline const char* N10_key_wifi()        { return N10_Const::G_N10_KEY_WIFI; }
static inline const char* N10_key_schedules()   { return N10_Const::G_N10_KEY_SCHEDULES; }
static inline const char* N10_key_userprof()    { return N10_Const::G_N10_KEY_USERPROF; }
static inline const char* N10_key_motion()      { return N10_Const::G_N10_KEY_MOTION; }
static inline const char* N10_key_runstate()    { return N10_Const::G_N10_KEY_RUNSTATE; }
static inline const char* N10_key_winddict()    { return N10_Const::G_N10_KEY_WINDDICT; }

/* ======================================================
 * 사용 예 (요약)
 * ------------------------------------------------------
 * CL_N10_NVSManager nvs;
 * nvs.begin();           // 기본 네임스페이스
 *
 * // JSON 즉시 저장/로드
 * JsonDocument d;
 * d["hello"] = "world";
 * nvs.saveJson(N10_key_system(), d);
 * d.clear();
 * nvs.loadJson(N10_key_system(), d);
 *
 * // Lazy 저장
 * d["cnt"] = 1;
 * nvs.queueSaveJson(N10_key_runstate(), d, 3000);
 * // loop에서 nvs.tick() 호출 → 디바운스 후 기록
 *
 * // Factory reset (스케줄 + 런상태만)
 * nvs.factoryReset(EN_N10_SEC_SCHEDULES | EN_N10_SEC_RUNSTATE);
 * ====================================================== */
