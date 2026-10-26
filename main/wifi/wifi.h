#ifndef WIFI_H
#define WIFI_H

#include <stdbool.h>

#define ESP_WIFI_SSID      "Isatech"
#define ESP_WIFI_PASS      "@Isatech2027@"

// Inicia NVS, netif, loop de eventos e Wi-Fi em modo estacao.
// Retorna antes de conectar; a reconexao e automatica.
void wifi_init_sta(void);

// true depois de receber IP; volta a false ao desconectar.
bool wifi_conectado(void);

#endif // WIFI_H
