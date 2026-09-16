#include "ha_devices.h"
#include "voice_credentials.h"
#include "esp_crt_bundle.h"
#include "network_gate.h"
#include "wifi_manager.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <atomic>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG="devices";
struct command_t { unsigned index; bool on; };
static QueueHandle_t commands;
static portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
static ha_devices_snapshot_t state={};
static std::atomic<bool> visible(false),refresh_requested(true);

static void message(const char *text)
{
    portENTER_CRITICAL(&mux);
    if(strcmp(state.message,text)){strlcpy(state.message,text,sizeof(state.message));++state.revision;}
    portEXIT_CRITICAL(&mux);
}
void ha_devices_snapshot(ha_devices_snapshot_t *out)
{
    portENTER_CRITICAL(&mux);*out=state;portEXIT_CRITICAL(&mux);
}
void ha_devices_set_visible(bool value){visible=value;if(value)refresh_requested=true;}
void ha_devices_refresh(void){refresh_requested=true;}

static bool request(const char *path,const char *body,cJSON **result)
{
    if(result)*result=nullptr;
    if(!wifi_manager_is_connected())return false;
    NetworkLease network(pdMS_TO_TICKS(15000));
    if(!network)return false;
    char url[256];snprintf(url,sizeof(url),HA_HTTP_BASE_URL "%s",path);
    esp_http_client_config_t config={};config.url=url;config.timeout_ms=5000;
    config.crt_bundle_attach=esp_crt_bundle_attach;config.buffer_size=1024;config.disable_auto_redirect=true;
    auto h=esp_http_client_init(&config);if(!h)return false;
    esp_http_client_set_header(h,"Authorization","Bearer " HA_VOICE_TOKEN);
    if(body){esp_http_client_set_method(h,HTTP_METHOD_POST);esp_http_client_set_header(h,"Content-Type","application/json");}
    const int length=body?strlen(body):0;
    bool ok=esp_http_client_open(h,length)==ESP_OK;
    if(ok && body)ok=esp_http_client_write(h,body,length)==length;
    if(ok){esp_http_client_fetch_headers(h);int status=esp_http_client_get_status_code(h);ok=status>=200 && status<300;}
    if(ok && result){
        constexpr size_t capacity=4096;
        char *buffer=(char *)malloc(capacity);size_t used=0;
        if(!buffer)ok=false;
        while(ok && used<capacity-1){
            int n=esp_http_client_read(h,buffer+used,capacity-1-used);
            if(n<0){ok=false;break;}if(!n)break;used+=n;
        }
        if(buffer){
            buffer[used]=0;
            if(ok && used<capacity-1)*result=cJSON_Parse(buffer);
            ok=ok && *result;free(buffer);
        }
    }
    esp_http_client_cleanup(h);return ok;
}
static bool refresh_one(unsigned index)
{
    char path[192];snprintf(path,sizeof(path),"/api/states/%s",DEVICE_CATALOG[index].entity);
    cJSON *root=nullptr;bool ok=request(path,nullptr,&root);
    cJSON *value=cJSON_GetObjectItemCaseSensitive(root,"state");
    device_power_t power=ok && cJSON_IsString(value) ? device_power_parse(value->valuestring,!strcmp(DEVICE_CATALOG[index].domain,"climate")) : DEVICE_UNAVAILABLE;
    cJSON_Delete(root);
    portENTER_CRITICAL(&mux);
    state.devices[index].power=power;state.devices[index].error=!ok;++state.revision;
    portEXIT_CRITICAL(&mux);
    return ok;
}
static void refresh_all(void)
{
    portENTER_CRITICAL(&mux);state.refreshing=true;++state.revision;portEXIT_CRITICAL(&mux);
    bool ok=true;for(unsigned i=0;i<HA_DEVICE_COUNT;++i)if(!refresh_one(i))ok=false;
    portENTER_CRITICAL(&mux);state.refreshing=false;++state.revision;portEXIT_CRITICAL(&mux);
    message(ok ? "上下滑动查看 · 点击开关控制" : "连接失败，点刷新重试");
    ESP_LOGI(TAG,"State refresh: %s",ok ? "complete" : "failed");
}
void ha_devices_toggle(unsigned index)
{
    if(index>=HA_DEVICE_COUNT || !commands)return;
    command_t command={index,false};
    portENTER_CRITICAL(&mux);
    auto &device=state.devices[index];
    bool accepted=!device.busy && (device.power==DEVICE_OFF || device.power==DEVICE_ON);
    if(accepted){command.on=device.power==DEVICE_OFF;device.busy=true;device.error=false;++state.revision;}
    portEXIT_CRITICAL(&mux);
    if(!accepted)return;
    if(xQueueSend(commands,&command,0)!=pdTRUE){
        portENTER_CRITICAL(&mux);state.devices[index].busy=false;state.devices[index].error=true;++state.revision;portEXIT_CRITICAL(&mux);
        message("请求较多，请稍后再试");
    }
}
static void control(const command_t &command)
{
    message("正在切换设备…");
    char path[80],body[192];
    snprintf(path,sizeof(path),"/api/services/%s/turn_%s",DEVICE_CATALOG[command.index].domain,command.on?"on":"off");
    snprintf(body,sizeof(body),"{\"entity_id\":\"%s\"}",DEVICE_CATALOG[command.index].entity);
    bool accepted=request(path,body,nullptr),confirmed=false;
    if(accepted){
        for(unsigned i=0;i<5;++i){
            if(i)vTaskDelay(pdMS_TO_TICKS(500));
            if(!refresh_one(command.index))continue;
            ha_devices_snapshot_t current;ha_devices_snapshot(&current);
            if(current.devices[command.index].power==(command.on?DEVICE_ON:DEVICE_OFF)){confirmed=true;break;}
        }
    }
    portENTER_CRITICAL(&mux);
    state.devices[command.index].busy=false;state.devices[command.index].error=!confirmed;++state.revision;
    portEXIT_CRITICAL(&mux);
    message(confirmed ? "上下滑动查看 · 点击开关控制" : accepted ? "指令已发送，请刷新确认状态" : "控制失败，请稍后重试");
    ESP_LOGI(TAG,"Control device=%u on=%d accepted=%d confirmed=%d",command.index,command.on,accepted,confirmed);
}
static void task(void *)
{
    int64_t last_refresh=0;
    bool connected=false;
#if CONFIG_DESK_CLOCK_VOICE_LAMP_TEST
    bool lamp_test_done=false;
#endif
    for(;;){
        command_t command;
        if(xQueueReceive(commands,&command,pdMS_TO_TICKS(200))==pdTRUE)control(command);
        if(!wifi_manager_is_connected()){
            if(connected){
                portENTER_CRITICAL(&mux);
                for(auto &device:state.devices)device.power=DEVICE_UNAVAILABLE;
                ++state.revision;portEXIT_CRITICAL(&mux);
            }
            connected=false;message("等待 WiFi 连接…");continue;
        }
        if(!connected){connected=true;refresh_requested=true;}
        if(refresh_requested.exchange(false) || (visible && esp_timer_get_time()-last_refresh>15000000)){
            refresh_all();last_refresh=esp_timer_get_time();
#if CONFIG_DESK_CLOCK_VOICE_LAMP_TEST
            if(!lamp_test_done){
                lamp_test_done=true;
                for(int step=0;step<2;++step){
                    bool on=step==0;
                    command_t test={2,on};control(test);
                    ESP_LOGI(TAG,"Manual lamp test on=%d stack=%u",on,(unsigned)uxTaskGetStackHighWaterMark(nullptr));
                }
            }
#endif
        }
    }
}
esp_err_t ha_devices_init(void)
{
    commands=xQueueCreate(HA_DEVICE_COUNT,sizeof(command_t));
    if(!commands)return ESP_ERR_NO_MEM;
    message("正在连接本地 HA…");
    if(xTaskCreate(task,"devices",7168,nullptr,5,nullptr)!=pdPASS){vQueueDelete(commands);commands=nullptr;return ESP_ERR_NO_MEM;}
    return ESP_OK;
}
