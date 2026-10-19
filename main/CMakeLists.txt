idf_component_register(SRCS "badge_main.c" "gb_core.c" "badge_led.c" "battle_fx.c"
    INCLUDE_DIRS "." "../vendor"
    REQUIRES esp_driver_gpio esp_driver_spi esp_driver_usb_serial_jtag
             esp_lcd esp_partition spi_flash esp_timer vfs esp_driver_rmt)
target_compile_options(${COMPONENT_LIB} PRIVATE -O3 -fno-strict-aliasing)
# Core runs in a normal task with flash cache available, never in an ISR.
set_source_files_properties(gb_core.c PROPERTIES COMPILE_OPTIONS
    "-Wno-unused-function;-fjump-tables;-ftree-switch-conversion")
