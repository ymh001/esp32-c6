#pragma once
#include <stdint.h>
using TickType_t=uint32_t;
constexpr unsigned portMAX_DELAY=UINT32_MAX;
constexpr int pdTRUE=1;
#define pdMS_TO_TICKS(ms) (ms)
