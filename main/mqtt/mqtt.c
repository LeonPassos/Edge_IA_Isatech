#include "../main.h"

static const char *TAG = "mqtt";

static esp_mqtt_client_handle_t s_client = NULL;
static atomic_bool s_iniciado = false;
static atomic_bool s_conectado = false;

static mqtt_on_conexao_t s_on_conexao = NULL;
static mqtt_on_mensagem_t s_on_mensagem = NULL;
static void *s_ctx = NULL;

static void log_erro(const esp_mqtt_error_codes_t *e)
{
    if (e == NULL) {
        ESP_LOGE(TAG, "Erro sem detalhes.");
    } else if (e->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
        const int sock = e->esp_transport_sock_errno;
        ESP_LOGE(TAG, "Erro de transporte: %s, errno %d (%s).",
                 esp_err_to_name(e->esp_tls_last_esp_err), sock, sock ? strerror(sock) : "-");
    } else if (e->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
        ESP_LOGE(TAG, "Broker recusou a conexao: codigo %d.", (int)e->connect_return_code);
    } else {
        ESP_LOGE(TAG, "Erro tipo %d.", (int)e->error_type);
    }
}

static void avisar_conexao(bool conectado)
{
    s_conectado = conectado;
    if (s_on_conexao) s_on_conexao(conectado, s_ctx);
}

// Executado na tarefa interna do esp-mqtt.
static void mqtt_event_handler(void *arg, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)base;
    const esp_mqtt_event_handle_t ev = event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "Conectado a %s.", MQTT_BROKER_URI);
        esp_mqtt_client_publish(s_client, MQTT_TOPICO_ESTADO, "online", 0, 1, 1);
        avisar_conexao(true);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "Desconectado do broker; nova tentativa em %d ms.", MQTT_RECONEXAO_MS);
        avisar_conexao(false);
        break;
    case MQTT_EVENT_DATA:
        // Mensagens maiores que o buffer chegam em partes; nao sao suportadas.
        if (ev->current_data_offset != 0 || ev->data_len != ev->total_data_len) {
            if (ev->current_data_offset == 0) {
                ESP_LOGW(TAG, "Mensagem de %d bytes ignorada: maior que o buffer.", ev->total_data_len);
            }
            break;
        }
        if (s_on_mensagem) s_on_mensagem(ev->topic, ev->topic_len, ev->data, ev->data_len, s_ctx);
        break;
    case MQTT_EVENT_ERROR:
        log_erro(ev->error_handle);
        break;
    default:
        break;
    }
}

// O exchange evita start/stop repetidos: STA_DISCONNECTED chega a cada
// tentativa de reconexao do Wi-Fi.
static void mqtt_start(void)
{
    if (atomic_exchange(&s_iniciado, true)) return;
    ESP_LOGI(TAG, "Wi-Fi com IP; conectando a %s...", MQTT_BROKER_URI);
    if (esp_mqtt_client_start(s_client) != ESP_OK) {
        s_iniciado = false;
        ESP_LOGE(TAG, "Falha ao iniciar o cliente.");
    }
}

static void mqtt_stop(void)
{
    if (!atomic_exchange(&s_iniciado, false)) return;
    esp_mqtt_client_stop(s_client);
    // stop nao gera MQTT_EVENT_DISCONNECTED.
    if (s_conectado) avisar_conexao(false);
    ESP_LOGW(TAG, "Wi-Fi caiu; cliente parado ate o proximo IP.");
}

// Executado no loop de eventos padrao, junto com o handler do wifi.c.
static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)data;
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        mqtt_start();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        mqtt_stop();
    }
}


esp_err_t mqtt_init(void)
{
    if (s_client != NULL) return ESP_ERR_INVALID_STATE;

    // O esp-mqtt copia as strings da configuracao; o buffer local basta.
    uint8_t mac[6] = {0};
    esp_efuse_mac_get_default(mac);
    char id[24];
    snprintf(id, sizeof(id), "esp32-%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    const esp_mqtt_client_config_t cfg = {
        .broker.address.uri = MQTT_BROKER_URI,
        .credentials.client_id = id,
        .session.keepalive = MQTT_KEEPALIVE_S,
        .session.last_will = {
            .topic = MQTT_TOPICO_ESTADO,
            .msg = "offline",
            .qos = 1,
            .retain = 1,
        },
        .network.reconnect_timeout_ms = MQTT_RECONEXAO_MS,
        .task.priority = MQTT_PRIORIDADE,
    };

    s_client = esp_mqtt_client_init(&cfg);
    if (s_client == NULL) {
      return ESP_FAIL;
    }

    esp_err_t err = esp_mqtt_client_register_event(s_client, MQTT_EVENT_ANY, mqtt_event_handler, NULL);
    if (err == ESP_OK) err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL);
    if (err == ESP_OK) err = esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, wifi_event_handler, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao registrar eventos (%s). wifi_init_sta() foi chamado antes?", esp_err_to_name(err));
        return err;
    }

    // O IP pode ter chegado antes do registro dos handlers.
    if (wifi_got_ip()) {
        mqtt_start();
    } else {
        ESP_LOGI(TAG, "Aguardando IP do Wi-Fi para conectar a %s.", MQTT_BROKER_URI);
    }
    
    return ESP_OK;
}

void mqtt_callbacks(mqtt_on_conexao_t on_conexao, mqtt_on_mensagem_t on_mensagem, void *ctx)
{
    s_ctx = ctx;
    s_on_conexao = on_conexao;
    s_on_mensagem = on_mensagem;
}

bool if_mqtt_conectado(void)
{
    return s_conectado;
}

esp_err_t mqtt_subscribe(const char *topico, int qos)
{
    if (topico == NULL) return ESP_ERR_INVALID_ARG;
    if (!s_conectado) return ESP_ERR_INVALID_STATE;
    return esp_mqtt_client_subscribe(s_client, topico, qos) < 0 ? ESP_FAIL : ESP_OK;
}

esp_err_t mqtt_publish(const char *topico, const char *mensagem, int qos, bool retain)
{
    if (topico == NULL || mensagem == NULL) return ESP_ERR_INVALID_ARG;
    if (!s_conectado) return ESP_ERR_INVALID_STATE;
    return esp_mqtt_client_publish(s_client, topico, mensagem, 0, qos, retain) < 0 ? ESP_FAIL : ESP_OK;
}
