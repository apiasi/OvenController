# OvenController 4.0.0

Controlador STM32F103C8T6 com display ST7789 320 × 240, termopar MAX6675,
encoder, PUSH, botão K0, buzzer e saída SSR. Duas modalidades independentes:
**Manual / Secagem**, com temperatura constante, e **Reflow**, com quatro pontos.

## Operação

Ao ligar, o seletor começa no perfil Sn63/Pb37. Gire o encoder para escolher:

- **MANUAL / SECAGEM**: temperatura constante durante 1–180 minutos.
- **SN63 / PB37**: exemplo inicial para solda com chumbo.
- **SN62 / PB36 / AG2**: exemplo inicial para solda com chumbo e prata.
- **PERSONALIZADO**: cópia editável de quatro pontos.

Não há perfil de solda sem chumbo. Não se inicia aquecimento apenas ao escolher
ou editar um perfil; **K0** inicia o ciclo.

### Manual / Secagem

1. Selecione MANUAL / SECAGEM girando o encoder no campo de perfil.
2. PUSH alterna **perfil → temperatura → duração → perfil**.
3. Gire para ajustar o campo selecionado. Temperatura: 30–250 °C;
   duração: 1–180 minutos. Padrões: 80 °C e 20 minutos.
4. K0 inicia. O temporizador começa somente ao atingir a temperatura alvo.
5. Durante a contagem, o aquecimento usa histerese de ±1,5 °C.
6. K0 interrompe. Ao terminar o tempo, o SSR é desligado.

A contagem continua mesmo se a temperatura cair depois de atingir o alvo.
O modo constante continua adequado à configuração de secagem em temperatura
baixa; nenhuma rampa de reflow é aplicada a ele.

### Reflow e editor

1. Escolha um perfil com o encoder.
2. PUSH abre a tabela de quatro pontos.
3. Cada ponto contém **ALVO / C**, **RAMPA / S** e **MANTER / S**.
4. Gire para editar a célula destacada; PUSH avança para a próxima.
5. Ao completar as 12 células, o editor volta à tela principal.
   Segure PUSH por aproximadamente 700 ms para voltar a qualquer momento.
6. Alterar um preset copia seus valores para PERSONALIZADO. O preset original
   permanece disponível. A cópia personalizada é preservada ao trocar de perfil
   durante a mesma sessão.
7. K0 inicia usando os valores atuais, inclusive a partir do editor.
8. Durante execução ou resfriamento, K0 interrompe o ciclo.

RAMPA é a duração para deslocar a referência do alvo anterior ao alvo daquele
ponto. A primeira rampa parte da temperatura medida ao iniciar. MANTER é o tempo
adicional no alvo; zero significa sem permanência. Rampas: 5–600 segundos;
permanências: 0–600 segundos. Temperaturas: 30–250 °C.

**Ajustes ficam em RAM e retornam aos padrões após desligar ou reiniciar.**

## Perfis iniciais com chumbo

| Ponto | Sn63/Pb37 | Sn62/Pb36/Ag2 | Rampa | Manter |
| --- | --- | --- | --- | --- |
| P1 — Pré-aquecimento | 120 °C | 120 °C | 90 s | 0 s |
| P2 — Soak | 150 °C | 150 °C | 45 s | 60 s |
| P3 — Subida | 185 °C | 185 °C | 30 s | 0 s |
| P4 — Pico | 210 °C | 205 °C | 30 s | 30 s |

Cada programa de aquecimento dura 4 min 45 s. Depois, o SSR permanece desligado
no resfriamento passivo, acompanhado pelo sensor até 60 °C. O limite de
resfriamento é 30 minutos. Não há controle de ventilador ou de abertura de porta.
A linha pontilhada após o pico é ilustrativa; não determina uma taxa de queda.

Os valores acima são exemplos de partida para qualificação, não receitas
universais ou cópias exatas de um perfil de fabricante. A Kester descreve que o
perfil depende da pasta, do forno e da placa em seu
[boletim R500, página 3](https://www.kester.com/DesktopModules/Bring2mind/DMX/API/Entries/Download?Command=Core_Download&EntryId=934&PortalId=0&TabId=96&language=en-US).
Ajuste à ficha técnica da pasta utilizada e confira o perfil medido na placa,
incluindo o pico e o tempo acima de liquidus. O sensor instalado pode medir o ar
ou outra região do forno, em vez da junta de solda.

## Interface

- Cabeçalho com estado: PRONTO, AQUECENDO, EXECUTANDO, RESFRIANDO, CONCLUIDO ou FALHA.
- Temperatura medida em destaque e referência do controle no painel ao lado.
- Curva programada em ciano; amostras medidas em âmbar; cursor de progresso.
- Etapa ativa, rampa/permanência, tempo restante da etapa e estado do SSR.
- Editor com destaque de célula, unidades explícitas e instruções no rodapé.
- Telas de falha com aquecimento bloqueado e temperatura medida quando disponível.

A resposta cooperativa da revisão anterior foi preservada: entradas, sensor,
buzzer e controle são atendidos durante o desenho. A UI recebe um snapshot e
só atualiza campos alterados, com período mínimo de 40 ms. Debounce: 25 ms na
pressão e na liberação; um evento por pressão. Não há resposta instantânea:
comunicação e execução acrescentam atraso.

Prévias obtidas dos comandos SPI do firmware emulado:
[perfil](Tests/previews/perfil.png), [editor](Tests/previews/editor.png),
[execução](Tests/previews/execucao.png), [resfriamento](Tests/previews/resfriamento.png),
[secagem](Tests/previews/secagem.png), [secagem aquecendo](Tests/previews/secagem_aquecendo.png),
[secagem em execução](Tests/previews/secagem_executando.png), [falha](Tests/previews/falha.png).

No Manual/Secagem, o rodapé explica o início da contagem durante o aquecimento
e informa o controle de temperatura constante durante a execução.
A legenda de cores das curvas aparece somente no Reflow.

## Proteções

- SSR ativo em nível alto em PB0, desligado na partida, em estados inativos e em falhas.
- Corte por software em 280 °C; sensor a cada 250 ms e validade máxima de 1 segundo.
- Timeouts SPI: 5 ms para sensor e 10 ms por transferência do display.
- Falha de comunicação com o display fica retida até reiniciar.
- Aquecimento inicial do modo Manual limitado a 30 minutos para atingir o alvo.
- Reflow exige início com até 50 °C e pontos dentro dos limites configurados.
- Ao terminar cada ponto, desvio acima de ±15 °C em relação ao alvo interrompe
  o perfil. Temperatura superior ao maior alvo +15 °C também interrompe.
- Rampas descendentes mantêm o aquecimento desligado durante a rampa.
- Watchdog independente; reinício por watchdog não retoma o ciclo.
- Handlers de falha desligam o comando do SSR por acesso direto aos registradores.

O controle continua ON/OFF com histerese, sem PID ou compensação automática de
inércia térmica. A verificação de desvio não certifica a qualidade da soldagem.
O software não substitui proteção térmica física e não detecta SSR travado ligado.

## Estrutura e hardware

| Módulo | Responsabilidade |
| --- | --- |
| `oven_app.c` | Estados, entradas, ciclos Manual/Reflow, proteção e SSR |
| `oven_profile.c/.h` | Presets, validação, duração e interpolação dos quatro pontos |
| `oven_ui.c` | Painel, gráfico, editor e atualização seletiva |
| `oven_config.h` | Versão, limites, períodos e parâmetros |
| `st7789.c/.h` | TFT via SPI1 e callback cooperativo |
| `max6675.c/.h` | Leitura do termopar via SPI2 |
| `buzzer.c/.h` | Padrões sonoros sem esperas bloqueantes |

| Função | Pino |
| --- | --- |
| TFT BLK / TIM2_CH2 | PA1 |
| TFT RESET / DC / CS | PA2 / PA3 / PA4 |
| TFT SCK / MOSI | PA5 / PA7 |
| SSR / buzzer | PB0 / PB1 |
| Encoder A / B, TIM4 | PB6 / PB7 |
| Encoder PUSH / K0 | PB8 / PB9 |
| MAX6675 CS / SCK / MISO | PB12 / PB13 / PB14 |
| SWDIO / SWCLK | PA13 / PA14 |

Clock: 72 MHz; APB1: 36 MHz; APB2: 72 MHz. SPI1 a 9 Mbit/s; SPI2 a 2,25 Mbit/s.
Pinagem, clock e configuração dos periféricos não foram alterados nesta versão.

## Compilação e testes

### Análise C/C++ no VS Code

Use a extensão **clangd**, recomendada pelo projeto. O arquivo `.clangd` aponta
para `Build/Release/compile_commands.json`, gerado com o compilador ARM, as
definições `STM32F103xB`/`USE_HAL_DRIVER` e os includes da HAL e do CMSIS.
Para configurar o editor sem compilar:

```text
python Tools/build.py --configuration Release --configure-only
```

A compilação normal também gera essa base. Ela contém caminhos locais e fica
fora do Git. Após clonar ou mudar a instalação do compilador, gere-a novamente.
Se os diagnósticos antigos persistirem, execute **clangd: Restart language server**
pela paleta de comandos (`Ctrl+Shift+P`).

O workspace usa clangd como analisador e desativa somente o IntelliSense paralelo
da extensão Microsoft C/C++, evitando diagnósticos duplicados. O GCC continua
com `-Wall -Werror`; avisos reais da compilação não são desativados. A análise de
includes não utilizados é dispensada apenas nos arquivos agregadores gerados
pelo CubeMX (`main.h`, `stm32f1xx_hal_conf.h` e `syscalls.c`).

### Compilar e executar testes

Depois de clonar, importe o projeto existente no STM32CubeIDE, selecione Debug
ou Release e execute Build Project. As pastas de compilação são geradas localmente.

Para compilar diretamente a partir dos fontes, sem abrir o CubeIDE:

```text
python Tools/build.py --configuration all
```

O script encontra o compilador no PATH ou na instalação do CubeIDE em `C:/ST`.
Também aceita `--compiler caminho/arm-none-eabi-gcc.exe`. Gera ELF, HEX e BIN em
`Build/Debug` e `Build/Release`, sem gravar a placa. Requer GNU Arm Embedded
com newlib, além de Python 3.

Após o CubeIDE gerar os makefiles, também é possível usar GNU Make e
`arm-none-eabi-*` no PATH, na raiz:

```text
make -C Release all
make -C Debug all
```

As duas configurações incluem `oven_profile.c`. O `.cproject` inclui a pasta
Core como fonte; o CubeIDE regenera seus makefiles. As pastas `Debug`, `Release`
e `Build` ficam fora do versionamento. As prévias em `Tests/previews` fazem
parte da documentação e continuam versionadas.

```text
python -m pip install -r Tests/requirements.txt
python Tests/run_regression.py
python Tests/run_regression.py --scenario inputs
python Tests/run_regression.py --scenario profiles
python Tests/run_regression.py --scenario preview
```

O executor aceita `--compiler caminho/arm-none-eabi-gcc.exe`. A prévia aceita
`--output pasta`, sendo `Tests/previews` o padrão. Os PNGs são reconstruídos dos
bytes RGB565 enviados pelo driver real, com a HAL simulada. Os testes não gravam
firmware nem acionam o equipamento. Tempos de SPI simulados não incluem os ciclos
reais da CPU. Veja `REVISAO_V4.txt` para resultados e roteiro de bancada.
