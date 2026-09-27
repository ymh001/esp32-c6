#include "file_transfer.h"
#include "transfer_files.h"
#include "services.h"
#include "board_sd.h"
#include "esp_http_server.h"
#include "esp_random.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <atomic>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "transfer_page.h"
namespace pocket {
namespace {
constexpr uint64_t max_upload=128*1024*1024;
std::atomic<bool> wanted{false}, alive{false}, running{false}, busy{false};
std::atomic<unsigned> progress{0};
std::atomic<int> state{0}; // 0 off, 1 waiting, 2 running, 3 failure
char code[9]{};
bool restore_wifi=false, guard=false;
QueueHandle_t queue=nullptr;SemaphoreHandle_t finished=nullptr;
files::Job job;
files::Session filesystem("/sdcard");
// Single HTTP server task submits jobs; main task is the only filesystem owner.
int perform(files::Op op) {
    job.op=op;files::Job *ptr=&job;
    xQueueSend(queue,&ptr,portMAX_DELAY);
    xSemaphoreTake(finished,portMAX_DELAY);
    return job.error;
}
bool usable(){return wanted.load()&&network_snapshot().state==NetState::Connected;}
struct Operation {
    Operation(){busy=true;progress=0;}
    ~Operation(){perform(files::Op::Cleanup);busy=false;}
};
void headers(httpd_req_t *r) {
    httpd_resp_set_hdr(r,"Cache-Control","no-store");
    httpd_resp_set_hdr(r,"X-Content-Type-Options","nosniff");
    httpd_resp_set_hdr(r,"Connection","close");
}
esp_err_t error(httpd_req_t *r,int err) {
    const char *status="500 Internal Server Error",*message="文件操作失败，请重试";
    if(err==EEXIST){status="409 Conflict";message="同名文件已存在，请先重命名";}
    else if(err==ENOENT){status="404 Not Found";message="文件不存在";}
    else if(err==EINVAL||err==ENAMETOOLONG||err==EACCES||err==EISDIR){status="400 Bad Request";message="文件路径无效";}
    else if(err==ENOSPC){status="507 Insufficient Storage";message="SD 卡空间不足";}
    else if(err==EFBIG){status="413 Content Too Large";message="单个文件最多 128 MB";}
    else if(err==ECANCELED||err==ENODEV){status="503 Service Unavailable";message="文件传输已关闭或 SD 卡不可用";}
    headers(r);httpd_resp_set_status(r,status);httpd_resp_set_type(r,"text/plain; charset=utf-8");
    httpd_resp_sendstr(r,message);
    return ESP_FAIL;
}
bool authorized(httpd_req_t *r) {
    headers(r);
    if(!usable()){error(r,ECANCELED);return false;}
    char supplied[16]{};
    bool ok=httpd_req_get_hdr_value_str(r,"X-File-Key",supplied,sizeof(supplied))==ESP_OK;
    unsigned difference=0;for(unsigned i=0;i<8;++i)difference|=(unsigned char)supplied[i]^(unsigned char)code[i];
    if(!ok||strlen(supplied)!=8||difference){
        vTaskDelay(pdMS_TO_TICKS(250));
        httpd_resp_set_status(r,"401 Unauthorized");httpd_resp_sendstr(r,"访问码不正确，请查看屏幕控制中心");return false;
    }
    return true;
}
int name(httpd_req_t *r,const char *key,bool root=false) {
    char query[1024],encoded[900];
    if(httpd_req_get_url_query_str(r,query,sizeof(query))!=ESP_OK)return EINVAL;
    if(httpd_query_key_value(query,key,encoded,sizeof(encoded))!=ESP_OK)return EINVAL;
    int err=files::decode(encoded,job.path,sizeof(job.path));
    if(!err&&!root&&!job.path[0])err=EINVAL;
    return err;
}
esp_err_t page(httpd_req_t *r) {
    headers(r);httpd_resp_set_type(r,"text/html; charset=utf-8");
    httpd_resp_set_hdr(r,"Content-Security-Policy","default-src 'self'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; connect-src 'self'; frame-ancestors 'none'");
    return httpd_resp_send(r,transfer_page,sizeof(transfer_page)-1);
}
esp_err_t list(httpd_req_t *r) {
    if(!authorized(r))return ESP_FAIL;
    Operation operation;
    int err=name(r,"dir",true);if(!err)err=perform(files::Op::ListOpen);if(err)return error(r,err);
    httpd_resp_set_type(r,"application/json");
    if(httpd_resp_sendstr_chunk(r,"{\"files\":[")!=ESP_OK)return ESP_FAIL;
    unsigned count=0;bool truncated=false;
    while(usable()){
        err=perform(files::Op::ListNext);if(err)return ESP_FAIL;
        if(job.end)break;
        if(!job.name[0])continue;
        if(count==256){truncated=true;break;}
        char escaped[1537],line[1660];
        if(!files::json_escape(job.name,escaped,sizeof(escaped)))return ESP_FAIL;
        snprintf(line,sizeof(line),"%s{\"name\":\"%s\",\"dir\":%s,\"size\":%llu}",count?",":"",escaped,job.directory?"true":"false",(unsigned long long)job.size);
        if(httpd_resp_sendstr_chunk(r,line)!=ESP_OK)return ESP_FAIL;
        ++count;
    }
    if(!usable())return ESP_FAIL;
    if(httpd_resp_sendstr_chunk(r,truncated?"],\"truncated\":true}":"],\"truncated\":false}")!=ESP_OK)return ESP_FAIL;
    return httpd_resp_send_chunk(r,nullptr,0);
}
esp_err_t download(httpd_req_t *r) {
    if(!authorized(r))return ESP_FAIL;
    Operation operation;
    int err=name(r,"name");if(!err)err=perform(files::Op::ReadOpen);if(err)return error(r,err);
    if(job.size>max_upload)return error(r,EFBIG);
    uint64_t total=job.size,done=0;
    httpd_resp_set_type(r,"application/octet-stream");
    httpd_resp_set_hdr(r,"Content-Disposition","attachment; filename=download.bin");
    while(usable()){
        if(perform(files::Op::Read))return ESP_FAIL;
        if(job.end)return httpd_resp_send_chunk(r,nullptr,0);
        if(httpd_resp_send_chunk(r,(char *)job.data,job.count)!=ESP_OK)return ESP_FAIL;
        done+=job.count;progress=total?(unsigned)(done*100/total):100;
    }
    return ESP_FAIL;
}
esp_err_t upload(httpd_req_t *r) {
    if(!authorized(r))return ESP_FAIL;
    Operation operation;
    int err=name(r,"name");if(err)return error(r,err);
    if(r->content_len>max_upload)return error(r,EFBIG);
    job.size=r->content_len;err=perform(files::Op::WriteOpen);if(err)return error(r,err);
    size_t received=0;
    while(received<r->content_len&&usable()){
        size_t size=r->content_len-received;if(size>sizeof(job.data))size=sizeof(job.data);
        int n=httpd_req_recv(r,(char *)job.data,size);
        if(n<=0)return ESP_FAIL;
        job.count=n;err=perform(files::Op::Write);if(err)return error(r,err);
        received+=n;progress=(unsigned)((uint64_t)received*100/r->content_len);
    }
    if(!usable())return error(r,ECANCELED);
    err=perform(files::Op::Commit);if(err)return error(r,err);
    httpd_resp_set_type(r,"application/json");return httpd_resp_sendstr(r,"{\"ok\":true}");
}
esp_err_t remove_file(httpd_req_t *r) {
    if(!authorized(r))return ESP_FAIL;
    Operation operation;
    int err=name(r,"name");if(!err)err=perform(files::Op::Remove);if(err)return error(r,err);
    httpd_resp_set_type(r,"application/json");return httpd_resp_sendstr(r,"{\"ok\":true}");
}
void server_task(void *) {
    httpd_handle_t server=nullptr;
    while(wanted&&(!usable()||esp_get_free_heap_size()<50000))vTaskDelay(pdMS_TO_TICKS(250));
    if(wanted){
        httpd_config_t cfg=HTTPD_DEFAULT_CONFIG();
        cfg.stack_size=8192;cfg.task_priority=2;cfg.max_open_sockets=2;
        cfg.max_uri_handlers=5;cfg.lru_purge_enable=true;cfg.recv_wait_timeout=2;cfg.send_wait_timeout=2;
        if(httpd_start(&server,&cfg)==ESP_OK){
            const httpd_uri_t routes[]={
                {.uri="/",.method=HTTP_GET,.handler=page,.user_ctx=nullptr},
                {.uri="/api/list",.method=HTTP_GET,.handler=list,.user_ctx=nullptr},
                {.uri="/api/file",.method=HTTP_GET,.handler=download,.user_ctx=nullptr},
                {.uri="/api/file",.method=HTTP_POST,.handler=upload,.user_ctx=nullptr},
                {.uri="/api/file",.method=HTTP_DELETE,.handler=remove_file,.user_ctx=nullptr}
            };
            bool ok=true;for(auto &route:routes)if(httpd_register_uri_handler(server,&route)!=ESP_OK)ok=false;
            if(ok){running=true;state=2;ESP_LOGI("transfer","File server started on port 80");}
            else {wanted=false;state=3;}
        }else {wanted=false;state=3;}
    }
    while(wanted)vTaskDelay(pdMS_TO_TICKS(100));
    running=false;
    // Stop from this task, never from UI: handlers may be waiting for a UI file job.
    if(server)httpd_stop(server);
    perform(files::Op::Cleanup);
    busy=false;alive=false;
    ESP_LOGI("transfer","File server stopped");
    vTaskDelete(nullptr);
}
}
bool transfer_enabled(){return wanted.load();}
bool transfer_busy(){return busy.load();}
TransferSnapshot transfer_snapshot(){
    TransferSnapshot s;s.enabled=wanted;s.running=running;s.busy=busy;s.stopping=alive&&!wanted;s.progress=progress;
    snprintf(s.code,sizeof(s.code),"%s",code);
    snprintf(s.message,sizeof(s.message),"%s",s.stopping?"正在关闭":state==3?"启动失败，请重试":!s.enabled?"关闭时不提供访问":!s.running?"等待 Wi-Fi 连接":s.busy?"正在传输":"同一 Wi-Fi 下打开网址");return s;
}
bool transfer_enable(bool enabled){
    if(!enabled){wanted=false;return true;}
    if(wanted)return true;
    if(alive||guard)return false;
    if(!board_sd_is_mounted()){state=3;return false;}
    if(!queue)queue=xQueueCreate(1,sizeof(files::Job *));
    if(!finished)finished=xSemaphoreCreateBinary();
    if(!queue||!finished){state=3;return false;}
    restore_wifi=!network_snapshot().enabled;
    if(restore_wifi&&!network_enable(true)){state=3;return false;}
    snprintf(code,sizeof(code),"%08lu",(unsigned long)(esp_random()%100000000));
    board_sd_transfer_guard(true);guard=true;wanted=true;alive=true;state=1;
    if(xTaskCreate(server_task,"file_manager",3072,nullptr,2,nullptr)!=pdPASS){wanted=false;alive=false;state=3;return false;}
    return true;
}
void transfer_process(){
    files::Job *pending=nullptr;
    if(queue&&xQueueReceive(queue,&pending,0)==pdTRUE){
        int64_t start=esp_timer_get_time();
        if(!wanted&&pending->op!=files::Op::Cleanup)pending->error=ECANCELED;
        else filesystem.execute(*pending);
        int64_t elapsed=esp_timer_get_time()-start;
        if(elapsed>50000)ESP_LOGW("transfer","SD operation took %lld us",(long long)elapsed);
        xSemaphoreGive(finished);
    }
    if(guard&&!alive){
        board_sd_transfer_guard(false);guard=false;
        if(restore_wifi){network_enable(false);restore_wifi=false;}
    }
}
}
