#include "servo/servo.h"

// Todos os servos compartilham o mesmo timer de 50 Hz.
static bool timer_config(void) {
  static bool configurado = false;
  if (configurado) {
    return true;
  }
  ledc_timer_config_t t = {0};
  t.speed_mode = MODO;
  t.duty_resolution = RESOLUCAO;
  t.timer_num = TIMER;
  t.freq_hz = FREQUENCIA_HZ;
  t.clk_cfg = LEDC_AUTO_CLK;
  configurado = ledc_timer_config(&t) == ESP_OK;

  return configurado;
}

void servo_Set_Angle(Servo *s, int angulo) {
  if (!s->conectado) return;
  if (angulo < 0) angulo = 0;
  if (angulo > 180) angulo = 180;
  const uint32_t us = s->minUs + (s->maxUs - s->minUs) * angulo / 180;
  const uint32_t duty = us * (1u << RESOLUCAO) / PERIODO_US;
  ledc_set_duty(MODO, s->canal, duty);
  ledc_update_duty(MODO, s->canal);
}

bool servo_init(Servo *s, int pino, ledc_channel_t canal, int minUs, int maxUs) {
  if (!timer_config()) return false;
  ledc_channel_config_t c = {0};
  c.gpio_num = pino;
  c.speed_mode = MODO;
  c.channel = canal;
  c.intr_type = LEDC_INTR_DISABLE;
  c.timer_sel = TIMER;
  c.duty = 0; // Sem pulso ate o primeiro servo_Set_Angle.
  if (ledc_channel_config(&c) != ESP_OK) return false;
  s->canal = canal;
  s->minUs = minUs;
  s->maxUs = maxUs;
  s->conectado = true;
  return true;
}

