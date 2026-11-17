#include "joystick/joystick.h"
#include <esp_adc/adc_oneshot.h>

static const char *TAG = "joystick";

static adc_oneshot_unit_handle_t adc = NULL;
static adc_channel_t canalX, canalY;
static int centroX = JOY_ADC_MAX / 2, centroY = JOY_ADC_MAX / 2;

static esp_err_t lerMedia(adc_channel_t canal, uint8_t amostras, int *media) {
  int soma = 0;
  for (uint8_t i = 0; i < amostras; ++i) {
    int valor = 0;
    const esp_err_t erro = adc_oneshot_read(adc, canal, &valor);
    if (erro != ESP_OK) return erro;
    soma += valor;
  }
  *media = soma / amostras;
  return ESP_OK;
}

// Converte a leitura bruta para -100..100 em relacao ao centro calibrado.
// Fora da zona morta, a escala recomeca do zero para o movimento ser suave.
static int normalizar(int bruto, int centro, bool inverter) {
  int v = 0;
  if (bruto >= centro && centro < JOY_ADC_MAX) {
    v = (bruto - centro) * 100 / (JOY_ADC_MAX - centro);
  } else if (bruto < centro && centro > 0) {
    v = -(centro - bruto) * 100 / centro;
  }
  if (v > 100) v = 100;
  if (v < -100) v = -100;

  const int modulo = abs(v);
  if (modulo <= JOY_ZONA_MORTA) return 0;
  v = (v > 0 ? 1 : -1) * (modulo - JOY_ZONA_MORTA) * 100 / (100 - JOY_ZONA_MORTA);
  return inverter ? -v : v;
}

static esp_err_t configurarCanal(int pino, adc_channel_t *canal) {
  adc_unit_t unidade;
  esp_err_t erro = adc_oneshot_io_to_channel(pino, &unidade, canal);
  if (erro != ESP_OK || unidade != ADC_UNIT_1) {
    ESP_LOGE(TAG, "GPIO %d nao e um pino do ADC1 (use 32..39).", pino);
    return ESP_ERR_INVALID_ARG;
  }
  const adc_oneshot_chan_cfg_t cfg = {
    .atten = ADC_ATTEN_DB_12, // Faixa completa ate ~3,1 V.
    .bitwidth = ADC_BITWIDTH_12,
  };
  return adc_oneshot_config_channel(adc, *canal, &cfg);
}

esp_err_t joystick_init(void) {
  const adc_oneshot_unit_init_cfg_t unidade = {
    .unit_id = ADC_UNIT_1,
    .ulp_mode = ADC_ULP_MODE_DISABLE,
  };
  esp_err_t erro = adc_oneshot_new_unit(&unidade, &adc);
  if (erro != ESP_OK) return erro;

  erro = configurarCanal(JOY_PINO_X, &canalX);
  if (erro == ESP_OK) erro = configurarCanal(JOY_PINO_Y, &canalY);
  if (erro != ESP_OK) return erro;

  // Botao fecha para GND; pull-up interno mantem nivel alto solto.
  gpio_config_t io = {0};
  io.pin_bit_mask = 1ULL << JOY_PINO_SW;
  io.mode = GPIO_MODE_INPUT;
  io.pull_up_en = GPIO_PULLUP_ENABLE;
  io.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io.intr_type = GPIO_INTR_DISABLE;
  erro = gpio_config(&io);
  if (erro != ESP_OK) return erro;

  erro = lerMedia(canalX, JOY_AMOSTRAS_CALIBRACAO, &centroX);
  if (erro == ESP_OK) erro = lerMedia(canalY, JOY_AMOSTRAS_CALIBRACAO, &centroY);
  if (erro != ESP_OK) return erro;

  ESP_LOGI(TAG, "Centro calibrado: X=%d, Y=%d.", centroX, centroY);
  return ESP_OK;
}

esp_err_t joystick_ler(JoystickLeitura *leitura) {
  if (adc == NULL || leitura == NULL) return ESP_ERR_INVALID_STATE;
  int brutoX = 0, brutoY = 0;
  esp_err_t erro = lerMedia(canalX, JOY_AMOSTRAS_LEITURA, &brutoX);
  if (erro == ESP_OK) erro = lerMedia(canalY, JOY_AMOSTRAS_LEITURA, &brutoY);
  if (erro != ESP_OK) return erro;

  leitura->x = normalizar(brutoX, centroX, JOY_INVERTER_X);
  leitura->y = normalizar(brutoY, centroY, JOY_INVERTER_Y);
  leitura->pressionado = gpio_get_level(JOY_PINO_SW) == 0;
  return ESP_OK;
}

JoystickEvento joystick_evento_botao(bool pressionado, uint32_t agora_ms) {
  static bool estavel = false, ultimoBruto = false;
  static uint32_t mudouEm = 0, soltouEm = 0;
  static uint8_t cliques = 0;

  if (pressionado != ultimoBruto) {
    ultimoBruto = pressionado;
    mudouEm = agora_ms;
  }

  // Estado so muda depois de ficar estavel por JOY_DEBOUNCE_MS.
  if (pressionado != estavel && (uint32_t)(agora_ms - mudouEm) >= JOY_DEBOUNCE_MS) {
    estavel = pressionado;
    if (!estavel) { // Clique conta ao soltar.
      soltouEm = agora_ms;
      if (++cliques >= 2) {
        cliques = 0;
        return JOY_DUPLO_CLIQUE;
      }
    }
  }

  if (cliques == 1 && !estavel && (uint32_t)(agora_ms - soltouEm) >= JOY_DUPLO_CLIQUE_MS) {
    cliques = 0;
    return JOY_CLIQUE;
  }
  return JOY_SEM_EVENTO;
}
