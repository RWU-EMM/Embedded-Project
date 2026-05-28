idf_component_register(SRCS "cmds.c" "serial_data_parser.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_timer json)
