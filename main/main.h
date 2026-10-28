#ifndef __MAIN_H__
#define __MAIN_H__

#include <stdint.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <esp_log.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <driver/gpio.h>
#include <esp_timer.h>

#include "ultrasonic/ultrasonic.h"
#include "comando_json.h"
#include "mqtt/mqtt.h"
#include "wifi/wifi.h"
#include "servo/servo.h"


// Configuracao da aplicacao ESP-IDF. Tempos em ms; pulsos em us.

// Topicos da aplicacao. O estado online/offline fica em MQTT_TOPICO_ESTADO.
#define TOPICO_COMANDO "isatech/comando" // i/iniciar, p/parar ou JSON
#define TOPICO_LOG     "isatech/log"     // mensagens do ciclo

// Bits de notificacao enviados a taskControle pelo MQTT.
// Se iniciar e parar estiverem pendentes juntos, parar prevalece.
#define CMD_INICIAR (1UL << 0)
#define CMD_PARAR   (1UL << 1)
#define CMD_JSON    (1UL << 2) // Ha comando novo em filaComandos.

#define SERVO_MIN_US 500
#define SERVO_MAX_US 2500

//duty cycle do PWM para 0 a 180 graus
#define SERVO_BASE_MIN_US 500
#define SERVO_BASE_MAX_US 2500

#define SERVO_OMBRO_MIN_US 1000
#define SERVO_OMBRO_MAX_US 2000

#define SERVO_COTOVELO_MIN_US 500
#define SERVO_COTOVELO_MAX_US 2500

#define SERVO_GARRA_MIN_US 500
#define SERVO_GARRA_MAX_US 2500

#define PIN_RELE GPIO_NUM_25
#define ATUALIZACAO_SERVO_MS 20
#define VELOCIDADE_GRAUS_S 70.0f
#define RELE_LIGADO 0
#define RELE_DESLIGADO (RELE_LIGADO == 0 ? 1 : 0)
#define MOVIMENTO_MS 1500
#define GARRA_MS 700
#define ESTEIRA_MS 12000
#define ACOMODAR_MS 700

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

typedef struct {
  Acao acao;
  uint8_t braco;
  Pose pose;
  uint32_t tempo;
  const char *mensagem;
} Etapa;


#endif // __MAIN_H__