#pragma once

#include <stdbool.h>
#include <driver/ledc.h>
#include <esp_err.h>

// PWM de 50 Hz pelo periferico LEDC, com 0 a 180 graus mapeados
// linearmente entre minUs e maxUs. Cada servo usa um canal LEDC exclusivo.
typedef struct {
  bool conectado;
  ledc_channel_t canal;
  int minUs, maxUs;
} Servo;

bool servoConectar(Servo *s, int pino, ledc_channel_t canal, int minUs, int maxUs);
void servoDefinirAngulo(Servo *s, int angulo);
esp_err_t servo_control_base(Servo *s, int angulo);
esp_err_t servo_control_ombro(Servo *s, int angulo);
esp_err_t servo_control_cotovelo(Servo *s, int angulo);
esp_err_t servo_control_garra(Servo *s, int angulo);
