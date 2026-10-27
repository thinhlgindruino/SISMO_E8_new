idf_component_register(SRCS "cloud.c" "cloud_link.c"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "."
                       PRIV_REQUIRES mqtt json mbedtls esp_timer app_config logger hmi network device_settings
                                     motor_modbus ota page_session page_program page_setting)
