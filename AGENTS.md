# Instruções para agentes — OvenController

Estas instruções se aplicam a todo o projeto. As instruções explícitas do usuário
têm prioridade. Converse em português brasileiro e descreva as mudanças de forma
direta, incluindo o que foi validado e o que ainda depende de bancada.

## Idioma dos comentários e da documentação

- Escreva sempre em **português brasileiro** os comentários de código,
  docstrings, explicações e documentação criados ou modificados pela IA.
- Mensagens de commit geradas pela IA também devem estar em português brasileiro,
  incluindo título, corpo e listas. A configuração específica do gerador do
  Copilot no VS Code está em `.vscode/settings.json`, na chave
  `github.copilot.chat.commitMessageGeneration.instructions`.
- Essa regra vale para todos os arquivos do projeto, inclusive C, headers,
  Python, scripts e testes. Não adote inglês nos comentários por acompanhar
  o idioma dos identificadores ou das bibliotecas.
- Preserve nomes de funções, variáveis, APIs, registradores e constantes quando
  forem necessários à compatibilidade ou ao padrão existente do código.
- Preserve marcadores do CubeMX (`USER CODE BEGIN/END`), avisos de licença e
  textos gerados por ferramentas ou fornecidos por terceiros. Ao acrescentar
  comentários próprios nesses arquivos, escreva-os em português brasileiro.
- Não traduza em massa os comentários existentes sem uma tarefa que peça isso.

## Contexto e fontes de referência

Firmware em C para controlador de forno com STM32F103C8T6, Cortex-M3 a 72 MHz,
64 KB de Flash e 20 KB de RAM. Usa STM32 HAL, CubeMX e STM32CubeIDE, com
GNU Tools for STM32. A aplicação executa tarefas cooperativas no laço principal,
sem RTOS.

- Leia `README.md` para operação e pinagem.
- `REVISAO_V4.txt` documenta os perfis de quatro pontos e a interface da versão 4.0.0.
- Consulte `REVISAO_V3_1.txt` para a revisão 3.1.0 e pendências de bancada;
  `REVISAO_V3.txt` documenta as proteções introduzidas na revisão anterior.
- Confira os valores atuais em `Core/Inc/oven_config.h`; este arquivo define a versão.
- Use os fontes e `OvenController.ioc` para conferir o estado real do projeto.
  Se houver divergência com a documentação, investigue e registre a correção.
- Não trate um binário existente como prova de que os fontes atuais compilam.

## Organização do código

| Arquivo ou pasta | Responsabilidade |
| --- | --- |
| `Core/Src/main.c` | Inicialização, clock, periféricos, corte de emergência e watchdog |
| `Core/Src/oven_app.c` | Estados, entradas, temporização, controle e comando do SSR |
| `Core/Src/oven_profile.c` | Presets com chumbo, validação e interpolação dos quatro pontos |
| `Core/Inc/oven_types.h` | Estados, erros e estrutura de status |
| `Core/Inc/oven_config.h` | Versão, limites e períodos configuráveis |
| `Core/Src/oven_ui.c` | Telas e atualização seletiva dos campos |
| `Core/Src/st7789.c` | Driver do TFT via SPI1 |
| `Core/Src/max6675.c` | Leitura do termopar via SPI2 |
| `Core/Src/buzzer.c` | Padrões sonoros processados pelo laço principal |
| `Core/Src/stm32f1xx_it.c` | Interrupções e handlers de falha |
| `Drivers/` | HAL e CMSIS fornecidos pela ST |
| `Tests/` | Regressão do código ARM com periféricos simulados |
| `Debug/`, `Release/` | Arquivos de compilação e artefatos gerados |

## Comportamento a preservar

- Estados: `IDLE`, `HEATING`, `RUNNING`, `COOLING`, `FINISHED` e `ERROR`.
- Preserve os dois modos: **Manual / Secagem**, com temperatura constante e
  temporização, e **Reflow**, com quatro pontos. O usuário precisa de ambos.
- O seletor inclui Manual, Sn63/Pb37, Sn62/Pb36/Ag2 e Personalizado. Não adicionar
  perfis sem chumbo como padrão: o usuário trabalha com pasta com chumbo.
- Encoder edita somente em `IDLE`. No Manual, PUSH alterna perfil/temperatura/tempo;
  no Reflow, abre o editor de 12 células. PUSH longo volta ao seletor.
- Cada ponto de Reflow define alvo, duração da rampa e permanência. Editar um
  preset cria a cópia Personalizado, preservando o preset original.
- K0 inicia, interrompe ou reconhece o estado, conforme a máquina de estados.
- No Manual, a contagem começa ao atingir o alvo e continua mesmo se a temperatura
  cair. No Reflow, a referência é interpolada em função do tempo desde o início.
  Os dois modos usam histerese, sem PID ou compensação automática de inércia.
- Reflow termina o aquecimento em `COOLING`, com SSR desligado até medir 60 °C.
  Não há atuador de resfriamento; a extensão pontilhada do gráfico é ilustrativa.
- Ajustes ficam em RAM e retornam aos padrões após reiniciar.
- Botões são validados por polling, com 25 ms de estabilidade na pressão
  e na liberação. Não voltar a aceitar comandos diretamente por bordas EXTI.
- Preserve o cache de campos da UI e evite redesenhos completos sem necessidade.
- O driver ST7789 atende um callback cooperativo entre blocos e caracteres.
  Esse callback processa entradas e controle, sem desenhar, acessar SPI1 ou
  alimentar o watchdog. Preserve essa separação para evitar recursão e conflitos.
- A UI desenha uma cópia do status. Mudanças ocorridas durante o desenho devem
  continuar pendentes para a próxima atualização, inclusive invalidações de tela.

Esses itens descrevem o funcionamento atual; podem mudar quando a tarefa pedir.
Ao alterar uma regra funcional, atualize documentação e testes relacionados.

## Segurança e tratamento de falhas

- SSR ativo em nível alto em PB0; nível baixo é o comando de desligamento.
- Preserve o desligamento durante a partida, nos estados inativos e nos erros.
- `Safety_HeaterOff()` deve continuar utilizável antes da HAL e em handlers de
  falha, sem SPI, espera, alocação dinâmica ou dependência de interrupções.
- Falhas do sensor, leituras antigas, sobretemperatura e prazo de aquecimento
  excedido devem impedir o aquecimento.
- Reflow inicia com até 50 °C; exige pontos válidos, verificação de desvio ao
  terminar cada ponto e proteção contra temperatura acima do pico + tolerância.
  O resfriamento tem limite de tempo e mantém a monitoração do sensor.
- Os presets são exemplos iniciais, não perfis qualificados. Consulte a ficha
  técnica da pasta real e diferencie temperatura medida no forno e na placa.
- Um erro SPI do display desliga o SSR e fica retido até reiniciar. O SPI
  unidirecional não confirma se o display está fisicamente conectado.
- Não introduza esperas infinitas em drivers. Verifique retornos das chamadas HAL
  relevantes e limite os tempos de comunicação.
- Alimente o watchdog somente após completar o processamento da aplicação.
  Não o alimente em interrupções ou dentro de operações que possam travar.
- Reinício por watchdog não pode retomar automaticamente um ciclo.
- O prazo do watchdog varia com o LSI; em Debug ele fica congelado com o core parado.
- Use diferenças de contadores sem sinal para intervalos e considere a passagem
  de `HAL_GetTick()` por zero.
- Não descreva STOP ou detecção de falha como instantâneos: existem atrasos de
  debounce, amostragem, comunicação e execução do laço.
- Preserve as proteções durante refatorações. Mudanças em limites, polaridade,
  recuperação de falhas ou watchdog precisam de justificativa na entrega.

Os valores atuais incluem corte em 280 °C, histerese de ±1,5 °C, leitura a cada
250 ms, validade máxima de 1 segundo e até 30 minutos para atingir o alvo.
Confirme-os em `oven_config.h` antes de trabalhar. O limite de aquecimento ainda
precisa ser validado no forno real.

## Regras para alterações

- Faça mudanças focadas na tarefa e preserve alterações existentes do usuário.
- Mantenha a separação entre controle, interface e drivers.
- Centralize parâmetros ajustáveis em `oven_config.h`.
- Preserve as regiões `USER CODE BEGIN/END` dos arquivos gerados pelo CubeMX.
  Coloque código próprio nessas regiões quando aplicável.
- Quando mudar periféricos, pinagem ou clock, mantenha o `.ioc` coerente com os
  fontes. Não regenere o projeto inteiro para resolver uma mudança pequena.
- Evite alterar HAL/CMSIS em `Drivers/` sem necessidade concreta.
- Não use arquivos gerados em `Debug/` ou `Release/` como única fonte de uma
  configuração permanente. A configuração do CubeIDE fica em `.cproject`.
- Ao adicionar fontes da aplicação, confira a inclusão tanto em Debug quanto
  em Release. Os testes devem continuar fora dos fontes do firmware.
- Siga o estilo local e evite reformatação ampla de código não relacionado.
- Atualize versão e notas de revisão quando a tarefa constituir uma nova versão;
  não incremente a versão automaticamente a cada pequena edição.
- Compilar e executar testes de software faz parte do trabalho autorizado.
  Gravação, reset da placa e acionamento do forno exigem um pedido que inclua
  essas ações. Não os deduza de uma solicitação de revisão de código.

## Compilação

Execute a partir da raiz, com GNU Make e `arm-none-eabi-*` no PATH:

```powershell
make -C Release all
make -C Debug all
```

Informe o alvo `all` explicitamente: os makefiles incluem regras de limpeza e
não se deve depender do alvo padrão. No CubeIDE, selecione a configuração e use
Build Project. Não execute limpeza sem necessidade.

Neste ambiente, as ferramentas foram encontradas em:

```text
C:/ST/STM32CubeIDE_2.0.0/STM32CubeIDE/plugins/
```

Os executáveis ficam em `tools/bin` dos plugins de GNU Tools e Make. Localize as
pastas instaladas; não presuma que o número de versão ou o PATH sejam os mesmos
em outra sessão ou máquina.

## Testes e entrega

Para alterações na lógica de controle, proteção ou drivers, execute os testes
pertinentes e compile Debug e Release. Alterações apenas na documentação não
exigem recompilação. Acrescente testes de regressão para mudanças de comportamento
e correções de falhas; evite testes que apenas repitam a implementação.

```powershell
python -m pip install -r Tests/requirements.txt
python Tests/run_regression.py
python Tests/run_regression.py --scenario inputs
python Tests/run_regression.py --scenario profiles
python Tests/run_regression.py --scenario preview
```

Prefira ambiente virtual ou instalação temporária das dependências. Se já
estiverem disponíveis, reutilize-as. O executor aceita
`--compiler caminho/arm-none-eabi-gcc.exe`.

Os testes compilam fontes reais para Cortex-M3 e os executam no Unicorn, com
HAL e periféricos simulados. Eles não comprovam os tempos elétricos, a dinâmica
térmica, o reset real do watchdog ou o funcionamento físico do SSR.

Na entrega, informe o comportamento alterado, os testes executados e eventuais
limitações. Diferencie compilação, emulação e validação na placa. Mantenha as
pendências de bancada documentadas e não declare testes físicos que não ocorreram.
