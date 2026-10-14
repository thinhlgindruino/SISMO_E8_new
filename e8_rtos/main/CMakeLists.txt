idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES app_config logger hmi motor_modbus ui_router page_session page_setting network ota device_settings)
