#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "usb/cdc_acm_host.h"

typedef struct {
    cdc_acm_dev_hdl_t handle;
} usb_display_t;

esp_err_t usb_display_host_start(void);
esp_err_t usb_display_open(usb_display_t *display);
void usb_display_close(usb_display_t *display);
bool usb_display_send(void *context, const uint8_t *data, size_t size);
void usb_display_pause(void *context, uint32_t milliseconds);
bool usb_display_connected(const usb_display_t *display);
