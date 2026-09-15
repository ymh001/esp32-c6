#include "network_gate.h"
#include "freertos/semphr.h"

static SemaphoreHandle_t gate;
esp_err_t network_gate_init(void)
{
    if(!gate)gate=xSemaphoreCreateRecursiveMutex();
    return gate ? ESP_OK : ESP_ERR_NO_MEM;
}
bool network_gate_take(TickType_t timeout)
{
    return gate && xSemaphoreTakeRecursive(gate,timeout)==pdTRUE;
}
void network_gate_give(void){xSemaphoreGiveRecursive(gate);}
