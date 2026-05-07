idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES led_blink_handler potentiometer_driver uart_cmd_parser wifi_driver mqtt_manager
    device_handler
)
