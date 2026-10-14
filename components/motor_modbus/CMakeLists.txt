idf_component_register(SRCS "motor_modbus.cpp"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_driver_uart freertos esp_timer)