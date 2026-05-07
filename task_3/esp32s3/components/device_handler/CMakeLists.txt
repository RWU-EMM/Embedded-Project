idf_component_register(SRCS "device_handler.c" "device_registry.c"
    INCLUDE_DIRS "include"
    REQUIRES json mqtt_manager auth_handler esp_timer led_blink_handler)
