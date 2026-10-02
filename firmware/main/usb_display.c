#include "usb_display.h"

#include <stdatomic.h>

#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "usb/usb_host.h"

static const char *TAG = "posiflex_usb";
static atomic_bool device_disconnected;

static void usb_events_task(void *argument)
{
    (void)argument;
    while (true) {
        uint32_t flags = 0;
        const esp_err_t error = usb_host_lib_handle_events(portMAX_DELAY, &flags);
        if (error != ESP_OK) {
            ESP_LOGE(TAG, "USB event error: %s", esp_err_to_name(error));
            continue;
        }
        if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            usb_host_device_free_all();
        }
    }
}

static void on_device_event(const cdc_acm_host_dev_event_data_t *event,
                            void *context)
{
    (void)context;
    if (event->type == CDC_ACM_HOST_DEVICE_DISCONNECTED) {
        atomic_store(&device_disconnected, true);
    }
}

esp_err_t usb_display_host_start(void)
{
    const usb_host_config_t config = {.intr_flags = ESP_INTR_FLAG_LEVEL1};
    esp_err_t error = usb_host_install(&config);
    if (error != ESP_OK) return error;
    if (xTaskCreate(usb_events_task, "usb_events", 4096, NULL, 7, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return cdc_acm_host_install(NULL);
}

esp_err_t usb_display_open(usb_display_t *display)
{
    if (!display) return ESP_ERR_INVALID_ARG;
    display->handle = NULL;
    atomic_store(&device_disconnected, false);
    const cdc_acm_host_device_config_t config = {
        .connection_timeout_ms = 1000,
        .out_buffer_size = 64,
        .in_buffer_size = 64,
        .event_cb = on_device_event,
    };
    esp_err_t error = cdc_acm_host_open(CDC_HOST_ANY_VID, CDC_HOST_ANY_PID, 0,
                                        &config, &display->handle);
    if (error != ESP_OK) return error;

    const cdc_acm_line_coding_t line = {
        .dwDTERate = 9600, .bCharFormat = 0, .bParityType = 0, .bDataBits = 8,
    };
    error = cdc_acm_host_line_coding_set(display->handle, &line);
    if (error != ESP_OK && error != ESP_ERR_NOT_SUPPORTED) {
        usb_display_close(display);
        return error;
    }
    error = cdc_acm_host_set_control_line_state(display->handle, true, true);
    if (error != ESP_OK && error != ESP_ERR_NOT_SUPPORTED) {
        usb_display_close(display);
        return error;
    }
    ESP_LOGI(TAG, "display VCOM opened at 9600 8N1");
    return ESP_OK;
}

void usb_display_close(usb_display_t *display)
{
    if (display && display->handle) {
        cdc_acm_host_close(display->handle);
        display->handle = NULL;
    }
}

bool usb_display_send(void *context, const uint8_t *data, size_t size)
{
    usb_display_t *display = context;
    if (!display || !display->handle || atomic_load(&device_disconnected)) return false;
    const esp_err_t error = cdc_acm_host_data_tx_blocking(display->handle, data, size, 1000);
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "VCOM write failed: %s", esp_err_to_name(error));
        return false;
    }
    return true;
}

void usb_display_pause(void *context, uint32_t milliseconds)
{
    (void)context;
    vTaskDelay(pdMS_TO_TICKS(milliseconds));
}

bool usb_display_connected(const usb_display_t *display)
{
    return display && display->handle && !atomic_load(&device_disconnected);
}
