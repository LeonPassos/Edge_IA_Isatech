idf_component_register(
    SRCS ultrasonic.c
    INCLUDE_DIRS .
    REQUIRES esp_driver_gpio
    PRIV_REQUIRES freertos esp_timer esp_rom
)
