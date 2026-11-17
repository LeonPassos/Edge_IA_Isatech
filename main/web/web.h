#ifndef WEB_H
#define WEB_H

#include "../main.h"

// Pagina de controle: selecao do joystick, processo, esteira e carrinho.
// Servidor HTTP na porta 80, na mesma rede (STA) do MQTT.
//   GET  /            pagina HTML (main/web/index.html, embutida no firmware)
//   GET  /estado      {"braco":0,"motor":0,"bracos":2,"executando":false,"esteira":false,"mqtt":true}
//   POST /selecionar?braco=N&motor=M   (qualquer um dos dois pode faltar)
//   POST /acao?nome=iniciar|parar|carrinho|esteira_ligar|esteira_desligar

typedef struct {
  uint8_t braco; // 0..BRACOS_ATIVOS-1
  uint8_t motor; // 0 base, 1 ombro, 2 cotovelo, 3 garra
} SelecaoJoystick;

typedef struct {
  SelecaoJoystick selecao;
  bool executando; // Ciclo automatico em andamento.
  bool esteira;    // Rele da esteira ligado.
  bool mqtt;       // Conectado ao broker.
} EstadoWeb;

typedef enum {
  WEB_INICIAR,
  WEB_PARAR,
  WEB_CARRINHO,
  WEB_ESTEIRA_LIGAR,
  WEB_ESTEIRA_DESLIGAR
} WebAcao;

// Callbacks rodam na tarefa do servidor HTTP: devem ser curtos e nao
// podem mexer em atuadores; apenas repassar ao controle.
typedef struct {
  EstadoWeb (*ler_estado)(void *ctx);
  void (*on_selecao)(SelecaoJoystick selecao, void *ctx);
  esp_err_t (*on_acao)(WebAcao acao, void *ctx); // Erro vira HTTP 503.
  void *ctx;
} WebCallbacks;

// Chamar depois de wifi_init_sta(). O link e logado a cada IP recebido.
esp_err_t web_init(const WebCallbacks *callbacks, uint8_t bracos);

#endif // WEB_H
