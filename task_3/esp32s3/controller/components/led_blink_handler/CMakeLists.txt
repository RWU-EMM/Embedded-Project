idf_component_register(SRCS "led_blink_handler.c"
                    PRIV_REQUIRES esp_driver_gpio esp_driver_gptimer
                    REQUIRES driver
                    INCLUDE_DIRS "include")
