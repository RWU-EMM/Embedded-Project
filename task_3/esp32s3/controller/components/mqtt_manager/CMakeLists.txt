idf_component_register(SRCS "mqtt_service.c" "mqtt_manager.c"
                    REQUIRES mqtt nvs_flash esp_netif esp_secure_cert_mgr json wifi_driver
                    INCLUDE_DIRS "include")
