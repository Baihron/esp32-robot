idf_component_register(SRCS "src/button_driver.c"
                       INCLUDE_DIRS "include"
                       REQUIRES driver
                       REQUIRES freertos
                       REQUIRES esp_timer
                       )