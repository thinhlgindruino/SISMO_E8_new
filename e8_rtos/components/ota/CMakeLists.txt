idf_component_register(SRCS "ota.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES app_update esp_app_format bootloader_support esp_timer)
