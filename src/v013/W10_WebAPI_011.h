#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : W10_WebAPI_010.h
 * 모듈명 : Smart Nature Wind Web API Manager
 * ------------------------------------------------------
 * 기능 요약:
 *  - LittleFS 정적 자산 서빙 (코어 설정 기반 + 기본 경로 동시 지원)
 *  - /api/state, /api/chart, /api/config(GET/POST), /api/reset(stream),
 *    /api/version, /api/logs, /api/diag, /api/scan, /api/sim/start|stop|preset(Body)
 *  - 정적 파일 업로드(/upload), OTA(/update)
 *  - API Key 인증(X-API-Key) / CORS / No-Cache 헤더 적용
 * ------------------------------------------------------
 * [코드 네이밍 규칙]
 *  - 현재 파일 모듈약어    : W10
 *  - 전역 상수,매크로      : G_모듈약어_
 *  - 전역 변수             : g_모듈약어_
 *  - 전역 함수             : 모듈약어_
 *  - 구조체, 열거형        : ST_, EN_ 접두사
 *  - 클래스명              : CL_모듈약어_
 *  - 클래스 정적 멤버      : s_
 *  - 클래스 private 멤버   : _
 * ------------------------------------------------------
 */

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <Update.h>
#include <WiFi.h>
#include <WiFiMulti.h>

#include "A10_Const_011.h"
#include "C10_ConfigManager_011.h"
#include "D10_Logger_011.h"
#include "M10_WiFiManager_011.h"
#include "S10_Simulation_011.h"
#include "P10_PWM_ctrl_011.h"

// ------------------------------------------------------
// 정적 파일 경로 상수
// ------------------------------------------------------
namespace W10_Const {
	constexpr char MAIN_PAGE_HTML_FILE[] = "/html/SC10_main_021.html";
	constexpr char MAIN_PAGE_HTML_URI[]  = "/SC10_main_021.html";
	constexpr char MAIN_PAGE_HTML_MIME[] = "text/html";

	constexpr char MAIN_PAGE_CSS_FILE[]  = "/html/SC10_main_021.css";
	constexpr char MAIN_PAGE_CSS_URI[]   = "/SC10_main_021.css";
	constexpr char MAIN_PAGE_CSS_MIME[]  = "text/css";

	constexpr char MAIN_PAGE_JS_FILE[]   = "/html/SC10_main_021.js";
	constexpr char MAIN_PAGE_JS_URI[]    = "/SC10_main_021.js";
	constexpr char MAIN_PAGE_JS_MIME[]   = "application/javascript";

	constexpr char CHART_PAGE_HTML_FILE[] = "/html/SC10_chart_003.html";
	constexpr char CHART_PAGE_HTML_URI[]  = "/SC10_chart_003.html";
	constexpr char CHART_PAGE_HTML_MIME[] = "text/html";

	constexpr char CHART_PAGE_CSS_FILE[]  = "/html/SC10_chart_003.css";
	constexpr char CHART_PAGE_CSS_URI[]   = "/SC10_chart_003.css";
	constexpr char CHART_PAGE_CSS_MIME[]  = "text/css";

	constexpr char CHART_PAGE_JS_FILE[]   = "/html/SC10_chart_003.js";
	constexpr char CHART_PAGE_JS_URI[]    = "/SC10_chart_003.js";
	constexpr char CHART_PAGE_JS_MIME[]   = "application/javascript";

	constexpr char CHART_ALIAS_URI[]      = "/chart";
	constexpr char CHART_ALIAS_FILE[]     = "/SC10_chart_003.html";
	constexpr char CHART_ALIAS_MIME[]     = "text/html";
}

// ------------------------------------------------------
// Web API Manager 클래스
// ------------------------------------------------------
class CL_W10_WebAPI {
public:
	static void W10_init(AsyncWebServer &srv,
						 CL_S10_Simulation &sim,
						 WiFiMulti &multi,
						 CL_P10_PWM &pwm)
	{
		s_pServer  = &srv;
		s_pSim     = &sim;
		s_pWiMulti = &multi;
		s_pPwm     = &pwm;

		_W10_buildStaticRoutes();
		_W10_mountStatic(srv);
		_W10_mountApi(srv);
	}

private:
	// ======================================================
	// 정적 라우팅 구성
	// ======================================================
	struct ST_W10_StaticRoute { const char* uri; const char* file; const char* mime; };
	static inline ST_W10_StaticRoute s_staticRoutes[16];
	static inline uint8_t s_staticCount = 0;

	static void _W10_buildStaticRoutes() {
		auto push = [](const char* uri, const char* file, const char* mime){
			if (s_staticCount < 16) s_staticRoutes[s_staticCount++] = {uri,file,mime};
		};

		// 메인 페이지: 코어 설정 우선
		if (strlen(g_A10_config_root.core.system.web.html) > 0)
			push(g_A10_config_root.core.system.web.html, g_A10_config_root.core.system.web.html, W10_Const::MAIN_PAGE_HTML_MIME);
		push(W10_Const::MAIN_PAGE_HTML_URI, W10_Const::MAIN_PAGE_HTML_FILE, W10_Const::MAIN_PAGE_HTML_MIME);

		if (strlen(g_A10_config_root.core.system.web.css) > 0)
			push(g_A10_config_root.core.system.web.css, g_A10_config_root.core.system.web.css, W10_Const::MAIN_PAGE_CSS_MIME);
		push(W10_Const::MAIN_PAGE_CSS_URI, W10_Const::MAIN_PAGE_CSS_FILE, W10_Const::MAIN_PAGE_CSS_MIME);

		if (strlen(g_A10_config_root.core.system.web.js) > 0)
			push(g_A10_config_root.core.system.web.js, g_A10_config_root.core.system.web.js, W10_Const::MAIN_PAGE_JS_MIME);
		push(W10_Const::MAIN_PAGE_JS_URI, W10_Const::MAIN_PAGE_JS_FILE, W10_Const::MAIN_PAGE_JS_MIME);

		// 차트
		push(W10_Const::CHART_PAGE_HTML_URI, W10_Const::CHART_PAGE_HTML_FILE, W10_Const::CHART_PAGE_HTML_MIME);
		push(W10_Const::CHART_PAGE_CSS_URI,  W10_Const::CHART_PAGE_CSS_FILE,  W10_Const::CHART_PAGE_CSS_MIME);
		push(W10_Const::CHART_PAGE_JS_URI,   W10_Const::CHART_PAGE_JS_FILE,   W10_Const::CHART_PAGE_JS_MIME);
		push(W10_Const::CHART_ALIAS_URI,     W10_Const::CHART_ALIAS_FILE,     W10_Const::CHART_ALIAS_MIME);
	}

	static void _W10_mountStatic(AsyncWebServer &srv) {
		srv.on("/", HTTP_GET, _W10_onRootGet);
		for (uint8_t i=0;i<s_staticCount;i++)
			srv.on(s_staticRoutes[i].uri, HTTP_GET, _W10_onStaticGet);
		srv.onNotFound(_W10_onNotFound);
	}

	static void _W10_onRootGet(AsyncWebServerRequest *req) {
		const char* f = g_A10_config_root.core.system.web.html;
		if (strlen(f)>0 && LittleFS.exists(f)) req->redirect(f);
		else req->redirect(W10_Const::MAIN_PAGE_HTML_URI);
	}

	static void _W10_onStaticGet(AsyncWebServerRequest *req) {
		const char* uri=req->url().c_str();
		for (uint8_t i=0;i<s_staticCount;i++) {
			if (strcmp(uri,s_staticRoutes[i].uri)==0) {
				if (LittleFS.exists(s_staticRoutes[i].file))
					req->send(LittleFS,s_staticRoutes[i].file,s_staticRoutes[i].mime);
				else {
					String msg="/* missing:"+String(s_staticRoutes[i].file)+" */";
					auto* r=req->beginResponse(200,s_staticRoutes[i].mime,msg);
					_W10_applyHeaders(r,true);
					req->send(r);
				}
				return;
			}
		}
		req->send(404,"text/plain","Not found");
	}

	static void _W10_onNotFound(AsyncWebServerRequest *req){
		if (req->method()==HTTP_OPTIONS){
			auto*r=req->beginResponse(204);
			_W10_applyHeaders(r,false);
			req->send(r);
			return;
		}
		req->send(404,"text/plain","Not found");
	}

	// ======================================================
	// API 라우팅
	// ======================================================
	static void _W10_mountApi(AsyncWebServer &srv){
		srv.on("/api/state",HTTP_GET,_W10_api_state);
		srv.on("/api/chart",HTTP_GET,_W10_api_chart);
		srv.on("/api/chart_data",HTTP_GET,_W10_api_chart);
		srv.on("/api/version",HTTP_GET,_W10_api_version);
		srv.on("/api/scan",HTTP_GET,_W10_api_scan);

		srv.on("/api/config",HTTP_GET,_W10_api_config_get);
		srv.on("/api/config",HTTP_POST,_W10_api_config_post,nullptr,_W10_api_config_body);

		srv.on("/api/config/init",HTTP_POST,_W10_api_config_init);
		srv.on("/api/reset",HTTP_POST,_W10_api_reset);

		srv.on("/api/logs",HTTP_GET,_W10_api_logs);
		srv.on("/api/diag",HTTP_GET,_W10_api_diag);

		srv.on("/api/sim/start",HTTP_POST,_W10_api_sim_start);
		srv.on("/api/sim/stop",HTTP_POST,_W10_api_sim_stop);
		srv.on("/api/sim/preset",HTTP_POST,_W10_api_sim_preset,nullptr,_W10_api_sim_preset_body);

		srv.on("/upload",HTTP_POST,_W10_api_upload_end,_W10_api_upload_body);
		srv.on("/update",HTTP_POST,_W10_api_update_end,_W10_api_update_body);
	}

	// ======================================================
	// 주요 API 핸들러들
	// ======================================================
	static void _W10_api_state(AsyncWebServerRequest *r){
		JsonDocument doc;
		if (s_pSim) s_pSim->S10_toJson(doc);

		JsonObject st=doc["status"].to<JsonObject>();
		if (WiFi.status()==WL_CONNECTED){
			st["wifi"]["mode"]="STA";
			st["wifi"]["ip"]=WiFi.localIP().toString();
			st["wifi"]["ssid"]=WiFi.SSID();
			st["wifi"]["rssi"]=WiFi.RSSI();
		}else{
			st["wifi"]["mode"]="AP";
			st["wifi"]["ip"]=WiFi.softAPIP().toString();
			st["wifi"]["ssid"]=g_A10_config_root.core.meta.device_name;
		}
		if (s_pPwm){
			st["pwm"]["percent"]=s_pPwm->P10_getDutyPercent();
			st["pwm"]["raw"]=s_pPwm->P10_getDutyRaw();
		}

		String out;serializeJson(doc,out);
		auto*resp=r->beginResponse(200,"application/json",out);
		_W10_applyHeaders(resp,true);r->send(resp);
	}

	static void _W10_api_chart(AsyncWebServerRequest *r){
		JsonDocument doc;if(s_pSim)s_pSim->S10_toChartJson(doc);
		String out;serializeJson(doc,out);
		auto*resp=r->beginResponse(200,"application/json",out);
		_W10_applyHeaders(resp,true);r->send(resp);
	}

	static void _W10_api_version(AsyncWebServerRequest *r){
		JsonDocument d;d["fw_version"]=A10_Const::FW_VERSION;
		d["cfg_json_ver"]=G_A10_CFG_JSON_FILE_VER;
		String out;serializeJson(d,out);
		auto*resp=r->beginResponse(200,"application/json",out);
		_W10_applyHeaders(resp,true);r->send(resp);
	}

	static void _W10_api_scan(AsyncWebServerRequest *r){
		String j=CL_M10_WiFiManager::M10_scanNetworksJson(r->hasParam("async"));
		auto*resp=r->beginResponse(200,"application/json",j);
		_W10_applyHeaders(resp,true);r->send(resp);
	}

	// ---------- CONFIG ----------
	static inline String s_bodyBuffer;
	static void _W10_api_config_get(AsyncWebServerRequest *r){
		JsonDocument d;
		JsonObject root=d.to<JsonObject>();
		JsonDocument c;CL_C10_ConfigManager::toCoreJson(g_A10_config_root.core,c);
		root["core"]=c.as<JsonObject>();
		_W10_toJsonWifi(root["wifi"].to<JsonObject>());
		_W10_toJsonSim(root["sim"].to<JsonObject>());
		_W10_toJsonSchedule(root["schedule"].to<JsonObject>());
		_W10_toJsonMotion(root["motion"].to<JsonObject>());
		String out;serializeJson(d,out);
		auto*resp=r->beginResponse(200,"application/json",out);
		_W10_applyHeaders(resp,true);r->send(resp);
	}
	static void _W10_api_config_post(AsyncWebServerRequest *r){
		if(r->method()==HTTP_OPTIONS){
			auto*resp=r->beginResponse(204);_W10_applyHeaders(resp,false);r->send(resp);
		}
	}
	static void _W10_api_config_body(AsyncWebServerRequest *r,uint8_t*d,size_t l,size_t i,size_t t){
		if(!_W10_authorize(r)){r->send(401,"application/json","{\"error\":\"unauthorized\"}");return;}
		if(i==0)s_bodyBuffer.clear();s_bodyBuffer.concat((const char*)d,l);
		if(i+l<t)return;
		JsonDocument v;DeserializationError e=deserializeJson(v,s_bodyBuffer);s_bodyBuffer.clear();
		if(e){r->send(400,"application/json","{\"error\":\"invalid json\"}");return;}
		bool wf=false,chg=CL_C10_ConfigManager::patchFromJson(g_A10_config_root,v,wf);
		if(chg){
			CL_C10_ConfigManager::saveAll(g_A10_config_root);
			if(wf&&g_A10_config_root.wifi)CL_M10_WiFiManager::M10_init(*g_A10_config_root.wifi,*s_pWiMulti);
			if(s_pSim&&g_A10_config_root.sim)s_pSim->S10_applyPreset(g_A10_config_root.sim->preset);
			r->send(200,"application/json","{\"message\":\"updated\"}");
		}else r->send(200,"application/json","{\"message\":\"no change\"}");
	}

	// ---------- config/init & reset (stream) ----------
	static void _W10_api_config_init(AsyncWebServerRequest *r){
		if(!_W10_authorize(r)){r->send(401,"application/json","{\"error\":\"unauthorized\"}");return;}
		AsyncResponseStream*s=r->beginResponseStream("application/json");_W10_applyHeaders(s,true);
		s->print("{\"stage\":\"saving\",\"progress\":10}");
		A10_resetToDefault(g_A10_config_root);delay(100);
		CL_C10_ConfigManager::saveAll(g_A10_config_root);
		s->print(",{\"stage\":\"saved\",\"progress\":70}");
		delay(200);
		s->print(",{\"stage\":\"done\",\"progress\":100,\"message\":\"Factory defaults written\",\"done\":true}");
		r->send(s);
	}

	static void _W10_api_reset(AsyncWebServerRequest *r){
		if(!_W10_authorize(r)){r->send(401,"application/json","{\"error\":\"unauthorized\"}");return;}
		AsyncResponseStream*s=r->beginResponseStream("application/json");_W10_applyHeaders(s,true);
		s->print("{\"stage\":\"erasing\",\"progress\":10}");
		CL_C10_ConfigManager::resetAll(g_A10_config_root);
		delay(100);
		s->print(",{\"stage\":\"rebooting\",\"progress\":80}");
		delay(200);
		s->print(",{\"stage\":\"done\",\"progress\":100,\"message\":\"Factory reset complete\",\"done\":true}");
		r->send(s);
		delay(500);ESP.restart();
	}

	// ---------- LOGS, DIAG ----------
	static void _W10_api_logs(AsyncWebServerRequest *r){
		String j=CL_D10_Logger::getLogsJson();
		auto*resp=r->beginResponse(200,"application/json",j);
		_W10_applyHeaders(resp,true);r->send(resp);
	}
	static void _W10_api_diag(AsyncWebServerRequest *r){
		JsonDocument d;
		d["heap"]=ESP.getFreeHeap();
		d["rssi"]=(WiFi.status()==WL_CONNECTED)?WiFi.RSSI():0;
		d["fs_total"]=LittleFS.totalBytes();
		d["fs_used"]=LittleFS.usedBytes();
		String out;serializeJson(d,out);
		auto*resp=r->beginResponse(200,"application/json",out);
		_W10_applyHeaders(resp,true);r->send(resp);
	}

	// ---------- SIM ----------
	static void _W10_api_sim_start(AsyncWebServerRequest *r){
		if(!_W10_authorize(r)){r->send(401,"application/json","{\"error\":\"unauthorized\"}");return;}
		if(s_pSim&&s_pPwm){s_pSim->S10_begin(*s_pPwm);r->send(200,"application/json","{\"message\":\"Simulation started\"}");}
		else r->send(500,"application/json","{\"error\":\"not ready\"}");
	}
	static void _W10_api_sim_stop(AsyncWebServerRequest *r){
		if(!_W10_authorize(r)){r->send(401,"application/json","{\"error\":\"unauthorized\"}");return;}
		if(s_pSim){s_pSim->S10_stop();r->send(200,"application/json","{\"message\":\"Simulation stopped\"}");}
		else r->send(500,"application/json","{\"error\":\"not ready\"}");
	}
	static void _W10_api_sim_preset(AsyncWebServerRequest *r){
		if(r->method()==HTTP_OPTIONS){
			auto*resp=r->beginResponse(204);_W10_applyHeaders(resp,false);r->send(resp);
		}
	}
	static void _W10_api_sim_preset_body(AsyncWebServerRequest *r,uint8_t*d,size_t l,size_t i,size_t t){
		if(!_W10_authorize(r)){r->send(401,"application/json","{\"error\":\"unauthorized\"}");return;}
		if(!s_pSim){r->send(500,"application/json","{\"error\":\"not ready\"}");return;}
		if(i==0)s_bodyBuffer.clear();s_bodyBuffer.concat((const char*)d,l);
		if(i+l<t)return;
		JsonDocument v;if(deserializeJson(v,s_bodyBuffer)){s_bodyBuffer.clear();r->send(400,"application/json","{\"error\":\"invalid json\"}");return;}
		s_bodyBuffer.clear();
		const char*preset=v["preset"]|"OCEAN";
		s_pSim->S10_applyPreset(preset);
		r->send(200,"application/json","{\"message\":\"preset applied\"}");
	}

	// ---------- UPLOAD ----------
	static void _W10_api_upload_end(AsyncWebServerRequest *r){
		r->send(200,"application/json","{\"message\":\"upload completed\"}");
	}
	static void _W10_api_upload_body(AsyncWebServerRequest *r,const String&filename,size_t index,uint8_t*data,size_t len,bool final){
		if(index==0){
			CL_D10_Logger::log(EN_L10_LOG_INFO, "Upload start: %s", filename.c_str());
			if(LittleFS.exists(filename))LittleFS.remove(filename);
			s_upFile=LittleFS.open(filename,"w");
		}
		if(s_upFile) s_upFile.write(data,len);
		if(final){
			if(s_upFile){s_upFile.close();}
			CL_D10_Logger::log(EN_L10_LOG_INFO, "Upload finished: %s", filename.c_str());
		}
	}

	// ---------- OTA UPDATE ----------
	static void _W10_api_update_end(AsyncWebServerRequest *r){
		r->send(200,"application/json","{\"message\":\"update done\"}");
		delay(1000);ESP.restart();
	}
	static void _W10_api_update_body(AsyncWebServerRequest *r,const String&filename,size_t index,uint8_t*data,size_t len,bool final){
		if(index==0){
			Update.begin(UPDATE_SIZE_UNKNOWN);
			CL_D10_Logger::log(EN_L10_LOG_INFO, "OTA start: %s", filename.c_str());
		}
		if(Update.write(data,len)!=len)CL_D10_Logger::log(EN_L10_LOG_ERROR,"OTA write error");
		if(final){
			if(Update.end(true))CL_D10_Logger::log(EN_L10_LOG_INFO,"OTA success, rebooting");
			else CL_D10_Logger::log(EN_L10_LOG_ERROR,"OTA failed");
		}
	}

	// ======================================================
	// JSON 보조 직렬화
	// ======================================================
	static void _W10_toJsonWifi(JsonObject o){
		ST_A10_WifiConfig tmp;ST_A10_WifiConfig*cfg=g_A10_config_root.wifi;
		if(!cfg){if(CL_C10_ConfigManager::loadWifi(tmp))cfg=&tmp;}
		if(!cfg)return;
		o["mode"]=cfg->mode;
		o["ap"]["ssid"]=cfg->ap.ssid;
		JsonArray sta=o["sta"].to<JsonArray>();
		for(uint8_t i=0;i<cfg->sta_count;i++){
			JsonObject n=sta.add<JsonObject>();
			n["ssid"]=cfg->sta[i].ssid;
		}
	}
	static void _W10_toJsonSim(JsonObject o){
		ST_A10_SimConfig tmp;ST_A10_SimConfig*cfg=g_A10_config_root.sim;
		if(!cfg){if(CL_C10_ConfigManager::loadSim(tmp))cfg=&tmp;}
		if(!cfg)return;
		o["preset"]=cfg->preset;
		o["wind_intensity"]=cfg->wind_intensity;
		o["gust_frequency"]=cfg->gust_frequency;
		o["wind_variability"]=cfg->wind_variability;
		o["fan_limit"]=cfg->fan_limit;
		o["min_fan"]=cfg->min_fan;
		o["turbulence"]["length_scale"]=cfg->turbulence.length_scale;
		o["turbulence"]["intensity_sigma"]=cfg->turbulence.intensity_sigma;
		o["thermal"]["bubble_strength"]=cfg->thermal.bubble_strength;
		o["thermal"]["bubble_radius"]=cfg->thermal.bubble_radius;
	}
	static void _W10_toJsonSchedule(JsonObject o){
		ST_A10_ScheduleConfig tmp;ST_A10_ScheduleConfig*cfg=g_A10_config_root.schedule;
		if(!cfg){if(CL_C10_ConfigManager::loadSchedule(tmp))cfg=&tmp;}
		if(!cfg)return;
		o["count"]=cfg->count;
	}
	static void _W10_toJsonMotion(JsonObject o){
		ST_A10_MotionConfig tmp;ST_A10_MotionConfig*cfg=g_A10_config_root.motion;
		if(!cfg){if(CL_C10_ConfigManager::loadMotion(tmp))cfg=&tmp;}
		if(!cfg)return;
		o["enabled"]=cfg->enabled;
		o["pir"]["enabled"]=cfg->pir.enabled;
		o["ble"]["enabled"]=cfg->ble.enabled;
		o["ble"]["rssi_threshold"]=cfg->ble.rssi_threshold;
		o["ble"]["hold_sec"]=cfg->ble.hold_sec;
		JsonArray dev=o["ble"]["devices"].to<JsonArray>();
		for(uint8_t i=0;i<cfg->ble.device_count;i++){
			const auto&d=cfg->ble.devices[i];
			JsonObject n=dev.add<JsonObject>();
			n["mac"]=d.mac;n["alias"]=d.alias;n["enabled"]=d.enabled;
		}
	}

	// ======================================================
	// 유틸리티 (공통)
	// ======================================================
	static void _W10_applyHeaders(AsyncWebServerResponse*r,bool nocache=false){
		if(nocache){
			r->addHeader("Cache-Control","no-store, no-cache, must-revalidate, max-age=0");
			r->addHeader("Pragma","no-cache");
		}
		r->addHeader("Access-Control-Allow-Origin","*");
		r->addHeader("Access-Control-Allow-Methods","GET, POST, OPTIONS");
		r->addHeader("Access-Control-Allow-Headers","Content-Type, X-API-Key");
	}

	static bool _W10_authorize(AsyncWebServerRequest *req){
		if(strlen(g_A10_config_root.core.security.api_key)==0)return true;
		if(!req->hasHeader("X-API-Key"))return false;
		auto*h=req->getHeader("X-API-Key");
		return(h&&h->value()==String(g_A10_config_root.core.security.api_key));
	}

private:
	static inline AsyncWebServer *s_pServer=nullptr;
	static inline CL_S10_Simulation *s_pSim=nullptr;
	static inline WiFiMulti *s_pWiMulti=nullptr;
	static inline CL_P10_PWM *s_pPwm=nullptr;
	static inline File s_upFile;
};
