#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "esp_err.h"

// Call once after IP_EVENT_STA_GOT_IP. SNTP resynchronizes in the background.
esp_err_t board_time_start(void);

// Call from a worker task, not from the Wi-Fi event handler.
esp_err_t board_time_wait_for_sync(uint32_t timeout_ms);

// Returns false until the first NTP response has set the system clock.
bool board_time_local(struct tm *result);
