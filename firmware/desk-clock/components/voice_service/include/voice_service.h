#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
enum voice_state_t { VOICE_IDLE, VOICE_CONNECTING, VOICE_LISTENING, VOICE_THINKING, VOICE_SPEAKING, VOICE_ERROR };
struct voice_snapshot_t { voice_state_t state; char text[384]; uint32_t revision; };
esp_err_t voice_service_init(void);
void voice_service_click(void);
void voice_service_snapshot(voice_snapshot_t *out);
