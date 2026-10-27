idf_component_register(SRCS "network.c" "web.c"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "."
                       PRIV_REQUIRES esp_wifi esp_eth esp_driver_gpio esp_netif esp_event esp_timer nvs_flash json
                                     esp_http_server ota lwip device_settings)
