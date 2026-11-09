#ifndef WIFI_H
#define WIFI_H

#include "../main.h"

// Inicia NVS, netif, loop de eventos e Wi-Fi em modo estacao.
// Retorna antes de conectar; a reconexao e automatica.
esp_err_t wifi_init_sta(void);

bool wifi_got_ip(void);

// true depois de receber IP; volta a false ao desconectar.
bool wifi_conectado(void);

#endif // WIFI_H
