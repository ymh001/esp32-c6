#include "energy_service.h"
#include "grafana_credentials.h"
#include "network_gate.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG="energy";
static SemaphoreHandle_t mutex;
static EventGroupHandle_t events;
static energy_snapshot_t snapshot;
static const char query[]=R"({"queries":[{"refId":"A","datasource":{"type":"grafana-postgresql-datasource","uid":"home-history"},"format":"table","rawSql":"SELECT * FROM screen_energy","editorMode":"code"}],"from":"now-1h","to":"now"})";

static const cJSON *member(const cJSON *o,const char *key){return cJSON_GetObjectItemCaseSensitive(o,key);}
static const cJSON *column(const cJSON *frame,const char *name)
{
    const auto fields=member(member(frame,"schema"),"fields");
    const auto values=member(member(frame,"data"),"values");
    for(int i=0;i<cJSON_GetArraySize(fields);++i){
        const auto n=member(cJSON_GetArrayItem(fields,i),"name");
        if(cJSON_IsString(n) && !strcmp(n->valuestring,name))return cJSON_GetArrayItem(cJSON_GetArrayItem(values,i),0);
    }
    return nullptr;
}
static float number(const cJSON *frame,const char *name)
{
    const auto v=column(frame,name);
    return cJSON_IsNumber(v) && isfinite(v->valuedouble)?v->valuedouble:NAN;
}
static bool fetch(energy_snapshot_t *out)
{
    NetworkLease lease(pdMS_TO_TICKS(15000));if(!lease)return false;
    esp_http_client_config_t cfg={};cfg.url=GRAFANA_QUERY_URL;cfg.timeout_ms=10000;
    cfg.crt_bundle_attach=esp_crt_bundle_attach;cfg.buffer_size=1024;cfg.disable_auto_redirect=true;
    auto h=esp_http_client_init(&cfg);if(!h)return false;
    esp_http_client_set_method(h,HTTP_METHOD_POST);
    esp_http_client_set_header(h,"Authorization","Bearer " GRAFANA_TOKEN);
    esp_http_client_set_header(h,"Content-Type","application/json");
    bool ok=esp_http_client_open(h,sizeof(query)-1)==ESP_OK;
    if(ok)ok=esp_http_client_write(h,query,sizeof(query)-1)==sizeof(query)-1;
    if(ok){esp_http_client_fetch_headers(h);ok=esp_http_client_get_status_code(h)==200;}
    char *buffer=(char *)calloc(1,4096);size_t used=0;if(!buffer)ok=false;
    while(ok && used<4095){int n=esp_http_client_read(h,buffer+used,4095-used);if(n<0){ok=false;break;}if(!n)break;used+=n;}
    esp_http_client_cleanup(h);
    auto root=ok && used<4095?cJSON_Parse(buffer):nullptr;free(buffer);
    const auto result=member(member(root,"results"),"A");
    const auto frame=cJSON_GetArrayItem(member(result,"frames"),0);
    const float stamp=number(frame,"updated_at");
    out->today_kwh=number(frame,"today_kwh");out->today_cost=number(frame,"today_cost");
    out->remaining_cost=number(frame,"remaining_cost");out->price_per_kwh=number(frame,"price_per_kwh");
    const auto timestamp=column(frame,"updated_at");
    out->updated_at=cJSON_IsNumber(timestamp)?(time_t)timestamp->valuedouble:0;
    out->stale=cJSON_IsTrue(column(frame,"stale"));
    ok=frame && !member(result,"error") && isfinite(stamp) && out->updated_at>0 &&
       isfinite(out->remaining_cost) && out->remaining_cost>=0 && isfinite(out->price_per_kwh) && out->price_per_kwh>0;
    out->loaded=ok;cJSON_Delete(root);return ok;
}
static void task(void *)
{
    for(;;){
        xSemaphoreTake(mutex,portMAX_DELAY);snapshot.refreshing=true;xSemaphoreGive(mutex);
        energy_snapshot_t next={};bool ok=fetch(&next);
        xSemaphoreTake(mutex,portMAX_DELAY);
        if(ok)snapshot=next;
        snapshot.refreshing=false;snapshot.refresh_failed=!ok;
        strlcpy(snapshot.message,ok?"数据已同步":"更新失败，保留上次数据",sizeof(snapshot.message));
        xSemaphoreGive(mutex);
        if(ok)ESP_LOGI(TAG,"Grafana today=%.2f kWh cost=%.2f remaining=%.2f stale=%d",next.today_kwh,next.today_cost,next.remaining_cost,next.stale);
        else ESP_LOGW(TAG,"Grafana energy request failed");
        xEventGroupWaitBits(events,BIT0,pdTRUE,pdFALSE,pdMS_TO_TICKS(10 * 60 * 1000));
    }
}
esp_err_t energy_service_init(void)
{
    mutex=xSemaphoreCreateMutex();events=xEventGroupCreate();
    if(!mutex || !events)return ESP_ERR_NO_MEM;
    snapshot.today_kwh=snapshot.today_cost=snapshot.remaining_cost=NAN;
    return ESP_OK;
}
esp_err_t energy_service_start(void)
{
    static bool started=false;if(started)return ESP_OK;
    if(xTaskCreate(task,"energy",8192,nullptr,4,nullptr)!=pdPASS)return ESP_ERR_NO_MEM;
    started=true;return ESP_OK;
}
void energy_service_request_refresh(void)
{
    if(!events)return;
    xSemaphoreTake(mutex,portMAX_DELAY);bool busy=snapshot.refreshing;
    if(!busy)snapshot.refreshing=true;
    xSemaphoreGive(mutex);
    if(!busy)xEventGroupSetBits(events,BIT0);
}
void energy_service_get_snapshot(energy_snapshot_t *out)
{
    if(!out)return;
    xSemaphoreTake(mutex,portMAX_DELAY);*out=snapshot;xSemaphoreGive(mutex);
    time_t now=time(nullptr);struct tm today={},updated={};localtime_r(&now,&today);localtime_r(&out->updated_at,&updated);
    if(today.tm_year!=updated.tm_year || today.tm_yday!=updated.tm_yday)out->today_kwh=out->today_cost=NAN;
    if(out->loaded && now-out->updated_at>7200)out->stale=true;
}
