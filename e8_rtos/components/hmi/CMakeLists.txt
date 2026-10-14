idf_component_register(SRCS "hmi.c" "hmi_map.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES app_config logger esp_driver_uart esp_timer)
