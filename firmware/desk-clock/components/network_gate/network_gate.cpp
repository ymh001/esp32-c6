#include "network_gate.h"
#include "wifi_manager.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <atomic>

static SemaphoreHandle_t gate;
static std::atomic<unsigned> interval{15};
static int64_t last_use;
static int64_t last_sync;
static std::atomic<bool> reschedule{false};

esp_err_t network_gate_init(void)
{
    if(!gate)gate=xSemaphoreCreateRecursiveMutex();
    // Allow initial time synchronization to finish before the first shutdown.
    last_use=esp_timer_get_time()+90000000;
    last_sync=esp_timer_get_time();
    return gate ? ESP_OK : ESP_ERR_NO_MEM;
}
void network_set_sync_minutes(unsigned minutes)
{
    if(interval.exchange(minutes)!=minutes)reschedule=true;
}
unsigned network_sync_minutes(void){return interval.load();}
bool network_gate_take(TickType_t timeout)
{
    if(!gate || xSemaphoreTakeRecursive(gate,timeout)!=pdTRUE)return false;
    if(wifi_manager_start()!=ESP_OK){xSemaphoreGiveRecursive(gate);return false;}
    const int64_t deadline=esp_timer_get_time()+30000000;
    while(!wifi_manager_is_connected() && esp_timer_get_time()<deadline)vTaskDelay(pdMS_TO_TICKS(100));
    last_use=esp_timer_get_time();
    if(!wifi_manager_is_connected()){xSemaphoreGiveRecursive(gate);return false;}
    return true;
}
void network_gate_give(void)
{
    last_use=esp_timer_get_time();
    xSemaphoreGiveRecursive(gate);
}
// app_main owns the periodic schedule. HTTP clients own the recursive mutex,
// so no shutdown can race a request or its confirmation reads.
bool network_process(void)
{
    const int64_t now=esp_timer_get_time();
    const unsigned minutes=interval.load();
    if(reschedule.exchange(false))last_sync=now;
    const bool due=now-last_sync >= (int64_t)(minutes ? minutes : 1)*60000000;
    if(due)last_sync=now;
    if(gate && xSemaphoreTakeRecursive(gate,0)==pdTRUE){
        if(!minutes)wifi_manager_start();
        else if(now-last_use>=30000000 && wifi_manager_state()!=WIFI_MANAGER_IDLE)wifi_manager_stop();
        xSemaphoreGiveRecursive(gate);
    }
    return due;
}
