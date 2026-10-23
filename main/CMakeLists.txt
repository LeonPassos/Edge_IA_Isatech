idf_component_register(SRCS "main.c" "servo.c" "mqtt.c" 
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES esp_driver_gpio esp_driver_ledc esp_driver_uart
                                     esp_timer esp_wifi esp_netif esp_event nvs_flash mqtt json)
