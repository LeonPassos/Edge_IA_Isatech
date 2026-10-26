#pragma once

#include <stdbool.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

// Inicia Wi-Fi e cliente MQTT em segundo plano (esp_wifi + esp-mqtt).
bool mqtt_init(TaskHandle_t controle, QueueHandle_t comandos);

// Envia uma mensagem estatica para publicacao no topico de status.
// Nao bloqueia: fila cheia ou MQTT nao iniciado descarta a mensagem.
void publicarMqtt(const char *mensagem);

void task_received_message(const char *mensagem, size_t tamanho);
