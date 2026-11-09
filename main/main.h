#ifndef __MAIN_H__
#define __MAIN_H__

#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <strings.h>
#include <stdatomic.h>
#include <assert.h>
#include <driver/gpio.h>
#include <driver/gpio.h>
#include <esp_mac.h>
#include <esp_timer.h>
#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <esp_log.h>

#include "comandos/comando_json.h"

//MQTT
#include "mqtt/mqtt.h"
#include <mqtt_client.h>

//Wifi
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include "wifi/wifi.h"
#include "nvs_flash.h"

//Ultrasonic
#include "ultrasonic/ultrasonic.h"

//Servo
#include <driver/ledc.h>
#include "servo/servo.h"

//JSON
#include <cJSON.h>

//Defines
#include "Defines.h"

// Coordenadas em graus. Servo posicional; nao usar servo de rotacao continua.
typedef struct {
  int base;
  int ombro;
  int cotovelo;
  int garra;
} Pose;

typedef enum {
    MOVER,
    GARRA,
    LIGAR_ESTEIRA,
    DESLIGAR_ESTEIRA,
} Acao;

typedef enum  {
  MOTIVO_SENSOR, 
  MOTIVO_COMANDO 
}cancellation_reason;

typedef struct {
  Acao acao;
  uint8_t braco;
  Pose pose;
  uint32_t tempo;
  const char *mensagem;
} Etapa;

typedef struct {
  esp_err_t erro;
  uint32_t distancia_cm;
  uint32_t instante_ms;
} LeituraSensor;

// ESP32 DevKit classica. Ordem: BASE, OMBRO, COTOVELO, GARRA.
typedef struct {
  const char *nome;
  int pino;
  int minimo;
  int maximo;
  int inicial;
} ServoCfg;

#endif // __MAIN_H__
