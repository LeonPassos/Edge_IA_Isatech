#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <driver/gpio.h>
#include <driver/uart.h>
#include <esp_timer.h>
#include "servo.h"
#include "tipos.h"
#include "mqtt.h"

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
  {"Garra",    26, 0, 180, 135}
};
static const ServoCfg cfgSegundoBraco[4] = {
  {"Base",     -1, 0, 180, 90},
  {"Ombro",    -1, 0, 180, 90},
  {"Cotovelo", -1, 0, 180, 90},
  {"Garra",    -1, 0, 180, 90}
};
static const ServoCfg *const CONFIG_BRACOS[2] = {cfg, cfgSegundoBraco};
#define BRACOS_ATIVOS 1 // Teste atual: somente o primeiro braco.
// Preencher cfgSegundoBraco com GPIOs livres antes de ativar dois bracos.
static_assert(BRACOS_ATIVOS >= 1 && BRACOS_ATIVOS <= 2, "Use um ou dois bracos");
// Canal LEDC de cada servo: braco * 4 + eixo (o ESP32 tem 8 canais low-speed).
static_assert(BRACOS_ATIVOS * 4 <= LEDC_CHANNEL_MAX, "Canais LEDC insuficientes");
#define PIN_RELE GPIO_NUM_25 // GPIO 27 ja pertence ao cotovelo.
#define ATUALIZACAO_SERVO_MS 20
#define VELOCIDADE_GRAUS_S 70.0f // Menor = mais lento; deve ser positiva.
#define RELE_LIGADO 0 // 1 para modulo ativo alto.
#define RELE_DESLIGADO (RELE_LIGADO == 0 ? 1 : 0)
#define MOVIMENTO_MS 1500
#define GARRA_MS 700
#define ESTEIRA_MS 3000
#define ACOMODAR_MS 700

// Console (monitor serial) no UART0, como o Serial do Arduino.
#define UART_CONSOLE UART_NUM_0
#define UART_RX_BUFFER 256

// EXEMPLOS: calibrar todos os angulos para a geometria real, sem carga.
// Coordenadas em graus. Servo posicional; nao usar servo de rotacao continua.
typedef struct { int base, ombro, cotovelo, garra; } Pose;
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

// Equivalente ao millis() do Arduino (esp_timer conta desde o boot).
static uint32_t millis(void) {
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
  publicarMqtt(mensagem);
}

static Servo servos[2][4];
static bool pronto = false;
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
  inicioMovimento = millis();
  ultimaAtualizacao = inicioMovimento;
  emMovimento = true;
}

static void atualizarMovimento(void) {
  if (!emMovimento) return;
  const uint32_t agora = millis();
  const uint32_t decorrido = agora - inicioMovimento;
  if (decorrido < duracaoMovimento && agora - ultimaAtualizacao < ATUALIZACAO_SERVO_MS) return;
  ultimaAtualizacao = agora;
  const float t = decorrido >= duracaoMovimento ? 1.0f : (float)decorrido / duracaoMovimento;
  const float suave = t * t * (3.0f - 2.0f * t);
  for (uint8_t eixo = 0; eixo < 4; ++eixo) {
    const int angulo = (int)lroundf(origem[eixo] + (destino[eixo] - origem[eixo]) * suave);
    posicao[bracoMovendo][eixo] = angulo;
    write_servo(&servos[bracoMovendo][eixo], angulo);
  }
  if (decorrido >= duracaoMovimento) {
    emMovimento = false;
    inicio = agora; // Tempo de acomodacao contado depois do movimento.
  }
}

static void atualizar(void) {
  if (!executando || emMovimento || (uint32_t)(millis() - inicio) < espera) return;
  while (proximaEtapa < TOTAL_ETAPAS && (SEQUENCIA[proximaEtapa].acao == MOVER || SEQUENCIA[proximaEtapa].acao == GARRA) && SEQUENCIA[proximaEtapa].braco >= BRACOS_ATIVOS) ++proximaEtapa;
  if (proximaEtapa >= TOTAL_ETAPAS) {
    pararEsteira();
    executando = false;
    registrar("Ciclo concluido. Envie i para repetir.");
    return;
  }
  const Etapa *e = &SEQUENCIA[proximaEtapa++];
  switch (e->acao) {
    case MOVER: mover(e->braco, &e->pose); break;
    case GARRA:
      mover(e->braco, &(Pose){posicao[e->braco][0], posicao[e->braco][1], posicao[e->braco][2], e->pose.garra});
      break;
    case LIGAR_ESTEIRA: gpio_set_level(PIN_RELE, RELE_LIGADO); break;
    case DESLIGAR_ESTEIRA: pararEsteira(); break;
  }
  registrar(e->mensagem);
  inicio = millis();
  espera = e->tempo;
}

static void config_pins(void){

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
}

// A notificacao transporta comandos sem compartilhar variaveis de controle.
// Se i e p chegarem no mesmo periodo, p prevalece.
static void taskControle(void *parameter) {
  (void)parameter;
  TickType_t ultimaExecucao = xTaskGetTickCount();
  const TickType_t periodo = ticks(PERIODO_CONTROLE_MS);
  for (;;) {
    uint32_t comandos = 0;
    xTaskNotifyWait(0, UINT32_MAX, &comandos, 0);
    if (comandos & CMD_PARAR) {
      pararEsteira();
      executando = false;
      emMovimento = false;
      registrar("Cancelado. Reposicione bracos e cubo antes de reiniciar.");
    } else if (comandos & CMD_INICIAR) {
      if (executando) registrar("Ciclo em andamento.");
      else {
        pararEsteira();
        proximaEtapa = 0;
        espera = 0;
        inicio = millis();
        executando = true;
      }
    }
    atualizarMovimento();
    atualizar();
    vTaskDelayUntil(&ultimaExecucao, periodo);
  }
}

// Serial fica fora do controle para que impressoes nao atrasem os servos.
static void taskSerial(void *parameter) {
  (void)parameter;
  for (;;) {
    // Limite por iteracao evita monopolizar CPU com entrada continua.
    char comando = 0;
    for (uint8_t n = 0; n < 32 && uart_read_bytes(UART_CONSOLE, &comando, 1, 0) == 1; ++n) {
      if (comando == 'p' || comando == 'P') {
        xTaskNotify(controleHandle, CMD_PARAR, eSetBits);
      } else if (comando == 'i' || comando == 'I') {
        xTaskNotify(controleHandle, CMD_INICIAR, eSetBits);
      }
    }
    const char *mensagem = NULL;
    for (uint8_t n = 0; n < 8 && xQueueReceive(filaMensagens, &mensagem, 0) == pdTRUE; ++n) {
      puts(mensagem);
    }
    vTaskDelay(ticks(10));
  }
}

void app_main(void) {
  config_pins();
  // O console ja roda a 115200 baud; o driver so adiciona o buffer de leitura.
  if (uart_driver_install(UART_CONSOLE, UART_RX_BUFFER, 0, 0, NULL, 0) != ESP_OK) {
    puts("Falha ao iniciar leitura da Serial; comandos somente por MQTT.");
  }
  pronto = true;

  // Validar antes de conectar qualquer servo.
  for (uint8_t b = 0; b < BRACOS_ATIVOS; ++b) {
    for (uint8_t eixo = 0; eixo < 4; ++eixo) {
      const ServoCfg *c = &CONFIG_BRACOS[b][eixo];
      if (c->pino < 0 || !GPIO_IS_VALID_OUTPUT_GPIO(c->pino) || c->pino == PIN_RELE ||
          c->minimo < 0 || c->maximo > 180 || c->minimo > c->maximo ||
          c->inicial < c->minimo || c->inicial > c->maximo) pronto = false;
      for (uint8_t anterior = 0; anterior < b * 4 + eixo; ++anterior) {
        if (c->pino == CONFIG_BRACOS[anterior / 4][anterior % 4].pino) pronto = false;
      }
    }
  }
  if (!pronto) {
    puts("Configuracao invalida: confira pinos exclusivos, limites e posicoes iniciais.");
    return;
  }

  for (uint8_t b = 0; b < BRACOS_ATIVOS; ++b) {
    for (uint8_t eixo = 0; eixo < 4; ++eixo) {
      const ServoCfg *c = &CONFIG_BRACOS[b][eixo];
      if (!servoConectar(&servos[b][eixo], c->pino, (ledc_channel_t)(b * 4 + eixo), 1000, 2000)) pronto = false;
      // Sem sensor, a primeira referencia e o angulo inicial configurado.
      posicao[b][eixo] = c->inicial;
      write_servo(&servos[b][eixo], c->inicial);
    }
  }
  if (!pronto) {
    puts("Falha ao conectar servos; inicio bloqueado.");
    return;
  }

  filaMensagens = xQueueCreate(24, sizeof(const char *));
  if (filaMensagens == NULL) {
    pararEsteira();
    puts("Falha ao criar fila RTOS. Reinicie a ESP32.");
    return;
  }

  if (xTaskCreate(taskControle, "Controle", 4096, NULL, 2, &controleHandle) != pdPASS) {
    vQueueDelete(filaMensagens);
    filaMensagens = NULL;
    pararEsteira();
    puts("Falha ao criar tarefa de controle. Reinicie a ESP32.");
    return;
  }

  // Controle ainda esta ocioso: nenhum comando foi enviado.
  puts("i = iniciar um ciclo; p = cancelar. Serial: 115200 baud.");
  if (xTaskCreate(taskSerial, "Serial", 4096, NULL, 1, NULL) != pdPASS) {
    vTaskDelete(controleHandle);
    controleHandle = NULL;
    vQueueDelete(filaMensagens);
    filaMensagens = NULL;
    pararEsteira();
    puts("Falha ao criar tarefa Serial. Reinicie a ESP32.");
    return;
  }

  // Wi-Fi/MQTT conectam em segundo plano; falha de rede nao bloqueia a Serial.
  if (!iniciarMqtt(controleHandle)) {
    puts("Falha ao iniciar MQTT; controle somente pela Serial.");
  }
  // Ao retornar, a tarefa main do IDF e encerrada; o trabalho fica nas tarefas.
}
