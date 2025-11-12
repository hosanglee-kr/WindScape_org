/*
 * ------------------------------------------------------
 * 소스명 : W10_Web_Upload_024.cpp
 * 모듈 약어 : W10
 * 모듈명 : Smart Nature Wind Web API (v024) - Upload/OTA Implementation
 * ------------------------------------------------------
 * 기능 요약:
 * - LittleFS 파일 업로드 및 OTA 펌웨어 업데이트 라우팅 구현
 * ------------------------------------------------------
 * [구현 규칙]
 * - 항상 소스 시작 주석 부분 체계 유지 및 내용 업데이트
 * - 소스 시작 주석 부분 구현규칙, 코드네이밍규칙 내용 그대로 유지, 수정금지
 * - ArduinoJson v7.x.x 사용 (v6 이하 사용 금지)
 * - JsonDocument 단일 타입만 사용
 * - createNestedArray/Object/containsKey 사용 금지
 * - memset + strlcpy 기반 안전 초기화
 * - 주석/필드명은 JSON 구조와 동일하게 유지
 * ------------------------------------------------------
 * [코드 네이밍 규칙]
 * - 전역 상수,매크로      : G_모듈약어_ 접두사
 * - 전역 변수             : g_모듈약어_ 접두사
 * - 전역 함수             : 모듈약어_ 접두사
 * - type                  : T_모듈약어_ 접두사
 * - typedef               : _t  접미사
 * - enum 상수             : EN_모듈약어_ 접두사
 * - 구조체                : ST_모듈약어_ 접두사
 * - 클래스명              : CL_모듈약어_ 접두사
 * - 클래스 private 멤버   : _ 접두사
 * - 클래스 멤버(함수/변수) : 모듈약어 접두사 미사용
 * - 클래스 정적 멤버      : s_ 접두사
 * ------------------------------------------------------
 */

#include "W10_Web_024.h"

// ------------------------------------------------------
// /upload (LittleFS 파일 업로드) 구현
// ------------------------------------------------------
void CL_W10_WebAPI::routeUpload() {
	s_server->on(
		"/upload",
		HTTP_POST,
		// End Handler (업로드 완료 시)
		[](AsyncWebServerRequest* p_request) {
			if (!checkApiKey(p_request)) {
				p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
				return;
			}
			// v012의 _api_upload_end 복구
			CL_W10_WebAPI::sendText(p_request, "{\"done\":true}");
		},
		// Data Handler (스트리밍 데이터 처리)
		[](AsyncWebServerRequest* p_request, const String& p_filename, size_t p_index, uint8_t* p_data, size_t p_len, bool p_final) {
			if (!checkApiKey(p_request))
				return; // 인증 실패 시 데이터 수신 중단 (p_request->send는 이미 End Handler에서 처리됨)

			// v012의 _api_upload_data 복구
			if (p_index == 0) {
				// 새 파일 업로드 시작
				if (LittleFS.exists(p_filename)) {
					LittleFS.remove(p_filename);
					CL_D10_Logger::log(EN_L10_LOG_INFO, "[W10] Removing existing file: %s", p_filename.c_str());
				}
				s_upFile = LittleFS.open(p_filename, "w");
				CL_D10_Logger::log(EN_L10_LOG_INFO, "[W10] Starting file upload: %s", p_filename.c_str());
			}

			if (s_upFile) {
				s_upFile.write(p_data, p_len);
			} else {
				CL_D10_Logger::log(EN_L10_LOG_ERROR, "[W10] File open failed for upload: %s", p_filename.c_str());
			}

			if (p_final) {
				if (s_upFile) {
					s_upFile.close();
					CL_D10_Logger::log(EN_L10_LOG_INFO, "[W10] File upload finished: %s", p_filename.c_str());
				}
			}
		});
}

// ------------------------------------------------------
// /update (OTA 펌웨어 업데이트) 구현
// ------------------------------------------------------
void CL_W10_WebAPI::routeUpdate() {
	s_server->on(
		"/update",
		HTTP_POST,
		// End Handler
		[](AsyncWebServerRequest* p_request) {
			if (!checkApiKey(p_request)) {
				p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
				return;
			}
			// v012의 _api_update_end 복구
			if (Update.is=='n') {
				CL_W10_WebAPI::sendText(p_request, "{\"ota\":\"fail\", \"error\":\"" + Update.errorString() + "\"}", 500);
			} else {
				CL_W10_WebAPI::sendText(p_request, "{\"ota\":\"ok\"}");
				delay(300);
				ESP.restart();
			}
		},
		// Data Handler
		[](AsyncWebServerRequest* p_request, const String& p_filename, size_t p_index, uint8_t* p_data, size_t p_len, bool p_final) {
			if (!checkApiKey(p_request))
				return; // 인증 실패 시 데이터 수신 중단

			// v012의 _api_update_data 복구
			if (p_index == 0) {
				// OTA 업데이트 시작
				if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
					CL_D10_Logger::log(EN_L10_LOG_ERROR, "[W10] OTA begin failed: %s", Update.errorString());
				}
			}

			if (p_len > 0) {
				Update.write(p_data, p_len);
			}

			if (p_final) {
				if (Update.end(true)) {
					CL_D10_Logger::log(EN_L10_LOG_INFO, "[W10] OTA finished successfully");
				} else {
					CL_D10_Logger::log(EN_L10_LOG_ERROR, "[W10] OTA end failed: %s", Update.errorString());
				}
			}
		});
}
