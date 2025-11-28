
/*
 * ------------------------------------------------------
 * 소스명 : C10_Config_Rules_026.cpp
 * 모듈 약어 : C10
 * 모듈명 : Smart Nature Wind Configuration Manager
 * ------------------------------------------------------
 * 기능 요약:
 *  - Smart Nature Wind 전체 설정(JSON 기반) 관리 매니저
 *  - 설정 파일 단위 분리 관리 (system / wifi / motion / schedules / userProfiles / windProfile)
 *  - 구조체 ↔ JSON 직렬화 및 역직렬화 (ArduinoJson v7 전용)
 *  - 파일 백업(.bak) / 복구 / 공장초기화(factoryResetFromDefault) 지원
 *  - PATCH 기반 부분 업데이트(patchConfigFromJson) 지원
 *  - Lazy-Load 하이브리드 구성 (필요 섹션만 동적 로드)
 *  - Wi-Fi 등 재초기화 판단 로직 확장 가능
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


#include "C10_Config_027.h"

extern ST_A10_ConfigRoot_t g_A10_config_root; 

extern bool ioSaveJson(const char* p_path, const char* p_bak, const JsonDocument& p_doc);
extern bool ioLoadJson(const char* p_path, const char* p_bak, JsonDocument& p_doc);

// =====================================================
// 5. JSON Patch (Schedules, UserProfiles) 구현
// (로직은 요청에 따라 이전 응답과 동일하게 유지)
// =====================================================

bool CL_C10_ConfigManager::patchSchedulesFromJson(ST_A10_SchedulesRoot_t& p_cfg,
									   const JsonDocument&	 p_patch) {

		// 💡 Mutex를 사용하여 쓰기 작업 보호
	    C10_MUTEX_ACQUIRE()
	    /*
        if (xSemaphoreTake(s_configMutex, G_C10_MUTEX_TIMEOUT) != pdTRUE) {
            CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] patchSchedulesFromJson() Mutex timeout!");
            return false; // Mutex 획득 실패 시 실패 처리
        }
        */

		
		JsonArrayConst arr = p_patch["schedules"].as<JsonArrayConst>();
		if (arr.isNull()) {
			CL_D10_Logger::log(EN_L10_LOG_WARN, "[C10] Schedules patch: 'schedules' array missing");
			xSemaphoreGive(s_configMutex);
			return false;
		}

		bool v_changed = false;

		for (JsonObjectConst j_patch : arr) {
			if (!j_patch["schId"].is<uint8_t>()) continue;
			uint8_t v_schId = j_patch["schId"];
			uint16_t v_schNo = j_patch["schNo"];

			ST_A10_ScheduleItem_t* v_item = nullptr;
			for (uint8_t i = 0; i < p_cfg.count; i++) {
				if (p_cfg.items[i].schId == v_schId) {
					v_item = &p_cfg.items[i];
					break;
				}
			}

			if (!v_item) {
				CL_D10_Logger::log(EN_L10_LOG_WARN, "[C10] Schedule patch skipped: ID %u not found", v_schNo);
				continue;
			}
            
            // --- 4. 필드별 덮어쓰기 (PATCH 로직) ---
            // A. 기본 속성
            if (j_patch["name"].is<const char*>()) {
                if (strcmp(j_patch["name"], v_item->name) != 0) {
                    strlcpy(v_item->name, j_patch["name"], sizeof(v_item->name));
                    v_changed = true;
                }
            }
            if (j_patch["enabled"].is<bool>()) {
                if (j_patch["enabled"].as<bool>() != v_item->enabled) {
                    v_item->enabled = j_patch["enabled"];
                    v_changed = true;
                }
            }

            // B. period
            JsonObjectConst j_per = j_patch["period"];
            if (!j_per.isNull()) {
                if (j_per["enabled"].is<bool>()) {
                    if (j_per["enabled"].as<bool>() != v_item->period.enabled) {
                        v_item->period.enabled = j_per["enabled"];
                        v_changed = true;
                    }
                }
                if (j_per["start_time"].is<const char*>()) {
                    if (strcmp(j_per["start_time"], v_item->period.start_time) != 0) {
                        strlcpy(v_item->period.start_time, j_per["start_time"], sizeof(v_item->period.start_time));
                        v_changed = true;
                    }
                }
                if (j_per["end_time"].is<const char*>()) {
                    if (strcmp(j_per["end_time"], v_item->period.end_time) != 0) {
                        strlcpy(v_item->period.end_time, j_per["end_time"], sizeof(v_item->period.end_time));
                        v_changed = true;
                    }
                }
                
                // days 배열 패치
                JsonArrayConst j_days = j_per["days"].as<JsonArrayConst>();
                if (!j_days.isNull()) {
                    for (uint8_t v_d = 0; v_d < 7 && v_d < j_days.size(); v_d++) {
                        if (j_days[v_d].is<uint8_t>() && j_days[v_d].as<uint8_t>() != v_item->period.days[v_d]) {
                            v_item->period.days[v_d] = j_days[v_d];
                            v_changed = true;
                        }
                    }
                }
            }
            
            // C. segments (배열 전체 덮어쓰기 - PUT 방식)
            JsonArrayConst j_segs = j_patch["segments"].as<JsonArrayConst>();
            if (!j_segs.isNull()) {
                v_item->seg_count = 0; // 기존 세그먼트 초기화
                for (JsonObjectConst jseg : j_segs) {
                    if (v_item->seg_count >= A10_Const::MAX_SEGMENTS_PER_SCHEDULE) break;

                    ST_A10_ScheduleSegment_t& sg = v_item->segments[v_item->seg_count++];
                    // 기존 loadSchedules 로직을 사용한 안전한 덮어쓰기
                    sg.segId = jseg["segId"] | 0;
					sg.segNo = jseg["segNo"] | 0;
                    
                    // on_minutes, off_minutes는 0이 유효할 수 있으므로 is<uint16_t>()로 존재 여부 확인
                    sg.on_minutes = jseg["on_minutes"].is<uint16_t>() ? jseg["on_minutes"].as<uint16_t>() : 10;
                    sg.off_minutes = jseg["off_minutes"].is<uint16_t>() ? jseg["off_minutes"].as<uint16_t>() : 0;


                    const char* v_mode = jseg["mode"] | "PRESET";
                    sg.mode = A10_modeFromString(v_mode);
                    strlcpy(sg.presetCode, jseg["presetCode"] | "", sizeof(sg.presetCode));
                    strlcpy(sg.styleCode, jseg["styleCode"] | "", sizeof(sg.styleCode));
                    
                    if (jseg["adjust"].is<JsonObjectConst>()) {
                        JsonObjectConst adj = jseg["adjust"];
                        // float 값은 | 0.0f 사용 시 0.0f이 기본값이 되므로, is<float>()로 존재 여부 확인 
                        sg.adjust.wind_intensity = adj["wind_intensity"].is<float>() ? adj["wind_intensity"].as<float>() : 0.0f;
                        sg.adjust.wind_variability = adj["wind_variability"].is<float>() ? adj["wind_variability"].as<float>() : 0.0f;
                        sg.adjust.gust_frequency = adj["gust_frequency"].is<float>() ? adj["gust_frequency"].as<float>() : 0.0f;
                        sg.adjust.fan_limit = adj["fan_limit"].is<float>() ? adj["fan_limit"].as<float>() : 0.0f;
                        sg.adjust.min_fan = adj["min_fan"].is<float>() ? adj["min_fan"].as<float>() : 0.0f;

                    } else {
						memset(&sg.adjust, 0, sizeof(sg.adjust));
					}
                    
                    // fixed_speed는 0이 유효할 수 있으므로 is<float>()로 존재 여부 확인
                    sg.fixed_speed = jseg["fixed_speed"].is<float>() ? jseg["fixed_speed"].as<float>() : 0.0f;
                }
                v_changed = true;
            }
            
            // D. autoOff 필드 PATCH (중첩 구조체)
            JsonObjectConst j_autoOff = j_patch["autoOff"];
            if (!j_autoOff.isNull()) {
				// 1. timer
				JsonObjectConst j_timer = j_autoOff["timer"];
				if (!j_timer.isNull()) {
					if (j_timer["enabled"].is<bool>() && j_timer["enabled"].as<bool>() != v_item->autoOff.timer.enabled) {
						v_item->autoOff.timer.enabled = j_timer["enabled"];
						v_changed = true;
					}
					if (j_timer["minutes"].is<uint16_t>()) { // 0이 유효할 수 있으므로 is<uint16_t>()로 존재 여부 확인
						if (j_timer["minutes"].as<uint16_t>() != v_item->autoOff.timer.minutes) {
							v_item->autoOff.timer.minutes = j_timer["minutes"];
							v_changed = true;
						}
					}
				}
				// 2. offTime
				JsonObjectConst j_offTime = j_autoOff["offTime"];
				if (!j_offTime.isNull()) {
					if (j_offTime["enabled"].is<bool>() && j_offTime["enabled"].as<bool>() != v_item->autoOff.offTime.enabled) {
						v_item->autoOff.offTime.enabled = j_offTime["enabled"];
						v_changed = true;
					}
					if (j_offTime["time"].is<const char*>()) {
						if (strcmp(j_offTime["time"], v_item->autoOff.offTime.time) != 0) {
							strlcpy(v_item->autoOff.offTime.time, j_offTime["time"], sizeof(v_item->autoOff.offTime.time));
							v_changed = true;
						}
					}
				}
				// 3. offTemp
				JsonObjectConst j_offTemp = j_autoOff["offTemp"];
				if (!j_offTemp.isNull()) {
					if (j_offTemp["enabled"].is<bool>() && j_offTemp["enabled"].as<bool>() != v_item->autoOff.offTemp.enabled) {
						v_item->autoOff.offTemp.enabled = j_offTemp["enabled"];
						v_changed = true;
					}
					if (j_offTemp["temp"].is<float>()) { // 0.0f가 유효할 수 있으므로 is<float>()로 존재 여부 확인
						if (abs(j_offTemp["temp"].as<float>() - v_item->autoOff.offTemp.temp) > 0.001f) {
							v_item->autoOff.offTemp.temp = j_offTemp["temp"];
							v_changed = true;
						}
					}
				}
            }

			// E. motion 필드 PATCH (중첩 구조체)
			JsonObjectConst j_motion = j_patch["motion"];
			if (!j_motion.isNull()) {
				// pir
				JsonObjectConst j_mpir = j_motion["pir"];
				if (!j_mpir.isNull()) {
					if (j_mpir["enabled"].is<bool>() && j_mpir["enabled"].as<bool>() != v_item->motion.pir.enabled) {
						v_item->motion.pir.enabled = j_mpir["enabled"];
						v_changed = true;
					}
					if (j_mpir["hold_sec"].is<uint16_t>()) { // 0이 유효할 수 있으므로 is<uint16_t>()로 존재 여부 확인
						if (j_mpir["hold_sec"].as<uint16_t>() != v_item->motion.pir.hold_sec) {
							v_item->motion.pir.hold_sec = j_mpir["hold_sec"];
							v_changed = true;
						}
					}
				}
				// ble
				JsonObjectConst j_mble = j_motion["ble"];
				if (!j_mble.isNull()) {
					if (j_mble["enabled"].is<bool>() && j_mble["enabled"].as<bool>() != v_item->motion.ble.enabled) {
						v_item->motion.ble.enabled = j_mble["enabled"];
						v_changed = true;
					}
					if (j_mble["rssi_threshold"].is<int8_t>()) { // 음수 포함, 0이 유효할 수 있으므로 is<int8_t>()로 존재 여부 확인
						if (j_mble["rssi_threshold"].as<int8_t>() != v_item->motion.ble.rssi_threshold) {
							v_item->motion.ble.rssi_threshold = j_mble["rssi_threshold"];
							v_changed = true;
						}
					}
					if (j_mble["hold_sec"].is<uint16_t>()) { // 0이 유효할 수 있으므로 is<uint16_t>()로 존재 여부 확인
						if (j_mble["hold_sec"].as<uint16_t>() != v_item->motion.ble.hold_sec) {
							v_item->motion.ble.hold_sec = j_mble["hold_sec"];
							v_changed = true;
						}
					}
				}
			}


			if (v_changed) {
				CL_D10_Logger::log(EN_L10_LOG_DEBUG, "[C10] Schedule %u patched.", v_schNo);
			}
		}

		// 5. 변경 사항이 있을 경우에만 Dirty Flag 설정
		if (v_changed) {
            _dirty_schedules = true;
            CL_D10_Logger::log(EN_L10_LOG_INFO, "[C10] schedules config patched (Memory Only). Dirty=true");
        }
	    C10_MUTEX_RELEASE()
		// xSemaphoreGive(s_configMutex); 
		
        return v_changed;
}


bool CL_C10_ConfigManager::patchUserProfilesFromJson(ST_A10_UserProfilesRoot_t& p_cfg,
										  const JsonDocument&		 p_patch) {

		// 💡 Mutex를 사용하여 쓰기 작업 보호
        if (xSemaphoreTake(s_configMutex, G_C10_MUTEX_TIMEOUT) != pdTRUE) {
            CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] patchUserProfilesFromJson() Mutex timeout!");
            return false; // Mutex 획득 실패 시 실패 처리
        }
		
		JsonArrayConst arr = p_patch["userProfiles"]["profiles"].as<JsonArrayConst>();
		if (arr.isNull()) {
			CL_D10_Logger::log(EN_L10_LOG_WARN, "[C10] UserProfiles patch: 'profiles' array missing");
			xSemaphoreGive(s_configMutex);
			return false;
		}

		bool v_changed = false;

		for (JsonObjectConst j_patch : arr) {
			if (!j_patch["profileId"].is<uint8_t>()) continue;
			uint8_t v_profileId = j_patch["profileId"];
			uint16_t v_profileNo = j_patch["profileNo"];

			ST_A10_UserProfileItem_t* v_item = nullptr;
			for (uint8_t i = 0; i < p_cfg.count; i++) {
				if (p_cfg.items[i].profileId == v_profileId) {
					v_item = &p_cfg.items[i];
					break;
				}
			}

			if (!v_item) {
				CL_D10_Logger::log(EN_L10_LOG_WARN, "[C10] UserProfile patch skipped: ID %u not found", v_profileNo);
				continue;
			}
            
            // --- 4. 필드별 덮어쓰기 (PATCH 로직) ---
            // A. 기본 속성
            if (j_patch["name"].is<const char*>()) {
                if (strcmp(j_patch["name"], v_item->name) != 0) {
                    strlcpy(v_item->name, j_patch["name"], sizeof(v_item->name));
                    v_changed = true;
                }
            }
            if (j_patch["enabled"].is<bool>()) {
                if (j_patch["enabled"].as<bool>() != v_item->enabled) {
                    v_item->enabled = j_patch["enabled"];
                    v_changed = true;
                }
            }
            if (j_patch["repeatSegments"].is<bool>()) {
                if (j_patch["repeatSegments"].as<bool>() != v_item->repeatSegments) {
                    v_item->repeatSegments = j_patch["repeatSegments"];
                    v_changed = true;
                }
            }

            // B. segments (배열 전체 덮어쓰기 - PUT 방식)
            JsonArrayConst j_segs = j_patch["segments"].as<JsonArrayConst>();
            if (!j_segs.isNull()) {
                v_item->seg_count = 0; // 기존 세그먼트 초기화
                for (JsonObjectConst jseg : j_segs) {
                    if (v_item->seg_count >= A10_Const::MAX_SEGMENTS_PER_PROFILE) break;

                    ST_A10_UserProfileSegment_t& sg = v_item->segments[v_item->seg_count++];
                    // 기존 loadUserProfiles 로직을 사용한 안전한 덮어쓰기
                    sg.segId = jseg["segId"] | 0;
					sg.segNo = jseg["segNo"] | 0;
                    
                    // on_minutes, off_minutes는 0이 유효할 수 있으므로 is<uint16_t>()로 존재 여부 확인
                    sg.on_minutes = jseg["on_minutes"].is<uint16_t>() ? jseg["on_minutes"].as<uint16_t>() : 10;
                    sg.off_minutes = jseg["off_minutes"].is<uint16_t>() ? jseg["off_minutes"].as<uint16_t>() : 0;


                    const char* v_mode = jseg["mode"] | "PRESET";
                    sg.mode = A10_modeFromString(v_mode);
                    strlcpy(sg.presetCode, jseg["presetCode"] | "", sizeof(sg.presetCode));
                    strlcpy(sg.styleCode, jseg["styleCode"] | "", sizeof(sg.styleCode));
                    
                    if (jseg["adjust"].is<JsonObjectConst>()) {
                        JsonObjectConst adj = jseg["adjust"];
                        // float 값은 | 0.0f 사용 시 0.0f이 기본값이 되므로, is<float>()로 존재 여부 확인 
                        sg.adjust.wind_intensity = adj["wind_intensity"].is<float>() ? adj["wind_intensity"].as<float>() : 0.0f;
                        sg.adjust.wind_variability = adj["wind_variability"].is<float>() ? adj["wind_variability"].as<float>() : 0.0f;
                        sg.adjust.gust_frequency = adj["gust_frequency"].is<float>() ? adj["gust_frequency"].as<float>() : 0.0f;
                        sg.adjust.fan_limit = adj["fan_limit"].is<float>() ? adj["fan_limit"].as<float>() : 0.0f;
                        sg.adjust.min_fan = adj["min_fan"].is<float>() ? adj["min_fan"].as<float>() : 0.0f;
                    } else {
						memset(&sg.adjust, 0, sizeof(sg.adjust));
					}
                    
                    // fixed_speed는 0이 유효할 수 있으므로 is<float>()로 존재 여부 확인
                    sg.fixed_speed = jseg["fixed_speed"].is<float>() ? jseg["fixed_speed"].as<float>() : 0.0f;
                }
                v_changed = true;
            }
            
            // C. autoOff 필드 PATCH (중첩 구조체)
            JsonObjectConst j_autoOff = j_patch["autoOff"];
            if (!j_autoOff.isNull()) {
				// 1. timer
				JsonObjectConst j_timer = j_autoOff["timer"];
				if (!j_timer.isNull()) {
					if (j_timer["enabled"].is<bool>() && j_timer["enabled"].as<bool>() != v_item->autoOff.timer.enabled) {
						v_item->autoOff.timer.enabled = j_timer["enabled"];
						v_changed = true;
					}
					if (j_timer["minutes"].is<uint16_t>()) { // 0이 유효할 수 있으므로 is<uint16_t>()로 존재 여부 확인
						if (j_timer["minutes"].as<uint16_t>() != v_item->autoOff.timer.minutes) {
							v_item->autoOff.timer.minutes = j_timer["minutes"];
							v_changed = true;
						}
					}
				}
				// 2. offTime
				JsonObjectConst j_offTime = j_autoOff["offTime"];
				if (!j_offTime.isNull()) {
					if (j_offTime["enabled"].is<bool>() && j_offTime["enabled"].as<bool>() != v_item->autoOff.offTime.enabled) {
						v_item->autoOff.offTime.enabled = j_offTime["enabled"];
						v_changed = true;
					}
					if (j_offTime["time"].is<const char*>()) {
						if (strcmp(j_offTime["time"], v_item->autoOff.offTime.time) != 0) {
							strlcpy(v_item->autoOff.offTime.time, j_offTime["time"], sizeof(v_item->autoOff.offTime.time));
							v_changed = true;
						}
					}
				}
				// 3. offTemp
				JsonObjectConst j_offTemp = j_autoOff["offTemp"];
				if (!j_offTemp.isNull()) {
					if (j_offTemp["enabled"].is<bool>() && j_offTemp["enabled"].as<bool>() != v_item->autoOff.offTemp.enabled) {
						v_item->autoOff.offTemp.enabled = j_offTemp["enabled"];
						v_changed = true;
					}
					if (j_offTemp["temp"].is<float>()) { // 0.0f가 유효할 수 있으므로 is<float>()로 존재 여부 확인
						if (abs(j_offTemp["temp"].as<float>() - v_item->autoOff.offTemp.temp) > 0.001f) {
							v_item->autoOff.offTemp.temp = j_offTemp["temp"];
							v_changed = true;
						}
					}
				}
            }

			// D. motion 필드 PATCH (중첩 구조체)
			JsonObjectConst j_motion = j_patch["motion"];
			if (!j_motion.isNull()) {
				// pir
				JsonObjectConst j_mpir = j_motion["pir"];
				if (!j_mpir.isNull()) {
					if (j_mpir["enabled"].is<bool>() && j_mpir["enabled"].as<bool>() != v_item->motion.pir.enabled) {
						v_item->motion.pir.enabled = j_mpir["enabled"];
						v_changed = true;
					}
					if (j_mpir["hold_sec"].is<uint16_t>()) { // 0이 유효할 수 있으므로 is<uint16_t>()로 존재 여부 확인
						if (j_mpir["hold_sec"].as<uint16_t>() != v_item->motion.pir.hold_sec) {
							v_item->motion.pir.hold_sec = j_mpir["hold_sec"];
							v_changed = true;
						}
					}
				}
				// ble
				JsonObjectConst j_mble = j_motion["ble"];
				if (!j_mble.isNull()) {
					if (j_mble["enabled"].is<bool>() && j_mble["enabled"].as<bool>() != v_item->motion.ble.enabled) {
						v_item->motion.ble.enabled = j_mble["enabled"];
						v_changed = true;
					}
					if (j_mble["rssi_threshold"].is<int8_t>()) { // 음수 포함, 0이 유효할 수 있으므로 is<int8_t>()로 존재 여부 확인
						if (j_mble["rssi_threshold"].as<int8_t>() != v_item->motion.ble.rssi_threshold) {
							v_item->motion.ble.rssi_threshold = j_mble["rssi_threshold"];
							v_changed = true;
						}
					}
					if (j_mble["hold_sec"].is<uint16_t>()) { // 0이 유효할 수 있으므로 is<uint16_t>()로 존재 여부 확인
						if (j_mble["hold_sec"].as<uint16_t>() != v_item->motion.ble.hold_sec) {
							v_item->motion.ble.hold_sec = j_mble["hold_sec"];
							v_changed = true;
						}
					}
				}
			}

			if (v_changed) {
				CL_D10_Logger::log(EN_L10_LOG_DEBUG, "[C10] UserProfile %u patched.", v_profileNo);
			}
		}

		// 5. 변경 사항이 있을 경우에만 Dirty Flag 설정
		if (v_changed) {
            _dirty_userProfiles = true;
            CL_D10_Logger::log(EN_L10_LOG_INFO, "[C10] userProfiles config patched (Memory Only). Dirty=true");
        }

		xSemaphoreGive(s_configMutex); 
		
        return v_changed;
}


// ===================================================== 
// 7. Schedules CRUD 구현
// ===================================================== 

/**
 * @brief JSON 문서로부터 새로운 스케줄 항목을 추가합니다.
 * @param p_doc 스케줄 데이터가 포함된 JsonDocument (ArduinoJson V7.x)
 * @return 새로 추가된 스케줄의 ID (성공 시), 또는 -1 (실패 시)
 */
int CL_C10_ConfigManager::addScheduleFromJson(const JsonDocument& p_doc) {
    // 1. 뮤텍스 획득
    if (xSemaphoreTake(s_configMutex, G_C10_MUTEX_TIMEOUT) != pdTRUE) {
        CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] addScheduleFromJson() Mutex timeout!");
        return -1;
    }

    ST_A10_SchedulesRoot_t* v_root = g_A10_config_root.schedules;
    if (!v_root) {
        xSemaphoreGive(s_configMutex);
        return -1;
    }

    // 2. 용량 확인
    if (v_root->count >= A10_Const::MAX_SCHEDULES) {
        CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] Schedule add failed: Max schedules (%u) reached.", A10_Const::MAX_SCHEDULES);
        xSemaphoreGive(s_configMutex);
        return -1;
    }

    // 3. Schedule Item의 인덱스 및 새 ID 할당
    uint8_t v_new_idx = v_root->count;
    uint16_t v_new_id = 0;

    // 현재 사용 중인 ID들 중 가장 작은 사용 가능 ID를 찾거나, 단순히 최대 ID + 1을 사용 (간소화를 위해 후자 사용)
    for (uint8_t i = 0; i < v_root->count; i++) {
        if (v_root->items[i].schId >= v_new_id) {
            v_new_id = v_root->items[i].schId + 1;
        }
    }
    // ID가 0인 경우는 보통 유효하지 않으므로 최소 1부터 시작하도록 보장 (선택적)
    if (v_new_id == 0) v_new_id = 1;


    // 4. 새 항목 초기화 및 ID 설정
    ST_A10_ScheduleItem_t& v_item = v_root->items[v_new_idx];
    memset(&v_item, 0, sizeof(ST_A10_ScheduleItem_t));
    v_item.schId = v_new_id;
    // 기본값 설정 (예: name 기본값, enabled=false 등)
    A10_safe_strlcpy(v_item.name, "New Schedule", sizeof(v_item.name));
    v_item.enabled = false;


    // 5. JSON 데이터 적용 (updateScheduleFromJson 로직 재활용)
    // NOTE: 새로운 스케줄을 추가할 때는 전체 데이터를 덮어쓰는 개념이므로, 
    // update 함수에서 사용된 패치 로직을 그대로 사용해도 무방합니다.
    bool v_changed = false;
    JsonObjectConst j_patch = p_doc.as<JsonObjectConst>();

    // name 패치
    if (j_patch["name"].is<const char*>()) {
        const char* v_new = j_patch["name"];
        if (strcmp(v_new, v_item.name) != 0) {
            A10_safe_strlcpy(v_item.name, v_new, sizeof(v_item.name));
            v_changed = true;
        }
    }
    
    // enabled 패치
    if (j_patch["enabled"].is<bool>()) {
        if (j_patch["enabled"].as<bool>() != v_item.enabled) {
            v_item.enabled = j_patch["enabled"];
            v_changed = true;
        }
    }
    
    // =========================================================================
    // period 패치 (ST_A10_SchedulePeriod_t) 
    // =========================================================================
    if (j_patch["period"].is<JsonObjectConst>()) {
        JsonObjectConst j_period = j_patch["period"];
        
        // enabled
        if (j_period["enabled"].is<bool>()) {
            if (j_period["enabled"].as<bool>() != v_item.period.enabled) {
                v_item.period.enabled = j_period["enabled"];
                v_changed = true;
            }
        }

        // start_time (char[6])
        if (j_period["start_time"].is<const char*>()) {
            const char* v_new_start = j_period["start_time"];
            if (strcmp(v_new_start, v_item.period.start_time) != 0) {
                A10_safe_strlcpy(v_item.period.start_time, v_new_start, sizeof(v_item.period.start_time));
                v_changed = true;
            }
        }

        // end_time (char[6])
        if (j_period["end_time"].is<const char*>()) {
            const char* v_new_end = j_period["end_time"];
            if (strcmp(v_new_end, v_item.period.end_time) != 0) {
                A10_safe_strlcpy(v_item.period.end_time, v_new_end, sizeof(v_item.period.end_time));
                v_changed = true;
            }
        }
        
        // days (uint8_t[7] 배열)
        JsonArrayConst j_days = j_period["days"].as<JsonArrayConst>();
        if (!j_days.isNull()) {
            bool v_days_changed = false;
            if (j_days.size() == 7) { 
                for (int i = 0; i < 7; i++) {
                    uint8_t v_day_val = j_days[i].is<uint8_t>() ? j_days[i].as<uint8_t>() : 0;
                    if (v_day_val != v_item.period.days[i]) {
                        v_item.period.days[i] = v_day_val;
                        v_days_changed = true;
                    }
                }
                if (v_days_changed) v_changed = true;
            }
        }
    }

    // =========================================================================
    // autoOff 패치 (ST_A10_SchAutoOff_t) 
    // =========================================================================
    if (j_patch["autoOff"].is<JsonObjectConst>()) {
        JsonObjectConst j_autoOff = j_patch["autoOff"];
        
        // 1. timer (minutes/enabled)
        if (j_autoOff["timer"].is<JsonObjectConst>()) {
            JsonObjectConst j_timer = j_autoOff["timer"];

            // timer.enabled
            if (j_timer["enabled"].is<bool>()) {
                if (j_timer["enabled"].as<bool>() != v_item.autoOff.timer.enabled) {
                    v_item.autoOff.timer.enabled = j_timer["enabled"];
                    v_changed = true;
                }
            }

            // timer.minutes (uint32_t)
            uint32_t v_minutes = j_timer["minutes"].is<uint32_t>() ? j_timer["minutes"].as<uint32_t>() : v_item.autoOff.timer.minutes;
            if (v_minutes != v_item.autoOff.timer.minutes) {
                v_item.autoOff.timer.minutes = v_minutes;
                v_changed = true;
            }
        }
        
        // 2. offTime (enabled/time)
        if (j_autoOff["offTime"].is<JsonObjectConst>()) {
            JsonObjectConst j_offTime = j_autoOff["offTime"];

            // offTime.enabled
            if (j_offTime["enabled"].is<bool>()) {
                if (j_offTime["enabled"].as<bool>() != v_item.autoOff.offTime.enabled) {
                    v_item.autoOff.offTime.enabled = j_offTime["enabled"];
                    v_changed = true;
                }
            }

            // offTime.time (char[6] - "HH:MM")
            if (j_offTime["time"].is<const char*>()) {
                const char* v_new_time = j_offTime["time"];
                if (strcmp(v_new_time, v_item.autoOff.offTime.time) != 0) {
                    A10_safe_strlcpy(v_item.autoOff.offTime.time, v_new_time, sizeof(v_item.autoOff.offTime.time));
                    v_changed = true;
                }
            }
        }

        // 3. offTemp (enabled/temp)
        if (j_autoOff["offTemp"].is<JsonObjectConst>()) {
            JsonObjectConst j_offTemp = j_autoOff["offTemp"];

            // offTemp.enabled
            if (j_offTemp["enabled"].is<bool>()) {
                if (j_offTemp["enabled"].as<bool>() != v_item.autoOff.offTemp.enabled) {
                    v_item.autoOff.offTemp.enabled = j_offTemp["enabled"];
                    v_changed = true;
                }
            }

            // offTemp.temp (float)
            float v_temp = j_offTemp["temp"].is<float>() ? j_offTemp["temp"].as<float>() : v_item.autoOff.offTemp.temp;
            if (v_temp != v_item.autoOff.offTemp.temp) {
                v_item.autoOff.offTemp.temp = v_temp;
                v_changed = true;
            }
        }
    }
    
    // =========================================================================
    // motion 패치 (ST_A10_Motion_t) 
    // =========================================================================
    if (j_patch["motion"].is<JsonObjectConst>()) {
        JsonObjectConst j_motion = j_patch["motion"];
        
        // 1. PIR (enabled/hold_sec)
        if (j_motion["pir"].is<JsonObjectConst>()) {
            JsonObjectConst j_pir = j_motion["pir"];

            // pir.enabled
            if (j_pir["enabled"].is<bool>()) {
                if (j_pir["enabled"].as<bool>() != v_item.motion.pir.enabled) {
                    v_item.motion.pir.enabled = j_pir["enabled"];
                    v_changed = true;
                }
            }

            // pir.hold_sec (int32_t)
            int32_t v_hold = j_pir["hold_sec"].is<int32_t>() ? j_pir["hold_sec"].as<int32_t>() : v_item.motion.pir.hold_sec;
            if (v_hold != v_item.motion.pir.hold_sec) {
                v_item.motion.pir.hold_sec = v_hold;
                v_changed = true;
            }
        }
        
        // 2. BLE (enabled/rssi_threshold/hold_sec)
        if (j_motion["ble"].is<JsonObjectConst>()) {
            JsonObjectConst j_ble = j_motion["ble"];

            // ble.enabled
            if (j_ble["enabled"].is<bool>()) {
                if (j_ble["enabled"].as<bool>() != v_item.motion.ble.enabled) {
                    v_item.motion.ble.enabled = j_ble["enabled"];
                    v_changed = true;
                }
            }

            // rssi_threshold
            int32_t v_rssi = j_ble["rssi_threshold"].is<int32_t>() ? j_ble["rssi_threshold"].as<int32_t>() : v_item.motion.ble.rssi_threshold;
            if (v_rssi != v_item.motion.ble.rssi_threshold) {
                v_item.motion.ble.rssi_threshold = v_rssi;
                v_changed = true;
            }

            // hold_sec
            int32_t v_ble_hold = j_ble["hold_sec"].is<int32_t>() ? j_ble["hold_sec"].as<int32_t>() : v_item.motion.ble.hold_sec;
            if (v_ble_hold != v_item.motion.ble.hold_sec) {
                v_item.motion.ble.hold_sec = v_ble_hold;
                v_changed = true;
            }
        }
    }


    // =========================================================================
    // segments 패치 (배열 전체 덮어쓰기)
    // =========================================================================
    JsonArrayConst j_segs = j_patch["segments"].as<JsonArrayConst>();
    if (!j_segs.isNull()) {
        v_item.seg_count = 0; // 기존 세그먼트 초기화
        // v_changed = true; // 세그먼트가 존재하면 무조건 변경으로 간주

        for (JsonObjectConst jseg : j_segs) {
            if (v_item.seg_count >= A10_Const::MAX_SEGMENTS_PER_SCHEDULE) {
                CL_D10_Logger::log(EN_L10_LOG_WARN, "[C10] Max segments reached for new Schedule ID %u.", v_new_id);
                break;
            }
            ST_A10_ScheduleSegment_t& sg = v_item.segments[v_item.seg_count++];

            // 기본 필드 로드
            sg.segId         = jseg["segId"] | 0;
            sg.segNo         = jseg["segNo"] | 0;
            sg.on_minutes    = jseg["on_minutes"].is<uint16_t>() ? jseg["on_minutes"].as<uint16_t>() : 0;
            sg.off_minutes   = jseg["off_minutes"].is<uint16_t>() ? jseg["off_minutes"].as<uint16_t>() : 0;
            
            // mode 로드 및 변환
            const char* v_mode = jseg["mode"] | "PRESET";
            sg.mode = A10_modeFromString(v_mode); 

            // presetCode와 styleCode 사용
            A10_safe_strlcpy(sg.presetCode, jseg["presetCode"] | "", sizeof(sg.presetCode));
            A10_safe_strlcpy(sg.styleCode, jseg["styleCode"] | "", sizeof(sg.styleCode));
            
            // fixed_speed
            sg.fixed_speed = jseg["fixed_speed"].is<float>() ? jseg["fixed_speed"].as<float>() : 0.0f;
            
            // adjust (ST_A10_AdjustDelta_t) 로드
            if (jseg["adjust"].is<JsonObjectConst>()) {
                JsonObjectConst adj = jseg["adjust"];
                sg.adjust.wind_intensity           = adj["wind_intensity"].is<float>() ? adj["wind_intensity"].as<float>() : 0.0f;
                sg.adjust.wind_variability         = adj["wind_variability"].is<float>() ? adj["wind_variability"].as<float>() : 0.0f;
                sg.adjust.gust_frequency           = adj["gust_frequency"].is<float>() ? adj["gust_frequency"].as<float>() : 0.0f;
                sg.adjust.fan_limit                = adj["fan_limit"].is<float>() ? adj["fan_limit"].as<float>() : 0.0f;
                sg.adjust.min_fan                  = adj["min_fan"].is<float>() ? adj["min_fan"].as<float>() : 0.0f;
                sg.adjust.turbulence_length_scale  = adj["turbulence_length_scale"].is<float>() ? adj["turbulence_length_scale"].as<float>() : 0.0f;
                sg.adjust.turbulence_intensity_sigma = adj["turbulence_intensity_sigma"].is<float>() ? adj["turbulence_intensity_sigma"].as<float>() : 0.0f;
            } else {
                // adjust 객체가 없으면 0으로 안전하게 초기화
                memset(&sg.adjust, 0, sizeof(sg.adjust));
            }
        }
        v_changed = true;
    }
    
    // 6. 새로운 항목 추가 및 변경 사항 플래그 설정
    v_root->count++;
    _dirty_schedules = true; // 새 항목 추가는 무조건 Dirty 플래그를 설정해야 함
    
    CL_D10_Logger::log(EN_L10_LOG_INFO, "[C10] New Schedule ID %u added. Dirty=true", v_new_id);
    

    // 7. 뮤텍스 반납
    xSemaphoreGive(s_configMutex);
    
    // 8. 새로 할당된 ID 반환
    return v_new_id;
}


bool CL_C10_ConfigManager::updateScheduleFromJson(uint16_t p_id, const JsonDocument& p_patch) {
    // 1. 뮤텍스 획득
    if (xSemaphoreTake(s_configMutex, G_C10_MUTEX_TIMEOUT) != pdTRUE) {
        CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] updateScheduleFromJson() Mutex timeout!");
        return false;
    }

    ST_A10_SchedulesRoot_t* v_root = g_A10_config_root.schedules;
    if (!v_root) {
        xSemaphoreGive(s_configMutex);
        return false;
    }

    // 2. Schedule Item 찾기 (schId 기준)
    ST_A10_ScheduleItem_t* v_item = nullptr;
    for (uint8_t i = 0; i < v_root->count; i++) {
        if (v_root->items[i].schId == p_id) {
            v_item = &v_root->items[i];
            break;
        }
    }

    if (!v_item) {
        CL_D10_Logger::log(EN_L10_LOG_WARN, "[C10] Schedule update failed: ID %u not found.", p_id);
        xSemaphoreGive(s_configMutex);
        return false;
    }

    bool v_changed = false;
    JsonObjectConst j_patch = p_patch.as<JsonObjectConst>();
    
    // name 패치
    if (j_patch["name"].is<const char*>()) {
        const char* v_new = j_patch["name"];
        if (strcmp(v_new, v_item->name) != 0) {
            A10_safe_strlcpy(v_item->name, v_new, sizeof(v_item->name));
            v_changed = true;
        }
    }
    
    // enabled 패치
    if (j_patch["enabled"].is<bool>()) {
        if (j_patch["enabled"].as<bool>() != v_item->enabled) {
            v_item->enabled = j_patch["enabled"];
            v_changed = true;
        }
    }
    
    // =========================================================================
    // period 패치 (ST_A10_SchedulePeriod_t) 
    // =========================================================================
    if (j_patch["period"].is<JsonObjectConst>()) {
        JsonObjectConst j_period = j_patch["period"];
        
        // enabled
        if (j_period["enabled"].is<bool>()) {
            if (j_period["enabled"].as<bool>() != v_item->period.enabled) {
                v_item->period.enabled = j_period["enabled"];
                v_changed = true;
            }
        }

        // start_time (char[6])
        if (j_period["start_time"].is<const char*>()) {
            const char* v_new_start = j_period["start_time"];
            if (strcmp(v_new_start, v_item->period.start_time) != 0) {
                A10_safe_strlcpy(v_item->period.start_time, v_new_start, sizeof(v_item->period.start_time));
                v_changed = true;
            }
        }

        // end_time (char[6])
        if (j_period["end_time"].is<const char*>()) {
            const char* v_new_end = j_period["end_time"];
            if (strcmp(v_new_end, v_item->period.end_time) != 0) {
                A10_safe_strlcpy(v_item->period.end_time, v_new_end, sizeof(v_item->period.end_time));
                v_changed = true;
            }
        }
        
        // days (uint8_t[7] 배열)
        JsonArrayConst j_days = j_period["days"].as<JsonArrayConst>();
        if (!j_days.isNull()) {
            bool v_days_changed = false;
            // 배열 크기 7을 명시적으로 체크
            if (j_days.size() == 7) { 
                for (int i = 0; i < 7; i++) {
                    // JSON에서 uint8_t 값을 읽어오고, 실패 시 0을 사용
                    uint8_t v_day_val = j_days[i].is<uint8_t>() ? j_days[i].as<uint8_t>() : 0;
                    if (v_day_val != v_item->period.days[i]) {
                        v_item->period.days[i] = v_day_val;
                        v_days_changed = true;
                    }
                }
                if (v_days_changed) v_changed = true;
            }
        }
    }

    // =========================================================================
    // autoOff 패치 (ST_A10_SchAutoOff_t = ST_A10_AutoOff_t) 
    // timer, offTime, offTemp 서브 구조체를 포함합니다.
    // =========================================================================
    if (j_patch["autoOff"].is<JsonObjectConst>()) {
        JsonObjectConst j_autoOff = j_patch["autoOff"];
        
        // 1. timer (minutes/enabled)
        if (j_autoOff["timer"].is<JsonObjectConst>()) {
            JsonObjectConst j_timer = j_autoOff["timer"];

            // timer.enabled
            if (j_timer["enabled"].is<bool>()) {
                if (j_timer["enabled"].as<bool>() != v_item->autoOff.timer.enabled) {
                    v_item->autoOff.timer.enabled = j_timer["enabled"];
                    v_changed = true;
                }
            }

            // timer.minutes (uint32_t)
            uint32_t v_minutes = j_timer["minutes"].is<uint32_t>() ? j_timer["minutes"].as<uint32_t>() : v_item->autoOff.timer.minutes;
            if (v_minutes != v_item->autoOff.timer.minutes) {
                v_item->autoOff.timer.minutes = v_minutes;
                v_changed = true;
            }
        }
        
        // 2. offTime (enabled/time)
        if (j_autoOff["offTime"].is<JsonObjectConst>()) {
            JsonObjectConst j_offTime = j_autoOff["offTime"];

            // offTime.enabled
            if (j_offTime["enabled"].is<bool>()) {
                if (j_offTime["enabled"].as<bool>() != v_item->autoOff.offTime.enabled) {
                    v_item->autoOff.offTime.enabled = j_offTime["enabled"];
                    v_changed = true;
                }
            }

            // offTime.time (char[6] - "HH:MM")
            if (j_offTime["time"].is<const char*>()) {
                const char* v_new_time = j_offTime["time"];
                if (strcmp(v_new_time, v_item->autoOff.offTime.time) != 0) {
                    A10_safe_strlcpy(v_item->autoOff.offTime.time, v_new_time, sizeof(v_item->autoOff.offTime.time));
                    v_changed = true;
                }
            }
        }

        // 3. offTemp (enabled/temp)
        if (j_autoOff["offTemp"].is<JsonObjectConst>()) {
            JsonObjectConst j_offTemp = j_autoOff["offTemp"];

            // offTemp.enabled
            if (j_offTemp["enabled"].is<bool>()) {
                if (j_offTemp["enabled"].as<bool>() != v_item->autoOff.offTemp.enabled) {
                    v_item->autoOff.offTemp.enabled = j_offTemp["enabled"];
                    v_changed = true;
                }
            }

            // offTemp.temp (float)
            float v_temp = j_offTemp["temp"].is<float>() ? j_offTemp["temp"].as<float>() : v_item->autoOff.offTemp.temp;
            if (v_temp != v_item->autoOff.offTemp.temp) {
                v_item->autoOff.offTemp.temp = v_temp;
                v_changed = true;
            }
        }
    }
    
    // =========================================================================
    // motion 패치 (ST_A10_Motion_t) 
    // pir, ble 서브 구조체를 포함합니다.
    // =========================================================================
    if (j_patch["motion"].is<JsonObjectConst>()) {
        JsonObjectConst j_motion = j_patch["motion"];
        
        // 1. PIR (enabled/hold_sec)
        if (j_motion["pir"].is<JsonObjectConst>()) {
            JsonObjectConst j_pir = j_motion["pir"];

            // pir.enabled
            if (j_pir["enabled"].is<bool>()) {
                if (j_pir["enabled"].as<bool>() != v_item->motion.pir.enabled) {
                    v_item->motion.pir.enabled = j_pir["enabled"];
                    v_changed = true;
                }
            }

            // pir.hold_sec (int32_t)
            int32_t v_hold = j_pir["hold_sec"].is<int32_t>() ? j_pir["hold_sec"].as<int32_t>() : v_item->motion.pir.hold_sec;
            if (v_hold != v_item->motion.pir.hold_sec) {
                v_item->motion.pir.hold_sec = v_hold;
                v_changed = true;
            }
        }
        
        // 2. BLE (enabled/rssi_threshold/hold_sec)
        if (j_motion["ble"].is<JsonObjectConst>()) {
            JsonObjectConst j_ble = j_motion["ble"];

            // ble.enabled
            if (j_ble["enabled"].is<bool>()) {
                if (j_ble["enabled"].as<bool>() != v_item->motion.ble.enabled) {
                    v_item->motion.ble.enabled = j_ble["enabled"];
                    v_changed = true;
                }
            }

            // ble.rssi_threshold (int32_t)
            // Note: ST_A10_Motion_t 구조체 정의에서는 ble에 rssi_threshold, hold_sec 필드가 직접 존재하지 않고,
            // ST_A10_MotionConfig 구조체에 ble.rssi.on/off/exit_delay_sec 형태로 존재하나, 
            // ST_A10_ScheduleItem_t의 motion 필드는 ST_A10_Motion_t 타입이며, 이는 pir/ble 서브 구조체만 가지고 있습니다.
            // ST_A10_Motion_t 정의에 따라 다음과 같이 필드를 찾습니다.
            // ST_A10_Motion_t { pir:{}, ble:{ enabled, rssi_threshold, hold_sec } }
            
            // rssi_threshold
            int32_t v_rssi = j_ble["rssi_threshold"].is<int32_t>() ? j_ble["rssi_threshold"].as<int32_t>() : v_item->motion.ble.rssi_threshold;
            if (v_rssi != v_item->motion.ble.rssi_threshold) {
                v_item->motion.ble.rssi_threshold = v_rssi;
                v_changed = true;
            }

            // hold_sec
            int32_t v_ble_hold = j_ble["hold_sec"].is<int32_t>() ? j_ble["hold_sec"].as<int32_t>() : v_item->motion.ble.hold_sec;
            if (v_ble_hold != v_item->motion.ble.hold_sec) {
                v_item->motion.ble.hold_sec = v_ble_hold;
                v_changed = true;
            }
        }
    }


    // =========================================================================
    // segments 패치 (배열 전체 덮어쓰기)
    // =========================================================================
    JsonArrayConst j_segs = j_patch["segments"].as<JsonArrayConst>();
    if (!j_segs.isNull()) {
        v_item->seg_count = 0; // 기존 세그먼트 초기화
        for (JsonObjectConst jseg : j_segs) {
            if (v_item->seg_count >= A10_Const::MAX_SEGMENTS_PER_SCHEDULE) {
                CL_D10_Logger::log(EN_L10_LOG_WARN, "[C10] Max segments reached for Schedule ID %u.", p_id);
                break;
            }
            ST_A10_ScheduleSegment_t& sg = v_item->segments[v_item->seg_count++];

            // 기본 필드 로드
            sg.segId         = jseg["segId"] | 0;
            sg.segNo         = jseg["segNo"] | 0;
            sg.on_minutes    = jseg["on_minutes"].is<uint16_t>() ? jseg["on_minutes"].as<uint16_t>() : 0;
            sg.off_minutes   = jseg["off_minutes"].is<uint16_t>() ? jseg["off_minutes"].as<uint16_t>() : 0;
            
            // mode 로드 및 변환
            const char* v_mode = jseg["mode"] | "PRESET";
            sg.mode = A10_modeFromString(v_mode); 

            // presetCode와 styleCode 사용
            A10_safe_strlcpy(sg.presetCode, jseg["presetCode"] | "", sizeof(sg.presetCode));
            A10_safe_strlcpy(sg.styleCode, jseg["styleCode"] | "", sizeof(sg.styleCode));
            
            // fixed_speed
            sg.fixed_speed = jseg["fixed_speed"].is<float>() ? jseg["fixed_speed"].as<float>() : 0.0f;
            
            // adjust (ST_A10_AdjustDelta_t) 로드
            if (jseg["adjust"].is<JsonObjectConst>()) {
                JsonObjectConst adj = jseg["adjust"];
                sg.adjust.wind_intensity           = adj["wind_intensity"].is<float>() ? adj["wind_intensity"].as<float>() : 0.0f;
                sg.adjust.wind_variability         = adj["wind_variability"].is<float>() ? adj["wind_variability"].as<float>() : 0.0f;
                sg.adjust.gust_frequency           = adj["gust_frequency"].is<float>() ? adj["gust_frequency"].as<float>() : 0.0f;
                sg.adjust.fan_limit                = adj["fan_limit"].is<float>() ? adj["fan_limit"].as<float>() : 0.0f;
                sg.adjust.min_fan                  = adj["min_fan"].is<float>() ? adj["min_fan"].as<float>() : 0.0f;
                sg.adjust.turbulence_length_scale  = adj["turbulence_length_scale"].is<float>() ? adj["turbulence_length_scale"].as<float>() : 0.0f;
                sg.adjust.turbulence_intensity_sigma = adj["turbulence_intensity_sigma"].is<float>() ? adj["turbulence_intensity_sigma"].as<float>() : 0.0f;
            } else {
                // adjust 객체가 없으면 0으로 안전하게 초기화
                memset(&sg.adjust, 0, sizeof(sg.adjust));
            }
        }
        v_changed = true;
    }
    
    // 3. 변경 사항이 있으면 플래그 설정
    if (v_changed) {
        _dirty_schedules = true;
        CL_D10_Logger::log(EN_L10_LOG_INFO, "[C10] Schedule ID %u patched. Dirty=true", p_id);
    }

    // 4. 뮤텍스 반납
    xSemaphoreGive(s_configMutex);
    return v_changed;
}


bool CL_C10_ConfigManager::deleteSchedule(uint16_t p_id) {
        if (xSemaphoreTake(s_configMutex, G_C10_MUTEX_TIMEOUT) != pdTRUE) {
            CL_D10_Logger::log(EN_L10_LOG_ERROR, "[C10] deleteSchedule() Mutex timeout!");
            return false;
        }

        ST_A10_SchedulesRoot_t* v_root = g_A10_config_root.schedules;
        if (!v_root) {
            xSemaphoreGive(s_configMutex);
            return false;
        }

        int v_del_idx = -1;
        for (uint8_t i = 0; i < v_root->count; i++) {
            if (v_root->items[i].schId == p_id) {
                v_del_idx = i;
                break;
            }
			/*
			if (v_root->items[i].schNo == p_id) {
                v_del_idx = i;
                break;
            }
			*/
        }

        if (v_del_idx == -1) {
            CL_D10_Logger::log(EN_L10_LOG_WARN, "[C10] Schedule deletion failed: ID %u not found.", p_id);
            xSemaphoreGive(s_configMutex);
            return false;
        }

        // 배열에서 항목 제거 (덮어쓰기)
        for (uint8_t i = v_del_idx; i < v_root->count - 1; i++) {
            v_root->items[i] = v_root->items[i + 1];
        }

        v_root->count--; // 카운트 감소

        _dirty_schedules = true;
        CL_D10_Logger::log(EN_L10_LOG_INFO, "[C10] Schedule ID %u deleted. count=%u. Dirty=true", p_id, v_root->count);

        xSemaphoreGive(s_configMutex);
        return true;
}


// **[추가 구현]** WindProfileDict를 JSON으로 변환하는 함수
void CL_C10_ConfigManager::toJson_WindProfileDict(const ST_A10_WindProfileDict_t& p,
									   JsonDocument&				   d) {
		JsonObject j_wind = d["windProfile"].to<JsonObject>();
		
		// 1. Presets
		j_wind["preset_count"] = p.preset_count;
		JsonArray j_presets = j_wind["presets"].to<JsonArray>();
		
		for (uint8_t i = 0; i < p.preset_count; i++) {
			const ST_A10_PresetEntry_t& v_p = p.presets[i];
			JsonObject j_item = j_presets.add<JsonObject>();

			j_item["name"] = v_p.name;
			j_item["code"] = v_p.code;

			// Base Parameters
			JsonObject j_base = j_item["base"].to<JsonObject>();
			j_base["wind_intensity"]			 = v_p.base.wind_intensity;
			j_base["gust_frequency"]			 = v_p.base.gust_frequency;
			j_base["wind_variability"]			 = v_p.base.wind_variability;
			j_base["fan_limit"]					 = v_p.base.fan_limit;
			j_base["min_fan"]					 = v_p.base.min_fan;
			j_base["turbulence_length_scale"]	 = v_p.base.turbulence_length_scale;
			j_base["turbulence_intensity_sigma"] = v_p.base.turbulence_intensity_sigma;
			j_base["thermal_bubble_strength"]	 = v_p.base.thermal_bubble_strength;
			j_base["thermal_bubble_radius"]		 = v_p.base.thermal_bubble_radius;
		}

		// 2. Styles
		j_wind["style_count"] = p.style_count;
		JsonArray j_styles = j_wind["styles"].to<JsonArray>();

		for (uint8_t i = 0; i < p.style_count; i++) {
			const ST_A10_StyleEntry_t& v_s = p.styles[i];
			JsonObject j_item = j_styles.add<JsonObject>();

			j_item["name"] = v_s.name;
			j_item["code"] = v_s.code;

			// Factors
			JsonObject j_factors = j_item["factors"].to<JsonObject>();
			j_factors["intensity_factor"]	= v_s.factors.intensity_factor;
			j_factors["variability_factor"]	= v_s.factors.variability_factor;
			j_factors["gust_factor"]		= v_s.factors.gust_factor;
			j_factors["thermal_factor"]		= v_s.factors.thermal_factor;
		}
}

// =====================================================
// 3-1. 목적물별 JSON Export 구현 (Schedules/UserProfiles)
// =====================================================
void CL_C10_ConfigManager::toJson_Schedules(const ST_A10_SchedulesRoot_t& p,
								 JsonDocument&				   d) {
		d["schedules_count"] = p.count;

		for (uint8_t i = 0; i < p.count; i++) {
			const ST_A10_ScheduleItem_t& s =
				p.items[i];
			JsonObject js =
				d["schedules"][i];

			js["schId"]	  = s.schId;
			js["schNo"]	  = s.schNo;
			js["name"]	  = s.name;
			js["enabled"] = s.enabled;

			js["period"]["enabled"] =
				s.period.enabled;
			for (uint8_t d_i = 0; d_i < 7; d_i++) {
				js["period"]["days"][d_i] =
					s.period.days[d_i];
			}
			js["period"]["start_time"] =
				s.period.start_time;
			js["period"]["end_time"] =
				s.period.end_time;

			js["seg_count"] = s.seg_count;
			for (uint8_t k = 0;
				 k < s.seg_count;
				 k++) {
				const ST_A10_ScheduleSegment_t& sg =
					s.segments[k];
				JsonObject jseg =
					js["segments"][k];

				jseg["segId"]		= sg.segId;
				jseg["segNo"]		= sg.segNo;
				jseg["on_minutes"]	= sg.on_minutes;
				jseg["off_minutes"] = sg.off_minutes;
				jseg["mode"] =
					A10_modeToString(sg.mode);
				jseg["presetCode"] = sg.presetCode;
				jseg["styleCode"]  = sg.styleCode;

				JsonObject adj =
					jseg["adjust"];
				adj["wind_intensity"] =
					sg.adjust.wind_intensity;
				adj["wind_variability"] =
					sg.adjust.wind_variability;
				adj["gust_frequency"] =
					sg.adjust.gust_frequency;
				adj["fan_limit"] =
					sg.adjust.fan_limit;
				adj["min_fan"] =
					sg.adjust.min_fan;

				jseg["fixed_speed"] =
					sg.fixed_speed;
			}

			JsonObject ao =
				js["autoOff"];
			ao["timer"]["enabled"] =
				s.autoOff.timer.enabled;
			ao["timer"]["minutes"] =
				s.autoOff.timer.minutes;
			ao["offTime"]["enabled"] =
				s.autoOff.offTime.enabled;
			ao["offTime"]["time"] =
				s.autoOff.offTime.time;
			ao["offTemp"]["enabled"] =
				s.autoOff.offTemp.enabled;
			ao["offTemp"]["temp"] =
				s.autoOff.offTemp.temp;

			js["motion"]["pir"]["enabled"] =
				s.motion.pir.enabled;
			js["motion"]["pir"]["hold_sec"] =
				s.motion.pir.hold_sec;
			js["motion"]["ble"]["enabled"] =
				s.motion.ble.enabled;
			js["motion"]["ble"]["rssi_threshold"] =
				s.motion.ble.rssi_threshold;
			js["motion"]["ble"]["hold_sec"] =
				s.motion.ble.hold_sec;
		}
}

void CL_C10_ConfigManager::toJson_UserProfiles(const ST_A10_UserProfilesRoot_t& p,
									JsonDocument&					 d) {
		d["userProfiles"]["count"] = p.count;

		for (uint8_t i = 0; i < p.count; i++) {
			const ST_A10_UserProfileItem_t& up =
				p.items[i];
			JsonObject jp =
				d["userProfiles"]["profiles"][i];

			jp["profileId"]		 = up.profileId;
			jp["profileNo"]		 = up.profileNo;
			jp["name"]			 = up.name;
			jp["enabled"]		 = up.enabled;
			jp["repeatSegments"] = up.repeatSegments;
			jp["seg_count"]		 = up.seg_count;

			for (uint8_t k = 0;
				 k < up.seg_count;
				 k++) {
				const ST_A10_UserProfileSegment_t& sg =
					up.segments[k];
				JsonObject jseg =
					jp["segments"][k];

				jseg["segId"]		= sg.segId;
				jseg["segNo"]		= sg.segNo;
				jseg["on_minutes"]	= sg.on_minutes;
				jseg["off_minutes"] = sg.off_minutes;
				jseg["mode"] =
					A10_modeToString(sg.mode);
				jseg["presetCode"] = sg.presetCode;
				jseg["styleCode"]  = sg.styleCode;

				JsonObject adj =
					jseg["adjust"];
				adj["wind_intensity"] =
					sg.adjust.wind_intensity;
				adj["wind_variability"] =
					sg.adjust.wind_variability;
				adj["gust_frequency"] =
					sg.adjust.gust_frequency;
				adj["fan_limit"] =
					sg.adjust.fan_limit;
				adj["min_fan"] =
					sg.adjust.min_fan;

				jseg["fixed_speed"] =
					sg.fixed_speed;
			}

			JsonObject ao =
				jp["autoOff"];
			ao["timer"]["enabled"] =
				up.autoOff.timer.enabled;
			ao["timer"]["minutes"] =
				up.autoOff.timer.minutes;
			ao["offTime"]["enabled"] =
				up.autoOff.offTime.enabled;
			ao["offTime"]["time"] =
				up.autoOff.offTime.time;
			ao["offTemp"]["enabled"] =
				up.autoOff.offTemp.enabled;
			ao["offTemp"]["temp"] =
				up.autoOff.offTemp.temp;

			jp["motion"]["pir"]["enabled"] =
				up.motion.pir.enabled;
			jp["motion"]["pir"]["hold_sec"] =
				up.motion.pir.hold_sec;
			jp["motion"]["ble"]["enabled"] =
				up.motion.ble.enabled;
			jp["motion"]["ble"]["rssi_threshold"] =
				up.motion.ble.rssi_threshold;
			jp["motion"]["ble"]["hold_sec"] =
				up.motion.ble.hold_sec;
		}
}




