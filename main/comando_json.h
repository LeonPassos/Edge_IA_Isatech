#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define COMANDO_JSON_MAX 256

typedef enum {
  JSON_INICIAR, JSON_PARAR, JSON_EIXO, JSON_ABRIR_GARRA,
  JSON_FECHAR_GARRA, JSON_REPOUSO
} ComandoAcao;

typedef struct {
  ComandoAcao acao;
  uint8_t braco; // Indice: 0 ou 1.
  uint8_t eixo;  // Base, ombro, cotovelo, garra: 0..3.
  int angulo;
} ComandoJson;

// Parser independente dos atuadores. Nao altera saida em caso de erro.
// Exemplo: {"action":"mexer_ombro","braco":0,"angulo":90}
bool comandoJsonParse(const char *dados, size_t tamanho, ComandoJson *saida);
