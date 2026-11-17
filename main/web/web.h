#ifndef WEB_H
#define WEB_H

#include "../main.h"

// Pagina de selecao do braco e do motor controlados pelo joystick.
// Servidor HTTP na porta 80, na mesma rede (STA) do MQTT.
//   GET  /            pagina HTML (main/web/index.html, embutida no firmware)
//   GET  /estado      {"braco":0,"motor":0,"bracos":2}
//   POST /selecionar?braco=N&motor=M   (qualquer um dos dois pode faltar)

typedef struct {
  uint8_t braco; // 0..BRACOS_ATIVOS-1
  uint8_t motor; // 0 base, 1 ombro, 2 cotovelo, 3 garra
} SelecaoJoystick;

// Callbacks rodam na tarefa do servidor HTTP: devem ser curtos e nao
// podem mexer em atuadores; apenas repassar ao controle.
typedef SelecaoJoystick (*web_ler_estado_t)(void *ctx);
typedef void (*web_on_selecao_t)(SelecaoJoystick selecao, void *ctx);

// Chamar depois de wifi_init_sta(). O link e logado a cada IP recebido.
esp_err_t web_init(web_ler_estado_t ler_estado, web_on_selecao_t on_selecao, uint8_t bracos, void *ctx);

#endif // WEB_H
