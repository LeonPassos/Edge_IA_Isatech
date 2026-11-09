#ifndef MQTT_H
#define MQTT_H

#include <stdbool.h>
#include <esp_err.h>
#include "wifi/wifi.h"
#include "../main.h"

#define MQTT_BROKER_URI     "mqtt://192.168.10.110:1883"
#define MQTT_KEEPALIVE_S    15
#define MQTT_RECONEXAO_MS   5000
#define MQTT_PRIORIDADE     1     // Abaixo do controle (2): rede nao atrasa os servos.



// Callbacks rodam na tarefa interna do esp-mqtt: devem ser curtos.
typedef void (*mqtt_on_conexao_t)(bool conectado, void *ctx);
// topico e dados NAO terminam em '\0'; use os tamanhos.
typedef void (*mqtt_on_mensagem_t)(const char *topico, int topico_len, const char *dados, int dados_len, void *ctx);

// Configura o cliente e passa a acompanhar o Wi-Fi: conecta ao broker quando
// recebe IP e para quando o Wi-Fi cai. Chamar depois de wifi_init_sta().
esp_err_t mqtt_init(void);

// Opcional. Pode ser chamado antes ou depois de mqtt_init(). Assine os
// topicos em on_conexao(true): a cada reconexao as assinaturas sao refeitas.
void mqtt_callbacks(mqtt_on_conexao_t on_conexao, mqtt_on_mensagem_t on_mensagem, void *ctx);

bool if_mqtt_conectado(void);

// Retornam ESP_ERR_INVALID_STATE se nao conectado. Podem bloquear enquanto
// o esp-mqtt usa o socket; nao chamar de tarefas com tempo critico.
esp_err_t mqtt_subscribe(const char *topico, int qos);
esp_err_t mqtt_publish(const char *topico, const char *mensagem, int qos, bool retain);

#endif // MQTT_H
