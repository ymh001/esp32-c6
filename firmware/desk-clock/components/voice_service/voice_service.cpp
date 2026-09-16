#include "voice_service.h"
#include "voice_text.h"
#include "network_gate.h"
#include "voice_credentials.h"
#include "esp_crt_bundle.h"
#include "board.h"
#include "wifi_manager.h"
#include "esp_websocket_client.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_attr.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include <atomic>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#if CONFIG_DESK_CLOCK_VOICE_SELF_TEST
extern const uint8_t diagnostic_pcm[] asm("_binary_voice_test_pcm_start");
extern const uint8_t diagnostic_pcm_end[] asm("_binary_voice_test_pcm_end");
static bool synthetic_test;
#if CONFIG_DESK_CLOCK_VOICE_LAMP_TEST
extern const uint8_t lamp_on_pcm[] asm("_binary_voice_lamp_on_start");
extern const uint8_t lamp_on_end[] asm("_binary_voice_lamp_on_end");
extern const uint8_t lamp_off_pcm[] asm("_binary_voice_lamp_off_start");
extern const uint8_t lamp_off_end[] asm("_binary_voice_lamp_off_end");
static unsigned diagnostic_turn;
#endif
#endif

static const char *TAG = "voice";
static TaskHandle_t worker;
static EventGroupHandle_t events;
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static voice_snapshot_t snapshot = {VOICE_IDLE, "点击说话，控制家中设备", 0};
static std::atomic<bool> stop_recording(false), busy(false);
static uint8_t handler_id;
static char message[8192];
static size_t message_length;
static char reply[384];
static char transcript[384];
static char failure_reason[128];
struct voice_checkpoint_t { uint32_t magic, stage, heap, largest, stack; };
static RTC_NOINIT_ATTR volatile voice_checkpoint_t last_checkpoint;
enum checkpoint_stage_t { TURN_START=1, WS_STOP, WS_DESTROYED, TTS_REQUEST,
    TTS_DOWNLOAD, AUDIO_PLAY, AUDIO_DONE, TURN_DONE };
static void checkpoint(checkpoint_stage_t stage)
{
    const uint32_t heap=esp_get_free_heap_size();
    const uint32_t largest=heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    const uint32_t stack=uxTaskGetStackHighWaterMark(NULL);
    last_checkpoint.magic=0x564f4943;last_checkpoint.stage=stage;
    last_checkpoint.heap=heap;last_checkpoint.largest=largest;last_checkpoint.stack=stack;
    ESP_LOGI(TAG,"Checkpoint=%u heap=%u largest=%u stack=%u",(unsigned)stage,
        (unsigned)heap,(unsigned)largest,(unsigned)stack);
}
static constexpr EventBits_t AUTH_REQUEST=1, AUTH_OK=2, STT_READY=4, INTENT_DONE=8, RUN_DONE=16, FAILED=32, STT_DONE=64;

static void copy_text(char *dst, size_t size, const char *src)
{
    if (!src) src = "";
    size_t n = strlen(src);
    if (n >= size) { n = size - 1; while (n && ((unsigned char)src[n] & 0xc0) == 0x80) --n; }
    memcpy(dst, src, n); dst[n] = 0;
}
static void update(voice_state_t state, const char *text)
{
    portENTER_CRITICAL(&mux);
    snapshot.state = state;
    copy_text(snapshot.text, sizeof(snapshot.text), text);
    ++snapshot.revision;
    portEXIT_CRITICAL(&mux);
}
void voice_service_snapshot(voice_snapshot_t *out)
{
    portENTER_CRITICAL(&mux); *out = snapshot; portEXIT_CRITICAL(&mux);
}
static const char *str(cJSON *o, const char *key)
{
    cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsString(v) ? v->valuestring : "";
}
static cJSON *obj(cJSON *o, const char *key) { return cJSON_GetObjectItemCaseSensitive(o, key); }
static void fail(const char *reason)
{
    ESP_LOGW(TAG,"Voice failure: %s",reason);
    portENTER_CRITICAL(&mux);
    if(!failure_reason[0]) copy_text(failure_reason,sizeof(failure_reason),reason);
    portEXIT_CRITICAL(&mux);
    // Keep the button disabled until the worker has released the socket/codecs.
    // Publishing ERROR here made an apparently enabled retry silently ignored.
    update(VOICE_THINKING,"正在结束本次请求…");
    xEventGroupSetBits(events, FAILED);
}
static void ws_event(void *, esp_event_base_t, int32_t id, void *raw)
{
    ESP_LOGD(TAG,"WebSocket event %ld",(long)id);
    if (id == WEBSOCKET_EVENT_DISCONNECTED || id == WEBSOCKET_EVENT_ERROR) {
        if (!(xEventGroupGetBits(events) & RUN_DONE)) fail("HA 连接断开，请重试");
        return;
    }
    if (id != WEBSOCKET_EVENT_DATA) return;
    const auto *d = (esp_websocket_event_data_t *)raw;
    ESP_LOGD(TAG,"WebSocket frame opcode=%u bytes=%d offset=%d total=%d fin=%d",d->op_code,d->data_len,d->payload_offset,d->payload_len,d->fin);
    if (d->op_code != 1 && d->op_code != 0) return;
    if (d->payload_offset == 0 && d->op_code == 1) message_length = 0;
    if (d->data_len < 0 || message_length + d->data_len >= sizeof(message)) {
        fail("HA 回复过长，请重试"); return;
    }
    memcpy(message + message_length, d->data_ptr, d->data_len); message_length += d->data_len;
    if (d->payload_offset + d->data_len < d->payload_len || !d->fin) return;
    message[message_length] = 0;
    cJSON *root = cJSON_Parse(message);
    if (!root) { fail("HA 响应格式错误"); return; }
    const char *type = str(root,"type");
    ESP_LOGD(TAG,"HA message type=%s",type);
    if (!strcmp(type,"auth_required")) xEventGroupSetBits(events,AUTH_REQUEST);
    else if (!strcmp(type,"auth_ok")) xEventGroupSetBits(events,AUTH_OK);
    else if (!strcmp(type,"auth_invalid")) fail("HA 授权失效");
    else if (!strcmp(type,"result") && cJSON_IsFalse(obj(root,"success"))) fail("HA 拒绝请求，请重试");
    else if (!strcmp(type,"event")) {
        cJSON *request_id=obj(root,"id");
        const bool recognition_run=cJSON_IsNumber(request_id) && request_id->valueint==1;
        cJSON *e=obj(root,"event"), *data=obj(e,"data");
        const char *event=str(e,"type");
        ESP_LOGI(TAG,"HA pipeline event=%s",event);
        if (!strcmp(event,"run-start") && recognition_run) {
            cJSON *n=obj(obj(data,"runner_data"),"stt_binary_handler_id");
            if (!cJSON_IsNumber(n) || n->valueint < 1 || n->valueint > 255) fail("HA 录音通道不可用");
            else handler_id=n->valueint;
        } else if (!strcmp(event,"stt-start")) xEventGroupSetBits(events,STT_READY);
        else if (!strcmp(event,"stt-vad-end")) { stop_recording=true; update(VOICE_THINKING,"正在识别…"); }
        else if (!strcmp(event,"stt-end")) {
            const char *raw_text=str(obj(data,"stt_output"),"text");
            if(!voice_text_clean(raw_text,transcript,sizeof(transcript)))fail("没有识别到有效指令，请再说一次");
            else {
                if(strcmp(raw_text,transcript))ESP_LOGI(TAG,"Cleaned trailing ASR replacement characters/whitespace");
                update(VOICE_THINKING,transcript);
            }
        }
        else if (!strcmp(event,"intent-end")) {
            cJSON *response=obj(obj(data,"intent_output"),"response");
            copy_text(reply,sizeof(reply),str(obj(obj(response,"speech"),"plain"),"speech"));
            if (!reply[0]) copy_text(reply,sizeof(reply),"没有收到回复，请重试");
            update(VOICE_THINKING,reply);
            xEventGroupSetBits(events,INTENT_DONE);
        } else if (!strcmp(event,"run-end")) xEventGroupSetBits(events,recognition_run ? STT_DONE : RUN_DONE);
        else if (!strcmp(event,"error")) {
            ESP_LOGW(TAG,"HA pipeline error: %s",str(data,"code"));
            fail(!strcmp(str(data,"code"),"stt-no-text-recognized") ? "没有听清，请再说一次" : "语音处理失败，请重试");
        }
    }
    cJSON_Delete(root);
}
static bool wait_for(EventBits_t bit, int ms)
{
    ESP_LOGI(TAG,"Waiting for stage bit=%u",(unsigned)bit);
    EventBits_t got=xEventGroupWaitBits(events,bit|FAILED,pdFALSE,pdFALSE,pdMS_TO_TICKS(ms));
    ESP_LOGI(TAG,"Stage bit=%u received bits=%u",(unsigned)bit,(unsigned)got);
    if (got & FAILED) return false;
    if (!(got & bit)) {
        const char *reason=bit==AUTH_REQUEST ? "HA 握手超时，请重试" :
            bit==AUTH_OK ? "HA 授权超时，请重试" :
            bit==STT_READY ? "语音识别启动超时，请重试" :
            bit==STT_DONE ? "语音识别超时，请重试" : "语音处理超时，请重试";
        fail(reason);return false;
    }
    return true;
}
static bool send_json(esp_websocket_client_handle_t ws,cJSON *json)
{
    char *text=cJSON_PrintUnformatted(json);cJSON_Delete(json);
    if (!text) return false;
    int len=strlen(text),sent=esp_websocket_client_send_text(ws,text,len,pdMS_TO_TICKS(3000));
    free(text);
    if(sent!=len){ESP_LOGW(TAG,"WebSocket send failed: sent=%d expected=%d",sent,len);fail("HA 请求发送失败，请重试");return false;}
    return true;
}
static bool http_read_exact(esp_http_client_handle_t h, void *dst, size_t size)
{
    size_t done=0;
    while (done<size) {
        int n=esp_http_client_read(h,(char *)dst+done,size-done);
        if(n<=0) return false;
        done+=n;
    }
    return true;
}
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
static uint16_t le16(const uint8_t *p) { return p[0] | p[1]<<8; }
static bool play_reply(void)
{
    NetworkLease network(pdMS_TO_TICKS(15000));
    if(!network)return false;
    update(VOICE_SPEAKING,reply);
    checkpoint(TTS_REQUEST);
    // Request PCM WAV explicitly; Assist's default TTS response is MP3.
    char url[512]=HA_HTTP_BASE_URL "/api/tts_get_url";
    esp_http_client_config_t cfg={};cfg.url=url;cfg.timeout_ms=15000;cfg.buffer_size=2048;
    cfg.disable_auto_redirect=true;cfg.crt_bundle_attach=esp_crt_bundle_attach;
    esp_http_client_handle_t h=esp_http_client_init(&cfg);
    if (!h) return false;
    esp_http_client_set_method(h,HTTP_METHOD_POST);
    esp_http_client_set_header(h,"Authorization","Bearer " HA_VOICE_TOKEN);
    esp_http_client_set_header(h,"Content-Type","application/json");
    cJSON *req=cJSON_CreateObject();cJSON_AddStringToObject(req,"engine_id","tts.piper");
    cJSON_AddStringToObject(req,"message",reply);cJSON_AddStringToObject(req,"language","zh_CN");
    cJSON *opt=cJSON_AddObjectToObject(req,"options");
    cJSON_AddStringToObject(opt,"voice","zh_CN-huayan-medium");cJSON_AddStringToObject(opt,"preferred_format","wav");
    cJSON_AddNumberToObject(opt,"preferred_sample_rate",16000);cJSON_AddNumberToObject(opt,"preferred_sample_channels",1);
    cJSON_AddNumberToObject(opt,"preferred_sample_bytes",2);
    char *body=cJSON_PrintUnformatted(req);cJSON_Delete(req);
    if (!body) {esp_http_client_cleanup(h);return false;}
    int length=strlen(body);
    bool ok=esp_http_client_open(h,length)==ESP_OK;
    if (ok) ok=esp_http_client_write(h,body,length)==length;
    free(body);
    char response[2048];int used=0;
    if(ok) {esp_http_client_fetch_headers(h);ok=esp_http_client_get_status_code(h)==200;}
    while(ok && used<(int)sizeof(response)-1) {
        int n=esp_http_client_read(h,response+used,sizeof(response)-1-used);
        if(n<0){ok=false;break;} if(!n)break;used+=n;
    }
    response[used]=0;esp_http_client_cleanup(h);
    if (!ok || used >= (int)sizeof(response)-1) return false;
    cJSON *r=cJSON_Parse(response);
    const char *path=str(r,"path");
    // Only fetch from the already-authorized HA host, never a returned external URL.
    ok=!strncmp(path,"/api/tts_proxy/",15) && strlen(path)<380;
    if(ok)snprintf(url,sizeof(url),HA_HTTP_BASE_URL "%s",path);
    cJSON_Delete(r);if(!ok)return false;
    checkpoint(TTS_DOWNLOAD);
    cfg.url=url;h=esp_http_client_init(&cfg);if(!h)return false;
    ok=esp_http_client_open(h,0)==ESP_OK;
    if(ok){esp_http_client_fetch_headers(h);ok=esp_http_client_get_status_code(h)==200;}
    uint8_t header[12];uint32_t rate=0,data_size=0;bool pcm=false;
    if(ok)ok=http_read_exact(h,header,12) && !memcmp(header,"RIFF",4) && !memcmp(header+8,"WAVE",4);
    for(int chunks=0;ok && chunks<16;++chunks) {
        uint8_t ch[8];if(!http_read_exact(h,ch,8)){ok=false;break;}
        uint32_t n=le32(ch+4);
        if(!memcmp(ch,"data",4)){data_size=n;break;}
        if(n>4096){ok=false;break;}
        uint8_t buf[64];uint32_t remaining=n+(n&1);
        if(!memcmp(ch,"fmt ",4)) {
            if(n<16 || !http_read_exact(h,buf,16)){ok=false;break;}
            pcm=le16(buf)==1 && le16(buf+2)==1 && le16(buf+14)==16;
            rate=le32(buf+4);remaining-=16;
        }
        while(remaining && ok){size_t step=remaining>sizeof(buf)?sizeof(buf):remaining;ok=http_read_exact(h,buf,step);remaining-=step;}
    }
    const bool streaming = data_size == UINT32_MAX;
    ok=ok && pcm && rate==16000 && data_size>0 && (streaming || data_size<=16000*2*30);
    if(ok)ok=board_audio_play_start(rate)==ESP_OK;
    if(ok){checkpoint(AUDIO_PLAY);update(VOICE_SPEAKING,reply);}
    alignas(int16_t) uint8_t audio[1026];
    size_t carried=0, total=0;
    while(ok && (streaming || data_size)) {
        size_t ask=streaming || data_size>1024 ? 1024 : data_size;
        int n=esp_http_client_read(h,(char *)audio+carried,ask);
        if(n<0){ok=false;break;}
        if(!n){ok=streaming && esp_http_client_is_complete_data_received(h);break;}
        total+=n;
        if(total>16000*2*30){ok=false;break;}
        size_t bytes=carried+n, aligned=bytes&~size_t(1);
        if(aligned)ok=board_audio_write((const int16_t *)audio,aligned/2)==ESP_OK;
        carried=bytes-aligned;if(carried)audio[0]=audio[aligned];
        if(!streaming)data_size-=n;
    }
    ok=ok && total>0 && !carried;
    board_audio_stop();esp_http_client_cleanup(h);checkpoint(AUDIO_DONE);return ok;
}
static void run_voice(void)
{
    if(!wifi_manager_is_connected()){fail("WiFi 未连接，请稍后重试");return;}
    NetworkLease network(pdMS_TO_TICKS(15000));
    if(!network){fail("网络忙，请稍后再试");return;}
    checkpoint(TURN_START);
    esp_websocket_client_config_t cfg={};cfg.uri=HA_WEBSOCKET_URL;
    cfg.crt_bundle_attach=esp_crt_bundle_attach;cfg.disable_auto_reconnect=true;cfg.network_timeout_ms=5000;cfg.task_stack=6144;cfg.buffer_size=2048;
    esp_websocket_client_handle_t ws=esp_websocket_client_init(&cfg);
    if(!ws){fail("内存不足，请重试");return;}
    ESP_LOGI(TAG,"Connecting to %s; heap=%u",cfg.uri,(unsigned)esp_get_free_heap_size());
    esp_err_t registration=esp_websocket_register_events(ws,WEBSOCKET_EVENT_ANY,ws_event,NULL);
    if(registration!=ESP_OK){esp_websocket_client_destroy(ws);fail("HA 连接初始化失败");return;}
    if(esp_websocket_client_start(ws)!=ESP_OK){esp_websocket_client_destroy(ws);fail("HA 连接启动失败");return;}
    bool ok=wait_for(AUTH_REQUEST,10000);
    if(ok){update(VOICE_CONNECTING,"正在验证 HA 授权…");cJSON *auth=cJSON_CreateObject();cJSON_AddStringToObject(auth,"type","auth");cJSON_AddStringToObject(auth,"access_token",HA_VOICE_TOKEN);ok=send_json(ws,auth) && wait_for(AUTH_OK,5000);}
    if(ok){update(VOICE_CONNECTING,"正在启动语音识别…");cJSON *req=cJSON_CreateObject();cJSON_AddNumberToObject(req,"id",1);cJSON_AddStringToObject(req,"type","assist_pipeline/run");cJSON_AddStringToObject(req,"pipeline",HA_VOICE_PIPELINE);cJSON_AddStringToObject(req,"start_stage","stt");cJSON_AddStringToObject(req,"end_stage","stt");cJSON_AddNumberToObject(req,"timeout",45);cJSON *input=cJSON_AddObjectToObject(req,"input");cJSON_AddNumberToObject(input,"sample_rate",16000);ok=send_json(ws,req) && wait_for(STT_READY,10000);}
    if(ok)ok=board_audio_record_start()==ESP_OK;
    uint8_t packet[641];int16_t samples[320];int64_t start=esp_timer_get_time();uint64_t energy=0;size_t count=0;
    if(ok)update(VOICE_LISTENING,"请说话，再点一次结束");
    while(ok && !stop_recording && esp_timer_get_time()-start<8000000 && !(xEventGroupGetBits(events)&(STT_DONE|FAILED))) {
        size_t sample_count=320;
#if CONFIG_DESK_CLOCK_VOICE_SELF_TEST
        if(synthetic_test){
            const uint8_t *input=diagnostic_pcm;
            size_t diagnostic_bytes=diagnostic_pcm_end-input;
#if CONFIG_DESK_CLOCK_VOICE_LAMP_TEST
            input=diagnostic_turn%2 ? lamp_off_pcm : lamp_on_pcm;
            diagnostic_bytes=(diagnostic_turn%2 ? lamp_off_end : lamp_on_end)-input;
#endif
            if(count*2>=diagnostic_bytes)break;
            size_t remaining=diagnostic_bytes/2-count;
            if(remaining<sample_count)sample_count=remaining;
            memcpy(samples,input+count*2,sample_count*2);
            vTaskDelay(pdMS_TO_TICKS(20));
        } else
#endif
        ok=board_audio_read(samples,320)==ESP_OK;
        if(!ok)break;
        for(size_t i=0;i<sample_count;++i){int16_t sample=samples[i];energy+=(int32_t)sample<0?-(int32_t)sample:sample;++count;}
        packet[0]=handler_id;memcpy(packet+1,samples,sample_count*2);
        ok=esp_websocket_client_send_bin(ws,(const char *)packet,sample_count*2+1,pdMS_TO_TICKS(2000))==(int)(sample_count*2+1);
    }
    board_audio_stop();
    ESP_LOGI(TAG,"Captured %u samples, mean amplitude %u",(unsigned)count,count?(unsigned)(energy/count):0);
    if(ok && !(xEventGroupGetBits(events)&(STT_DONE|FAILED))) {
        update(VOICE_THINKING,"正在识别…");
        ok=esp_websocket_client_send_bin(ws,(const char *)&handler_id,1,pdMS_TO_TICKS(2000))==1;
    }
    if(ok)ok=wait_for(STT_DONE,45000);
    // Keep the configured HA pipeline, but sanitize ASR text before intent matching.
    // Some Whisper outputs end in U+FFFD, making a valid device name unmatchable.
    if(ok){
        cJSON *req=cJSON_CreateObject();cJSON_AddNumberToObject(req,"id",2);
        cJSON_AddStringToObject(req,"type","assist_pipeline/run");
        cJSON_AddStringToObject(req,"pipeline",HA_VOICE_PIPELINE);
        cJSON_AddStringToObject(req,"start_stage","intent");cJSON_AddStringToObject(req,"end_stage","intent");
        cJSON_AddNumberToObject(req,"timeout",45);
        cJSON *input=cJSON_AddObjectToObject(req,"input");cJSON_AddStringToObject(input,"text",transcript);
        ok=send_json(ws,req) && wait_for(INTENT_DONE,45000);
    }
    // Mark intentional disconnect so it cannot overwrite the completed response.
    xEventGroupSetBits(events,RUN_DONE);
    checkpoint(WS_STOP);
    esp_websocket_client_stop(ws);esp_websocket_client_destroy(ws);
    checkpoint(WS_DESTROYED);
    if(ok)ok=play_reply();
    if(ok) update(VOICE_IDLE,reply);
    else if(!(xEventGroupGetBits(events)&FAILED))fail("语音连接或音频失败，请重试");
}
static void task(void *)
{
#if CONFIG_DESK_CLOCK_VOICE_SELF_TEST
    busy=true;
    for (int i=0;i<100 && !wifi_manager_is_connected();++i) vTaskDelay(pdMS_TO_TICKS(200));
    bool audio_ok=board_audio_record_start()==ESP_OK;
    int16_t test[320];uint64_t sum=0;unsigned count=0;
    for(int i=0;i<10 && audio_ok;++i){audio_ok=board_audio_read(test,320)==ESP_OK;for(int16_t v:test){sum+=v<0?-(int)v:v;++count;}}
    board_audio_stop();
    copy_text(reply,sizeof(reply),"语音助手已就绪");
    bool tts_ok=play_reply();
    ESP_LOGI(TAG,"AUDIO SELF TEST mic=%d samples=%u amplitude=%u playback=%d",audio_ok,count,count?(unsigned)(sum/count):0,tts_ok);
    update(VOICE_IDLE, "点击说话，控制家中设备");
    synthetic_test=true;
    for(int test=0;test<CONFIG_DESK_CLOCK_VOICE_SELF_TEST_RUNS;++test){
#if CONFIG_DESK_CLOCK_VOICE_LAMP_TEST
        diagnostic_turn=test;
#endif
        xEventGroupClearBits(events,0xff);reply[0]=0;transcript[0]=0;failure_reason[0]=0;handler_id=0;message_length=0;stop_recording=false;
        ESP_LOGI(TAG,"Synthetic voice test %d",test+1);
        run_voice();
        ESP_LOGI(TAG,"Synthetic voice result %d: bits=%u heap=%u",test+1,(unsigned)xEventGroupGetBits(events),(unsigned)esp_get_free_heap_size());
        ESP_LOGI(TAG,"Synthetic reply: %s",reply);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    synthetic_test=false;
    update(VOICE_IDLE,"点击说话，控制家中设备");
    busy=false;
#endif
    for(;;){
        ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
        xEventGroupClearBits(events,0xff);
        reply[0]=0;transcript[0]=0;failure_reason[0]=0;handler_id=0;message_length=0;stop_recording=false;
        run_voice();board_audio_stop();busy=false;
        if(xEventGroupGetBits(events)&FAILED)update(VOICE_ERROR,failure_reason);
        checkpoint(TURN_DONE);
        ESP_LOGI(TAG,"Voice turn complete; heap=%u stack=%u",(unsigned)esp_get_free_heap_size(),(unsigned)uxTaskGetStackHighWaterMark(NULL));
    }
}
esp_err_t voice_service_init(void)
{
    ESP_LOGI(TAG,"Reset reason=%d",(int)esp_reset_reason());
    if(last_checkpoint.magic==0x564f4943)ESP_LOGI(TAG,
        "Previous checkpoint=%u heap=%u largest=%u stack=%u",
        (unsigned)last_checkpoint.stage,(unsigned)last_checkpoint.heap,
        (unsigned)last_checkpoint.largest,(unsigned)last_checkpoint.stack);
    events=xEventGroupCreate();if(!events)return ESP_ERR_NO_MEM;
    if(xTaskCreate(task,"voice",10240,NULL,5,&worker)!=pdPASS)return ESP_ERR_NO_MEM;
    return ESP_OK;
}
void voice_service_click(void)
{
    if(!worker)return;
    if(busy){voice_snapshot_t s;voice_service_snapshot(&s);if(s.state==VOICE_LISTENING)stop_recording=true;return;}
    busy=true;update(VOICE_CONNECTING,"正在连接本地 HA…");xTaskNotifyGive(worker);
}
