#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

typedef enum {
    POSIFLEX_CHECKING,
    POSIFLEX_WIFI_OFF,
    POSIFLEX_WIFI_LAN,
    POSIFLEX_INTERNET_OK,
} posiflex_network_status_t;

typedef struct {
    void *context;
    bool (*send)(void *context, const uint8_t *data, size_t size);
    void (*pause_ms)(void *context, uint32_t milliseconds);
} posiflex_output_t;

// next_code is the first unused custom character code after this frame.
bool posiflex_render_full(const posiflex_output_t *output, const struct tm *local,
                          bool colon_on, posiflex_network_status_t status,
                          uint16_t *next_code);
bool posiflex_render_colon(const posiflex_output_t *output, const struct tm *local,
                           bool colon_on, uint16_t *next_code);
bool posiflex_render_waiting_for_time(const posiflex_output_t *output);
