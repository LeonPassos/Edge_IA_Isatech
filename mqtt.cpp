#include "mqtt.h"

#include <WiFi.h>
#include <PubSubClient.h>

// Rede e broker.
static const char *WIFI_SSID = "Isatech";
static const char *WIFI_SENHA = "@Isatech2027@";
static const char *MQTT_SERVIDOR = "test.mosquitto.org";
constexpr uint16_t MQTT_PORTA = 1883;

// IP estatico opcional. Com false, o roteador entrega o IP por DHCP.
constexpr bool USAR_IP_ESTATICO = false;
static const IPAddress IP_LOCAL(192, 168, 0, 50);
static const IPAddress GATEWAY(192, 168, 0, 1);
static const IPAddress SUBNET(255, 255, 255, 0);

// test.mosquitto.org e publico: qualquer um pode publicar nestes topicos.
static const char *TOPICO_COMANDO = "edge_ia/comando"; // i/iniciar, p/parar
static const char *TOPICO_STATUS = "edge_ia/status";   // mensagens do ciclo
static const char *TOPICO_ESTADO = "edge_ia/estado";   // online/offline (retido)

constexpr uint32_t RECONEXAO_MS = 5000;
constexpr size_t TAMANHO_FILA = 24;

static WiFiClient wifiClient;
static PubSubClient mqtt(wifiClient);
static TaskHandle_t controleHandle = nullptr;
static QueueHandle_t filaMqtt = nullptr;

void publicarMqtt(const char *mensagem) {
  if (filaMqtt != nullptr) xQueueSend(filaMqtt, &mensagem, 0);
}

// Executado dentro de mqtt.loop(), no contexto de taskMqtt.
static void ReceberMensagem(char *topico, byte *payload, unsigned int tamanho) {
  if (strcmp(topico, TOPICO_COMANDO) != 0) return;
  char texto[16] = {};
  const unsigned int n = min(tamanho, (unsigned int)(sizeof(texto) - 1));
  for (unsigned int k = 0; k < n; ++k) texto[k] = tolower(payload[k]);

  if (strcmp(texto, "p") == 0 || strcmp(texto, "parar") == 0) {
    xTaskNotify(controleHandle, CMD_PARAR, eSetBits);
  } else if (strcmp(texto, "i") == 0 || strcmp(texto, "iniciar") == 0) {
    xTaskNotify(controleHandle, CMD_INICIAR, eSetBits);
  } else {
    mqtt.publish(TOPICO_STATUS, "Comando desconhecido. Use i/iniciar ou p/parar.");
  }
}

static void conectarWifi() {
  if (USAR_IP_ESTATICO && !WiFi.config(IP_LOCAL, GATEWAY, SUBNET)) {
    Serial.println("Falha ao configurar IP estatico; usando DHCP.");
  }
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_SENHA);
}

static bool conectarMqtt() {

  char id[32];
  snprintf(id, sizeof(id), "edge_ia-%012llx", ESP.getEfuseMac());
  
  if (!mqtt.connect(id, TOPICO_ESTADO, 1, true, "offline")){
    return false;
  } 

  mqtt.publish(TOPICO_ESTADO, "online", true);
  mqtt.subscribe(TOPICO_COMANDO);

  return true;
}

// Somente esta tarefa usa o PubSubClient, que nao e thread-safe.
static void taskMqtt(void *parameter) {
  (void)parameter;

  conectarWifi();
  bool wifiAnterior = false;
  uint32_t ultimaTentativa = 0;
  bool primeiraTentativa = true;
  
  for (;;) {
    const bool wifiOk = WiFi.status() == WL_CONNECTED;
    if (wifiOk != wifiAnterior) {
      wifiAnterior = wifiOk;
      if (wifiOk) {
        Serial.print("Wi-Fi conectado. IP: ");
        Serial.println(WiFi.localIP());
      } else {
        Serial.println("Wi-Fi desconectado.");
      }
    }

    if (wifiOk && !mqtt.connected() && (primeiraTentativa || millis() - ultimaTentativa >= RECONEXAO_MS)) {
      primeiraTentativa = false;
      ultimaTentativa = millis();
      if (conectarMqtt()) {
        Serial.printf("MQTT conectado. Comandos em %s.\n", TOPICO_COMANDO);
      } else {
        Serial.printf("Falha MQTT (estado %d); nova tentativa em %lu ms.\n", mqtt.state(), (unsigned long)RECONEXAO_MS);
      }
    }

    if (mqtt.connected()) {
      mqtt.loop();
      const char *mensagem = nullptr;
      for (uint8_t n = 0; n < 8 && xQueueReceive(filaMqtt, &mensagem, 0) == pdTRUE; ++n) {
        mqtt.publish(TOPICO_STATUS, mensagem);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

bool iniciarMqtt(TaskHandle_t controle) {
  controleHandle = controle;
  mqtt.setServer(MQTT_SERVIDOR, MQTT_PORTA);
  mqtt.setCallback(ReceberMensagem);

  filaMqtt = xQueueCreate(TAMANHO_FILA, sizeof(const char *));
  if (filaMqtt == nullptr) {
    return false;
  }

  // Prioridade 1: abaixo do controle, para rede nao atrasar os servos.
  if (xTaskCreate(taskMqtt, "MQTT", 6*1024, nullptr, 1, nullptr) != pdPASS) {
    vQueueDelete(filaMqtt);
    filaMqtt = nullptr;
    return false;
  }

  return true;
}
