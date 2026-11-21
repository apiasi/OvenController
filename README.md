# OvenController 3.1.0 - STM32F103 + ST7789 + MAX6675

Controlador de forno baseado no projeto CubeMX atual.

Revisão 3.1.0: leitura das entradas e controle atendidos durante o desenho,
atualização da UI a cada 40 ms quando há mudanças, debounce de 25 ms e desenho
somente do valor alterado pelo encoder. Preserva as proteções da revisão 3.0.0.
Detalhes e roteiro de bancada em `REVISAO_V3_1.txt`; histórico em `REVISAO_V3.txt`.

## Fluxo funcional

1. Na tela inicial, gire o encoder para ajustar o campo selecionado.
2. Pressione **PUSH** para alternar entre **temperatura** e **tempo**.
3. Pressione **K0** para iniciar.
4. O forno entra em **AQUECENDO** e mantém o SSR ligado até atingir o setpoint.
5. Ao atingir o setpoint, inicia a contagem do tempo configurado.
6. Durante a contagem, a temperatura é mantida por controle ON/OFF com histerese.
7. Ao terminar o tempo, o SSR é desligado e a tela mostra **FINALIZADO**.
8. Durante aquecimento/controle, **K0** interrompe o ciclo após validar a pressão por 25 ms, acrescidos do tempo de processamento entre atendimentos das entradas.
9. Toda interação por encoder/botões gera feedback pelo buzzer.

## Estados

- `IDLE`: configuração.
- `HEATING`: aquecimento inicial; o tempo ainda não está correndo.
- `RUNNING`: temperatura atingida; temporização ativa e controle térmico.
- `FINISHED`: ciclo encerrado e SSR desligado.
- `ERROR`: sensor aberto/falha, sobretemperatura, aquecimento excedido,
  falha SPI do display ou reinício por watchdog; SSR desligado.

O erro de comunicação com o display fica retido até reiniciar o controlador.
O firmware não consegue confirmar por SPI se um display desconectado recebeu
os dados; a detecção cobre erros retornados pela HAL.

## Pinagem usada

| Função | STM32F103 |
|---|---|
| TFT BLK / TIM2_CH2 | PA1 |
| TFT RESET | PA2 |
| TFT DC | PA3 |
| TFT CS | PA4 |
| TFT SCL / SPI1 SCK | PA5 |
| TFT SDA / SPI1 MOSI | PA7 |
| SSR_CMD | PB0 |
| BUZZER | PB1 |
| Encoder A / TIM4_CH1 | PB6 |
| Encoder B / TIM4_CH2 | PB7 |
| Encoder PUSH / EXTI8 | PB8 |
| K0 / EXTI9 | PB9 |
| MAX6675 CS | PB12 |
| MAX6675 SCK / SPI2 | PB13 |
| MAX6675 SO / SPI2 MISO | PB14 |
| SWDIO | PA13 |
| SWCLK | PA14 |

## Arquivos novos

- `oven_config.h`: limites e parâmetros ajustáveis.
- `oven_types.h`: estados e estrutura de status.
- `max6675.c/.h`: leitura do termopar K.
- `buzzer.c/.h`: bips não bloqueantes.
- `oven_ui.c/.h`: telas do TFT.
- `oven_app.c/.h`: máquina de estados, encoder, botões, timer e SSR.

## CubeMX esperado

- SYS: Serial Wire.
- SPI1: Master, 8-bit, CPOL Low, CPHA 1 Edge, software NSS, prescaler /8.
- SPI2: Master, 8-bit, CPOL Low, CPHA 1 Edge, software NSS, prescaler /16.
- TIM2 CH2: PWM Generation CH2, prescaler 71, period 999, pulse 999.
- TIM4: Encoder Mode TI1/TI2, period 65535.
- PB8/PB9: EXTI Falling Edge + Pull-Up.
- Clock: SYSCLK 72 MHz; APB1 36 MHz; APB2 72 MHz.

## Ajustes principais

Em `oven_config.h`:

```c
#define OVEN_DEFAULT_SETPOINT_C       80U
#define OVEN_DEFAULT_TIME_MIN         20U
#define OVEN_SETPOINT_MAX_C           250U
#define OVEN_HYSTERESIS_C             1.5f
#define OVEN_HARD_MAX_C               280.0f
#define OVEN_ENCODER_COUNTS_PER_STEP  4
#define OVEN_ENCODER_DIRECTION        1
#define OVEN_HEATING_TIMEOUT_MS        (30UL * 60UL * 1000UL)
#define OVEN_SENSOR_STALE_MS           1000U
#define OVEN_SENSOR_SPI_TIMEOUT_MS     5U
#define OVEN_DISPLAY_SPI_TIMEOUT_MS    10U
#define OVEN_UI_PERIOD_MS             40U
#define OVEN_BUTTON_DEBOUNCE_MS       25U
```

Se o encoder girar ao contrário, troque `OVEN_ENCODER_DIRECTION` para `-1`.

Se cada clique do encoder alterar vários valores ou não alterar, teste `OVEN_ENCODER_COUNTS_PER_STEP` com 1, 2 ou 4.

## Compilação e testes

As configurações Debug e Release incluem todos os módulos da aplicação.
No STM32CubeIDE, selecione a configuração e execute Build Project.
Pelo terminal, com o compilador e GNU Make no PATH:

```text
make -C Release all
make -C Debug all
```

Os testes executam código ARM emulado, com chamadas da HAL simuladas:

```text
python -m pip install -r Tests/requirements.txt
python Tests/run_regression.py
python Tests/run_regression.py --scenario inputs
```

O executor localiza o compilador no PATH ou em `C:/ST/STM32CubeIDE*`.
Também aceita `--compiler caminho/arm-none-eabi-gcc.exe`.
Os testes não gravam firmware nem acionam equipamentos conectados.
O cenário `inputs` usa a UI e o buzzer reais, com transmissões SPI simuladas
a 9 Mbit/s e eventos de entrada durante desenhos completos. Os tempos obtidos
nesse cenário não incluem o custo real de execução da CPU.

## Segurança

O `OVEN_HARD_MAX_C` é apenas proteção de software. Um forno de 1800 W em ~110 V trabalha na faixa de 16 A. Use proteção independente do firmware: fusível/disjuntor adequado, termostato/fusível térmico físico, SSR com dissipador e cabos/bornes dimensionados para a corrente.
