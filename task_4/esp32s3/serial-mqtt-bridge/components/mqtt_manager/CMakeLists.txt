idf_component_register(SRCS "mqtt_service.c" "mqtt_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES mqtt nvs_flash esp_netif esp_secure_cert_mgr json wifi_driver esp_timer
)
