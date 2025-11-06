idf_component_register(
    SRCS "sensors.c" "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_hostinger
        captive_manager
        esp_wifi
        esp_netif
        esp_http_client
        esp_timer
        esp_event
        nvs_flash
        driver
        mbedtls
        lwip
        mdns
)   