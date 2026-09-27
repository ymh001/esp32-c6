#pragma once
#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>
// Call from the UI/main task: mount/unmount must not race LCD IO submission.
// Ordinary card transfers and LCD DMA share SPI2 through the IDF SPI arbiter.
esp_err_t board_sd_mount();
esp_err_t board_sd_unmount();
void board_sd_status();
void board_sd_list();
int board_sd_read(const char *relative,void *data,size_t capacity,size_t *size);
int board_sd_write_new(const char *relative,const void *data,size_t size);
void board_sd_self_test();
