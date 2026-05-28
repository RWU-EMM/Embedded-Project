idf_component_register(SRCS "wifi_driver.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_wifi nvs_flash esp_http_server json)
