#include "network.h"

#include <stdatomic.h>
#include <string.h>

#include "board_time.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define GOT_IP_BIT BIT0
#define PROBE_INTERVAL_MS 10000
#define PROBE_TIMEOUT_MS 2500

static const char *TAG = "network";
static EventGroupHandle_t events;
static atomic_bool wifi_associated;
static atomic_int current_status;

static void set_status(posiflex_network_status_t status)
{
    if (atomic_exchange(&current_status, status) != status) {
        ESP_LOGI(TAG, "network status: %s", status == POSIFLEX_WIFI_OFF ? "off" :
                 status == POSIFLEX_WIFI_LAN ? "Wi-Fi without Internet" :
                 status == POSIFLEX_INTERNET_OK ? "Internet OK" : "checking");
    }
}

static bool probe_url(const char *url)
{
    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = PROBE_TIMEOUT_MS,
        .disable_auto_redirect = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return false;
    const esp_err_t error = esp_http_client_perform(client);
    const bool reachable = error == ESP_OK && esp_http_client_get_status_code(client) == 204;
    esp_http_client_cleanup(client);
    return reachable;
}

static bool internet_reachable(void)
{
    return probe_url("http://connectivitycheck.gstatic.com/generate_204") ||
           probe_url("http://www.google.com/gen_204");
}

static void network_task(void *argument)
{
    (void)argument;
    int failed_probes = 0;
    bool sntp_started = false;
    while (true) {
        if (!atomic_load(&wifi_associated)) {
            failed_probes = 0;
            set_status(POSIFLEX_WIFI_OFF);
        } else if (xEventGroupGetBits(events) & GOT_IP_BIT) {
            if (!sntp_started) {
                const esp_err_t error = board_time_start();
                if (error == ESP_OK) {
                    sntp_started = true;
                } else {
                    ESP_LOGW(TAG, "NTP start failed: %s", esp_err_to_name(error));
                }
            }
            if (internet_reachable()) {
                failed_probes = 0;
                set_status(POSIFLEX_INTERNET_OK);
            } else if (++failed_probes >= 2) {
                set_status(POSIFLEX_WIFI_LAN);
            }
        } else {
            set_status(POSIFLEX_WIFI_LAN);
        }
        vTaskDelay(pdMS_TO_TICKS(PROBE_INTERVAL_MS));
    }
}

static void on_network_event(void *argument, esp_event_base_t base,
                             int32_t id, void *event_data)
{
    (void)argument;
    (void)event_data;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_CONNECTED) {
        atomic_store(&wifi_associated, true);
        set_status(POSIFLEX_WIFI_LAN);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        atomic_store(&wifi_associated, false);
        set_status(POSIFLEX_WIFI_OFF);
        xEventGroupClearBits(events, GOT_IP_BIT);
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(events, GOT_IP_BIT);
    }
}

esp_err_t network_start(void)
{
    const size_t ssid_length = strlen(CONFIG_POSIFLEX_WIFI_SSID);
    const size_t password_length = strlen(CONFIG_POSIFLEX_WIFI_PASSWORD);
    if (!ssid_length || ssid_length >= sizeof(((wifi_config_t *)0)->sta.ssid) ||
        password_length >= sizeof(((wifi_config_t *)0)->sta.password)) {
        ESP_LOGE(TAG, "Set Wi-Fi credentials in menuconfig");
        return ESP_ERR_INVALID_ARG;
    }

    events = xEventGroupCreate();
    if (!events) return ESP_ERR_NO_MEM;
    atomic_store(&current_status, POSIFLEX_WIFI_OFF);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    if (!esp_netif_create_default_wifi_sta()) return ESP_ERR_NO_MEM;
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_event_handler_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, on_network_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, on_network_event, NULL));

    wifi_config_t wifi = {0};
    memcpy(wifi.sta.ssid, CONFIG_POSIFLEX_WIFI_SSID, ssid_length + 1);
    memcpy(wifi.sta.password, CONFIG_POSIFLEX_WIFI_PASSWORD, password_length + 1);
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi));
    if (xTaskCreate(network_task, "network_probe", 6144, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return esp_wifi_start();
}

posiflex_network_status_t network_status(void)
{
    return (posiflex_network_status_t)atomic_load(&current_status);
}
