#pragma once

// Configuracao da aplicacao ESP-IDF. Tempos em ms; pulsos em us.

// Bits de notificacao enviados a taskControle pelo MQTT.
// Se iniciar e parar estiverem pendentes juntos, parar prevalece.
#define CMD_INICIAR (1UL << 0)
#define CMD_PARAR   (1UL << 1)

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

