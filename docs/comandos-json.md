# Controle dos servos por JSON e MQTT

Publique em `edge_ia/comando`, sem retain. As respostas chegam em
`edge_ia/status`. Envie um objeto JSON por mensagem, com ate 256 bytes.

```json
{"action":"mexer_ombro","braco":0,"angulo":90}
```

`braco` e opcional (padrao 0). O indice 0 identifica o primeiro braco;
1 identifica o segundo, que so funciona quando configurado e ativado em
`main/main.c` (`BRACOS_ATIVOS`).

| action | Campos adicionais | Resultado |
| --- | --- | --- |
| `mexer_base` | `braco`, `angulo` | Move somente a base |
| `mexer_ombro` | `braco`, `angulo` | Move somente o ombro |
| `mexer_cotovelo` | `braco`, `angulo` | Move somente o cotovelo |
| `mexer_garra` | `braco`, `angulo` | Move a garra para um angulo |
| `abrir_garra` | `braco` | Usa `ABERTA_1` ou `ABERTA_2` |
| `fechar_garra` | `braco` | Usa `FECHADA_1` ou `FECHADA_2` |
| `repouso` | `braco` | Usa a pose `REPOUSO` com a garra aberta |
| `iniciar` | Nenhum | Inicia a sequencia automatica |
| `parar` | Nenhum | Interrompe movimento e ciclo, desliga esteira e limpa a fila |

`angulo` e obrigatorio nas acoes `mexer_*`: inteiro entre 0 e 180,
tambem respeitando os limites configurados do servo. Os outros eixos
mantem a posicao. Campos desconhecidos, duplicados e tipos incorretos
sao rejeitados. Os nomes das acoes diferenciam maiusculas de minusculas.

Exemplos (publique cada linha separadamente, aguardando a conclusao):

```json
{"action":"abrir_garra","braco":0}
{"action":"fechar_garra","braco":0}
{"action":"repouso","braco":0}
{"action":"iniciar"}
{"action":"parar"}
```

`repouso` corresponde a `{MOVER, 0, {REPOUSO, ABERTA_1}, ...}` para o
primeiro braco. O movimento usa a velocidade e a rampa suave existentes;
o JSON nao adiciona o tempo de acomodacao `MOVIMENTO_MS` da sequencia.

Enquanto houver movimento ou ciclo em andamento, novos movimentos manuais
sao recusados com uma mensagem de controle ocupado. A fila de transporte
tem oito entradas; ela nao agenda uma sequencia de movimentos. Aguarde
`Comando JSON: movimento concluido.` antes do proximo movimento manual.
`parar` tem prioridade sobre `iniciar` quando ambos estao pendentes.
Os comandos de texto `i`/`iniciar` e `p`/`parar` continuam disponiveis.

Exemplo no PowerShell com o cliente Mosquitto instalado:

```powershell
mosquitto_sub -h 192.168.1.179 -t edge_ia/status
# Em outro terminal:
'{"action":"mexer_ombro","braco":0,"angulo":90}' | mosquitto_pub -h 192.168.1.179 -t edge_ia/comando -s
```

A biblioteca `main/comando_json.h` expoe `comandoJsonParse(dados, tamanho,
&comando)`: recebe bytes com tamanho explicito, valida e devolve um
`ComandoJson` por valor. Ela nao acessa servos nem MQTT. `mqtt.c` reconstroi
payloads fragmentados e envia o comando por fila FreeRTOS; somente
`taskControle`, em `main.c`, acessa os atuadores.
