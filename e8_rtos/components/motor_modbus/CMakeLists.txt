idf_component_register(SRCS "motor_modbus.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES app_config esp_driver_uart esp_timer)
