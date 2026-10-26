# Edge IA ? controle de bracos e esteira

Firmware em C para ESP32 com ESP-IDF 5.4.4 e FreeRTOS.
A entrada da aplicacao e `app_main()`. Servos usam PWM LEDC de 50 Hz;
Wi-Fi usa `esp_wifi` e MQTT usa `esp-mqtt`, a unica entrada de comandos.

## Compilar e gravar

Abra um terminal com o ambiente ESP-IDF exportado, ou use a extensao
ESP-IDF do VS Code. O alvo configurado e `esp32` e a porta atual e `COM5`.

```powershell
idf.py build
idf.py -p COM5 flash monitor
```

Se a pasta `build` tiver cache de outro gerador CMake, use uma pasta nova,
sem precisar apagar o build anterior:

```powershell
idf.py -B build/idf build
idf.py -B build/idf -p COM5 flash monitor
```

A validacao desta reorganizacao usa `idf.py -B build/json-check build`.
Use `Ctrl+]` para sair do monitor. O console opera a 115200 baud.
`sdkconfig.defaults` define tick FreeRTOS de 1 ms, flash de 4 MB, particao
de aplicacao de 1500 KB e logs de componentes a partir de WARN.

## Estrutura

| Arquivo ou pasta | Responsabilidade |
| --- | --- |
| `main/main.c` | Inicializacao, tarefas, poses, sequencia e controle dos atuadores |
| `main/app_config.h` | Pulsos PWM, tempos, polaridade do rele e bits de comando |
| `main/servo.c` e `servo.h` | Driver de servo posicional com LEDC |
| `main/comando_json.c` e `.h` | Parser e validacao dos comandos JSON |
| `main/mqtt.c` e `.h` | Wi-Fi, cliente MQTT e filas de comunicacao |
| `components/ultrasonic/` | Componente nativo de medicao de distancia |
| `docs/` | Protocolo, comportamento da esteira e datasheet |

Cada cabecalho declara seus includes necessarios. O driver do sensor e
registrado automaticamente a partir de `components/`, sem depender de
cabecalhos da aplicacao. Os arquivos de terceiros conservam os avisos de
licenca existentes.

## Tarefas e sincronizacao

- `taskControle`: prioridade 2, periodo de 10 ms. Processa comandos,
  interpola os angulos, avanca a sequencia e controla o rele. Depois da
  inicializacao, somente essa tarefa altera os atuadores.
- `taskLog`: prioridade 1. Imprime a fila de mensagens no monitor serial
  sem bloquear o controle.
- `task_esteira_with_ultrasonic`: prioridade 1. Mede em centimetros, espera
  100 ms entre medicoes e envia a leitura mais recente por uma fila.
- A tarefa interna do cliente MQTT recebe os comandos. Uma tarefa de
  publicacao separada esvazia a fila de status.

Comandos de iniciar/parar usam notificacoes FreeRTOS; parar prevalece se
ambos estiverem pendentes. Movimentos manuais usam fila de comandos por
valor. Mensagens de status usam ponteiros para textos estaticos e podem
ser descartadas se as filas estiverem cheias.

Tempos e rampas usam `esp_timer_get_time()`, convertido para milissegundos.
Os periodos das tarefas sao sujeitos ao escalonamento; o driver do sensor
usa uma regiao critica durante a captura do eco.

## Montagem e calibracao

Somente o primeiro braco esta ativo (`BRACOS_ATIVOS = 1`).

| Eixo | GPIO | Angulo inicial |
| --- | --- | --- |
| Base | 13 | 90 |
| Ombro | 14 | 60 |
| Cotovelo | 27 | 90 |
| Garra | 26 | 60 |

Rele: GPIO 25, ativo em nivel baixo. Sensor: trigger GPIO 5 e echo GPIO 18.
Os pulsos efetivamente usados na conexao dos servos sao `SERVO_MIN_US` e
`SERVO_MAX_US` em `main/app_config.h`. As constantes por eixo tambem estao
nesse arquivo, mas ainda nao sao usadas pela chamada de conexao.
Calibre os pulsos e as poses conforme a montagem antes de executar ciclos.
A posicao fisica inicial nao e medida por este firmware.

## Operacao

`i` ou `iniciar` pelo MQTT inicia um ciclo; `p` ou `parar` cancela o ciclo,
interrompe a rampa e desliga a esteira. O monitor serial apenas exibe logs.
Reposicione a montagem antes de reiniciar um ciclo cancelado.

Os comandos JSON permitem movimentos individuais e poses:
[protocolo e exemplos](docs/comandos-json.md).
`MOVIMENTO_MS` e `GARRA_MS` sao pausas apos o movimento; a velocidade da
rampa e configurada por `VELOCIDADE_GRAUS_S`.

A esteira opera apenas na etapa prevista e com leitura valida do sensor.
Para ao detectar menos de 12 cm ou ao atingir `ESTEIRA_MS`, atualmente
12000 ms. Falha de leitura ou leitura vencida durante o funcionamento
cancela o ciclo. Consulte [o controle do sensor](docs/esteira-ultrassonico.md).

## Rede e MQTT

SSID, senha, URI do broker e opcao de IP estatico ficam em `main/mqtt.c`.
O broker configurado atualmente e `mqtt://192.168.1.179:1883`.
O cliente usa DHCP, reconexao MQTT a cada 5 segundos e keepalive de 15 s.

| Topico | Direcao | Conteudo |
| --- | --- | --- |
| `edge_ia/comando` | Recebe | JSON ou comandos de iniciar/parar, sem retain |
| `edge_ia/status` | Publica | Mensagens do controle |
| `edge_ia/estado` | Publica, retido | `online` e Last Will `offline` |

```powershell
mosquitto_sub -h 192.168.1.179 -t "edge_ia/#" -v
```

O broker precisa aceitar as credenciais utilizadas pelo cliente e estar
acessivel da ESP32 na porta TCP 1883.

## Validacao

A compilacao verifica o firmware e gera o binario. O funcionamento fisico
dos servos, sensor e rele deve ser conferido na bancada apos gravar.
