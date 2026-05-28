idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES uart_driver mqtt_manager esp_driver_gpio json)
