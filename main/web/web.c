#include "web/web.h"
#include <esp_http_server.h>

static const char *TAG = "web";

// Gerado por EMBED_TXTFILES no CMakeLists (termina em '\0').
extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[] asm("_binary_index_html_end");

static httpd_handle_t servidor = NULL;
static web_ler_estado_t lerEstado = NULL;
static web_on_selecao_t aoSelecionar = NULL;
static void *contexto = NULL;
static uint8_t totalBracos = 1;

static esp_err_t paginaHandler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  return httpd_resp_send(req, index_html_start, index_html_end - index_html_start - 1);
}

static esp_err_t estadoHandler(httpd_req_t *req) {
  const SelecaoJoystick s = lerEstado(contexto);
  char json[64];
  snprintf(json, sizeof(json), "{\"braco\":%u,\"motor\":%u,\"bracos\":%u}", s.braco, s.motor, totalBracos);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_sendstr(req, json);
}

// Le um inteiro 0..maximo da query. Retorna false se ausente ou invalido.
static bool lerParametro(const char *query, const char *chave, int maximo, uint8_t *saida, bool *invalido) {
  char valor[8];
  if (httpd_query_key_value(query, chave, valor, sizeof(valor)) != ESP_OK) return false;
  char *fim = NULL;
  const long n = strtol(valor, &fim, 10);
  if (fim == valor || *fim != '\0' || n < 0 || n > maximo) {
    *invalido = true;
    return false;
  }
  *saida = (uint8_t)n;
  return true;
}

static esp_err_t selecionarHandler(httpd_req_t *req) {
  char query[48];
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Use ?braco=N&motor=M");
  }
  SelecaoJoystick s = lerEstado(contexto);
  bool invalido = false;
  const bool temBraco = lerParametro(query, "braco", totalBracos - 1, &s.braco, &invalido);
  const bool temMotor = lerParametro(query, "motor", 3, &s.motor, &invalido);
  if (invalido || (!temBraco && !temMotor)) {
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "braco ou motor invalido");
  }
  aoSelecionar(s, contexto);
  httpd_resp_set_status(req, "204 No Content");
  return httpd_resp_send(req, NULL, 0);
}

static void aoReceberIp(void *arg, esp_event_base_t base, int32_t id, void *data) {
  (void)arg; (void)base; (void)id;
  const ip_event_got_ip_t *ev = data;
  ESP_LOGI(TAG, "Pagina de controle: http://" IPSTR "/", IP2STR(&ev->ip_info.ip));
}

esp_err_t web_init(web_ler_estado_t ler_estado, web_on_selecao_t on_selecao, uint8_t bracos, void *ctx) {
  if (servidor != NULL) return ESP_ERR_INVALID_STATE;
  if (ler_estado == NULL || on_selecao == NULL || bracos == 0) return ESP_ERR_INVALID_ARG;
  lerEstado = ler_estado;
  aoSelecionar = on_selecao;
  totalBracos = bracos;
  contexto = ctx;

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.task_priority = 1; // Abaixo do controle (2): rede nao atrasa os servos.
  config.lru_purge_enable = true;
  esp_err_t erro = httpd_start(&servidor, &config);
  if (erro != ESP_OK) return erro;

  const httpd_uri_t rotas[] = {
    {.uri = "/", .method = HTTP_GET, .handler = paginaHandler},
    {.uri = "/estado", .method = HTTP_GET, .handler = estadoHandler},
    {.uri = "/selecionar", .method = HTTP_POST, .handler = selecionarHandler},
  };
  for (size_t i = 0; i < sizeof(rotas) / sizeof(rotas[0]); ++i) {
    httpd_register_uri_handler(servidor, &rotas[i]);
  }

  erro = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &aoReceberIp, NULL, NULL);
  if (erro != ESP_OK) return erro;

  // Se o IP chegou antes do registro, loga agora.
  esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  esp_netif_ip_info_t ip = {0};
  if (sta != NULL && esp_netif_get_ip_info(sta, &ip) == ESP_OK && ip.ip.addr != 0) {
    ESP_LOGI(TAG, "Pagina de controle: http://" IPSTR "/", IP2STR(&ip.ip));
  } else {
    ESP_LOGI(TAG, "Servidor HTTP iniciado; o link aparece quando o Wi-Fi receber IP.");
  }
  return ESP_OK;
}
