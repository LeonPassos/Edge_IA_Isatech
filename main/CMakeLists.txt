idf_component_register(
    SRCS 
    "main.c" 
    "servo/servo.c" 
    "mqtt/mqtt.c" 
    "ultrasonic/ultrasonic.c"
    "wifi/wifi.c"
    "comandos/comando_json.c"
    "joystick/joystick.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_driver_gpio esp_driver_ledc esp_adc
                  esp_timer esp_wifi esp_netif esp_event nvs_flash mqtt json 
)
