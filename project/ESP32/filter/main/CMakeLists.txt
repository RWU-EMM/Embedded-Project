idf_component_register(SRCS "coeff_loader.c" "main.c"
    INCLUDE_DIRS "."
    REQUIRES usb_cdc_acm_driver wifi_udp_handler driver esp_timer digital_filters uart_driver esp_adc fgen_sim)

target_compile_options(${COMPONENT_LIB} PRIVATE "-save-temps=obj")
