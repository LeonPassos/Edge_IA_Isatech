#pragma once

#include <stdbool.h>
#include <driver/ledc.h>
#include "tipos.h"

// Substitui a ESP32Servo: PWM de 50 Hz pelo LEDC, 0 a 180 graus mapeados
// linearmente entre minUs e maxUs. Cada servo usa um canal LEDC exclusivo.
typedef struct {
  bool conectado;
  ledc_channel_t canal;
  int minUs, maxUs;
} Servo;

bool servoConectar(Servo *s, int pino, ledc_channel_t canal, int minUs, int maxUs);
void write_servo(Servo *s, int angulo);
esp_err_t servo_control_mqtt(Servo *s, int angulo, const char *nome);
