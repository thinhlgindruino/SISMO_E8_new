idf_component_register(SRCS "hmi.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_timer
                       PRIV_REQUIRES esp_driver_uart motor_modbus)