# Controle dos bracos e esteira com FreeRTOS

Projeto ESP-IDF v5.4.4 em C para ESP32 (migrado do sketch Arduino com o mesmo
comportamento). Usa o FreeRTOS do proprio ESP-IDF.

## Compilar e gravar

No terminal do ESP-IDF (ou pela extensao ESP-IDF do VS Code):

    idf.py set-target esp32   # somente na primeira vez
    idf.py build
    idf.py -p COM5 flash monitor

No monitor, digite i ou p. Ctrl+] sai do monitor.

| Arduino | ESP-IDF |
| --- | --- |
| ESP32Servo | main/servo.c (LEDC, 50 Hz, 1000 a 2000 us) |
| Serial | driver UART0 a 115200 baud + printf/puts |
| WiFi.h + PubSubClient | esp_wifi + esp-mqtt (main/mqtt.c) |
| setup()/loop() | app_main() em main/main.c |
| millis() | esp_timer_get_time() / 1000 |

sdkconfig.defaults fixa tick de 1000 Hz (igual ao Arduino), particao de
aplicacao de 1,5 MB e log do IDF em nivel WARN.

## Organizacao

- taskControle: prioridade 2, stack de 4096 bytes, periodo solicitado de 10 ms
  usando vTaskDelayUntil. Processa comandos, atualiza a rampa suave e avanca
  a sequencia. E a unica tarefa que acessa os atuadores e o estado do ciclo.
- taskSerial: prioridade 1, stack de 4096 bytes. Recebe i/p a 115200 baud,
  envia notificacoes de bits ao controle e imprime mensagens de uma fila.
- Fila de 24 mensagens: transporta ponteiros para textos estaticos. O envio
  nao bloqueia o controle; mensagens sao descartadas se a fila estiver cheia.
- app_main valida a montagem, inicializa os servos e cria os recursos RTOS.
  Falha de alocacao mantem a esteira desligada e nao aceita ciclos.
- app_main retorna ao fim da inicializacao. As tarefas sao criadas sem fixar
  um nucleo.

Se iniciar e parar estiverem pendentes na mesma leitura, parar prevalece.
Comandos iguais pendentes sao agrupados, sem enfileirar ciclos futuros.
O periodo e quantizado pelo tick do FreeRTOS e pode sofrer atraso de
escalonamento; nao foi medida uma garantia de prazo no hardware.
As rampas e tempos da sequencia continuam baseados em millis().

## Configuracao preservada da versao funcional

Somente o primeiro braco esta ativo (BRACOS_ATIVOS = 1).

| Eixo | GPIO | Angulo inicial |
| --- | --- | --- |
| Base | 13 | 90 |
| Ombro | 14 | 90 |
| Cotovelo | 27 | 90 |
| Garra | 26 | 135 |

Rele no GPIO 25, ativo em LOW. Velocidade comandada: 70 graus/s.
Garra 1 aberta: 30 graus; fechada: 120 graus. Os limites individuais estao
em cfg; cfgSegundoBraco aguarda pinos exclusivos antes de ativar dois bracos.
As poses, tabela de etapas e calculo da rampa foram mantidos no refactor.

## Operacao

No monitor serial em 115200 baud, i inicia um ciclo e p cancela a sequencia,
desliga a esteira e interrompe a rampa no ultimo angulo enviado. Isso nao
mede nem interrompe instantaneamente o movimento mecanico do servo.
Reposicione os bracos e o cubo antes de reiniciar um ciclo cancelado.

O braco 1 pega, levanta, entrega e retorna; a esteira transporta por tempo;
o braco 2 executa a retirada quando configurado e ativado. MOVIMENTO_MS e
GARRA_MS sao pausas de acomodacao apos os movimentos. Sem sensores, nao ha
confirmacao de pega ou chegada. Na energizacao pode haver deslocamento
rapido ate a posicao inicial, pois a posicao real nao e lida.

Use fonte externa adequada aos servos, com GND comum a ESP32. Nao alimente
motores pelo 3V3. Use modulo rele compativel com logica de 3,3 V, contatos
adequados a corrente de partida e supressao de transientes do motor DC.

## MQTT

main/mqtt.c conecta ao Wi-Fi (esp_wifi, reconexao automatica) e ao broker
test.mosquitto.org:1883 com o cliente esp-mqtt do IDF. A tarefa interna do
esp-mqtt roda com prioridade 1 e reconecta a cada 5 s; keepalive de 15 s.
Uma tarefa MQTT propria (prioridade 1) publica a fila de status.

| Topico | Direcao | Conteudo |
| --- | --- | --- |
| edge_ia/comando | recebe | i ou iniciar; p ou parar |
| edge_ia/status | publica | as mesmas mensagens do Monitor Serial |
| edge_ia/estado | publica (retido) | online; offline via Last Will |

Teste: `mosquitto_sub -h test.mosquitto.org -t "edge_ia/#" -v` e
`mosquitto_pub -h test.mosquitto.org -t edge_ia/comando -m i`.
O broker e publico: qualquer pessoa pode enviar comandos nesses topicos.
Credenciais Wi-Fi e IP estatico opcional ficam no inicio de main/mqtt.c.

## Verificacao

Configuracao, poses, sequencia e calculo da rampa foram mantidos da versao
Arduino. Compila com ESP-IDF v5.4.4 sem avisos; a versao IDF ainda nao foi
testada em hardware.

Referencias:
- https://docs.espressif.com/projects/esp-idf/en/v5.4.4/esp32/api-reference/peripherals/ledc.html
- https://docs.espressif.com/projects/esp-idf/en/v5.4.4/esp32/api-reference/protocols/mqtt.html
- https://docs.espressif.com/projects/esp-idf/en/v5.4.4/esp32/api-reference/system/freertos.html
