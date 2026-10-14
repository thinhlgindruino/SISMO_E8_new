idf_component_register(SRCS "page_setting.c"
                       INCLUDE_DIRS "include"
                       REQUIRES hmi
                       PRIV_REQUIRES app_config network page_session logger esp_timer)
