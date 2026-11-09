#ifndef DEFINES_H
#define DEFINES_H

// Wi-Fi
#define ESP_WIFI_SSID      "Isatech"
#define ESP_WIFI_PASS      "@Isatech2027@"

//Testando com o celular
// #define ESP_WIFI_SSID      "S23 de Leonidas"
// #define ESP_WIFI_PASS      "12345678"

// Topicos da aplicacao. O estado online/offline fica em MQTT_TOPICO_ESTADO.
#define TOPICO_COMANDO      "isatech/comando" // i/iniciar, p/parar ou JSON
#define TOPICO_LOG          "isatech/log"     // mensagens do ciclo
#define TOPICO_CARRINHO     "isatech/carrinho" //comunicar com o carrinho para iniciar o ciclo de entrega
#define MQTT_TOPICO_ESTADO  "isatech/status" //online ou offline

// Bits de notificacao enviados a taskControle pelo MQTT.
// Se iniciar e parar estiverem pendentes juntos, parar prevalece.
#define CMD_INICIAR (1UL << 0)
#define CMD_PARAR   (1UL << 1)
#define CMD_JSON    (1UL << 2) // Ha comando novo em filaComandos.

#define SERVO_MIN_US 500
#define SERVO_MAX_US 2500

#define PIN_RELE GPIO_NUM_33
#define ATUALIZACAO_SERVO_MS 20
#define VELOCIDADE_GRAUS_S 60.0f
// Rele ativo em nivel baixo: IN em GND liga; IN em nivel alto desliga.
#define RELE_LIGADO 0
#define RELE_DESLIGADO 1
#define MOVIMENTO_MS 1500
#define GARRA_MS 700
#define ESTEIRA_MS 12000
#define ACOMODAR_MS 700

//#DEFINE ULTIMO PINO GPIO 33

// EXEMPLOS: calibrar todos os angulos para a geometria real, sem carga.
// Neste exemplo, abrir reduz o angulo. Calibrar em passos de 5 graus.
#define ABERTA_1 20
#define FECHADA_1 90
#define ABERTA_2 110
#define FECHADA_2 90
// Base, ombro e cotovelo de cada posicao; a garra vem em cada etapa.
#define REPOUSO                 90, 120, 90
#define REPOUSO_2               90, 180, 50
#define ACIMA_CUBO              0, 120, 90
#define PEGA_CUBO               0, 70,  110
#define ACIMA_ESTEIRA_1         90, 130, 115
#define ENTREGA_ESTEIRA         90, 100, 115

#define PARA_ESTEIRA_2          90, 180, 50
#define PEGA_CUBO_ESTEIRA_2     90, 155, 100
#define ACIMA_ESTEIRA_2         90, 180, 100
#define ENTREGA_SAIDA           180, 150, 120



// Depois do app_main, somente taskControle acessa atuadores e estado da sequencia.
#define PERIODO_CONTROLE_MS 10
#define MAX_DISTANCE_CM 50
#define LIMITE_DISTANCIA 11  
#define TRIGGER_GPIO 5
#define ECHO_GPIO 17
#define PERIODO_SENSOR_MS 50
#define VALIDADE_SENSOR_MS 50

#endif // DEFINES_H