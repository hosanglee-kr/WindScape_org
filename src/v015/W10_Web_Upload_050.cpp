/*
 * ------------------------------------------------------
 * 소스명 : W10_Web_Upload_050.cpp
 * 모듈 약어 : W10
 * 모듈명 : Smart Nature Wind Web API (v029) - Upload/OTA Implementation
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

#include "W10_Web_050.h"

// #define   G_W10_UPLOAD_FOLDPATH_JSON   "/json"
// #define   G_W10_UPLOAD_FOLDPATH_WEB   "/html_v2"

// ------------------------------------------------------
// 파일 저장 경로를 결정하는 내부 함수 (클래스 내부에 정의)
// ------------------------------------------------------
String CL_W10_WebAPI::getUploadPath(const String& p_filename) {
	// 1. 파일명에서 마지막 '.' 위치를 찾습니다.
	int v_dotIndex = p_filename.lastIndexOf('.');

	// 2. '.'이 없거나, 파일명 시작에 있다면 확장자가 없다고 간주하고 루트 폴더를 반환합니다.
	if (v_dotIndex == -1 || v_dotIndex == 0) {
		return "/" + p_filename;
	}

	// 3. 확장자를 추출하고 소문자로 변환합니다.
	String v_extension = p_filename.substring(v_dotIndex + 1);
	v_extension.toLowerCase();

	// 4. 확장자에 따라 저장 폴더를 결정합니다.
	String v_folderPath;

	if (v_extension == "json") {
		v_folderPath = W10_Const::PATH_STATIC_JSON;
		// v_folderPath = G_W10_UPLOAD_FOLDPATH_JSON;
		// v_folderPath = "/json/";
	} else if (v_extension == "html" || v_extension == "js" || v_extension == "css") {
		v_folderPath = W10_Const::PATH_STATIC_HTML;
		// v_folderPath 	= G_W10_UPLOAD_FOLDPATH_WEB;
		//  v_folderPath = "/html_v2/";
	} else {
		// 지정된 확장자가 아니면 루트 폴더에 저장합니다.
		v_folderPath = "";
	}

	// 5. 최종 경로를 반환합니다. (예: "/json/config.json", "/html_v2/index.html")
	return v_folderPath + "/" + p_filename;
}

// ------------------------------------------------------
// /upload (LittleFS 파일 업로드)
// ------------------------------------------------------
void CL_W10_WebAPI::routeUpload() {
	s_server->on(
		W10_Const::HTTP_API_FILE_UPLOAD,  // "/upload",
		HTTP_POST,
		[](AsyncWebServerRequest* p_request) {
			if (!checkApiKey(p_request)) {
				p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
				return;
			}
			CL_W10_WebAPI::sendText(p_request, "{\"done\":true}");
		},
		[](AsyncWebServerRequest* p_request, const String& p_filename, size_t p_index, uint8_t* p_data, size_t p_len, bool p_final) {
			// 변경된 부분 시작 ------------------------------------------------------
			// 1. 최종 저장될 파일 경로를 결정합니다.
			String v_finalPath = CL_W10_WebAPI::getUploadPath(p_filename);
			// 2. 파일 경로를 저장할 전역/정적 변수가 필요하다면, s_upFileName을 선언하고 사용해야 합니다.
			//    현재 로직은 p_filename을 그대로 사용하고 있어, 첫 번째 청크에서 경로를 결정한 후
			//    이 경로를 이후 청크에서도 사용할 수 있도록 변수가 필요합니다.
			//    임시 방편으로 파일 열기/삭제에 v_finalPath를 사용하고, 로그에는 원래 파일명을 사용했습니다.

			if (!checkApiKey(p_request))
				return;

			if (p_index == 0) {
				if (LittleFS.exists(v_finalPath)) {
					LittleFS.remove(v_finalPath);
					CL_D10_Logger::log(EN_L10_LOG_INFO, "[W10] Removing existing file: %s", v_finalPath.c_str());
				}
				s_upFile = LittleFS.open(v_finalPath, "w");
				CL_D10_Logger::log(EN_L10_LOG_INFO, "[W10] Starting file upload: %s (to %s)", p_filename.c_str(), v_finalPath.c_str());
			}

			if (s_upFile) {
				s_upFile.write(p_data, p_len);
			} else {
				CL_D10_Logger::log(EN_L10_LOG_ERROR, "[W10] File open failed for upload: %s", v_finalPath.c_str());
			}

			if (p_final) {
				if (s_upFile) {
					s_upFile.close();
					CL_D10_Logger::log(EN_L10_LOG_INFO, "[W10] File upload finished: %s (at %s)", p_filename.c_str(), v_finalPath.c_str());
				}
			}
			// 변경된 부분 끝 --------------------------------------------------------
		});
}

// ------------------------------------------------------
// /update (OTA 펌웨어 업데이트)
// ------------------------------------------------------
void CL_W10_WebAPI::routeUpdate() {
	s_server->on(
		W10_Const::HTTP_API_FW_UPDATE,	// "/update",
		HTTP_POST,
		[](AsyncWebServerRequest* p_request) {
			if (!checkApiKey(p_request)) {
				p_request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
				return;
			}
			if (Update.hasError()) {
				String v_errMsg = "{\"ota\":\"fail\", \"error\":\"" + String(Update.errorString()) + "\"}";
				CL_W10_WebAPI::sendText(p_request, v_errMsg, 500);
			} else {
				CL_W10_WebAPI::sendText(p_request, "{\"ota\":\"ok\"}");
				delay(300);
				ESP.restart();
			}
		},
		[](AsyncWebServerRequest* p_request, const String&, size_t p_index, uint8_t* p_data, size_t p_len, bool p_final) {
			if (!checkApiKey(p_request))
				return;

			if (p_index == 0) {
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
