#ifndef SERVO_H
#define SERVO_H

#include "../main.h"

//defines apenas para os controles do servo
#define MODO          LEDC_LOW_SPEED_MODE
#define TIMER         LEDC_TIMER_0
#define RESOLUCAO     LEDC_TIMER_14_BIT
#define FREQUENCIA_HZ 50
#define PERIODO_US    (1000000 / FREQUENCIA_HZ)

// PWM de 50 Hz pelo periferico LEDC, com 0 a 180 graus mapeados
// linearmente entre minUs e maxUs. Cada servo usa um canal LEDC exclusivo.
typedef struct {
  bool conectado;
  ledc_channel_t canal;
  int minUs, maxUs;
} Servo;

bool servo_init(Servo *s, int pino, ledc_channel_t canal, int minUs, int maxUs);
void servo_Set_Angle(Servo *s, int angulo);

#endif // SERVO_H