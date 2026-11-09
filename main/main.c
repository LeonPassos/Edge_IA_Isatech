
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
  //o de vdd é o primeiro 
  {"Base",     18, 0, 180, 90},
  {"Ombro",    19, 0, 180, 90},
  {"Cotovelo", 23, 0, 180, 90},
  {"Garra",    12, 0, 180, 90}
  // {"Base",     16, 0, 180, 90},
  // {"Ombro",    17, 0, 180, 90},
  // {"Cotovelo", 21, 0, 180, 90},
  // {"Garra",    23, 0, 180, 90}
};
static const ServoCfg cfgSegundoBraco[4] = {

  {"Base",     14, 0, 180, 90},
  {"Ombro",    27, 0, 180, 170},
  {"Cotovelo", 26, 0, 180, 50},
  {"Garra",    25, 0, 180, 90}
  // {"Base",     13, 0, 180, 90},
  // {"Ombro",    14, 0, 180, 120},
  // {"Cotovelo", 27, 0, 180, 30},
  // {"Garra",    26, 0, 180, 90}
};
static const ServoCfg *const CONFIG_BRACOS[2] = {cfg, cfgSegundoBraco};
#define BRACOS_ATIVOS 2 // Teste atual: somente o primeiro braco.
static_assert(BRACOS_ATIVOS >= 1 && BRACOS_ATIVOS <= 2, "Use um ou dois bracos");
static_assert(BRACOS_ATIVOS * 4 <= LEDC_CHANNEL_MAX, "Canais LEDC insuficientes");

// EXEMPLOS: calibrar todos os angulos para a geometria real, sem carga.
// Neste exemplo, abrir reduz o angulo. Calibrar em passos de 5 graus.
#define ABERTA_1 20
#define FECHADA_1 90
#define ABERTA_2 110
#define FECHADA_2 90
// Base, ombro e cotovelo de cada posicao; a garra vem em cada etapa.
#define REPOUSO          90, 120, 90
#define REPOUSO_2          90, 180, 50
#define ACIMA_CUBO        0, 120, 90
#define PEGA_CUBO         0, 70,  110
#define ACIMA_ESTEIRA_1  90, 130, 115
#define ENTREGA_ESTEIRA  90, 100, 115

#define PARA_ESTEIRA_2      90, 180, 50
#define PEGA_CUBO_ESTEIRA_2  90, 165, 100
#define ACIMA_ESTEIRA_2          90, 180, 100
#define ENTREGA_SAIDA       180, 150, 120

// Depois do app_main, somente taskControle acessa atuadores e estado da sequencia.
#define PERIODO_CONTROLE_MS 10
static TaskHandle_t controleHandle = NULL;
static QueueHandle_t filaMensagens = NULL;
static QueueHandle_t filaComandos = NULL;
static QueueHandle_t filaSensor = NULL;

#define MAX_DISTANCE_CM 50
#define LIMITE_DISTANCIA 11  
#define TRIGGER_GPIO 5
#define ECHO_GPIO 17
#define PERIODO_SENSOR_MS 50
#define VALIDADE_SENSOR_MS 50

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
// Nunca bloqueia o controle: taskLog imprime e publica; fila cheia descarta.
static void registrar(const char *mensagem) {
  if (filaMensagens != NULL) xQueueSend(filaMensagens, &mensagem, 0);
}

static Servo servos[2][4];
static bool executando = false, emMovimento = false;
static int posicao[2][4] = {0}, origem[4] = {0}, destino[4] = {0};
static size_t proximaEtapa = 0;
static uint8_t bracoMovendo = 0;
static uint32_t inicioMovimento = 0, duracaoMovimento = 0, ultimaAtualizacao = 0, inicio = 0, espera = 0;


#define PARA_ESTEIRA_2      90, 180, 50
#define PEGA_CUBO_ESTEIRA_2  90, 155, 100
#define ACIMA_ESTEIRA_2          90, 180, 100
#define ENTREGA_SAIDA       180, 150, 120
// O braco que nao recebe comando mantem sua ultima posicao.
static const Etapa SEQUENCIA[] = {
  {MOVER, 0, {REPOUSO, ABERTA_1}, MOVIMENTO_MS, "Braco 1: repouso."},
  {MOVER, 1, {REPOUSO_2, ABERTA_2}, MOVIMENTO_MS, "Braco 2: repouso."},
  {MOVER, 0, {ACIMA_CUBO, ABERTA_1}, MOVIMENTO_MS, "Braco 1: acima do cubo."},
  {MOVER, 0, {PEGA_CUBO, ABERTA_1}, MOVIMENTO_MS, "Braco 1: descer para pegar."},
  {GARRA, 0, {0, 0, 0, FECHADA_1}, GARRA_MS, "Braco 1: fechar garra."},
  {MOVER, 0, {ACIMA_CUBO, FECHADA_1}, MOVIMENTO_MS, "Braco 1: levantar cubo."},
  {MOVER, 0, {ACIMA_ESTEIRA_1, FECHADA_1}, MOVIMENTO_MS, "Braco 1: acima da esteira."},
  {MOVER, 0, {ENTREGA_ESTEIRA, FECHADA_1}, MOVIMENTO_MS, "Braco 1: baixar cubo."},
  {GARRA, 0, {0, 0, 0, ABERTA_1}, GARRA_MS, "Braco 1: soltar cubo."},
  {MOVER, 0, {ACIMA_ESTEIRA_1, ABERTA_1}, MOVIMENTO_MS, "Braco 1: levantar vazio."},
  {MOVER, 0, {REPOUSO, FECHADA_1}, MOVIMENTO_MS, "Braco 1: liberar esteira."},
  {LIGAR_ESTEIRA, 0, {0, 0, 0, 0}, ESTEIRA_MS, "Esteira ligada."},
  {DESLIGAR_ESTEIRA, 0, {0, 0, 0, 0}, ACOMODAR_MS, "Aguardando parada da esteira."},
  {MOVER, 1, {PARA_ESTEIRA_2, ABERTA_2}, MOVIMENTO_MS, "Braco 2: acima do cubo."},
  {MOVER, 1, {PEGA_CUBO_ESTEIRA_2, ABERTA_2}, MOVIMENTO_MS, "Braco 2: descer para pegar."},
  {GARRA, 1, {0, 0, 0, FECHADA_2}, GARRA_MS, "Braco 2: fechar garra."},
  {MOVER, 1, {ACIMA_ESTEIRA_2, FECHADA_2}, MOVIMENTO_MS, "Braco 2: levantar cubo."},
  {MOVER, 1, {ACIMA_ESTEIRA_2, FECHADA_2}, MOVIMENTO_MS, "Braco 2: acima da saida."},
  {MOVER, 1, {ENTREGA_SAIDA, FECHADA_2}, MOVIMENTO_MS, "Braco 2: baixar cubo."},
  {GARRA, 1, {0, 0, 0, ABERTA_2}, GARRA_MS, "Braco 2: soltar cubo."},
  {MOVER, 1, {PARA_ESTEIRA_2, ABERTA_2}, MOVIMENTO_MS, "Braco 2: levantar vazio."},
  {MOVER, 1, {REPOUSO_2, FECHADA_2}, MOVIMENTO_MS, "Braco 2: retornar."}
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

static bool busy_control(void) {
  return executando || emMovimento;
}

// Para esteira, ciclo e movimento, e descarta comandos JSON pendentes.
static void cancel_op(const char *msg, cancellation_reason motivo_cancelamento) {
  switch (motivo_cancelamento)
  {
    case MOTIVO_COMANDO:
        pararEsteira();
        executando = false;
        emMovimento = false;
        xQueueReset(filaComandos);
        registrar(msg);

      break;
    case MOTIVO_SENSOR:
        pararEsteira();
        executando = false;
        emMovimento = false;
        xQueueReset(filaComandos);
        registrar(msg);
      break;
    default:
        ESP_LOGE(TAG, "Motivo de cancelamento desconhecido.");
      break;
  }

}

static void atualizarSensor(void) {
  LeituraSensor leitura;
  if (xQueueReceive(filaSensor, &leitura, 0) == pdTRUE) {
    ultimaLeitura = leitura;
    leituraRecebida = true;
  }
  if (!esteiraLigada) return;
  if (!sensorValido()) {
      // cancel_op("Ciclo cancelado: sensor sem leitura valida. Confira o sensor e envie iniciar.", MOTIVO_SENSOR);
      // ESP_LOGI(TAG, "Medição do sensor: %d", ultimaLeitura.erro);
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
    servo_Set_Angle(&servos[bracoMovendo][eixo], angulo);
  }
  if (decorrido >= duracaoMovimento) {
    emMovimento = false;
    inicio = agora; // Tempo de acomodacao contado depois do movimento.
  }
}

static void atualizar(void) {
  if (!executando || emMovimento || (uint32_t)(tempoAtualMs() - inicio) < espera) return;
  while (proximaEtapa < TOTAL_ETAPAS && (SEQUENCIA[proximaEtapa].acao == MOVER || SEQUENCIA[proximaEtapa].acao == GARRA) && SEQUENCIA[proximaEtapa].braco >= BRACOS_ATIVOS) ++proximaEtapa;
  if (proximaEtapa >= TOTAL_ETAPAS) {
    pararEsteira();
    executando = false;
    ESP_LOGI(TAG, "Ciclo concluido. Mandando comando MQTT para o carrinho.");
    mqtt_publish(TOPICO_CARRINHO, "Finalizar", 0, false);
    registrar("Ciclo concluido. Envie i para repetir.");
    return;
  }

  const Etapa *e = &SEQUENCIA[proximaEtapa++];
  switch (e->acao) {
    case MOVER: mover(e->braco, &e->pose); 
      break;
    case GARRA:
      mover(e->braco, &(Pose){posicao[e->braco][0], posicao[e->braco][1], posicao[e->braco][2], e->pose.garra});
      break;
    case LIGAR_ESTEIRA:
      // if (!sensorValido()) {
      // cancel_op("Ciclo cancelado: sensor sem leitura valida. Confira o sensor e envie iniciar.", MOTIVO_SENSOR);
      //   return;
      // }
      if (ultimaLeitura.distancia_cm < LIMITE_DISTANCIA) {
        mqtt_publish(TOPICO_CARRINHO, "Iniciar", 0, false);
        pararEsteira();
        espera = 0;
        registrar("Objeto ja detectado a menos de 12 cm: esteira permanece parada.");
        return;
      }
      gpio_set_level(PIN_RELE, RELE_LIGADO);
      esteiraLigada = true;
      break;
    case DESLIGAR_ESTEIRA: 
        pararEsteira();     
    break;
  }

  registrar(e->mensagem);
  inicio = tempoAtualMs();
  espera = e->tempo;
}

// O sensor e iniciado pela propria tarefa de medicao.
esp_err_t config_pins(void){
  esp_err_t ret;
  bool done = true;

  // Nivel definido antes de habilitar a saida: o rele nao pulsa no boot.
  gpio_set_level(PIN_RELE, RELE_DESLIGADO);
  gpio_config_t io = {0};
  io.pin_bit_mask = 1ULL << PIN_RELE;
  io.mode = GPIO_MODE_OUTPUT;
  // Manter IN alto quando possivel; pull-down acionaria este rele.
  io.pull_up_en = GPIO_PULLUP_ENABLE;
  io.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io.intr_type = GPIO_INTR_DISABLE;
  ret = gpio_config(&io);
  if(ret != ESP_OK) {
    ESP_LOGE(TAG, "Falha ao configurar pino do rele.");
    return ret;
  }
  pararEsteira();

  // Validar antes de conectar qualquer servo.
  for (uint8_t b = 0; b < BRACOS_ATIVOS; ++b) {
    for (uint8_t eixo = 0; eixo < 4; ++eixo) {
      const ServoCfg *c = &CONFIG_BRACOS[b][eixo];
      if (c->pino < 0 || !GPIO_IS_VALID_OUTPUT_GPIO(c->pino) || c->pino == PIN_RELE ||
          c->minimo < 0 || c->maximo > 180 || c->minimo > c->maximo ||
          c->inicial < c->minimo || c->inicial > c->maximo) done = false;
      for (uint8_t anterior = 0; anterior < b * 4 + eixo; ++anterior) {
        if (c->pino == CONFIG_BRACOS[anterior / 4][anterior % 4].pino) done = false;
      }
    }
  }
  if (!done) {
    ESP_LOGE(TAG, "Configuracao invalida: confira pinos exclusivos, limites e posicoes iniciais.");
    return ESP_FAIL;
  }

  for (uint8_t b = 0; b < BRACOS_ATIVOS; ++b) {
    for (uint8_t eixo = 0; eixo < 4; ++eixo) {
      const ServoCfg *c = &CONFIG_BRACOS[b][eixo];
      if (!servo_init(&servos[b][eixo], c->pino, (ledc_channel_t)(b * 4 + eixo), SERVO_MIN_US, SERVO_MAX_US)) done = false;
      // Sem sensor, a primeira referencia e o angulo inicial configurado.
      posicao[b][eixo] = c->inicial;
      servo_Set_Angle(&servos[b][eixo], c->inicial);
    }
  }
  if (!done) {
    ESP_LOGE(TAG, "Falha ao conectar servos; inicio bloqueado.");
    return ESP_FAIL;
  }
  ESP_LOGI(TAG, "Servos conectados nas posicoes iniciais.");

  return ESP_OK;
}

static void executarComandoJson(const ComandoJson *comando) {
  
  if (comando->braco >= BRACOS_ATIVOS) {
    registrar("Braco solicitado nao esta ativo.");
    return;
  }

  if (busy_control()) {
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
  if (busy_control()) {
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

// Ocioso: dorme ate chegar um comando. Com movimento, ciclo ou esteira em
// andamento, acorda a cada periodo para animar servos e seguir as etapas.
static TickType_t wait_time(void) {
  return busy_control() || esteiraLigada ? ticks(PERIODO_CONTROLE_MS) : portMAX_DELAY;
}

static void taskControle() {

  bool estavaMovendo;
  bool comandoAvulsoConcluido;

  while(1) {
    uint32_t comandos = 0;
    xTaskNotifyWait(0, UINT32_MAX, &comandos, wait_time());

    // Se i e p chegarem no mesmo periodo, p prevalece.
    // Usasse o & sozinho para verificação bit a bit, para não da conflito com outros bits.
    if (comandos & CMD_PARAR) { 
      cancel_op("Cancelado. Reposicione bracos e cubo antes de reiniciar.", MOTIVO_COMANDO);
    } else {

      if (comandos & CMD_INICIAR){
        iniciarCiclo();
      };
      
      if (comandos & CMD_JSON) {
        processarComandosJson();
      }
      
    }

    atualizarSensor();

    estavaMovendo = emMovimento;
    atualizarMovimento();
    
    comandoAvulsoConcluido = estavaMovendo && !emMovimento && !executando;
    if (comandoAvulsoConcluido) {
      registrar("Comando JSON: movimento concluido.");
    }

    atualizar();
  }
}

static bool ehComando(const char *texto, const char *curto, const char *longo) {
  return strcasecmp(texto, curto) == 0 || strcasecmp(texto, longo) == 0;
}

// Executado na tarefa do esp-mqtt: so traduz a mensagem e entrega ao controle.
static void aoReceberMqtt(const char *topico, int topico_len, const char *dados, int dados_len, void *ctx) {

  (void)ctx;

  if (topico_len != (int)strlen(TOPICO_COMANDO) || memcmp(topico, TOPICO_COMANDO, topico_len) != 0) return;
  if (dados_len <= 0 || dados_len > COMANDO_JSON_MAX) {
    registrar("Comando vazio ou maior que 256 bytes.");
    return;
  }
  char texto[COMANDO_JSON_MAX + 1];
  memcpy(texto, dados, dados_len);
  texto[dados_len] = '\0';

  if (ehComando(texto, "p", "parar")) {
    xTaskNotify(controleHandle, CMD_PARAR, eSetBits);
  } else if (ehComando(texto, "i", "iniciar")) {
    xTaskNotify(controleHandle, CMD_INICIAR, eSetBits);
  } else {
    ComandoJson comando;
    if (!comandoJsonParse(texto, dados_len, &comando)) {
      registrar("JSON invalido. Consulte docs/comandos-json.md.");
    } else if (comando.acao == JSON_PARAR) {
      xTaskNotify(controleHandle, CMD_PARAR, eSetBits);
    } else if (comando.acao == JSON_INICIAR) {
      xTaskNotify(controleHandle, CMD_INICIAR, eSetBits);
    } else if (xQueueSend(filaComandos, &comando, 0) != pdTRUE) {
      registrar("Fila de comandos cheia. Tente novamente.");
    } else {
      xTaskNotify(controleHandle, CMD_JSON, eSetBits);
    }
  }
}

// A cada (re)conexao a assinatura precisa ser refeita.
static void aoConectarMqtt(bool conectado, void *ctx) {
  (void)ctx;  
  if (conectado) mqtt_subscribe(TOPICO_COMANDO, 0);
}

// Serial e rede ficam fora do controle: imprimir e publicar podem bloquear.
static void taskLog(void *parameter) {
  (void)parameter;
  const char *mensagem = NULL;
  for (;;) {
    if (xQueueReceive(filaMensagens, &mensagem, portMAX_DELAY) != pdTRUE) continue;
        ESP_LOGI("task_log", "%s", mensagem);
    if (if_mqtt_conectado()) mqtt_publish(TOPICO_LOG, mensagem, 0, false);
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
  esp_err_t erroAnterior = ESP_OK;
  for (;;) {
    LeituraSensor leitura = {0};
    leitura.erro = ultrasonic_measure_cm(&sensor, MAX_DISTANCE_CM, &leitura.distancia_cm);
    // Eco alem de MAX_DISTANCE_CM: nada na frente do sensor. E leitura valida.
    if (leitura.erro == ESP_ERR_ULTRASONIC_ECHO_TIMEOUT) {
      leitura.erro = ESP_OK;
      leitura.distancia_cm = MAX_DISTANCE_CM;
    }
    // Loga so quando o estado muda, para nao inundar a Serial a cada 100 ms.
    // if (leitura.erro != erroAnterior) {
    //   if (leitura.erro == ESP_OK) ESP_LOGI("ultrasonic", "Leituras normalizadas.");
    //   // 0x200: eco preso do ping anterior; 0x201: sem resposta (fiacao/alimentacao).
    //   else ESP_LOGW("ultrasonic", "Falha na medicao: 0x%x", (unsigned)leitura.erro);
    //   erroAnterior = leitura.erro;
    // }
    leitura.instante_ms = tempoAtualMs();
    xQueueOverwrite(filaSensor, &leitura);
    // d(TAG, "Distancia: %lu cm, erro: 0x%x", (unsigned long)leitura.distancia_cm, (unsigned)leitura.erro);    
    vTaskDelay(ticks(PERIODO_SENSOR_MS));
  }
}

void app_main(void) {

  esp_err_t ret; 

  // Primeiro: cria o loop de eventos usado pelo MQTT.
  ret = wifi_init_sta();
  if(ret != ESP_OK) {
    ESP_LOGE(TAG, "Falha na inicializacao do Wi-Fi. Reinicie a ESP32.");
    return;
  }

  ret = config_pins();
  if(ret != ESP_OK) {
    ESP_LOGE(TAG, "Falha na configuracao de pinos. Verifique os pinos e reinicie a ESP32.");
    return;
  }

  //Criando as filas de mensagens, comandos e sensor. Tamanho definido para evitar bloqueio do controle.
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

  // Sem produtor, sensorValido() impede ligar a esteira.
  if (xTaskCreate(task_esteira_with_ultrasonic, "Sensor", 4096, NULL, 1, NULL) != pdPASS) {
    ESP_LOGE(TAG, "Falha ao criar tarefa do sensor: etapa da esteira bloqueada.");
  }

  // MQTT conecta quando o Wi-Fi recebe IP. E a unica entrada de comandos.
  // Callbacks antes do init: o controle ja existe para receber os comandos.
  mqtt_callbacks(aoConectarMqtt, aoReceberMqtt, NULL);
  if (mqtt_init() != ESP_OK) {
    ESP_LOGE(TAG, "Falha ao iniciar MQTT; nenhum comando sera recebido.");
  }

}
