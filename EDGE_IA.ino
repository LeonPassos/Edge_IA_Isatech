#include <ESP32Servo.h>
#include <math.h>
#include "tipos.h"
#include "mqtt.h"

// ESP32 DevKit classica. Ordem: BASE, OMBRO, COTOVELO, GARRA.
struct ServoCfg {
  const char *nome;
  int pino;
  int minimo;
  int maximo;
  int inicial;
};
constexpr ServoCfg cfg[4] = {
  {"Base",     13, 0, 180, 90},
  {"Ombro",    14, 0, 180, 90},
  {"Cotovelo", 27, 0, 180, 90},
  {"Garra",    26, 0, 180, 135}
};
constexpr ServoCfg cfgSegundoBraco[4] = {
  {"Base",     -1, 0, 180, 90},
  {"Ombro",    -1, 0, 180, 90},
  {"Cotovelo", -1, 0, 180, 90},
  {"Garra",    -1, 0, 180, 90}
};
const ServoCfg *const CONFIG_BRACOS[2] = {cfg, cfgSegundoBraco};
constexpr uint8_t BRACOS_ATIVOS = 1; // Teste atual: somente o primeiro braco.
// Preencher cfgSegundoBraco com GPIOs livres antes de ativar dois bracos.
static_assert(BRACOS_ATIVOS >= 1 && BRACOS_ATIVOS <= 2, "Use um ou dois bracos");
constexpr uint8_t PIN_RELE = 25; // GPIO 27 ja pertence ao cotovelo.
constexpr uint32_t ATUALIZACAO_SERVO_MS = 20;
constexpr float VELOCIDADE_GRAUS_S = 70.0f; // Menor = mais lento.
static_assert(VELOCIDADE_GRAUS_S > 0, "Velocidade deve ser positiva");
constexpr uint8_t RELE_LIGADO = LOW; // HIGH para modulo ativo alto.
constexpr uint8_t RELE_DESLIGADO = RELE_LIGADO == LOW ? HIGH : LOW;
constexpr uint32_t MOVIMENTO_MS = 1500;
constexpr uint32_t GARRA_MS = 700;
constexpr uint32_t ESTEIRA_MS = 3000;
constexpr uint32_t ACOMODAR_MS = 700;

// EXEMPLOS: calibrar todos os angulos para a geometria real, sem carga.
// Coordenadas em graus. Servo posicional; nao usar servo de rotacao continua.
struct Pose { int base, ombro, cotovelo, garra; };
// Neste exemplo, abrir reduz o angulo. Calibrar em passos de 5 graus.
constexpr int ABERTA_1 = 30, FECHADA_1 = 120;
constexpr int ABERTA_2 = 70, FECHADA_2 = 110;
constexpr Pose REPOUSO_1 = {90, 90, 90, ABERTA_1};
constexpr Pose REPOUSO_2 = {90, 90, 90, ABERTA_2};
constexpr Pose ACIMA_ENTRADA = {30, 90, 90, ABERTA_1};
constexpr Pose PEGA_ENTRADA = {30, 120, 60, ABERTA_1};
constexpr Pose ACIMA_ESTEIRA_1 = {150, 90, 90, FECHADA_1};
constexpr Pose ENTREGA_ESTEIRA = {150, 120, 60, FECHADA_1};
constexpr Pose ACIMA_ESTEIRA_2 = {30, 90, 90, ABERTA_2};
constexpr Pose PEGA_ESTEIRA = {30, 120, 60, ABERTA_2};
constexpr Pose ACIMA_SAIDA = {150, 90, 90, FECHADA_2};
constexpr Pose ENTREGA_SAIDA = {150, 120, 60, FECHADA_2};

// Depois do setup, somente taskControle acessa atuadores e estado da sequencia.
constexpr uint32_t PERIODO_CONTROLE_MS = 10;
TaskHandle_t controleHandle = nullptr;
QueueHandle_t filaMensagens = nullptr;

// Apenas literais estaticos sao enviados: os ponteiros continuam validos.
// Log nunca bloqueia o controle; fila cheia descarta a mensagem.
void registrar(const char *mensagem) {
  if (filaMensagens != nullptr) xQueueSend(filaMensagens, &mensagem, 0);
  publicarMqtt(mensagem);
}

Servo servos[2][4];
bool pronto = false;
bool executando = false;
size_t proximaEtapa = 0;
uint32_t inicio = 0, espera = 0;
int posicao[2][4] = {};
int origem[4] = {}, destino[4] = {};
bool emMovimento = false;
uint8_t bracoMovendo = 0;
uint32_t inicioMovimento = 0, duracaoMovimento = 0, ultimaAtualizacao = 0;

enum Acao { 
    MOVER, 
    GARRA, 
    LIGAR_ESTEIRA, 
    DESLIGAR_ESTEIRA 
};

struct Etapa {
  Acao acao;
  uint8_t braco;
  Pose pose;
  uint32_t tempo;
  const char *mensagem;
};

// O braco que nao recebe comando mantem sua ultima posicao.
const Etapa SEQUENCIA[] = {
  {MOVER, 0, REPOUSO_1, MOVIMENTO_MS, "Braco 1: repouso."},
  {MOVER, 1, REPOUSO_2, MOVIMENTO_MS, "Braco 2: repouso."},
  {MOVER, 0, ACIMA_ENTRADA, MOVIMENTO_MS, "Braco 1: acima do cubo."},
  {MOVER, 0, PEGA_ENTRADA, MOVIMENTO_MS, "Braco 1: descer para pegar."},
  {GARRA, 0, {0, 0, 0, FECHADA_1}, GARRA_MS, "Braco 1: fechar garra."},
  {MOVER, 0, {ACIMA_ENTRADA.base, ACIMA_ENTRADA.ombro, ACIMA_ENTRADA.cotovelo, FECHADA_1}, MOVIMENTO_MS, "Braco 1: levantar cubo."},
  {MOVER, 0, ACIMA_ESTEIRA_1, MOVIMENTO_MS, "Braco 1: acima da esteira."},
  {MOVER, 0, ENTREGA_ESTEIRA, MOVIMENTO_MS, "Braco 1: baixar cubo."},
  {GARRA, 0, {0, 0, 0, ABERTA_1}, GARRA_MS, "Braco 1: soltar cubo."},
  {MOVER, 0, {ACIMA_ESTEIRA_1.base, ACIMA_ESTEIRA_1.ombro, ACIMA_ESTEIRA_1.cotovelo, ABERTA_1}, MOVIMENTO_MS, "Braco 1: levantar vazio."},
  {MOVER, 0, REPOUSO_1, MOVIMENTO_MS, "Braco 1: liberar esteira."},
  {LIGAR_ESTEIRA, 0, {}, ESTEIRA_MS, "Esteira ligada."},
  {DESLIGAR_ESTEIRA, 0, {}, ACOMODAR_MS, "Aguardando parada da esteira."},
  {MOVER, 1, ACIMA_ESTEIRA_2, MOVIMENTO_MS, "Braco 2: acima do cubo."},
  {MOVER, 1, PEGA_ESTEIRA, MOVIMENTO_MS, "Braco 2: descer para pegar."},
  {GARRA, 1, {0, 0, 0, FECHADA_2}, GARRA_MS, "Braco 2: fechar garra."},
  {MOVER, 1, {ACIMA_ESTEIRA_2.base, ACIMA_ESTEIRA_2.ombro, ACIMA_ESTEIRA_2.cotovelo, FECHADA_2}, MOVIMENTO_MS, "Braco 2: levantar cubo."},
  {MOVER, 1, ACIMA_SAIDA, MOVIMENTO_MS, "Braco 2: acima da saida."},
  {MOVER, 1, ENTREGA_SAIDA, MOVIMENTO_MS, "Braco 2: baixar cubo."},
  {GARRA, 1, {0, 0, 0, ABERTA_2}, GARRA_MS, "Braco 2: soltar cubo."},
  {MOVER, 1, {ACIMA_SAIDA.base, ACIMA_SAIDA.ombro, ACIMA_SAIDA.cotovelo, ABERTA_2}, MOVIMENTO_MS, "Braco 2: levantar vazio."},
  {MOVER, 1, REPOUSO_2, MOVIMENTO_MS, "Braco 2: retornar."}
};
constexpr size_t TOTAL_ETAPAS = sizeof(SEQUENCIA) / sizeof(SEQUENCIA[0]);

void pararEsteira() { 
  digitalWrite(PIN_RELE, RELE_DESLIGADO); 
}

void mover(uint8_t braco, const Pose &p) {
  const int alvo[4] = {p.base, p.ombro, p.cotovelo, p.garra};
  int maiorDeslocamento = 0;
  
  for (uint8_t eixo = 0; eixo < 4; ++eixo) {
    origem[eixo] = posicao[braco][eixo];
    const ServoCfg &c = CONFIG_BRACOS[braco][eixo];
    destino[eixo] = constrain(alvo[eixo], c.minimo, c.maximo);
    maiorDeslocamento = max(maiorDeslocamento, abs(destino[eixo] - origem[eixo]));
  }
  
  bracoMovendo = braco;
  // Smoothstep tem velocidade maxima 1,5 vezes a velocidade media.
  duracaoMovimento = max(uint32_t(1), uint32_t(1500.0f * maiorDeslocamento / VELOCIDADE_GRAUS_S));
  inicioMovimento = millis();
  ultimaAtualizacao = inicioMovimento;
  emMovimento = true;
}

void atualizarMovimento() {
  if (!emMovimento) return;
  const uint32_t agora = millis();
  const uint32_t decorrido = agora - inicioMovimento;
  if (decorrido < duracaoMovimento && agora - ultimaAtualizacao < ATUALIZACAO_SERVO_MS) return;
  ultimaAtualizacao = agora;
  const float t = decorrido >= duracaoMovimento ? 1.0f : float(decorrido) / duracaoMovimento;
  const float suave = t * t * (3.0f - 2.0f * t);
  for (uint8_t eixo = 0; eixo < 4; ++eixo) {
    const int angulo = int(lroundf(origem[eixo] + (destino[eixo] - origem[eixo]) * suave));
    posicao[bracoMovendo][eixo] = angulo;
    servos[bracoMovendo][eixo].write(angulo);
  }
  if (decorrido >= duracaoMovimento) {
    emMovimento = false;
    inicio = agora; // Tempo de acomodacao contado depois do movimento.
  }
}

void atualizar() {
  if (!executando || emMovimento || uint32_t(millis() - inicio) < espera) return;
  while (proximaEtapa < TOTAL_ETAPAS &&
         (SEQUENCIA[proximaEtapa].acao == MOVER || SEQUENCIA[proximaEtapa].acao == GARRA) &&
         SEQUENCIA[proximaEtapa].braco >= BRACOS_ATIVOS) ++proximaEtapa;
  if (proximaEtapa >= TOTAL_ETAPAS) {
    pararEsteira();
    executando = false;
    registrar("Ciclo concluido. Envie i para repetir.");
    return;
  }
  const Etapa &e = SEQUENCIA[proximaEtapa++];
  switch (e.acao) {
    case MOVER: mover(e.braco, e.pose); break;
    case GARRA:
      mover(e.braco, {posicao[e.braco][0], posicao[e.braco][1], posicao[e.braco][2], e.pose.garra});
      break;
    case LIGAR_ESTEIRA: digitalWrite(PIN_RELE, RELE_LIGADO); break;
    case DESLIGAR_ESTEIRA: pararEsteira(); break;
  }
  registrar(e.mensagem);
  inicio = millis();
  espera = e.tempo;
}

void config_pins(){

  digitalWrite(PIN_RELE, RELE_DESLIGADO);
  pinMode(PIN_RELE, OUTPUT);
  pararEsteira();
}

// A notificacao transporta comandos sem compartilhar variaveis de controle.
// Se i e p chegarem no mesmo periodo, p prevalece.
void taskControle(void *parameter) {
  (void)parameter;
  TickType_t ultimaExecucao = xTaskGetTickCount();
  const TickType_t periodo = max(TickType_t(1), pdMS_TO_TICKS(PERIODO_CONTROLE_MS));
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
void taskSerial(void *parameter) {
  (void)parameter;
  for (;;) {
    // Limite por iteracao evita monopolizar CPU com entrada continua.
    for (uint8_t n = 0; n < 32 && Serial.available() > 0; ++n) {
      const char comando = Serial.read();
      if (comando == 'p' || comando == 'P') {
        xTaskNotify(controleHandle, CMD_PARAR, eSetBits);
      } else if (comando == 'i' || comando == 'I') {
        xTaskNotify(controleHandle, CMD_INICIAR, eSetBits);
      }
    }
    const char *mensagem = nullptr;
    for (uint8_t n = 0; n < 8 && xQueueReceive(filaMensagens, &mensagem, 0) == pdTRUE; ++n) {
      Serial.println(mensagem);
    }
    vTaskDelay(max(TickType_t(1), pdMS_TO_TICKS(10)));
  }
}

void setup() {
  config_pins();
  Serial.begin(115200);
  pronto = true;
  
  // Validar antes de conectar qualquer servo.
  for (uint8_t b = 0; b < BRACOS_ATIVOS; ++b) {
    for (uint8_t eixo = 0; eixo < 4; ++eixo) {
      const ServoCfg &c = CONFIG_BRACOS[b][eixo];
      if (c.pino < 0 || c.pino == PIN_RELE || c.minimo < 0 ||
          c.maximo > 180 || c.minimo > c.maximo ||
          c.inicial < c.minimo || c.inicial > c.maximo) pronto = false;
      for (uint8_t anterior = 0; anterior < b * 4 + eixo; ++anterior) {
        if (c.pino == CONFIG_BRACOS[anterior / 4][anterior % 4].pino) pronto = false;
      }
    }
  }
  if (!pronto) {
    Serial.println("Configuracao invalida: confira pinos exclusivos, limites e posicoes iniciais.");
    return;
  }
  
  for (uint8_t b = 0; b < BRACOS_ATIVOS; ++b) {
    for (uint8_t eixo = 0; eixo < 4; ++eixo) {
      const ServoCfg &c = CONFIG_BRACOS[b][eixo];
      servos[b][eixo].setPeriodHertz(50);
      servos[b][eixo].attach(c.pino, 1000, 2000);
      if (!servos[b][eixo].attached()) pronto = false;
      // Sem sensor, a primeira referencia e o angulo inicial configurado.
      posicao[b][eixo] = c.inicial;
      servos[b][eixo].write(c.inicial);
    }
  }
  if (!pronto) {
    Serial.println("Falha ao conectar servos; inicio bloqueado.");
    return;
  }

  filaMensagens = xQueueCreate(24, sizeof(const char *));
  if (filaMensagens == nullptr) {
    pararEsteira();
    Serial.println("Falha ao criar fila RTOS. Reinicie a ESP32.");
    return;
  }

  if (xTaskCreate(taskControle, "Controle", 4096, nullptr, 2, &controleHandle) != pdPASS) {
    vQueueDelete(filaMensagens);
    filaMensagens = nullptr;
    pararEsteira();
    Serial.println("Falha ao criar tarefa de controle. Reinicie a ESP32.");
    return;
  }

  // Controle ainda esta ocioso: nenhum comando foi enviado.
  Serial.println("i = iniciar um ciclo; p = cancelar. Serial: 115200 baud.");
  if (xTaskCreate(taskSerial, "Serial", 4096, nullptr, 1, nullptr) != pdPASS) {
    vTaskDelete(controleHandle);
    controleHandle = nullptr;
    vQueueDelete(filaMensagens);
    filaMensagens = nullptr;
    pararEsteira();
    Serial.println("Falha ao criar tarefa Serial. Reinicie a ESP32.");
    return;
  }

  // Wi-Fi/MQTT conectam em segundo plano; falha de rede nao bloqueia a Serial.
  if (!iniciarMqtt(controleHandle)) {
    Serial.println("Falha ao iniciar MQTT; controle somente pela Serial.");
  }
}

void loop() {
  // O trabalho fica nas tarefas; bloquear evita gastar CPU no loop do Arduino.
  vTaskDelay(portMAX_DELAY);
}
