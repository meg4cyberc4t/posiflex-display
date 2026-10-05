#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "board_time.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "network.h"
#include "nvs_flash.h"
#include "render.h"
#include "usb_display.h"

static const char *TAG = "clock";

void app_main(void)
{
    esp_err_t error = nvs_flash_init();
    if (error == ESP_ERR_NVS_NO_FREE_PAGES || error == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        error = nvs_flash_init();
    }
    ESP_ERROR_CHECK(error);
    ESP_ERROR_CHECK(network_start());
    ESP_ERROR_CHECK(usb_display_host_start());

    while (true) {
        usb_display_t display = {0};
        error = usb_display_open(&display);
        if (error != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        const posiflex_output_t output = {
            .context = &display,
            .send = usb_display_send,
            .pause_ms = usb_display_pause,
        };
        posiflex_renderer_t renderer = {0};
        bool have_frame = false;
        bool waiting_message_shown = false;
        int last_hour = -1;
        int last_minute = -1;
        int last_day = -1;
        int last_second = -1;
        posiflex_network_status_t last_status = POSIFLEX_CHECKING;

        while (usb_display_connected(&display)) {
            struct tm local;
            if (!board_time_local(&local)) {
                if (!waiting_message_shown) {
                    if (!posiflex_render_waiting_for_time(&output)) break;
                    waiting_message_shown = true;
                }
                vTaskDelay(pdMS_TO_TICKS(200));
                continue;
            }

            const posiflex_network_status_t status = network_status();
            const bool colon_on = (local.tm_sec % 2) == 0;
            const bool changed = !have_frame || local.tm_hour != last_hour ||
                                 local.tm_min != last_minute ||
                                 local.tm_yday != last_day || status != last_status;
            if (changed || local.tm_sec != last_second) {
                if (!posiflex_render_frame(&renderer, &output, &local, colon_on,
                                           status)) break;
                have_frame = true;
            }
            if (changed) {
                last_hour = local.tm_hour;
                last_minute = local.tm_min;
                last_day = local.tm_yday;
                last_status = status;
                ESP_LOGI(TAG, "%02d:%02d frame, network=%d", local.tm_hour,
                         local.tm_min, status);
            }
            last_second = local.tm_sec;
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        usb_display_close(&display);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
