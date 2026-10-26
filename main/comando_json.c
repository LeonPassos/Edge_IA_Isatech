#include "comando_json.h"

#include <math.h>
#include <string.h>
#include <cJSON.h>

static bool inteiro(const cJSON *item, int minimo, int maximo) {
  return cJSON_IsNumber(item) && isfinite(item->valuedouble) && 
        item->valuedouble >= minimo && item->valuedouble <= maximo &&
        floor(item->valuedouble) == item->valuedouble;
}

bool comandoJsonParse(const char *dados, size_t tamanho, ComandoJson *saida) {
  if (!dados || !saida || tamanho == 0 || tamanho > COMANDO_JSON_MAX || memchr(dados, '\0', tamanho)) return false;
  char texto[COMANDO_JSON_MAX + 1];
  memcpy(texto, dados, tamanho);
  texto[tamanho] = '\0';
  cJSON *json = cJSON_ParseWithOpts(texto, NULL, true);
  if (!json) return false;
  bool valido = false;
  ComandoJson comando = {0};
  const cJSON *action = cJSON_GetObjectItemCaseSensitive(json, "action");
  const cJSON *braco = cJSON_GetObjectItemCaseSensitive(json, "braco");
  const cJSON *angulo = cJSON_GetObjectItemCaseSensitive(json, "angulo");
  if (!cJSON_IsObject(json) || !cJSON_IsString(action)) goto fim;
  // Rejeitar campos desconhecidos e duplicados para evitar comandos ambiguos.
  unsigned campos = 0;
  for (const cJSON *campo = json->child; campo; campo = campo->next) {
    unsigned bit = strcmp(campo->string, "action") == 0 ? 1 :
                   strcmp(campo->string, "braco") == 0 ? 2 :
                   strcmp(campo->string, "angulo") == 0 ? 4 : 0;
    if (!bit || (campos & bit)) goto fim;
    campos |= bit;
  }
  if (braco && !inteiro(braco, 0, 1)) goto fim;
  if (braco) comando.braco = (uint8_t)braco->valueint;
  const char *acao = action->valuestring;
  if (strcmp(acao, "iniciar") == 0) comando.acao = JSON_INICIAR;
  else if (strcmp(acao, "parar") == 0) comando.acao = JSON_PARAR;
  else if (strcmp(acao, "abrir_garra") == 0) comando.acao = JSON_ABRIR_GARRA;
  else if (strcmp(acao, "fechar_garra") == 0) comando.acao = JSON_FECHAR_GARRA;
  else if (strcmp(acao, "repouso") == 0) comando.acao = JSON_REPOUSO;
  else {
    static const char *const nomes[] = {
      "mexer_base", "mexer_ombro", "mexer_cotovelo", "mexer_garra"
    };
    bool encontrado = false;
    for (uint8_t i = 0; i < 4; ++i) {
      if (strcmp(acao, nomes[i]) == 0) {
        comando.acao = JSON_EIXO;
        comando.eixo = i;
        encontrado = true;
        break;
      }
    }
    if (!encontrado || !inteiro(angulo, 0, 180)) goto fim;
    comando.angulo = angulo->valueint;
  }
  if (comando.acao != JSON_EIXO && angulo) goto fim;
  if ((comando.acao == JSON_INICIAR || comando.acao == JSON_PARAR) && braco) goto fim;
  *saida = comando;
  valido = true;
fim:
  cJSON_Delete(json);
  return valido;
}
