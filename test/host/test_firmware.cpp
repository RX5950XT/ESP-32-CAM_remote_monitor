// Compile the actual firmware sources against deterministic hardware/socket doubles.
#include <cassert>
#include <iostream>
#include <map>
#include "esp_camera.h"
#include "../../src/scheduler.h"
#undef BOT_TOKEN
#undef CHAT_ID
#undef MCP_AUTH_TOKEN
#undef DEVICE_NAME
#define BOT_TOKEN "host-test-bot-token"
#define CHAT_ID "host-test-chat"
#define MCP_AUTH_TOKEN "host-test-token-not-a-secret-123456"
#define DEVICE_NAME ""
#include "../../src/device_identity.cpp"
#include "../../src/camera.cpp"
#include "../../src/device_control.cpp"
#include "../../src/telegram.cpp"
#include "../../src/mcp_server.cpp"
#include "../../src/scheduler.cpp"

static PersonDetectionResult nextDetection;
static float calledMinimum=0,calledMargin=0;
static int inferenceCalls=0;
PersonDetectionResult runPersonDetection(float minimum,float margin) {
    ++inferenceCalls; calledMinimum=minimum; calledMargin=margin;
    delay(nextDetection.durationMs); return nextDetection;
}

static sensor_t sensor;
static camera_config_t config;
static int initCount=0, resizeCount=0, getCount=0, returnCount=0;
static int failedGetCount=0;
static bool failRegister=false, failCapture=false, badFormat=false, failResize=false;
static std::map<std::string,int> registers;
static uint8_t pixels[96*96];
static camera_fb_t frame;
sensor_t* esp_camera_sensor_get() { return &sensor; }
esp_err_t esp_camera_init(const camera_config_t* c) {
    config=*c; ++initCount; sensor.status.framesize=c->frame_size; return ESP_OK;
}
esp_err_t esp_camera_deinit() { return ESP_OK; }
camera_fb_t* esp_camera_fb_get() {
    ++getCount;
    if(failCapture) { ++failedGetCount; return nullptr; }
    const bool gray=config.pixel_format==PIXFORMAT_GRAYSCALE;
    size_t width=96,height=96;
    if(!gray) {
        if(sensor.status.framesize==FRAMESIZE_UXGA) { width=1600; height=1200; }
        else if(sensor.status.framesize==FRAMESIZE_XGA) { width=1024; height=768; }
        else if(sensor.status.framesize==FRAMESIZE_VGA) { width=640; height=480; }
        else if(sensor.status.framesize==FRAMESIZE_CIF) { width=400; height=296; }
        else { width=800; height=600; }
    }
    frame={pixels,gray?sizeof(pixels):size_t(1541),width,height,
           badFormat?PIXFORMAT_GRAYSCALE:config.pixel_format};
    return &frame;
}
void esp_camera_fb_return(camera_fb_t*) { ++returnCount; }

#define REGISTER(field) sensor.set_##field=[](sensor_t*,int value) { registers[#field]=value; return failRegister?-1:0; }
static void setupSensor() {
    sensor.set_framesize=[](sensor_t* s,framesize_t size) { ++resizeCount; if(failResize)return -1; s->status.framesize=size; return 0; };
    REGISTER(brightness); REGISTER(contrast); REGISTER(saturation); REGISTER(ae_level);
    REGISTER(quality); REGISTER(aec2); REGISTER(exposure_ctrl); REGISTER(gain_ctrl);
    REGISTER(whitebal); REGISTER(awb_gain); REGISTER(wb_mode); REGISTER(bpc);
    REGISTER(wpc); REGISTER(raw_gma); REGISTER(lenc);
    sensor.set_gainceiling=[](sensor_t*,gainceiling_t value) { registers["gainceiling"]=value; return failRegister?-1:0; };
    for(size_t i=0;i<sizeof(pixels);++i) pixels[i]=uint8_t(i);
}

static void testCamera() {
    setupSensor();
    assert(cameraInit());
    assert(config.frame_size==FRAMESIZE_UXGA);
    assert(registers["brightness"]==0 && registers["ae_level"]==1);
    assert(registers["gainceiling"]==GAINCEILING_16X && registers["quality"]==8);
    for(const char* key:{"exposure_ctrl","gain_ctrl","whitebal","awb_gain","lenc"}) assert(registers[key]==1);
    const int initialInit=initCount, initialResize=resizeCount;
    auto fb=capturePhoto(FRAMESIZE_SVGA); assert(fb); esp_camera_fb_return(fb);
    assert(initCount==initialInit && resizeCount==initialResize);
    assert(getCount==returnCount);
    jpegLightSequence={{40,40,40},{70,70,70},{100,100,100},{140,140,140},{140,140,140},{140,140,140},{140,140,140}};
    jpegSampleIndex=0;
    fb=capturePhoto(FRAMESIZE_SVGA); assert(fb); esp_camera_fb_return(fb);
    assert(lastJpegLight[0]==140); // A light change must settle even without a mode/settings change.
    jpegLightSequence.clear();
    CameraSettings tuned=cameraSettings(); tuned.exposureLevel=-1;
    assert(cameraSetSettings(tuned));
    const unsigned long changedAt=millis();
    fb=capturePhoto(FRAMESIZE_SVGA); assert(fb); esp_camera_fb_return(fb);
    assert(millis()-changedAt>=2000); // New settings need a full sensor adaptation period.
    const unsigned long steadyAt=millis();
    fb=capturePhoto(FRAMESIZE_SVGA); assert(fb); esp_camera_fb_return(fb);
    assert(millis()-steadyAt<2000); // Steady captures must keep the short path.
    jpegLightSequence={{20,30,20},{40,50,30},{80,80,60},{120,120,100},{140,140,120},{143,143,123},{143,143,123},{143,143,123},{143,143,123}};
    jpegSampleIndex=0;
    assert(cameraSetSettings(tuned));
    fb=capturePhoto(FRAMESIZE_SVGA); assert(fb); esp_camera_fb_return(fb);
    assert(lastJpegLight[0]>=140 && lastJpegLight[2]>=120);
    jpegLightSequence.clear(); alternatingJpegLight=true;
    assert(cameraSetSettings(tuned));
    const unsigned long unstableAt=millis();
    fb=capturePhoto(FRAMESIZE_SVGA); assert(fb); esp_camera_fb_return(fb);
    assert(millis()-unstableAt>=10000 && millis()-unstableAt<12000);
    alternatingJpegLight=false; failJpegDecode=true;
    assert(cameraSetSettings(tuned)); assert(!capturePhoto(FRAMESIZE_SVGA));
    failJpegDecode=false;
    uint8_t gray[96*96];
    assert(captureGray96(gray,sizeof(gray)));
    const int grayInit=initCount;
    assert(captureGray96(gray,sizeof(gray)) && initCount==grayInit);
    assert(registers["ae_level"]==-1 && gray[177]==uint8_t(177));
    fb=capturePhoto(FRAMESIZE_UXGA); assert(fb && fb->width==1600); esp_camera_fb_return(fb);
    assert(registers["ae_level"]==-1 && initCount==grayInit+1);
    tuned.jpegQuality=7; assert(!cameraSetSettings(tuned));
    tuned=cameraSettings(); tuned.gainCeiling=5; assert(!cameraSetSettings(tuned));
    tuned=cameraSettings(); tuned.brightness=1;
    failRegister=true; assert(!cameraSetSettings(tuned)); failRegister=false;
    assert(cameraSettings().brightness==0);
    fb=capturePhoto(FRAMESIZE_SVGA); assert(fb && registers["brightness"]==0); esp_camera_fb_return(fb);
    badFormat=true; assert(!capturePhoto(FRAMESIZE_SVGA)); badFormat=false;
    failResize=true; assert(!capturePhoto(FRAMESIZE_UXGA)); failResize=false;
    failCapture=true;
    assert(!capturePhoto(FRAMESIZE_SVGA));
    assert(!captureGray96(gray,sizeof(gray)));
    failCapture=false;
    assert(getCount==returnCount+failedGetCount); // Every successfully acquired frame was returned.
    assert(!captureGray96(gray,12));
    assert(!capturePhoto(FRAMESIZE_96X96));
    esp_camera_deinit(); g_cameraReady=false; hasPsram=false;
    fb=capturePhoto(FRAMESIZE_UXGA); assert(fb && sensor.status.framesize==FRAMESIZE_CIF); esp_camera_fb_return(fb);
    hasPsram=true; g_cameraReady=false; assert(cameraInit());
    std::cout<<"camera: auto controls, lazy modes, fresh frames, rollback and PSRAM fallback passed\n";
}

static SchedulerState state;
static JsonDocument snapshot() {
    JsonDocument result; deviceStatus(result.to<JsonObject>(),state); return result;
}
static bool configure(const char* group,const std::string& json,String& error) {
    JsonDocument args; assert(!deserializeJson(args,json));
    return deviceConfigure(group,args.as<JsonObjectConst>(),state,error);
}
static std::string jsonText(const JsonDocument& value) {
    std::string text; serializeJson(value,text); return text;
}
static void goodDetection() {
    nextDetection={true,true,0.85f,0.15f,5600,"","person=0.85 no_person=0.15"};
}
static void testSharedControl() {
    preferencesData.clear();
    assert(deviceControlInit(state));
    String error;
    auto defaults=snapshot();
    assert(defaults["free_heap_bytes"]==123456 && defaults["free_psram_bytes"]==2345678);
    preferencesFailSave=true;
    assert(!configure("monitor","{\"monitor_enabled\":false}",error));
    assert(preferencesData.empty() && snapshot()["settings_persisted"]==false);
    preferencesFailSave=false;
    assert(configure("monitor","{\"monitor_enabled\":false}",error));
    assert(snapshot()["settings_persisted"]==true && preferencesWrites>0);
    assert(configure("camera","{\"brightness\":2,\"contrast\":-1,\"exposure_level\":1,\"jpeg_quality\":12}",error));
    assert(configure("monitor","{\"monitor_enabled\":true,\"ai_enabled\":true,\"monitor_interval_sec\":31,\"ai_interval_sec\":23}",error));
    assert(configure("detection","{\"person_min_score\":0.8,\"person_score_margin\":0.3,\"alert_cooldown_sec\":60}",error));
    const auto saved=preferencesData;
    assert(saved.size()==1 && saved.begin()->second.size()<=1024);
    const size_t savedWrites=preferencesWrites;
    assert(configure("detection","{\"person_min_score\":0.8}",error));
    assert(preferencesWrites==savedWrites); // An unchanged command must not wear NVS.
    state=SchedulerState{};
    CameraSettings neutral; assert(cameraSetSettings(neutral));
    assert(deviceControlInit(state));
    assert(state.monitorEnabled && state.aiEnabled && state.monitorIntervalSec==31 && state.aiIntervalSec==23);
    assert(cameraSettings().brightness==2 && cameraSettings().contrast==-1 && cameraSettings().jpegQuality==12);
    auto restored=snapshot();
    assert(restored["person_min_score"].as<float>()==0.8f && restored["person_score_margin"].as<float>()==0.3f);
    assert(restored["alert_cooldown_sec"]==60 && restored["settings_persisted"]==true);
    const std::string before=jsonText(restored);
    for(const auto& invalid:std::vector<std::pair<const char*,std::string>>{
        {"camera","{\"brightness\":1,\"jpeg_quality\":7}"},{"camera","{\"gain_ceiling\":true}"},
        {"monitor","{\"monitor_enabled\":false,\"ai_interval_sec\":2}"},{"monitor","{\"ai_enabled\":1}"},
        {"detection","{\"person_min_score\":true}"},{"detection","{\"person_min_score\":1.1}"},
        {"detection","{\"person_score_margin\":-0.1}"},{"detection","{\"alert_cooldown_sec\":1.5}"},
        {"detection","{\"alert_cooldown_sec\":86401}"},{"detection","{}"},{"detection","{\"typo\":0}"}}) {
        assert(!configure(invalid.first,invalid.second,error));
        assert(preferencesData==saved && state.monitorEnabled && cameraSettings().brightness==2);
    }
    assert(jsonText(snapshot())==before);
    JsonDocument nonfinite;
    nonfinite["person_min_score"]=std::numeric_limits<float>::infinity();
    assert(!deviceConfigure("detection",nonfinite.as<JsonObjectConst>(),state,error));
    nonfinite["person_min_score"]=std::numeric_limits<float>::quiet_NaN();
    assert(!deviceConfigure("detection",nonfinite.as<JsonObjectConst>(),state,error));
    preferencesFailSave=true;
    assert(!configure("monitor","{\"monitor_enabled\":false,\"ai_interval_sec\":44}",error));
    assert(state.monitorEnabled && state.aiIntervalSec==23 && preferencesData==saved);
    assert(!configure("camera","{\"brightness\":-2}",error));
    assert(cameraSettings().brightness==2 && preferencesData==saved);
    assert(!configure("detection","{\"person_min_score\":0.1}",error));
    assert(snapshot()["person_min_score"].as<float>()==0.8f);
    preferencesFailSave=false;
    failRegister=true;
    assert(!configure("camera","{\"brightness\":-2}",error));
    failRegister=false;
    assert(cameraSettings().brightness==2 && preferencesData==saved);
    assert(deviceControlInit(state));
    assert(deviceActionReady(error)); deviceActionDone();
    assert(!deviceActionReady(error)); delay(2999); assert(!deviceActionReady(error));
    delay(1); assert(deviceActionReady(error));
    // Corrupt or unavailable storage must be visible, never silently applied.
    preferencesData.begin()->second={'{'};
    assert(!deviceControlInit(state));
    assert(snapshot()["settings_error"].as<std::string>().size()>0);
    preferencesData=saved;
    preferencesFailOpen=true; assert(!deviceControlInit(state));
    assert(!configure("monitor","{\"monitor_enabled\":true}",error));
    preferencesFailOpen=false; assert(deviceControlInit(state));
    goodDetection();
    assert(deviceDetect(error));
    assert(calledMinimum==0.8f && calledMargin==0.3f);
    JsonDocument detection; deviceDetectionStatus(detection.to<JsonObject>());
    assert(detection["available"]==true && detection["success"]==true);
    nextDetection.success=false; nextDetection.error="model invocation failed";
    assert(!deviceDetect(error));
    detection.clear(); deviceDetectionStatus(detection.to<JsonObject>());
    assert(detection["available"]==true && detection["success"]==false);
    assert(detection["person_detected"].isNull());
    assert(snapshot()["health"]["inference"]["success"]==false);
    const auto beforeFailedInference=telegramRequests.size();
    deviceRunScheduledAI(); assert(telegramRequests.size()==beforeFailedInference);
    goodDetection();
    auto fb=deviceCapture(FRAMESIZE_SVGA); assert(fb); esp_camera_fb_return(fb);
    assert(snapshot()["health"]["camera"]["success"]==true);
    failCapture=true; assert(!deviceCapture(FRAMESIZE_SVGA)); failCapture=false;
    assert(snapshot()["health"]["camera"]["success"]==false);
    telegramConnectSuccess=false;
    assert(!deviceSendPhoto(FRAMESIZE_SVGA,"test",error));
    telegramConnectSuccess=true;
    assert(snapshot()["health"]["telegram"]["success"]==false);
    telegramWriteLimit=73;
    assert(deviceSendPhoto(FRAMESIZE_UXGA,"host photo",error));
    assert(telegramRequests.back().find(std::string(reinterpret_cast<char*>(pixels),1541))!=std::string::npos);
    assert(snapshot()["health"]["telegram"]["success"]==true);
    assert(snapshot()["health"]["telegram"]["error"].isNull());
    assert(!snapshot()["health"]["telegram"]["last_error"].isNull());
    assert(!snapshot()["health"]["telegram"]["last_error_uptime_sec"].isNull());
    telegramWriteLimit=0;
    const auto writeStarted=millis();
    assert(!deviceSendPhoto(FRAMESIZE_SVGA,"cannot write",error));
    assert(millis()-writeStarted>=10000);
    telegramWriteLimit=73;
    const auto sends=telegramRequests.size();
    deviceRunScheduledAI();
    assert(telegramRequests.size()==sends+1);
    deviceRunScheduledAI(); assert(telegramRequests.size()==sends+1);
    delay(60000); deviceRunScheduledAI(); assert(telegramRequests.size()==sends+2);
    assert(configure("detection","{\"alert_cooldown_sec\":0}",error));
    telegramConnectSuccess=false;
    deviceRunScheduledAI();
    assert(snapshot()["health"]["alert"]["success"]==false);
    assert(snapshot()["alert_cooldown_remaining_sec"]==0 && snapshot()["alert_retry_remaining_sec"].as<int>()>0);
    const auto captureBeforeRetry=getCount;
    deviceRunScheduledAI(); assert(getCount==captureBeforeRetry);
    telegramConnectSuccess=true; delay(30000);
    deviceRunScheduledAI(); assert(snapshot()["health"]["alert"]["success"]==true);
    assert(configure("monitor","{\"monitor_enabled\":false,\"ai_enabled\":false}",error));
    std::cout<<"shared control: validated atomic settings, NVS reboot/failure, health, detection and runtime alert cooldown passed\n";
}

static std::string exchange(const std::string& body,const std::string& extra="",
                            const std::string& auth=MCP_AUTH_TOKEN,const std::string& verb="POST",
                            const std::string& host="192.168.2.50",uint32_t remote=0xc0a80202) {
    delay(100);
    auto socket=std::make_shared<SocketMock>();
    socket->remote=remote;
    socket->input=verb+" /mcp HTTP/1.1\r\nHost: "+host+"\r\nAuthorization: Bearer "+auth+
        "\r\nContent-Type: application/json\r\nContent-Length: "+std::to_string(body.size())+"\r\n"+extra+"\r\n"+body;
    pendingSocket=socket;
    assert(handleMcpClient(state));
    assert(!socket->open);
    return socket->output;
}
static JsonDocument payload(const std::string& response,int status=200) {
    assert(response.find("HTTP/1.1 "+std::to_string(status)+" ")==0);
    const size_t start=response.find("\r\n\r\n"); assert(start!=std::string::npos);
    const size_t lengthStart=response.find("Content-Length: ")+16;
    assert(std::stoul(response.substr(lengthStart))==response.size()-start-4);
    JsonDocument doc; assert(!deserializeJson(doc,response.substr(start+4))); return doc;
}
static std::string call(const std::string& name,const std::string& args="{}") {
    return "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":{\"name\":\""+name+"\",\"arguments\":"+args+"}}";
}
static void testIdentity() {
    const auto originalMac=ESP.efuseMac;
    assert(deviceId()=="246f28a1b2c3");
    assert(deviceHostname()=="esp32cam-246f28a1b2c3");
    const auto originalHostname=deviceHostname();
    assert(deviceHostname()==originalHostname && deviceDisplayName()==originalHostname);
    assert(deviceTag().find(originalHostname)!=std::string::npos);
    ESP.efuseMac=0x0000060504030201ULL;
    assert(deviceId()=="010203040506"); // Leading zeros and low-byte-first factory MAC order.
    assert(deviceHostname()=="esp32cam-010203040506" && deviceHostname()!=originalHostname);
    ESP.efuseMac=originalMac;
    assert(deviceHostname()==originalHostname);
    assert(deviceMcpUrl()=="http://esp32cam-246f28a1b2c3.local/mcp");
    JsonDocument identity; deviceIdentityStatus(identity.to<JsonObject>());
    assert(identity["device_id"]=="246f28a1b2c3");
    assert(identity["device_name"]==originalHostname);
    assert(identity["hostname"]==originalHostname+".local");
    assert(identity["ip_address"]=="192.168.2.50");
    assert(identity["mcp_enabled"]==false && identity["mcp_url"].isNull());
    assert(identity["mcp_ip_url"].isNull());
    assert(deviceConnectionMessage().find("http://")==std::string::npos);
    std::cout<<"identity: unique stable full factory MAC, byte order, leading zeros and disabled metadata passed\n";
}

static void testMcp() {
    MDNS.beginSuccess=false;
    mcpInit();
    assert(mcpEnabled() && !mcpMdnsReady());
    auto noMdns=snapshot();
    assert(noMdns["mcp_enabled"]==true && noMdns["mdns_ready"]==false);
    assert(noMdns["mcp_url"].isNull());
    assert(deviceConnectionMessage().find(deviceMcpUrl())==std::string::npos);
    assert(deviceConnectionMessage().find("http://192.168.2.50/mcp")!=std::string::npos);
    assert(noMdns["mcp_ip_url"]=="http://192.168.2.50/mcp");
    MDNS.beginSuccess=true;
    mcpInit();
    assert(mcpEnabled() && mcpMdnsReady());
    assert(MDNS.hostname==deviceHostname() && MDNS.instance==deviceHostname());
    assert(MDNS.services.size()==1 && MDNS.services[0].name=="http");
    assert(MDNS.services[0].protocol=="tcp" && MDNS.services[0].port==80);
    assert(MDNS.txt["path"]=="/mcp" && MDNS.txt["id"]==deviceId() && MDNS.txt["name"]==deviceDisplayName());
    assert(!handleMcpClient(state));
    const std::string status=call("camera_status");
    const std::string ownHost=deviceHostname()+".local";
    for(const std::string& host:{ownHost,ownHost+":80",std::string("192.168.2.50:80")}) {
        auto own=payload(exchange(status,"Origin: http://"+host+"\r\n",MCP_AUTH_TOKEN,"POST",host));
        assert(own["result"]["structuredContent"]["device_id"]==deviceId());
        assert(own["result"]["structuredContent"]["hostname"]==ownHost);
        assert(own["result"]["structuredContent"]["mcp_url"]==deviceMcpUrl());
    }
    payload(exchange(status,"",MCP_AUTH_TOKEN,"POST","esp32cam-010203040506.local"),403);
    payload(exchange(status,"Origin: http://esp32cam-010203040506.local\r\n"),403);
    payload(exchange(status,"",MCP_AUTH_TOKEN,"POST","esp32cam.local"),403);
    payload(exchange(status,"Origin: http://esp32cam.local\r\n"),403);
    payload(exchange(status,"","wrong"),401);
    payload(exchange(status,"Origin: https://evil.example\r\n"),403);
    payload(exchange(status,"",MCP_AUTH_TOKEN,"POST","evil.example"),403);
    payload(exchange(status,"",MCP_AUTH_TOKEN,"POST","192.168.2.50",0xc0a80301),403);
    payload(exchange(status,"MCP-Protocol-Version: invalid\r\n"),400);
    payload(exchange(status,"Transfer-Encoding: chunked\r\n"),400);
    payload(exchange(status,"",MCP_AUTH_TOKEN,"GET"),405);
    payload(exchange(std::string(2049,'x')),413);
    payload(exchange(status,"Content-Length: 10\r\n"),400);
    payload(exchange(status,"X-Padding: "+std::string(520,'x')+"\r\n"),431);
    assert(payload(exchange("[1,2]"))["error"]["code"]==-32600);
    assert(payload(exchange("{"))["error"]["code"]==-32700);
    assert(payload(exchange("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"unknown\"}"))["error"]["code"]==-32601);
    for(const char* version:{"2025-03-26","2025-06-18","2025-11-25"}) {
        const auto request=std::string("{\"jsonrpc\":\"2.0\",\"id\":0,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"")+version+"\"}}";
        auto initialized=payload(exchange(request));
        assert(initialized["result"]["protocolVersion"]==version);
        assert(initialized["result"]["serverInfo"]["name"]==deviceHostname());
        assert(initialized["result"]["serverInfo"]["version"]=="1.1.0");
    }
    const auto notification=exchange("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}");
    assert(notification.find("HTTP/1.1 202 ")==0 && notification.back()=='\n');
    assert(payload(exchange("{\"jsonrpc\":\"2.0\",\"id\":\"list\",\"method\":\"tools/list\"}"))["result"]["tools"].size()==8);
    assert(payload(exchange(status))["result"]["structuredContent"]["automatic_exposure"]==true);
    for(const std::string args:{"{\"jpeg_quality\":7}","{\"gain_ceiling\":5}","{\"brightness\":true}","{\"contrast\":1.5}","{\"typo\":0}","{}","[]","null"})
        assert(payload(exchange(call("camera_configure",args)))["error"]["code"]==-32602);
    for(const std::string args:{"{\"ai_interval_sec\":2}","{\"monitor_interval_sec\":86401}","{\"ai_enabled\":1}"})
        assert(payload(exchange(call("monitor_configure",args)))["error"]["code"]==-32602);
    for(const std::string args:{"{\"person_min_score\":true}","{\"person_min_score\":-0.1}","{\"person_score_margin\":1.01}","{\"alert_cooldown_sec\":86401}","{\"alert_cooldown_sec\":0.5}","{\"person_min_score\":\"0.8\"}","{}"})
        assert(payload(exchange(call("detection_configure",args)))["error"]["code"]==-32602);
    assert(payload(exchange(call("monitor_configure","{\"monitor_enabled\":true,\"ai_interval_sec\":86400}")))["result"]["structuredContent"]["monitor_enabled"]==true);
    assert(state.monitorEnabled && state.aiIntervalSec==86400);
    assert(payload(exchange(call("camera_capture")))["result"]["isError"]==true);
    delay(3000);
    assert(payload(exchange(call("camera_configure","{\"brightness\":1}")))["result"]["structuredContent"]["brightness"]==1);
    delay(3000);
    const int before=getCount-returnCount;
    auto image=payload(exchange(call("camera_capture","{\"resolution\":\"uxga\"}")));
    assert(image["result"]["content"][0]["type"]=="image");
    assert(image["result"]["structuredContent"]["device_id"]==deviceId());
    assert(image["result"]["structuredContent"]["device_name"]==deviceDisplayName());
    const std::string encoded=image["result"]["content"][0]["data"];
    assert(encoded.size()==((1541+2)/3)*4 && encoded.substr(0,8)=="AAECAwQF" && encoded.back()=='=');
    assert(getCount-returnCount==before);
    delay(3000); failCapture=true;
    assert(payload(exchange(call("camera_capture")))["result"]["isError"]==true); failCapture=false;
    delay(3000);
    assert(payload(exchange(call("detection_configure","{\"person_min_score\":0.75,\"person_score_margin\":0.25,\"alert_cooldown_sec\":7}")))["result"]["structuredContent"]["alert_cooldown_sec"]==7);
    delay(3000); goodDetection();
    auto detected=payload(exchange(call("detection_run")));
    assert(detected["result"]["structuredContent"]["success"]==true);
    assert(detected["result"]["structuredContent"]["device_id"]==deviceId());
    assert(detected["result"]["structuredContent"]["device_name"]==deviceDisplayName());
    const auto previousInferences=inferenceCalls;
    assert(payload(exchange(call("detection_status")))["result"]["structuredContent"]["success"]==true);
    assert(inferenceCalls==previousInferences); // Read-only calls neither run inference nor consume cooldown.
    assert(payload(exchange(call("camera_status")))["result"]["structuredContent"]["free_heap_bytes"]==123456);
    delay(3000); nextDetection.success=false; nextDetection.error="inference failed";
    assert(payload(exchange(call("detection_run")))["result"]["isError"]==true);
    auto failed=payload(exchange(call("detection_status")));
    assert(failed["result"]["structuredContent"]["success"]==false && failed["result"]["structuredContent"]["person_detected"].isNull());
    delay(3000); goodDetection();
    const auto beforeSend=telegramRequests.size();
    auto sent=payload(exchange(call("camera_send_telegram","{\"resolution\":\"vga\"}")));
    assert(sent["result"]["isError"]!=true && telegramRequests.size()==beforeSend+1);
    assert(sent["result"]["structuredContent"]["device_id"]==deviceId());
    assert(sent["result"]["structuredContent"]["device_name"]==deviceDisplayName());
    delay(3000); telegramConnectSuccess=false;
    assert(payload(exchange(call("camera_send_telegram")))["result"]["isError"]==true);
    telegramConnectSuccess=true;
    // Invalid camera settings must not have any side effects.
    assert(cameraSettings().brightness==1);
    std::cout<<"MCP: auth, subnet, Origin/Host, limits, lifecycle, schemas, atomic controls, rate limit and streamed images passed\n";
}

static String telegramCommand(const String& command,bool wait=true,const String& chat=CHAT_ID) {
    delay(wait?3000:1000);
    const size_t before=telegramReplies.size();
    telegramPending.push_back({chat,command}); handleTelegramMessages(state);
    String reply;
    for(size_t i=before;i<telegramReplies.size();++i) { reply+=telegramReplies[i]; reply+='\n'; }
    return reply;
}
static void testTelegram() {
    WiFiClientSecure client; telegramInit(client);
    String error; assert(deviceControlInit(state));
    assert(configure("monitor","{\"monitor_enabled\":false,\"ai_enabled\":false}",error));
    const auto writes=preferencesWrites;
    assert(telegramCommand("/monitor_on",true,"unauthorized").empty());
    assert(preferencesWrites==writes && !state.monitorEnabled);
    auto help=telegramCommand("/start");
    assert(help.startsWith(deviceTag().c_str()));
    for(const char* command:{"/photo","/camera","/monitor","/detect","/detect_status","/detect_config","/ai_on","/ai_off","/monitor_interval","/status"})
        assert(help.find(command)!=std::string::npos);
    telegramCommand("/monitor_on"); assert(state.monitorEnabled);
    telegramCommand("/monitor_off"); assert(!state.monitorEnabled);
    telegramCommand("/ai_on"); assert(state.aiEnabled);
    telegramCommand("/ai_off"); assert(!state.aiEnabled);
    telegramCommand("/monitor_interval 34"); assert(state.monitorIntervalSec==34);
    telegramCommand("/ai_interval 19"); assert(state.aiIntervalSec==19);
    const auto beforeInvalid=preferencesWrites;
    for(const char* command:{"/monitor_interval 2","/ai_interval 86401","/monitor_interval 10abc","/ai_interval 999999999999999999999999","/monitor_interval_bad 20","/detect_config {\"person_min_score\":true}","/camera {\"brightness\":1,\"jpeg_quality\":7}"})
        telegramCommand(command);
    assert(preferencesWrites==beforeInvalid && state.monitorIntervalSec==34 && state.aiIntervalSec==19);
    telegramCommand("/camera {\"brightness\":-1,\"saturation\":1,\"gain_ceiling\":4}");
    assert(cameraSettings().brightness==-1 && cameraSettings().saturation==1 && cameraSettings().gainCeiling==4);
    telegramCommand("/monitor {\"monitor_interval_sec\":36,\"ai_interval_sec\":21}");
    assert(state.monitorIntervalSec==36 && state.aiIntervalSec==21);
    telegramCommand("/detect_config {\"person_min_score\":0.9,\"person_score_margin\":0.4,\"alert_cooldown_sec\":25}");
    assert(snapshot()["alert_cooldown_sec"]==25 && std::abs(snapshot()["person_min_score"].as<float>()-0.9f)<0.00001f);
    assert(payload(exchange(call("camera_status")))["result"]["structuredContent"]["brightness"]==-1);
    delay(3000);
    assert(payload(exchange(call("camera_configure","{\"contrast\":2}")))["result"]["isError"]!=true);
    telegramCommand("/camera {\"contrast\":-2}",false); // MCP and Telegram share one action limit.
    assert(cameraSettings().contrast==2);
    auto status=telegramCommand("/status");
    assert(status.startsWith(deviceTag().c_str()));
    assert(status.find(deviceId())!=std::string::npos);
    assert(status.find(deviceMcpUrl())!=std::string::npos);
    assert(status.find("http://192.168.2.50/mcp")!=std::string::npos);
    for(const char* key:{"亮度","剩餘記憶體","最低分數","警報冷卻","執行紀錄","最近偵測"})
        assert(status.find(key)!=std::string::npos);
    goodDetection(); telegramCommand("/detect");
    assert(std::abs(calledMinimum-0.9f)<0.00001f && std::abs(calledMargin-0.4f)<0.00001f);
    const int runs=inferenceCalls; auto detected=telegramCommand("/detect_status",false);
    assert(detected.find("有人分數")!=std::string::npos && inferenceCalls==runs);
    nextDetection.success=false; nextDetection.error="inference failed";
    assert(telegramCommand("/detect").find("inference failed")!=std::string::npos);
    auto failed=telegramCommand("/detect_status");
    assert(failed.find("偵測失敗")!=std::string::npos && failed.find("有人分數")==std::string::npos);
    for(const char* resolution:{"vga","svga","xga","uxga"}) {
        const auto count=telegramRequests.size();
        telegramCommand(String("/photo ")+resolution);
        assert(telegramRequests.size()==count+1);
        assert(telegramRequests.back().find(deviceTag())!=std::string::npos);
    }
    const auto count=telegramRequests.size(); telegramCommand("/photo");
    assert(telegramRequests.size()==count+1);
    telegramCommand("/photo invalid"); assert(telegramRequests.size()==count+1);
    delay(3000);
    telegramPending.push_back({CHAT_ID,"/photo"}); telegramPending.push_back({CHAT_ID,"/photo"});
    handleTelegramMessages(state); assert(telegramRequests.size()==count+2);
    const auto beforeBusyReply=telegramReplies.size();
    handleTelegramMessages(state); assert(telegramRequests.size()==count+2);
    assert(telegramReplies.size()==beforeBusyReply+1 && telegramReplies.back().find("Busy")!=std::string::npos);
    delay(3000);
    telegramPending.push_back({CHAT_ID,"/status"}); telegramPending.push_back({CHAT_ID,"/detect_status"});
    const auto beforeBatch=telegramReplies.size();
    handleTelegramMessages(state); assert(telegramReplies.size()==beforeBatch+1);
    handleTelegramMessages(state); assert(telegramReplies.size()==beforeBatch+2);
    telegramConnectSuccess=false;
    assert(!telegramCommand("/photo").empty()); telegramConnectSuccess=true;
    assert(!snapshot()["health"]["telegram"]["last_error"].isNull());
    preferencesFailSave=true;
    telegramCommand("/monitor_on"); assert(!state.monitorEnabled);
    preferencesFailSave=false;
    state=SchedulerState{}; assert(deviceControlInit(state));
    assert(cameraSettings().brightness==-1 && cameraSettings().contrast==2);
    assert(state.monitorIntervalSec==36 && state.aiIntervalSec==21 && !state.monitorEnabled && !state.aiEnabled);
    assert(snapshot()["alert_cooldown_sec"]==25);
    for(const auto& reply:telegramReplies) assert(reply.startsWith(deviceTag().c_str()));
    std::cout<<"Telegram: authorization, all old/new commands, strict arguments, MCP parity, cross-interface cooldown and persisted reboot passed\n";
}

int main(int argc,char** argv) {
    setupSensor();
    if(argc>1 && std::string(argv[1])=="--bridge") {
        cameraInit(); assert(deviceControlInit(state)); goodDetection(); mcpInit();
        std::string line;
        while(std::getline(std::cin,line)) {
            delay(3000);
            JsonDocument input,output;
            if(deserializeJson(input,line)) return 1;
            auto socket=std::make_shared<SocketMock>();
            socket->input=input["request"].as<std::string>();
            pendingSocket=socket;
            assert(handleMcpClient(state));
            output["response"]=socket->output;
            std::string encoded; serializeJson(output,encoded);
            std::cout<<encoded<<std::endl;
        }
        return 0;
    }
    testIdentity(); testCamera(); testSharedControl(); testMcp(); testTelegram();
    std::cout<<"All firmware host checks passed\n";
}
