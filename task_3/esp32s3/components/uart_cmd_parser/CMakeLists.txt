idf_component_register(SRCS "uart_cmd_parser.c"
                    REQUIRES driver led_blink_handler device_handler
                    INCLUDE_DIRS "include")
