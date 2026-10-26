# Esteira e sensor ultrassonico

Somente `taskControle` aciona o rele durante a execucao. A tarefa
`task_esteira_with_ultrasonic` mede em centimetros e envia o resultado por
uma fila de uma entrada, substituindo a leitura anterior.

Configuracao em `main/main.c`:

| Constante | Valor | Significado |
| --- | --- | --- |
| `TRIGGER_GPIO` | 5 | Saida de disparo do sensor |
| `ECHO_GPIO` | 18 | Entrada de eco |
| `MAX_DISTANCE_CM` | 50 | Alcance maximo aceito na medicao |
| `LIMITE_DISTANCIA` | 12 | Parar ao medir menos que 12 cm |
| `PERIODO_SENSOR_MS` | 100 | Pausa entre medicoes |
| `VALIDADE_SENSOR_MS` | 500 | Idade maxima aceita de uma leitura |

A esteira fica desligada no repouso e durante movimentos manuais. Na etapa
`LIGAR_ESTEIRA`, precisa de leitura recente e valida, com distancia de pelo
menos 12 cm. Permanece ligada ate `ESTEIRA_MS` (12000 ms) ou ate detectar um
objeto abaixo de 12 cm. A deteccao antecipa a etapa `DESLIGAR_ESTEIRA`, que
aguarda `ACOMODAR_MS` antes de continuar o ciclo. Se o objeto ja estiver
abaixo de 12 cm na entrada dessa etapa, a esteira nem chega a ligar.

Erro de inicializacao, erro de medicao ou leitura antiga impedem ligar a
esteira. Se acontecerem enquanto esta ligada, ela desliga e cancela o
ciclo. Corrigir a causa e enviar `iniciar` para tentar novamente; a volta
das leituras sozinha nao reinicia o movimento. `parar` tambem cancela o
ciclo e impede que leituras posteriores religuem a esteira.

Uma medicao fora do alcance de 50 cm ou sem eco e tratada como erro, e nao
como caminho livre. Posicione o sensor para haver um retorno valido na
condicao sem objeto, ou ajuste o alcance conforme a montagem.

Validacao na bancada, apos gravar:

1. No repouso, variar a distancia: a esteira deve continuar desligada.
2. Iniciar o ciclo com leitura livre valida: a esteira deve ligar apenas
   na etapa correspondente e desligar apos no maximo cerca de 12 segundos.
3. Durante essa etapa, aproximar um objeto a menos de 12 cm: ela deve
   parar e seguir para a acomodacao, sem religar ao afastar o objeto.
4. Enviar `parar` durante o funcionamento: deve desligar e permanecer assim.
5. Interromper o eco durante o funcionamento: deve cancelar o ciclo;
   restaurar o eco nao deve religar automaticamente.

A resposta inclui o intervalo entre medicoes, o processamento da tarefa e
o tempo fisico do rele/motor. Este controle por software nao substitui um
dispositivo fisico de parada de emergencia.
