#include "mqtt.h"
#include "app_config.h"
#include "comando_json.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <esp_event.h>
#include <esp_mac.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <mqtt_client.h>
#include <nvs_flash.h>

// Rede e broker.
static const char *WIFI_SSID = "Isatech";
static const char *WIFI_SENHA = "@Isatech2027@";
// static const char *MQTT_URI = "mqtt://test.mosquitto.org:1883";
static const char *MQTT_URI = "mqtt://192.168.1.179:1883";

// IP estatico opcional. Com false, o roteador entrega o IP por DHCP.
// No modo estatico o gateway tambem e usado como DNS.
#define USAR_IP_ESTATICO false
#define IP_LOCAL ESP_IP4TOADDR(192, 168, 0, 50)
#define GATEWAY  ESP_IP4TOADDR(192, 168, 0, 1)
#define SUBNET   ESP_IP4TOADDR(255, 255, 255, 0)

// Topicos da aplicacao no broker configurado.
static const char *TOPICO_COMANDO = "edge_ia/comando"; // i/iniciar, p/parar
static const char *TOPICO_STATUS = "edge_ia/status";   // mensagens do ciclo
static const char *TOPICO_ESTADO = "edge_ia/estado";   // online/offline (retido)

#define RECONEXAO_MS 5000
#define KEEPALIVE_S 15
#define TAMANHO_FILA 24

static esp_mqtt_client_handle_t mqtt = NULL;
static TaskHandle_t controleHandle = NULL;
static QueueHandle_t filaMqtt = NULL;
static QueueHandle_t filaComandos = NULL;
static atomic_bool mqttConectado = false;
// Acessados somente no loop de eventos padrao.
static bool wifiConectado = false;
static bool mqttIniciado = false;

void publicarMqtt(const char *mensagem) {
  if (filaMqtt != NULL) xQueueSend(filaMqtt, &mensagem, 0);
}

static void receberMensagem(esp_mqtt_event_handle_t ev) {
  static char texto[COMANDO_JSON_MAX + 1];
  static int recebidos = 0, total = 0;
  static bool retido;
  if (ev->current_data_offset == 0) {
    recebidos = total = 0;
    if (ev->topic_len != (int)strlen(TOPICO_COMANDO) || !ev->topic || memcmp(ev->topic, TOPICO_COMANDO, ev->topic_len) != 0){
      return;
    } 
    if (ev->total_data_len <= 0 || ev->total_data_len > COMANDO_JSON_MAX) {
      publicarMqtt("Comando vazio ou maior que 256 bytes.");
      return;
    }
    total = ev->total_data_len;
    retido = ev->retain;
  }
  if (!total) {
    return;
  }
  if (ev->current_data_offset != recebidos || ev->total_data_len != total || ev->data_len <= 0 || ev->data_len > total - recebidos || !ev->data) {
    recebidos = total = 0;
    return;
  }
  memcpy(texto + recebidos, ev->data, ev->data_len);
  recebidos += ev->data_len;
  if (recebidos != total) return;
  const int tamanho = total;
  recebidos = total = 0;
  texto[tamanho] = '\0';
  if (retido || memchr(texto, '\0', tamanho)) {
    publicarMqtt("Comando retido ou invalido ignorado. Publique sem retain.");
    return;
  }
  if (strcmp(texto, "p") == 0 || strcmp(texto, "parar") == 0 || strcmp(texto, "P") == 0) {
    xTaskNotify(controleHandle, CMD_PARAR, eSetBits);
  } else if (strcmp(texto, "i") == 0 || strcmp(texto, "iniciar") == 0 || strcmp(texto, "I") == 0) {
    xTaskNotify(controleHandle, CMD_INICIAR, eSetBits);
  } else {
    ComandoJson comando;
    if (!comandoJsonParse(texto, tamanho, &comando)) {
      publicarMqtt("JSON invalido. Consulte docs/comandos-json.md.");
    } else if (comando.acao == JSON_PARAR) {
      xTaskNotify(controleHandle, CMD_PARAR, eSetBits);
    } else if (comando.acao == JSON_INICIAR) {
      xTaskNotify(controleHandle, CMD_INICIAR, eSetBits);
    } else if (xQueueSend(filaComandos, &comando, 0) != pdTRUE) {
      publicarMqtt("Fila de comandos cheia. Tente novamente.");
    }
  }
}

// Executado na tarefa interna do esp-mqtt.
static void eventoMqtt(void *arg, esp_event_base_t base, int32_t id, void *dados) {
  (void)arg;
  (void)base;
  esp_mqtt_event_handle_t ev = dados;
  switch (id) {
    case MQTT_EVENT_CONNECTED:
      esp_mqtt_client_publish(mqtt, TOPICO_ESTADO, "online", 0, 1, 1);
      esp_mqtt_client_subscribe(mqtt, TOPICO_COMANDO, 0);
      mqttConectado = true;
      printf("MQTT conectado. Comandos em %s.\n", TOPICO_COMANDO);
      break;
    case MQTT_EVENT_DISCONNECTED:
      mqttConectado = false;
      printf("MQTT desconectado; nova tentativa em %lu ms.\n", (unsigned long)RECONEXAO_MS);
      break;
    case MQTT_EVENT_ERROR:
      if (!ev->error_handle) {
        puts("Falha MQTT sem detalhes de transporte.");
        break;
      }
      printf("Falha MQTT em %s (tipo %d).\n", MQTT_URI, (int)ev->error_handle->error_type);
      if (ev->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
        const int erro = ev->error_handle->esp_transport_sock_errno;
        printf("Transporte: esp_err=0x%x (%s), stack=0x%x, socket_errno=%d (%s).\n",
               (unsigned int)ev->error_handle->esp_tls_last_esp_err,
               esp_err_to_name(ev->error_handle->esp_tls_last_esp_err),
               (unsigned int)ev->error_handle->esp_tls_stack_err,
               erro, erro ? strerror(erro) : "nao informado");
      } else if (ev->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
        printf("Broker recusou conexao MQTT: codigo %d.\n",
               (int)ev->error_handle->connect_return_code);
      }
      break;
    case MQTT_EVENT_DATA:
      receberMensagem(ev);
      break;
    default:
      break;
  }
}

// Executado no loop de eventos padrao. O driver Wi-Fi reconecta sozinho
// a cada desconexao; o MQTT so e iniciado depois do primeiro IP.
static void eventoRede(void *arg, esp_event_base_t base, int32_t id, void *dados) {
  (void)arg;
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    if (wifiConectado) {
      wifiConectado = false;
      puts("Wi-Fi desconectado.");
    }
    esp_wifi_connect();
  } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *ev = dados;
    wifiConectado = true;
    printf("Wi-Fi conectado. IP: " IPSTR ", mascara: " IPSTR ", gateway: " IPSTR "\n", IP2STR(&ev->ip_info.ip), IP2STR(&ev->ip_info.netmask), IP2STR(&ev->ip_info.gw));
    printf("Broker MQTT: %s\n", MQTT_URI);
    if (!mqttIniciado) mqttIniciado = esp_mqtt_client_start(mqtt) == ESP_OK;
  }
}

static bool conectarWifi(void) {

  esp_err_t err = ESP_OK;

  if (err != ESP_OK) return false;
  if (esp_netif_init() != ESP_OK) return false;
  err = esp_event_loop_create_default();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return false;

  esp_netif_t *netif = esp_netif_create_default_wifi_sta();
  if (netif == NULL) return false;

  if (USAR_IP_ESTATICO) {
    esp_netif_ip_info_t ip = {0};
    ip.ip.addr = IP_LOCAL;
    ip.gw.addr = GATEWAY;
    ip.netmask.addr = SUBNET;
    esp_netif_dns_info_t dns = {0};
    dns.ip.type = ESP_IPADDR_TYPE_V4;
    dns.ip.u_addr.ip4.addr = GATEWAY;
    if (esp_netif_dhcpc_stop(netif) != ESP_OK || esp_netif_set_ip_info(netif, &ip) != ESP_OK ||
        esp_netif_set_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns) != ESP_OK) {
      puts("Falha ao configurar IP estatico; usando DHCP.");
      esp_netif_dhcpc_start(netif);
    }
  }

  wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
  if (esp_wifi_init(&init) != ESP_OK) return false;
  esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, eventoRede, NULL);
  esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, eventoRede, NULL);

  wifi_config_t wifi = {0};
  strlcpy((char *)wifi.sta.ssid, WIFI_SSID, sizeof(wifi.sta.ssid));
  strlcpy((char *)wifi.sta.password, WIFI_SENHA, sizeof(wifi.sta.password));
  if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK ||
      esp_wifi_set_config(WIFI_IF_STA, &wifi) != ESP_OK) return false;
  return esp_wifi_start() == ESP_OK;
}

static bool criarClienteMqtt(void) {
  // Identificador exclusivo baseado no MAC de fabrica.
  uint64_t mac = 0;
  esp_efuse_mac_get_default((uint8_t *)&mac);
  static char id[32];
  snprintf(id, sizeof(id), "edge_ia-%012llx", (unsigned long long)mac);

  esp_mqtt_client_config_t cfg = {0};
  cfg.broker.address.uri = MQTT_URI;
  cfg.credentials.client_id = id;
  cfg.session.keepalive = KEEPALIVE_S;
  cfg.session.last_will.topic = TOPICO_ESTADO;
  cfg.session.last_will.msg = "offline";
  cfg.session.last_will.qos = 1;
  cfg.session.last_will.retain = 1;
  cfg.network.reconnect_timeout_ms = RECONEXAO_MS;
  // Prioridade 1: abaixo do controle, para rede nao atrasar os servos.
  cfg.task.priority = 1;

  mqtt = esp_mqtt_client_init(&cfg);
  if (mqtt == NULL) return false;
  return esp_mqtt_client_register_event(mqtt, MQTT_EVENT_ANY, eventoMqtt, NULL) == ESP_OK;
}

// Publica a fila de status. Fica separada do controle porque a publicacao
// pode bloquear enquanto o esp-mqtt usa o socket.
static void taskMqtt(void *parameter) {
  (void)parameter;
  for (;;) {
    if (mqttConectado) {
      const char *mensagem = NULL;
      for (uint8_t n = 0; n < 8 && xQueueReceive(filaMqtt, &mensagem, 0) == pdTRUE; ++n) {
        esp_mqtt_client_publish(mqtt, TOPICO_STATUS, mensagem, 0, 0, 0);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

bool mqtt_init(TaskHandle_t controle, QueueHandle_t comandos) {
  if (!controle || !comandos) return false;
  filaComandos = comandos;
  controleHandle = controle;
  if (!criarClienteMqtt()) return false;

  filaMqtt = xQueueCreate(TAMANHO_FILA, sizeof(const char *));
  if (filaMqtt == NULL) {
    return false;
  }

  if (xTaskCreate(taskMqtt, "MQTT", 4096, NULL, 1, NULL) != pdPASS) {
    vQueueDelete(filaMqtt);
    filaMqtt = NULL;
    return false;
  }

  // Wi-Fi por ultimo: o evento de IP inicia o cliente ja criado.
  return conectarWifi();
}
