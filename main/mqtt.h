#pragma once

#include "tipos.h"

// Inicia Wi-Fi e cliente MQTT em segundo plano (esp_wifi + esp-mqtt).
bool iniciarMqtt(TaskHandle_t controle);

// Envia uma mensagem estatica para publicacao no topico de status.
// Nao bloqueia: fila cheia ou MQTT nao iniciado descarta a mensagem.
void publicarMqtt(const char *mensagem);
