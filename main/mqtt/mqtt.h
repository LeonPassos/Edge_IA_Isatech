#ifndef MQTT_H
#define MQTT_H

#include <stdbool.h>
#include <esp_err.h>

// Callbacks rodam na tarefa interna do esp-mqtt: devem ser curtos e nao
// podem chamar mqtt_stop().
typedef void (*mqtt_on_conexao_t)(bool conectado, void *ctx);
// topico e dados NAO terminam em '\0'; use os tamanhos.
typedef void (*mqtt_on_mensagem_t)(const char *topico, int topico_len,
                                   const char *dados, int dados_len, void *ctx);

typedef struct {
    const char *uri;          // Ex.: "mqtt://192.168.1.179:1883"
    const char *client_id;    // NULL: gera "esp32-<MAC>"
    int keepalive_s;          // 0: padrao do esp-mqtt (120 s)
    int reconexao_ms;         // 0: padrao do esp-mqtt (10 s)
    int prioridade_tarefa;    // 0: padrao do esp-mqtt (5)

    // Last Will opcional (lwt_topico NULL desativa).
    const char *lwt_topico;
    const char *lwt_mensagem;
    int lwt_qos;
    bool lwt_retain;

    mqtt_on_conexao_t on_conexao;   // Opcional
    mqtt_on_mensagem_t on_mensagem; // Opcional
    void *ctx;                      // Repassado aos callbacks
} mqtt_config_t;

// Cria o cliente sem conectar. Chamar uma vez. A uri deve continuar valida
// (use um literal); as demais strings sao copiadas.
esp_err_t mqtt_init(const mqtt_config_t *config);

// Conecta ao broker; reconecta sozinho ate mqtt_stop(). Exige rede com IP.
// Chamadas repetidas sao ignoradas.
esp_err_t mqtt_start(void);

// Desconecta e para a tarefa do cliente. Pode bloquear ate o fim da
// operacao de rede em curso. Chamadas repetidas sao ignoradas.
esp_err_t mqtt_stop(void);

bool mqtt_conectado(void);

// Retornam ESP_ERR_INVALID_STATE se nao conectado. Podem bloquear enquanto
// o esp-mqtt usa o socket; nao chamar de tarefas com tempo critico.
esp_err_t mqtt_subscribe(const char *topico, int qos);
esp_err_t mqtt_publish(const char *topico, const char *mensagem, int qos, bool retain);

#endif // MQTT_H
