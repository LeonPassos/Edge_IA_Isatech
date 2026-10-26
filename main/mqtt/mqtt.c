#include "mqtt/mqtt.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <mqtt_client.h>

static const char *TAG = "mqtt";

static esp_mqtt_client_handle_t s_client = NULL;
static mqtt_config_t s_config;
static atomic_bool s_iniciado = false;
static atomic_bool s_conectado = false;

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
    if (s_config.on_conexao) s_config.on_conexao(conectado, s_config.ctx);
}

// Executado na tarefa interna do esp-mqtt.
static void mqtt_event_handler(void *arg, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)base;
    const esp_mqtt_event_handle_t ev = event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "Conectado a %s.", s_config.uri);
        avisar_conexao(true);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "Desconectado do broker.");
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
        if (s_config.on_mensagem) {
            s_config.on_mensagem(ev->topic, ev->topic_len, ev->data, ev->data_len, s_config.ctx);
        }
        break;
    case MQTT_EVENT_ERROR:
        log_erro(ev->error_handle);
        break;
    default:
        break;
    }
}

esp_err_t mqtt_init(const mqtt_config_t *config)
{
    if (config == NULL || config->uri == NULL) return ESP_ERR_INVALID_ARG;
    if (s_client != NULL) return ESP_ERR_INVALID_STATE;
    s_config = *config;

    // O esp-mqtt copia as strings da configuracao; o buffer local basta.
    char id[24];
    const char *client_id = config->client_id;
    if (client_id == NULL) {
        uint8_t mac[6] = {0};
        esp_efuse_mac_get_default(mac);
        snprintf(id, sizeof(id), "esp32-%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        client_id = id;
    }

    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = config->uri,
        .credentials.client_id = client_id,
        .session.keepalive = config->keepalive_s,
        .network.reconnect_timeout_ms = config->reconexao_ms,
        .task.priority = config->prioridade_tarefa,
    };
    if (config->lwt_topico != NULL) {
        cfg.session.last_will.topic = config->lwt_topico;
        cfg.session.last_will.msg = config->lwt_mensagem;
        cfg.session.last_will.qos = config->lwt_qos;
        cfg.session.last_will.retain = config->lwt_retain;
    }

    s_client = esp_mqtt_client_init(&cfg);
    if (s_client == NULL) return ESP_FAIL;
    const esp_err_t err = esp_mqtt_client_register_event(s_client, MQTT_EVENT_ANY, mqtt_event_handler, NULL);
    if (err != ESP_OK) {
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
    }
    return err;
}

esp_err_t mqtt_start(void)
{
    if (s_client == NULL) return ESP_ERR_INVALID_STATE;
    if (atomic_exchange(&s_iniciado, true)) return ESP_OK;
    ESP_LOGI(TAG, "Conectando a %s...", s_config.uri);
    const esp_err_t err = esp_mqtt_client_start(s_client);
    if (err != ESP_OK) s_iniciado = false;
    return err;
}

esp_err_t mqtt_stop(void)
{
    if (s_client == NULL) return ESP_ERR_INVALID_STATE;
    if (!atomic_exchange(&s_iniciado, false)) return ESP_OK;
    const esp_err_t err = esp_mqtt_client_stop(s_client);
    // stop nao gera MQTT_EVENT_DISCONNECTED.
    if (s_conectado) avisar_conexao(false);
    ESP_LOGI(TAG, "Cliente parado.");
    return err;
}

bool mqtt_conectado(void)
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
