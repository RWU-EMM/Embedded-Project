idf_component_register(SRCS "potentiometer_driver.c"
                    PRIV_REQUIRES esp_driver_gptimer
                    REQUIRES driver  esp_adc
                    INCLUDE_DIRS "include")
