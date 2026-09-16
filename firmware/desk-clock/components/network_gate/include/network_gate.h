#pragma once
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

esp_err_t network_gate_init(void);
bool network_gate_take(TickType_t timeout);
void network_gate_give(void);

// The C6 has no PSRAM. Serialize TLS refreshes with foreground HA operations.
class NetworkLease {
public:
    explicit NetworkLease(TickType_t timeout=portMAX_DELAY): held_(network_gate_take(timeout)) {}
    ~NetworkLease(){if(held_)network_gate_give();}
    explicit operator bool() const {return held_;}
    NetworkLease(const NetworkLease&)=delete;
    NetworkLease& operator=(const NetworkLease&)=delete;
private:
    bool held_;
};

void network_set_sync_minutes(unsigned minutes);
unsigned network_sync_minutes(void);
bool network_process(void);
