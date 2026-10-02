idf_component_register(
    SRCS "main.c" "render.c" "network.c" "usb_display.c"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_netif nvs_flash esp_http_client usb_host_cdc_acm board_time
)
