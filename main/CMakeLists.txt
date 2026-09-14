set(
    srcs
    "main.c"
    "display_core.c"
    "link_core.c"
    "ui/images.c"
    "ui/screens.c"
    "ui/styles.c"
    "ui/ui.c"
)

set(
    includes
    "."
    "ui"
)


idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS ${includes}
    REQUIRES
        driver
        esp_driver_gpio
        esp_timer
        nvs_flash
        esp_wifi
        esp_event
        config
        display
        usb_proto
        now_connect
        neopixel
        lvgl
)
