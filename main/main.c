#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <driver/gpio.h>
#include <esp_timer.h>
#include "servo/servo.h"
#include "app_config.h"
#include "ultrasonic/ultrasonic.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <esp_log.h>
#include "mqtt/mqtt.h"
#include "comando_json.h"
#include "wifi/wifi.h"
#include "main.h"

static const char *TAG = "main";

// ESP32 DevKit classica. Ordem: BASE, OMBRO, COTOVELO, GARRA.
typedef struct {
  const char *nome;
  int pino;
  int minimo;
  int maximo;
  int inicial;
} ServoCfg;

static const ServoCfg cfg[4] = {
  {"Base",     13, 0, 180, 90},
  {"Ombro",    14, 0, 180, 60},
  {"Cotovelo", 27, 0, 180, 90},
  {"Garra",    26, 0, 180, 60}
};
static const ServoCfg cfgSegundoBraco[4] = {
  {"Base",     -1, 0, 180, 90},
  {"Ombro",    -1, 0, 180, 90},
  {"Cotovelo", -1, 0, 180, 90},
  {"Garra",    -1, 0, 180, 90}
};
static const ServoCfg *const CONFIG_BRACOS[2] = {cfg, cfgSegundoBraco};
#define BRACOS_ATIVOS 1 // Teste atual: somente o primeiro braco.
static_assert(BRACOS_ATIVOS >= 1 && BRACOS_ATIVOS <= 2, "Use um ou dois bracos");
static_assert(BRACOS_ATIVOS * 4 <= LEDC_CHANNEL_MAX, "Canais LEDC insuficientes");

// EXEMPLOS: calibrar todos os angulos para a geometria real, sem carga.
// Coordenadas em graus. Servo posicional; nao usar servo de rotacao continua.
typedef struct {
  int base;
  int ombro;
  int cotovelo;
  int garra;
} Pose;

// Neste exemplo, abrir reduz o angulo. Calibrar em passos de 5 graus.
#define ABERTA_1 30
#define FECHADA_1 120
#define ABERTA_2 70
#define FECHADA_2 110
// Base, ombro e cotovelo de cada posicao; a garra vem em cada etapa.
#define REPOUSO          90,  90, 90
#define ACIMA_ENTRADA    30,  90, 90
#define PEGA_ENTRADA     30, 120, 60
#define ACIMA_ESTEIRA_1 150,  90, 90
#define ENTREGA_ESTEIRA 150, 120, 60
#define ACIMA_ESTEIRA_2  30,  90, 90
#define PEGA_ESTEIRA     30, 120, 60
#define ACIMA_SAIDA     150,  90, 90
#define ENTREGA_SAIDA   150, 120, 60

// Depois do app_main, somente taskControle acessa atuadores e estado da sequencia.
#define PERIODO_CONTROLE_MS 10
static TaskHandle_t controleHandle = NULL;
static QueueHandle_t filaMensagens = NULL;
static QueueHandle_t filaComandos = NULL;
static QueueHandle_t filaSensor = NULL;

#define MAX_DISTANCE_CM 50
#define LIMITE_DISTANCIA 12
#define TRIGGER_GPIO 5
#define ECHO_GPIO 18
#define PERIODO_SENSOR_MS 100
#define VALIDADE_SENSOR_MS 500

typedef struct {
  esp_err_t erro;
  uint32_t distancia_cm;
  uint32_t instante_ms;
} LeituraSensor;

// Estado acessado apenas por taskControle; o sensor envia copias pela fila.
static LeituraSensor ultimaLeitura = {.erro = ESP_ERR_INVALID_STATE};
static bool leituraRecebida = false;
static bool esteiraLigada = false;

// Tempo monotonicamente crescente desde o boot, fornecido pelo esp_timer.
static uint32_t tempoAtualMs(void) {
  return (uint32_t)(esp_timer_get_time() / 1000);
}

// Converte ms em ticks, com no minimo 1 tick.
static TickType_t ticks(uint32_t ms) {
  const TickType_t t = pdMS_TO_TICKS(ms);
  return t > 0 ? t : 1;
}

// Apenas literais estaticos sao enviados: os ponteiros continuam validos.
// Log nunca bloqueia o controle; fila cheia descarta a mensagem.
static void registrar(const char *mensagem) {
  if (filaMensagens != NULL) xQueueSend(filaMensagens, &mensagem, 0);
  // publicarMqtt(mensagem);
}

static Servo servos[2][4];
static bool done = false;
static bool executando = false;
static size_t proximaEtapa = 0;
static uint32_t inicio = 0, espera = 0;
static int posicao[2][4] = {0};
static int origem[4] = {0}, destino[4] = {0};
static bool emMovimento = false;
static uint8_t bracoMovendo = 0;
static uint32_t inicioMovimento = 0, duracaoMovimento = 0, ultimaAtualizacao = 0;

typedef enum {
    MOVER,
    GARRA,
    LIGAR_ESTEIRA,
    DESLIGAR_ESTEIRA
} Acao;

typedef struct {
  Acao acao;
  uint8_t braco;
  Pose pose;
  uint32_t tempo;
  const char *mensagem;
} Etapa;

// O braco que nao recebe comando mantem sua ultima posicao.
static const Etapa SEQUENCIA[] = {
  {MOVER, 0, {REPOUSO, ABERTA_1}, MOVIMENTO_MS, "Braco 1: repouso."},
  {MOVER, 1, {REPOUSO, ABERTA_2}, MOVIMENTO_MS, "Braco 2: repouso."},
  {MOVER, 0, {ACIMA_ENTRADA, ABERTA_1}, MOVIMENTO_MS, "Braco 1: acima do cubo."},
  {MOVER, 0, {PEGA_ENTRADA, ABERTA_1}, MOVIMENTO_MS, "Braco 1: descer para pegar."},
  {GARRA, 0, {0, 0, 0, FECHADA_1}, GARRA_MS, "Braco 1: fechar garra."},
  {MOVER, 0, {ACIMA_ENTRADA, FECHADA_1}, MOVIMENTO_MS, "Braco 1: levantar cubo."},
  {MOVER, 0, {ACIMA_ESTEIRA_1, FECHADA_1}, MOVIMENTO_MS, "Braco 1: acima da esteira."},
  {MOVER, 0, {ENTREGA_ESTEIRA, FECHADA_1}, MOVIMENTO_MS, "Braco 1: baixar cubo."},
  {GARRA, 0, {0, 0, 0, ABERTA_1}, GARRA_MS, "Braco 1: soltar cubo."},
  {MOVER, 0, {ACIMA_ESTEIRA_1, ABERTA_1}, MOVIMENTO_MS, "Braco 1: levantar vazio."},
  {MOVER, 0, {REPOUSO, ABERTA_1}, MOVIMENTO_MS, "Braco 1: liberar esteira."},
  {LIGAR_ESTEIRA, 0, {0, 0, 0, 0}, ESTEIRA_MS, "Esteira ligada."},
  {DESLIGAR_ESTEIRA, 0, {0, 0, 0, 0}, ACOMODAR_MS, "Aguardando parada da esteira."},
  {MOVER, 1, {ACIMA_ESTEIRA_2, ABERTA_2}, MOVIMENTO_MS, "Braco 2: acima do cubo."},
  {MOVER, 1, {PEGA_ESTEIRA, ABERTA_2}, MOVIMENTO_MS, "Braco 2: descer para pegar."},
  {GARRA, 1, {0, 0, 0, FECHADA_2}, GARRA_MS, "Braco 2: fechar garra."},
  {MOVER, 1, {ACIMA_ESTEIRA_2, FECHADA_2}, MOVIMENTO_MS, "Braco 2: levantar cubo."},
  {MOVER, 1, {ACIMA_SAIDA, FECHADA_2}, MOVIMENTO_MS, "Braco 2: acima da saida."},
  {MOVER, 1, {ENTREGA_SAIDA, FECHADA_2}, MOVIMENTO_MS, "Braco 2: baixar cubo."},
  {GARRA, 1, {0, 0, 0, ABERTA_2}, GARRA_MS, "Braco 2: soltar cubo."},
  {MOVER, 1, {ACIMA_SAIDA, ABERTA_2}, MOVIMENTO_MS, "Braco 2: levantar vazio."},
  {MOVER, 1, {REPOUSO, ABERTA_2}, MOVIMENTO_MS, "Braco 2: retornar."}
};
#define TOTAL_ETAPAS (sizeof(SEQUENCIA) / sizeof(SEQUENCIA[0]))

static void pararEsteira(void) {
  gpio_set_level(PIN_RELE, RELE_DESLIGADO);
  esteiraLigada = false;
}

static void iniciarEsteira(void) {
  gpio_set_level(PIN_RELE, RELE_LIGADO);
  esteiraLigada = true;
}

static bool sensorValido(void) {
  return leituraRecebida && ultimaLeitura.erro == ESP_OK && (uint32_t)(tempoAtualMs() - ultimaLeitura.instante_ms) <= VALIDADE_SENSOR_MS;
}

static bool controleOcupado(void) {
  return executando || emMovimento;
}

// Para esteira, ciclo e movimento, e descarta comandos JSON pendentes.
static void cancelar(const char *motivo) {
  pararEsteira();
  executando = false;
  emMovimento = false;
  xQueueReset(filaComandos);
  registrar(motivo);
}

static void cancelarPorSensor(void) {
  cancelar("Ciclo cancelado: sensor sem leitura valida. Confira o sensor e envie iniciar.");
}

static void atualizarSensor(void) {
  LeituraSensor leitura;
  if (xQueueReceive(filaSensor, &leitura, 0) == pdTRUE) {
    ultimaLeitura = leitura;
    leituraRecebida = true;
  }
  if (!esteiraLigada) return;
  if (!sensorValido()) {
    cancelarPorSensor();
  } else if (ultimaLeitura.distancia_cm < LIMITE_DISTANCIA) {
    pararEsteira();
    // Antecipar a etapa DESLIGAR_ESTEIRA, que conta o tempo de acomodacao.
    espera = 0;
    registrar("Objeto detectado a menos de 12 cm: esteira parada.");
  }
}

static void mover(uint8_t braco, const Pose *p) {
  const int alvo[4] = {p->base, p->ombro, p->cotovelo, p->garra};
  int maiorDeslocamento = 0;

  for (uint8_t eixo = 0; eixo < 4; ++eixo) {
    origem[eixo] = posicao[braco][eixo];
    const ServoCfg *c = &CONFIG_BRACOS[braco][eixo];
    int d = alvo[eixo];
    if (d < c->minimo) d = c->minimo;
    if (d > c->maximo) d = c->maximo;
    destino[eixo] = d;
    const int deslocamento = abs(destino[eixo] - origem[eixo]);
    if (deslocamento > maiorDeslocamento) maiorDeslocamento = deslocamento;
  }

  bracoMovendo = braco;
  // Smoothstep tem velocidade maxima 1,5 vezes a velocidade media.
  duracaoMovimento = (uint32_t)(1500.0f * maiorDeslocamento / VELOCIDADE_GRAUS_S);
  if (duracaoMovimento < 1) duracaoMovimento = 1;
  inicioMovimento = tempoAtualMs();
  ultimaAtualizacao = inicioMovimento;
  emMovimento = true;
}

static void atualizarMovimento(void) {
  if (!emMovimento) return;
  const uint32_t agora = tempoAtualMs();
  const uint32_t decorrido = agora - inicioMovimento;
  if (decorrido < duracaoMovimento && agora - ultimaAtualizacao < ATUALIZACAO_SERVO_MS) return;
  ultimaAtualizacao = agora;
  const float t = decorrido >= duracaoMovimento ? 1.0f : (float)decorrido / duracaoMovimento;
  const float suave = t * t * (3.0f - 2.0f * t);
  for (uint8_t eixo = 0; eixo < 4; ++eixo) {
    const int angulo = (int)lroundf(origem[eixo] + (destino[eixo] - origem[eixo]) * suave);
    posicao[bracoMovendo][eixo] = angulo;
    servoDefinirAngulo(&servos[bracoMovendo][eixo], angulo);
  }
  if (decorrido >= duracaoMovimento) {
    emMovimento = false;
    inicio = agora; // Tempo de acomodacao contado depois do movimento.
  }
}

static void atualizar(void) {
  if (!executando || emMovimento || (uint32_t)(tempoAtualMs() - inicio) < espera) return;
  while (proximaEtapa < TOTAL_ETAPAS && (SEQUENCIA[proximaEtapa].acao == MOVER || SEQUENCIA[proximaEtapa].acao == GARRA) && SEQUENCIA[proximaEtapa].braco >= BRACOS_ATIVOS) ++proximaEtapa;
  // if (proximaEtapa >= TOTAL_ETAPAS) {
  //   pararEsteira();
  //   executando = false;
  //   registrar("Ciclo concluido. Envie i para repetir.");
  //   return;
  // }
  const Etapa *e = &SEQUENCIA[proximaEtapa++];
  switch (e->acao) {
    case MOVER: mover(e->braco, &e->pose); break;
    case GARRA:
      mover(e->braco, &(Pose){posicao[e->braco][0], posicao[e->braco][1], posicao[e->braco][2], e->pose.garra});
      break;
    case LIGAR_ESTEIRA:
      if (!sensorValido()) {
        cancelarPorSensor();
        return;
      }
      if (ultimaLeitura.distancia_cm < LIMITE_DISTANCIA) {
        pararEsteira();
        espera = 0;
        registrar("Objeto ja detectado a menos de 12 cm: esteira permanece parada.");
        return;
      }
      gpio_set_level(PIN_RELE, RELE_LIGADO);
      esteiraLigada = true;
      break;
    case DESLIGAR_ESTEIRA: pararEsteira(); break;
  }
  registrar(e->mensagem);
  inicio = tempoAtualMs();
  espera = e->tempo;
}

esp_err_t config_pins(void){

  esp_err_t err;

  ultrasonic_sensor_t sensor = { 
      .trigger_pin = TRIGGER_GPIO,
      .echo_pin = ECHO_GPIO
  };

  if(ultrasonic_init(&sensor) != ESP_OK) {
    ESP_LOGE(TAG, "Falha ao iniciar sensor ultrassonico.");
  }




  // Nivel definido antes de habilitar a saida: o rele nao pulsa no boot.
  gpio_set_level(PIN_RELE, RELE_DESLIGADO);
  gpio_config_t io = {0};
  io.pin_bit_mask = 1ULL << PIN_RELE;
  io.mode = GPIO_MODE_OUTPUT;
  io.pull_up_en = GPIO_PULLUP_DISABLE;
  io.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&io);
  pararEsteira();


  return ESP_OK;
}

static void executarComandoJson(const ComandoJson *comando) {
  
  if (comando->braco >= BRACOS_ATIVOS) {
    registrar("Braco solicitado nao esta ativo.");
    return;
  }

  if (controleOcupado()) {
    registrar("Controle ocupado. Aguarde terminar ou envie parar.");
    return;
  }

  const uint8_t b = comando->braco;
  int alvo[4] = {posicao[b][0], posicao[b][1], posicao[b][2], posicao[b][3]};
  switch (comando->acao) {
    case JSON_EIXO: alvo[comando->eixo] = comando->angulo; break;
    case JSON_ABRIR_GARRA: alvo[3] = b == 0 ? ABERTA_1 : ABERTA_2; break;
    case JSON_FECHAR_GARRA: alvo[3] = b == 0 ? FECHADA_1 : FECHADA_2; break;
    case JSON_REPOUSO: {
      const Pose repouso = {REPOUSO, b == 0 ? ABERTA_1 : ABERTA_2};
      alvo[0] = repouso.base;
      alvo[1] = repouso.ombro;
      alvo[2] = repouso.cotovelo;
      alvo[3] = repouso.garra;
      break;
    }
    default: return;
  }
  for (uint8_t eixo = 0; eixo < 4; ++eixo) {
    const ServoCfg *c = &CONFIG_BRACOS[b][eixo];
    if (alvo[eixo] < c->minimo || alvo[eixo] > c->maximo) {
      registrar("Angulo fora dos limites configurados para o servo.");
      return;
    }
  }
  const Pose pose = {alvo[0], alvo[1], alvo[2], alvo[3]};
  mover(b, &pose);
  registrar("Comando JSON: movimento iniciado.");
}

static void iniciarCiclo(void) {
  if (controleOcupado()) {
    registrar("Controle ocupado. Aguarde terminar ou envie parar.");
    return;
  }
  pararEsteira();
  proximaEtapa = 0;
  espera = 0;
  inicio = tempoAtualMs();
  executando = true;
}

// Limite por periodo evita que uma rajada de comandos atrase o controle.
static void processarComandosJson(void) {
  ComandoJson comando;
  for (uint8_t n = 0; n < 8 && xQueueReceive(filaComandos, &comando, 0) == pdTRUE; ++n) {
    executarComandoJson(&comando);
  }
}

static void taskControle(void *parameter) {
  (void)parameter;
  TickType_t ultimaExecucao = xTaskGetTickCount();
  const TickType_t periodo = ticks(PERIODO_CONTROLE_MS);

  for (;;) {
    uint32_t comandos = 0;
    xTaskNotifyWait(0, UINT32_MAX, &comandos, 0);

    // Se i e p chegarem no mesmo periodo, p prevalece.
    if (comandos & CMD_PARAR) {
      cancelar("Cancelado. Reposicione bracos e cubo antes de reiniciar.");
    } else {
      if (comandos & CMD_INICIAR) iniciarCiclo();
      processarComandosJson();
    }

    atualizarSensor();

    const bool estavaMovendo = emMovimento;
    atualizarMovimento();
    const bool comandoAvulsoConcluido = estavaMovendo && !emMovimento && !executando;
    if (comandoAvulsoConcluido) registrar("Comando JSON: movimento concluido.");

    atualizar();
    vTaskDelayUntil(&ultimaExecucao, periodo);
  }
}

// Impressao fica fora do controle para que a Serial nao atrase os servos.
static void taskLog(void *parameter) {
  (void)parameter;
  const char *mensagem = NULL;
  for (;;) {
    if (xQueueReceive(filaMensagens, &mensagem, portMAX_DELAY) == pdTRUE) puts(mensagem);
  }
}

// Esta tarefa somente mede. Nunca escreve no rele nem no estado do ciclo.
static void task_esteira_with_ultrasonic(void *parameter) {
  
  const ultrasonic_sensor_t sensor = {
    .trigger_pin = TRIGGER_GPIO,
    .echo_pin = ECHO_GPIO
  };
  const esp_err_t inicializacao = ultrasonic_init(&sensor);
  if (inicializacao != ESP_OK) {
    const LeituraSensor leitura = {
      .erro = inicializacao, .instante_ms = tempoAtualMs()
    };
    xQueueOverwrite(filaSensor, &leitura);
    ESP_LOGE("ultrasonic", "Falha ao iniciar sensor: %s", esp_err_to_name(inicializacao));
    vTaskDelete(NULL);
    return;
  }
  for (;;) {
    LeituraSensor leitura = {0};
    leitura.erro = ultrasonic_measure_cm(&sensor, MAX_DISTANCE_CM, &leitura.distancia_cm);
    leitura.instante_ms = tempoAtualMs();
    xQueueOverwrite(filaSensor, &leitura);
    vTaskDelay(ticks(PERIODO_SENSOR_MS));
  }
}

void app_main(void) {

  // Primeiro: cria o loop de eventos usado pelo MQTT.
  wifi_init_sta();

  // bool done = true;
  
  // config_pins();

  // // Validar antes de conectar qualquer servo.
  // for (uint8_t b = 0; b < BRACOS_ATIVOS; ++b) {
  //   for (uint8_t eixo = 0; eixo < 4; ++eixo) {
  //     const ServoCfg *c = &CONFIG_BRACOS[b][eixo];
  //     if (c->pino < 0 || !GPIO_IS_VALID_OUTPUT_GPIO(c->pino) || c->pino == PIN_RELE ||
  //         c->minimo < 0 || c->maximo > 180 || c->minimo > c->maximo ||
  //         c->inicial < c->minimo || c->inicial > c->maximo) done = false;
  //     for (uint8_t anterior = 0; anterior < b * 4 + eixo; ++anterior) {
  //       if (c->pino == CONFIG_BRACOS[anterior / 4][anterior % 4].pino) done = false;
  //     }
  //   }
  // }
  // if (!done) {
  //   ESP_LOGE(TAG, "Configuracao invalida: confira pinos exclusivos, limites e posicoes iniciais.");
  //   return;
  // }

  // for (uint8_t b = 0; b < BRACOS_ATIVOS; ++b) {
  //   for (uint8_t eixo = 0; eixo < 4; ++eixo) {
  //     const ServoCfg *c = &CONFIG_BRACOS[b][eixo];
  //     if (!servoConectar(&servos[b][eixo], c->pino, (ledc_channel_t)(b * 4 + eixo), SERVO_MIN_US, SERVO_MAX_US)) done = false;
  //     // Sem sensor, a primeira referencia e o angulo inicial configurado.
  //     posicao[b][eixo] = c->inicial;
  //     servoDefinirAngulo(&servos[b][eixo], c->inicial);
  //   }
  // }
  // if (!done) {
  //   ESP_LOGE(TAG, "Falha ao conectar servos; inicio bloqueado.");
  //   return;
  // }

  filaMensagens = xQueueCreate(24, sizeof(const char *));
  filaComandos = xQueueCreate(8, sizeof(ComandoJson));
  filaSensor = xQueueCreate(1, sizeof(LeituraSensor));
  if (filaMensagens == NULL || filaComandos == NULL || filaSensor == NULL) {
    if (filaMensagens) vQueueDelete(filaMensagens);
    if (filaComandos) vQueueDelete(filaComandos);
    if (filaSensor) vQueueDelete(filaSensor);
    filaSensor = NULL;
    filaMensagens = filaComandos = NULL;
    pararEsteira();
    ESP_LOGE(TAG, "Falha ao criar fila RTOS. Reinicie a ESP32.");
    return;
  }

  if (xTaskCreate(taskControle, "Controle", 4096, NULL, 2, &controleHandle) != pdPASS) {
    vQueueDelete(filaMensagens);
    vQueueDelete(filaComandos);
    vQueueDelete(filaSensor);
    filaSensor = NULL;
    filaComandos = NULL;
    filaMensagens = NULL;
    pararEsteira();
    ESP_LOGE(TAG, "Falha ao criar tarefa de controle. Reinicie a ESP32.");
    return;
  }

  // Sem esta tarefa as mensagens ainda saem pelo MQTT; a fila cheia apenas descarta.
  if (xTaskCreate(taskLog, "Log", 4096, NULL, 1, NULL) != pdPASS) {
    ESP_LOGE(TAG, "Falha ao criar tarefa de log; mensagens somente por MQTT.");
  }

  // MQTT conecta quando o Wi-Fi recebe IP. E a unica entrada de comandos.

  mqtt_init(&(mqtt_config_t){
    .uri = MQTT_BROKER_URI,
    .client_id = NULL, // Gera "esp32-<MAC>"
    .keepalive_s = 120,
    .reconexao_ms = 10000,
    .prioridade_tarefa = 5,
    .lwt_topico = "isatech/status",
    .lwt_mensagem = "offline",
    .lwt_qos = 0,
    .lwt_retain = true,
    .ctx = NULL
  });

  mqtt_start();
  // if (xTaskCreate(task_esteira_with_ultrasonic, "EsteiraUltrasonic", 4096, NULL, 1, NULL) == pdPASS) {
  //   ESP_LOGE(TAG, "Task criada com sucesso.");
  // }


  // Ao retornar, a tarefa main do IDF e encerrada; o trabalho fica nas tarefas.
}
