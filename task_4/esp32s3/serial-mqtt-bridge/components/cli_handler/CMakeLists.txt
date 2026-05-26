idf_component_register(SRCS "cli_handler.c"
                    INCLUDE_DIRS "include"
                    REQUIRES uart_driver serial_data_parser)
