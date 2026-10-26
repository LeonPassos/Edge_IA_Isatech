idf_component_register(
    SRCS 
    "main.c" 
    "servo/servo.c" 
    "mqtt/mqtt.c" 
    "comando_json.c" 
    "ultrasonic/ultrasonic.c"
    "wifi/wifi.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_driver_gpio esp_driver_ledc
                  esp_timer esp_wifi esp_netif esp_event nvs_flash mqtt json 
)
