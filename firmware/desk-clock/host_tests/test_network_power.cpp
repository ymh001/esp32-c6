#include <initializer_list>

#include <cassert>
#include <cstdio>
#include "network_gate.h"
#include "wifi_manager.h"
#include "freertos/semphr.h"
static int64_t now;
static int task_id,starts,stops;
static bool available=true;
static wifi_manager_state_t wifi=WIFI_MANAGER_IDLE;
static FakeMutex mutex;
int64_t esp_timer_get_time(){return now;}
void vTaskDelay(TickType_t ticks){now+=(int64_t)ticks*1000;}
SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(){return &mutex;}
int xSemaphoreTakeRecursive(SemaphoreHandle_t m,TickType_t){
    if(m->depth && m->owner!=task_id)return 0;
    m->owner=task_id;++m->depth;return pdTRUE;
}
void xSemaphoreGiveRecursive(SemaphoreHandle_t m){assert(m->owner==task_id && m->depth>0);--m->depth;}
esp_err_t wifi_manager_start(){if(wifi==WIFI_MANAGER_IDLE){++starts;wifi=available?WIFI_MANAGER_CONNECTED:WIFI_MANAGER_CONNECTING;}return ESP_OK;}
void wifi_manager_stop(){++stops;wifi=WIFI_MANAGER_IDLE;}
bool wifi_manager_is_connected(){return wifi==WIFI_MANAGER_CONNECTED;}
wifi_manager_state_t wifi_manager_state(){return wifi;}
int main(){
    assert(network_gate_init()==ESP_OK);
    network_set_sync_minutes(1);assert(!network_process());
    task_id=1;assert(network_gate_take(0));assert(starts==1);
    // A batch owns WiFi across nested requests; main cannot stop an active batch.
    assert(network_gate_take(0));network_gate_give();
    now+=60000000;task_id=0;assert(network_process());assert(stops==0);
    task_id=1;network_gate_give();task_id=0;
    now+=29999000;network_process();assert(stops==0);
    now+=1000;network_process();assert(stops==1);
    task_id=1;assert(network_gate_take(0));assert(starts==2);network_gate_give();
    // Changing schedule reschedules from now, and always-online prevents shutdown.
    task_id=0;network_set_sync_minutes(0);network_process();
    now+=3600000000LL;network_process();assert(stops==1);
    for(unsigned minutes:{1U,5U,15U,30U,60U}){
        network_set_sync_minutes(minutes);assert(!network_process());
        now+=(int64_t)minutes*60000000-1;assert(!network_process());
        ++now;assert(network_process());assert(!network_process());
    }
    // Connection failure is bounded and releases the lock; later retries succeed.
    available=false;wifi=WIFI_MANAGER_IDLE;task_id=1;int64_t begin=now;
    assert(!network_gate_take(0));assert(now-begin==30000000 && mutex.depth==0);
    task_id=0;now+=30000000;network_process();assert(wifi==WIFI_MANAGER_IDLE);
    available=true;task_id=1;assert(network_gate_take(0));network_gate_give();
    assert(mutex.depth==0);puts("network power: schedules, nested leases, idle stop, wake and timeout recovery passed");
}
