#pragma once
#include "FreeRTOS.h"
struct FakeMutex {int owner=-1,depth=0;};
using SemaphoreHandle_t=FakeMutex*;
SemaphoreHandle_t xSemaphoreCreateRecursiveMutex();
int xSemaphoreTakeRecursive(SemaphoreHandle_t,TickType_t);
void xSemaphoreGiveRecursive(SemaphoreHandle_t);
