idf_component_register(SRCS "page_program.c" "program_data.c" "grid_data.c"
                       INCLUDE_DIRS "include"
                       REQUIRES hmi
                       PRIV_REQUIRES app_config logger esp_timer page_session device_settings)
