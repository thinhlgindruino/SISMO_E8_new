idf_component_register(SRCS "page_session.c"
                       INCLUDE_DIRS "include"
                       REQUIRES hmi
                       PRIV_REQUIRES app_config motor_modbus logger esp_timer)
