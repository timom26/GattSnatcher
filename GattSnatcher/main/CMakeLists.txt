#idf_component_register(SRCS "collector-ad.cpp" INCLUDE_DIRS ".")
idf_component_register(SRCS
        "struct_and_definitions.cpp"
        "device_scanner.cpp"
        "hci_event_parser.cpp"
        "output_handler.cpp"
        "uart_controller.cpp"
        "rom_print_controller.cpp"
        "collector_utils.cpp"
        "device_interrogator.cpp"
        "device_database.cpp"
        "interrogator_event_loop.cpp"
        "console_print_controller.cpp"
        "mac_cache.cpp"
        "main.cpp"
        INCLUDE_DIRS "."
        )
# idf_component_register(SRCS "single-channel-advertiser.c" INCLUDE_DIRS ".")
# idf_component_register(SRCS "beeper.c" INCLUDE_DIRS ".")