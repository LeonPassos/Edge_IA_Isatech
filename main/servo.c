#include "servo.h"

#define MODO          LEDC_LOW_SPEED_MODE
#define TIMER         LEDC_TIMER_0
#define RESOLUCAO     LEDC_TIMER_14_BIT
#define FREQUENCIA_HZ 50
#define PERIODO_US    (1000000 / FREQUENCIA_HZ)

// Todos os servos compartilham o mesmo timer de 50 Hz.
static bool configurarTimer(void) {
  static bool configurado = false;
  if (configurado) return true;
  ledc_timer_config_t t = {0};
  t.speed_mode = MODO;
  t.duty_resolution = RESOLUCAO;
  t.timer_num = TIMER;
  t.freq_hz = FREQUENCIA_HZ;
  t.clk_cfg = LEDC_AUTO_CLK;
  configurado = ledc_timer_config(&t) == ESP_OK;
  return configurado;
}

bool servoConectar(Servo *s, int pino, ledc_channel_t canal, int minUs, int maxUs) {
  if (!configurarTimer()) return false;
  ledc_channel_config_t c = {0};
  c.gpio_num = pino;
  c.speed_mode = MODO;
  c.channel = canal;
  c.intr_type = LEDC_INTR_DISABLE;
  c.timer_sel = TIMER;
  c.duty = 0; // Sem pulso ate o primeiro write_servo.
  if (ledc_channel_config(&c) != ESP_OK) return false;
  s->canal = canal;
  s->minUs = minUs;
  s->maxUs = maxUs;
  s->conectado = true;
  return true;
}

void write_servo(Servo *s, int angulo) {
  if (!s->conectado) return;
  if (angulo < 0) angulo = 0;
  if (angulo > 180) angulo = 180;
  const uint32_t us = s->minUs + (s->maxUs - s->minUs) * angulo / 180;
  const uint32_t duty = us * (1u << RESOLUCAO) / PERIODO_US;
  ledc_set_duty(MODO, s->canal, duty);
  ledc_update_duty(MODO, s->canal);
}

esp_err_t servo_control_mqtt(Servo *s, int angulo, const char *nome) {
  char mensagem[64];

  if (!s->conectado) {
    ESP_LOGE("servo_control_mqtt", "Servo %s nao conectado", nome);
    return ESP_FAIL;
  }
  write_servo(s, angulo);

  //snprintf(mensagem, sizeof(mensagem), "%s: %d", nome, angulo);
  //publicarMqtt(mensagem);
  return ESP_OK;
}

