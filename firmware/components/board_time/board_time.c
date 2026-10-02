#include "board_time.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <sys/time.h>

#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "freertos/FreeRTOS.h"
#include "sdkconfig.h"

#if CONFIG_LWIP_SNTP_MAX_SERVERS < 2
#error "Set CONFIG_LWIP_SNTP_MAX_SERVERS to at least 2"
#endif

static atomic_bool time_synchronized;
static const char *TAG = "board_time";

static void on_time_sync(struct timeval *now)
{
    atomic_store(&time_synchronized, true);
    struct tm local;
    if (localtime_r(&now->tv_sec, &local)) {
        ESP_LOGI(TAG, "NTP synchronized: %04d-%02d-%02d %02d:%02d:%02d MSK",
                 local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                 local.tm_hour, local.tm_min, local.tm_sec);
    }
}

esp_err_t board_time_start(void)
{
    // POSIX TZ uses the opposite sign: MSK-3 means UTC+3.
    if (setenv("TZ", "MSK-3", 1) != 0) {
        return ESP_FAIL;
    }
    tzset();

    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(
        2, ESP_SNTP_SERVER_LIST("0.pool.ntp.org", "1.pool.ntp.org"));
    config.wait_for_sync = true;
    config.sync_cb = on_time_sync;
    return esp_netif_sntp_init(&config);
}

esp_err_t board_time_wait_for_sync(uint32_t timeout_ms)
{
    return esp_netif_sntp_sync_wait(pdMS_TO_TICKS(timeout_ms));
}

bool board_time_local(struct tm *result)
{
    if (result == NULL || !atomic_load(&time_synchronized)) {
        return false;
    }
    time_t now = time(NULL);
    return localtime_r(&now, result) != NULL;
}
