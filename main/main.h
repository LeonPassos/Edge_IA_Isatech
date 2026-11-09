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
#define TOPICO_CARRINHO "isatech/carrinho"
// Last Will: o broker publica "offline" (retido) se a ESP sumir.
#define MQTT_TOPICO_ESTADO  "isatech/status"

// Bits de notificacao enviados a taskControle pelo MQTT.
// Se iniciar e parar estiverem pendentes juntos, parar prevalece.
#define CMD_INICIAR (1UL << 0)
#define CMD_PARAR   (1UL << 1)
#define CMD_JSON    (1UL << 2) // Ha comando novo em filaComandos.

#define SERVO_MIN_US 500
#define SERVO_MAX_US 2500

#define PIN_RELE GPIO_NUM_33
#define ATUALIZACAO_SERVO_MS 20
#define VELOCIDADE_GRAUS_S 60.0f
// Rele ativo em nivel baixo: IN em GND liga; IN em nivel alto desliga.
#define RELE_LIGADO 0
#define RELE_DESLIGADO 1
#define MOVIMENTO_MS 1500
#define GARRA_MS 700
#define ESTEIRA_MS 12000
#define ACOMODAR_MS 700

//#DEFINE ULTIMO PINO GPIO 32

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


#endif // __MAIN_H__
