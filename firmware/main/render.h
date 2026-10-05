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

#define POSIFLEX_GLYPH_CAPACITY 96

typedef struct {
    uint8_t pixels[5];
} posiflex_glyph_t;

typedef struct {
    bool initialized;
    uint8_t glyph_count;
    posiflex_glyph_t glyphs[POSIFLEX_GLYPH_CAPACITY];
    uint8_t displayed[2][20];
} posiflex_renderer_t;

bool posiflex_render_frame(posiflex_renderer_t *renderer,
                           const posiflex_output_t *output, const struct tm *local,
                           bool colon_on, posiflex_network_status_t status);
bool posiflex_render_waiting_for_time(const posiflex_output_t *output);
