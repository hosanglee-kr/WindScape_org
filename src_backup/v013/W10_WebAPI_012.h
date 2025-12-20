#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : W10_WebAPI_012.h
 * 모듈약어 : W10
 * 모듈명 : Smart Nature Wind Web API Manager (v012)
 * ------------------------------------------------------
 * 기능 요약:
 *  - LittleFS 정적 자산 + cfg_system_022.json UI path 적용
 *  - /api/state, /api/chart, /api/config (GET/POST)
 *  - /api/control (mode/preset/override/schedule)
 *  - /api/motion (status/feed/test)
 *  - /api/reset, /api/version, /api/diag, /api/logs
 *  - BLE/WiFi scan, Upload, OTA
 *  - API Key 인증, CORS, No-cache
 * ------------------------------------------------------
 * [구현 규칙]
 *  - 항상 소스 시작 주석 체계 유지
 *  - ArduinoJson v7.x.x 사용 (v6 이하 사용 금지)
 *  - JsonDocument 단일 타입만 사용
 *  - createNestedArray/Object/containsKey 사용 금지
 *  - memset + strlcpy 기반 안전 초기화
 *  - 주석/필드명은 JSON 구조와 동일하게 유지
 *  - 모듈별 단일 헤더(h)파일로만 구성 (cpp 없음)
 *  - 소스 앞부분 구현규칙, 코드네이밍규칙 변경 금지
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
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <Update.h>

#include "A10_Const_012.h"
#include "C10_ConfigManager_012.h"
#include "D10_Logger_011.h"
#include "M10_WiFiManager_012.h"
#include "M10_MotionLogic_013.h"
#include "CT10_ControlManager_013.h"
#include "S10_Simulation_012.h"
#include "P10_PWM_ctrl_012.h"

// ------------------------------------------------------
// 정적 페이지 경로 (system config 기반으로 override)
// ------------------------------------------------------
namespace W10_Const {
constexpr char MAIN_HTML[] = "/html/SC10_main_021.html";
constexpr char MAIN_CSS[]  = "/html/SC10_main_021.css";
constexpr char MAIN_JS[]   = "/html/SC10_main_021.js";

constexpr char CHART_HTML[] = "/html/SC10_chart_003.html";
constexpr char CHART_CSS[]  = "/html/SC10_chart_003.css";
constexpr char CHART_JS[]   = "/html/SC10_chart_003.js";
}

// ------------------------------------------------------
// WebAPI Manager
// ------------------------------------------------------
class CL_W10_WebAPI {
public:
	static void W10_init(
		AsyncWebServer &p_srv,
		CL_S10_Simulation &p_sim,
		CL_CT10_ControlManager &p_ct,
		CL_M10_MotionLogic &p_motion,
		WiFiMulti &p_multi,
		CL_P10_PWM &p_pwm
	){
		s_srv     = &p_srv;
		s_sim     = &p_sim;
		s_ct      = &p_ct;
		s_motion  = &p_motion;
		s_multi   = &p_multi;
		s_pwm     = &p_pwm;

		_buildRoutes();
		_mountStatic();
		_mountAPI();
	}

private:
	// =====================================================
	// Static routing table
	// =====================================================
	struct ST_Route { const char* uri; const char* file; const char* mime; };
	static inline ST_Route s_routes[16];
	static inline uint8_t s_routeCnt=0;

	static void _push(const char* p_uri,const char*p_file,const char*p_mime){
		if(s_routeCnt<16) s_routes[s_routeCnt++] = {p_uri,p_file,p_mime};
	}

	// =====================================================
	// Build routes from config
	// =====================================================
	static void _buildRoutes(){
		auto &web = g_A10_config_root.core.system.web;

		auto add = [&](const char* p_cfg,const char* p_def,const char* p_mime){
			if(strlen(p_cfg)>0) _push(p_cfg,p_cfg,p_mime);
			_push(p_def,p_def,p_mime);
		};

		add(web.html, W10_Const::MAIN_HTML, "text/html");
		add(web.css,  W10_Const::MAIN_CSS,  "text/css");
		add(web.js,   W10_Const::MAIN_JS,   "application/javascript");

		_push("/chart",W10_Const::CHART_HTML,"text/html");
		_push(W10_Const::CHART_HTML,W10_Const::CHART_HTML,"text/html");
		_push(W10_Const::CHART_CSS,W10_Const::CHART_CSS,"text/css");
		_push(W10_Const::CHART_JS, W10_Const::CHART_JS,"application/javascript");
	}

	// =====================================================
	// Mount Static pages
	// =====================================================
	static void _mountStatic(){
		s_srv->on("/",HTTP_GET,[](AsyncWebServerRequest *r){
			const char*f=g_A10_config_root.core.system.web.html;
			if(strlen(f)&&LittleFS.exists(f)) r->redirect(f);
			else r->redirect(W10_Const::MAIN_HTML);
		});

		for(uint8_t i=0;i<s_routeCnt;i++){
			s_srv->on(s_routes[i].uri,HTTP_GET,[](AsyncWebServerRequest *r){
				const char*u=r->url().c_str();
				for(uint8_t k=0;k<s_routeCnt;k++){
					if(strcmp(u,s_routes[k].uri)==0){
						if(LittleFS.exists(s_routes[k].file)){
							r->send(LittleFS,s_routes[k].file,s_routes[k].mime);
							return;
						}
						String msg="/* missing:"+String(s_routes[k].file)+" */";
						auto *res=r->beginResponse(200,s_routes[k].mime,msg);
						_applyHeaders(res,true);r->send(res);
						return;
					}
				}
				r->send(404,"text/plain","not found");
			});
		}
	}

	// =====================================================
	// Mount REST API
	// =====================================================
	static void _mountAPI(){

		// ---- system ----
		_route("/api/version",HTTP_GET,_api_version);
		_route("/api/diag",HTTP_GET,_api_diag);
		_route("/api/logs",HTTP_GET,_api_logs);
		_route("/api/reset",HTTP_POST,_api_reset);
		_route("/api/scan",HTTP_GET,_api_scan);

		// ---- config ----
		_route("/api/config",HTTP_GET,_api_cfg_get);
		_route_post("/api/config",_api_cfg_post,_api_cfg_body);
		_route("/api/config/init",HTTP_POST,_api_cfg_init);

		// ---- motion ----
		_route("/api/motion",HTTP_GET,_api_motion);
		_route("/api/motion/feed",HTTP_POST,_api_motion_feed);

		// ---- control/sim/override ----
		_route("/api/control",HTTP_GET,_api_control_get);
		_route_post("/api/control",_api_control_post,_api_control_body);
		_route("/api/sim/stop",HTTP_POST,_api_sim_stop);
		_route_post("/api/sim/preset",_api_sim_preset,_api_sim_preset_body);

		// ---- chart ----
		_route("/api/chart",HTTP_GET,_api_chart);

		// ---- upload / OTA ----
		_route_post("/upload",_api_upload_end,_api_upload_data);
		_route_post("/update",_api_update_end,_api_update_data);
	}

	// =====================================================
	// Web route helpers
	// =====================================================
	static inline AsyncWebServer *s_srv=nullptr;
	static inline CL_S10_Simulation *s_sim=nullptr;
	static inline CL_CT10_ControlManager *s_ct=nullptr;
	static inline CL_M10_MotionLogic *s_motion=nullptr;
	static inline WiFiMulti *s_multi=nullptr;
	static inline CL_P10_PWM *s_pwm=nullptr;
	static inline File s_upFile;
	static inline String s_body;

	static void _route(const char* u,WebRequestMethod m,AwsRequestHandlerFunction f){
		s_srv->on(u,m,[=](AsyncWebServerRequest* r){ if(!_auth(r)){r->send(401);return;} f(r);} );
	}
	static void _route_post(const char* u,AwsRequestHandlerFunction f,AwsBodyHandlerFunction b){
		s_srv->on(u,HTTP_POST,[=](AsyncWebServerRequest* r){ if(!_auth(r)){r->send(401);return;} f(r);} ,nullptr,
		[=](AsyncWebServerRequest*r,uint8_t*d,size_t l,size_t i,size_t t){
			if(!_auth(r)){r->send(401);return;}
			if(i==0)s_body.clear();
			s_body.concat((char*)d,l);
			if(i+l<t)return;
			b(r,nullptr,0,0,0);
		});
	}

	// =====================================================
	// API handlers
	// =====================================================

	// ---- /api/version ----
	static void _api_version(AsyncWebServerRequest*r){
		JsonDocument d;
		d["fw"]=A10_Const::FW_VERSION;
		d["cfg_ver"]=G_A10_CFG_JSON_FILE_VER;
		String o;serializeJson(d,o);
		_sendJson(r,o);
	}

	// ---- /api/diag ----
	static void _api_diag(AsyncWebServerRequest*r){
		JsonDocument d;
		d["heap"]=ESP.getFreeHeap();
		d["fs_used"]=LittleFS.usedBytes();
		d["fs_total"]=LittleFS.totalBytes();
		String o;serializeJson(d,o);
		_sendJson(r,o);
	}

	// ---- /api/logs ----
	static void _api_logs(AsyncWebServerRequest*r){
		_sendJson(r,CL_D10_Logger::getLogsJson());
	}

	// ---- /api/reset ----
	static void _api_reset(AsyncWebServerRequest*r){
		CL_C10_ConfigManager::resetAll(g_A10_config_root);
		_sendText(r,"{\"restored\":true}");
		delay(200);ESP.restart();
	}

	// ---- /api/scan ----
	static void _api_scan(AsyncWebServerRequest*r){
		_sendJson(r, CL_M10_WiFiManager::M10_scanNetworksJson(false));
	}

	// ---- /api/config GET ----
	static void _api_cfg_get(AsyncWebServerRequest*r){
		JsonDocument d;
		CL_C10_ConfigManager::toJson_All(g_A10_config_root,d);
		String o;serializeJson(d,o);
		_sendJson(r,o);
	}

	// ---- /api/config POST ----
	static void _api_cfg_post(AsyncWebServerRequest*r){
		_sendText(r,"{\"msg\":\"ok\"}");
	}

	static void _api_cfg_body(AsyncWebServerRequest*r, uint8_t*, size_t, size_t, size_t){
		JsonDocument v;
		if(deserializeJson(v,s_body)){
			_sendText(r,"{\"err\":\"json\"}");return;
		}
		bool wifiChanged=false;
		bool changed=CL_C10_ConfigManager::patchFromJson(g_A10_config_root,v,wifiChanged);
		if(changed) CL_C10_ConfigManager::saveAll(g_A10_config_root);
		if(wifiChanged && g_A10_config_root.wifi){
			CL_M10_WiFiManager::init(*g_A10_config_root.wifi,*s_multi);
		}
		if(g_A10_config_root.control && s_sim){
			s_sim->S10_applyPreset(g_A10_config_root.control->sim.preset);
		}
		_sendText(r,"{\"updated\":"+String(changed?"true":"false")+"}");
	}

	// ---- /api/config/init ----
	static void _api_cfg_init(AsyncWebServerRequest*r){
		A10_resetToDefault(g_A10_config_root);
		CL_C10_ConfigManager::saveAll(g_A10_config_root);
		_sendText(r,"{\"factory\":true}");
	}

	// ---- /api/motion ----
	static void _api_motion(AsyncWebServerRequest*r){
		JsonDocument d;
		JsonObject m=d["motion"].to<JsonObject>();
		m["active"]=s_motion->M10_isMotionPresent();
		String o;serializeJson(d,o);_sendJson(r,o);
	}

	// ---- /api/motion/feed ----
	static void _api_motion_feed(AsyncWebServerRequest*r){
		JsonDocument v;deserializeJson(v,s_body);
		s_motion->feedPIR(v["pir"]|false);
		s_motion->feedBLE(v["ble"]|false);
		_sendText(r,"{\"fed\":true}");
	}

	// ---- /api/control GET ----
	static void _api_control_get(AsyncWebServerRequest*r){
		JsonDocument d;
		s_ct->toJson(d);
		String o;serializeJson(d,o);_sendJson(r,o);
	}

	// ---- /api/control POST ----
	static void _api_control_post(AsyncWebServerRequest*r){
		_sendText(r,"{\"msg\":\"ok\"}");
	}

	static void _api_control_body(AsyncWebServerRequest*r,uint8_t*,size_t,size_t,size_t){
		JsonDocument v;deserializeJson(v,s_body);
		s_ct->fromJson(v);
		s_ct->apply();
		_sendText(r,"{\"control_updated\":true}");
	}

	// ---- /api/sim/stop ----
	static void _api_sim_stop(AsyncWebServerRequest*r){
		s_sim->stop();
		s_ct->setFanEnable(false);
		_sendText(r,"{\"sim\":\"stopped\"}");
	}

	// ---- /api/sim/preset ----
	static void _api_sim_preset(AsyncWebServerRequest*r){}

	static void _api_sim_preset_body(AsyncWebServerRequest*r,uint8_t*,size_t,size_t,size_t){
		JsonDocument v;deserializeJson(v,s_body);
		const char* p=v["preset"]|"OCEAN";
		s_sim->S10_applyPreset(p);
		_sendText(r,String("{\"preset\":\"")+p+"\"}");
	}

	// ---- /api/chart ----
	static void _api_chart(AsyncWebServerRequest*r){
		JsonDocument d;s_sim->S10_toChartJson(d);
		String o;serializeJson(d,o);_sendJson(r,o);
	}

	// ---- Upload ----
	static void _api_upload_end(AsyncWebServerRequest*r){_sendText(r,"{\"done\":true}");}
	static void _api_upload_data(AsyncWebServerRequest*r,const String&fn,size_t idx,uint8_t*data,size_t len,bool fin){
		if(idx==0){ if(LittleFS.exists(fn))LittleFS.remove(fn); s_upFile=LittleFS.open(fn,"w"); }
		if(s_upFile) s_upFile.write(data,len);
		if(fin){ if(s_upFile) s_upFile.close(); }
	}

	// ---- OTA ----
	static void _api_update_end(AsyncWebServerRequest*r){
		_sendText(r,"{\"ota\":\"ok\"}");
		delay(300);ESP.restart();
	}
	static void _api_update_data(AsyncWebServerRequest*,const String&,size_t idx,uint8_t*d,size_t len,bool fin){
		if(idx==0)Update.begin(UPDATE_SIZE_UNKNOWN);
		Update.write(d,len);
		if(fin)Update.end(true);
	}

	// =====================================================
	// Utility
	// =====================================================
	static void _applyHeaders(AsyncWebServerResponse*r,bool nocache){
		if(nocache){
			r->addHeader("Cache-Control","no-store");
			r->addHeader("Pragma","no-cache");
		}
		r->addHeader("Access-Control-Allow-Origin","*");
		r->addHeader("Access-Control-Allow-Headers","Content-Type, X-API-Key");
		r->addHeader("Access-Control-Allow-Methods","GET, POST, OPTIONS");
	}

	static bool _auth(AsyncWebServerRequest*r){
		if(strlen(g_A10_config_root.core.security.api_key)==0) return true;
		if(!r->hasHeader("X-API-Key")) return false;
		return r->getHeader("X-API-Key")->value() == g_A10_config_root.core.security.api_key;
	}

	static void _sendJson(AsyncWebServerRequest*r,const String&s){
		auto*resp=r->beginResponse(200,"application/json",s);
		_applyHeaders(resp,true);r->send(resp);
	}
	static void _sendText(AsyncWebServerRequest*r,const String&s){
		auto*resp=r->beginResponse(200,"application/json",s);
		_applyHeaders(resp,true);r->send(resp);
	}
};

