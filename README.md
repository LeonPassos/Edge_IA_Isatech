# Controle dos bracos e esteira com FreeRTOS

Sketch Arduino para ESP32 com ESP32Servo. Usa o FreeRTOS que ja acompanha
o core Arduino-ESP32; nao e necessario instalar outro RTOS.

## Organizacao

- taskControle: prioridade 2, stack de 4096 bytes, periodo solicitado de 10 ms
  usando vTaskDelayUntil. Processa comandos, atualiza a rampa suave e avanca
  a sequencia. E a unica tarefa que acessa os atuadores e o estado do ciclo.
- taskSerial: prioridade 1, stack de 4096 bytes. Recebe i/p a 115200 baud,
  envia notificacoes de bits ao controle e imprime mensagens de uma fila.
- Fila de 24 mensagens: transporta ponteiros para textos estaticos. O envio
  nao bloqueia o controle; mensagens sao descartadas se a fila estiver cheia.
- setup valida a montagem, inicializa os servos e cria os recursos RTOS.
  Falha de alocacao mantem a esteira desligada e nao aceita ciclos.
- loop fica bloqueado. As tarefas sao criadas sem fixar um nucleo.

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

Instale ESP32Servo pelo Gerenciador de Bibliotecas da Arduino IDE.
No Monitor Serial em 115200 baud, i inicia um ciclo e p cancela a sequencia,
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

mqtt.cpp conecta ao Wi-Fi e ao broker test.mosquitto.org:1883 com
PubSubClient, em uma tarefa propria (prioridade 1, stack 6144 bytes) que
reconecta sozinha a cada 5 s. Somente essa tarefa usa o cliente MQTT.

| Topico | Direcao | Conteudo |
| --- | --- | --- |
| edge_ia/comando | recebe | i ou iniciar; p ou parar |
| edge_ia/status | publica | as mesmas mensagens do Monitor Serial |
| edge_ia/estado | publica (retido) | online; offline via Last Will |

Teste: `mosquitto_sub -h test.mosquitto.org -t "edge_ia/#" -v` e
`mosquitto_pub -h test.mosquitto.org -t edge_ia/comando -m i`.
O broker e publico: qualquer pessoa pode enviar comandos nesses topicos.
Credenciais Wi-Fi e IP estatico opcional ficam no inicio de mqtt.cpp.

## Verificacao

Comparacao automatizada confirmou preservacao da configuracao, poses,
sequencia, inicializacao dos servos e calculo da rampa. Revisado isolamento
do controle e tratamento de falhas de criacao dos recursos. Compila com
arduino-cli (esp32:esp32 3.3.12); ainda nao testado em hardware.

Referencias:
- https://github.com/madhephaestus/ESP32Servo
- https://docs.espressif.com/projects/esp-idf/en/v5.0/esp32/api-reference/system/freertos.html
