idf_component_register(
    SRCS 
    "main.c" 
    "servo/servo.c" 
    "mqtt/mqtt.c" 
    "ultrasonic/ultrasonic.c"
    "wifi/wifi.c"
    "comandos/comando_json.c"
    "joystick/joystick.c"
    "web/web.c"
    INCLUDE_DIRS "."
    EMBED_TXTFILES "web/index.html"
    PRIV_REQUIRES esp_driver_gpio esp_driver_ledc esp_adc esp_http_server
                  esp_timer esp_wifi esp_netif esp_event nvs_flash mqtt json 
)
