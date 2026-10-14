idf_component_register(SRCS "ui_router.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES hmi
                       PRIV_REQUIRES page_home page_setting)