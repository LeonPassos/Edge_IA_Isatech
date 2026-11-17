#ifndef JOYSTICK_H
#define JOYSTICK_H

#include "../main.h"

// Joystick analogico (KY-023 ou similar): VRx e VRy no ADC1, SW em GPIO
// digital ativo em nivel baixo. O ADC2 nao pode ser usado junto com o Wi-Fi.

typedef enum {
  JOY_SEM_EVENTO,
  JOY_CLIQUE,
  JOY_DUPLO_CLIQUE
} JoystickEvento;

typedef struct {
  int x;            // -100..100, ja com zona morta aplicada.
  int y;            // -100..100, ja com zona morta aplicada.
  bool pressionado; // Leitura bruta do botao, sem debounce.
} JoystickLeitura;

// Calibra o centro na inicializacao: nao mexa no joystick durante o boot.
esp_err_t joystick_init(void);
esp_err_t joystick_ler(JoystickLeitura *leitura);

// Debounce e deteccao de clique/duplo clique. Chamar em periodo fixo.
// O clique simples so e reportado depois de JOY_DUPLO_CLIQUE_MS sem
// segundo clique, para nao disparar junto com o duplo clique.
JoystickEvento joystick_evento_botao(bool pressionado, uint32_t agora_ms);

#endif // JOYSTICK_H
