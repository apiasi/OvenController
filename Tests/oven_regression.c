/* Executa o codigo ARM da aplicacao com perifericos simulados. */
#include "../Core/Src/main.c"
#include "../Core/Src/oven_app.c"
#include "../Core/Src/st7789.c"
#include "../Core/Src/max6675.c"
#include "../Core/Src/stm32f1xx_it.c"
#ifdef OVEN_TEST_REAL_UI
#include "../Core/Src/oven_ui.c"
#include "../Core/Src/buzzer.c"
#endif

static uint32_t testTick;
static uint16_t pressedPins;
static uint16_t sensorWord;
static HAL_StatusTypeDef sensorResult;
static HAL_StatusTypeDef displayResult;
static unsigned displayTransfers;
static unsigned alarms;
volatile unsigned testCount;
volatile unsigned failedLine;
volatile unsigned testCompleted;

#ifdef OVEN_TEST_REAL_UI
static bool simulateWire;
static uint32_t wireRemainder;
static unsigned displayBytes;
static uint16_t scheduledPin;
static uint32_t pressStart;
static uint32_t pressEnd;
static bool simulateEncoder;
static uint32_t encoderStart;
static unsigned injectedSteps;
static uint32_t heaterTransitionTick;

static void UpdateSimulatedInputs(void) {
    if (scheduledPin != 0U) {
        pressedPins = (testTick >= pressStart && testTick < pressEnd)
            ? scheduledPin : 0U;
    }
    if (simulateEncoder) {
        while (injectedSteps < 10U && testTick >= encoderStart + injectedSteps * 10U) {
            TIM4->CNT = (uint16_t)(TIM4->CNT + OVEN_ENCODER_COUNTS_PER_STEP);
            injectedSteps++;
        }
    }
}
#endif

#define CHECK(condition) do { testCount++; if (!(condition)) { \
    failedLine = __LINE__; __asm volatile("bkpt #0"); } } while (0)

uint32_t HAL_GetTick(void) { return testTick; }
void HAL_Delay(uint32_t ms) { testTick += ms; }
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin) {
    (void)port;
#ifdef OVEN_TEST_REAL_UI
    UpdateSimulatedInputs();
#endif
    return (pressedPins & pin) ? GPIO_PIN_RESET : GPIO_PIN_SET;
}
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state) {
#ifdef OVEN_TEST_REAL_UI
    if (port == SSR_CMD_GPIO_Port && pin == SSR_CMD_Pin &&
        ((port->ODR & pin) != 0U) != (state == GPIO_PIN_SET)) {
        heaterTransitionTick = testTick;
    }
#endif
    if (state == GPIO_PIN_SET) { port->ODR |= pin; }
    else { port->ODR &= ~pin; }
}
HAL_StatusTypeDef HAL_TIM_Encoder_Start(TIM_HandleTypeDef *timer, uint32_t channel) {
    (void)timer; (void)channel; return HAL_OK;
}
HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *spi, const uint8_t *data,
                                  uint16_t size, uint32_t timeout) {
    (void)spi; (void)data; (void)size;
    displayTransfers++;
#ifdef OVEN_TEST_REAL_UI
    displayBytes += size;
    if (simulateWire && displayResult == HAL_OK) {
        /* Custo de transmissao no SPI1 a 9 Mbit/s; nao simula ciclos da CPU. */
        wireRemainder += (uint32_t)size * 8U;
        testTick += wireRemainder / 9000U;
        wireRemainder %= 9000U;
        UpdateSimulatedInputs();
    }
#endif
    if (displayResult != HAL_OK) { testTick += timeout; }
    return displayResult;
}
HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *spi, const uint8_t *tx,
                                        uint8_t *rx, uint16_t size, uint32_t timeout) {
    (void)spi; (void)tx; (void)size;
    rx[0] = (uint8_t)(sensorWord >> 8); rx[1] = (uint8_t)sensorWord;
    if (sensorResult != HAL_OK) { testTick += timeout; }
    return sensorResult;
}
#ifndef OVEN_TEST_REAL_UI
void OvenUI_Init(void) {}
void OvenUI_Invalidate(void) {}
void OvenUI_Render(const OvenStatus_t *status) { (void)status; }
void Buzzer_Init(void) {}
void Buzzer_Process(void) {}
void Buzzer_Click(void) {}
void Buzzer_Confirm(void) {}
void Buzzer_StartCycle(void) {}
void Buzzer_StopCycle(void) {}
void Buzzer_FinishCycle(void) {}
void Buzzer_Error(void) { alarms++; }
#endif

static void resetTest(void) {
#ifdef OVEN_TEST_REAL_UI
    simulateWire = false;
    scheduledPin = 0U;
    simulateEncoder = false;
    displayBytes = 0U;
    wireRemainder = 0U;
#endif
    testTick = 1000U;
    pressedPins = 0U;
    sensorWord = (uint16_t)(25U * 4U * 8U);
    sensorResult = HAL_OK;
    displayResult = HAL_OK;
    displayTransfers = 0U;
    alarms = 0U;
    spiFailed = false;
    RCC->CSR = 0U;
    htim4.Instance = TIM4;
    TIM4->CNT = 0U;
    OvenApp_Init();
    OvenApp_Process();
}

void test_start(void) {
    testTick = 0U;
    IWDG->SR = 0U;
    Safety_WatchdogInit();
    CHECK(IWDG->PR == 6U && IWDG->RLR == 1249U);
    CHECK(IWDG->KR == 0xAAAAU);
    IWDG->KR = 0U;
    Safety_WatchdogRefresh();
    CHECK(IWDG->KR == 0xAAAAU);

    resetTest();
    CHECK(oven.sensorValid && oven.currentTempC == 25.0f);
    Oven_StartCycle();
    CHECK(oven.state == OVEN_STATE_HEATING && oven.heaterOn);
    testTick += OVEN_HEATING_TIMEOUT_MS;
    lastValidSensorTick = testTick;
    ProcessControl(testTick);
    CHECK(oven.error == OVEN_ERROR_HEATING_TIMEOUT && !oven.heaterOn);

    resetTest();
    oven.currentTempC = (float)oven.setpointC;
    Oven_StartCycle();
    ProcessControl(testTick);
    CHECK(oven.state == OVEN_STATE_RUNNING);
    sensorWord = 4U;
    testTick += OVEN_SENSOR_PERIOD_MS;
    OvenApp_Process();
    CHECK(oven.error == OVEN_ERROR_SENSOR && !oven.heaterOn);

    resetTest();
    Oven_StartCycle();
    sensorWord = 4U; /* Termopar aberto durante aquecimento. */
    testTick += OVEN_SENSOR_PERIOD_MS;
    OvenApp_Process();
    CHECK(oven.error == OVEN_ERROR_SENSOR && !oven.heaterOn);
    CHECK((GPIOB->ODR & SSR_CMD_Pin) == 0U);

    resetTest();
    Oven_StartCycle();
    sensorResult = HAL_TIMEOUT;
    testTick += OVEN_SENSOR_PERIOD_MS;
    OvenApp_Process();
    CHECK(oven.error == OVEN_ERROR_SENSOR && !oven.heaterOn);
    CHECK((GPIOB->ODR & MAX6675_CS_Pin) != 0U);

    resetTest();
    Oven_StartCycle();
    testTick += OVEN_SENSOR_STALE_MS;
    ProcessControl(testTick);
    CHECK(oven.error == OVEN_ERROR_SENSOR && !oven.heaterOn);

    resetTest();
    oven.currentTempC = 100.0f;
    Oven_StartCycle();
    CHECK(!oven.heaterOn);
    ProcessControl(testTick);
    CHECK(oven.state == OVEN_STATE_RUNNING);
    oven.currentTempC = 78.5f;
    ProcessControl(testTick);
    CHECK(oven.heaterOn);
    oven.currentTempC = 80.0f;
    ProcessControl(testTick);
    CHECK(oven.heaterOn);
    oven.currentTempC = 81.5f;
    ProcessControl(testTick);
    CHECK(!oven.heaterOn);
    testTick += (uint32_t)oven.setTimeMin * 60000U;
    lastValidSensorTick = testTick;
    ProcessControl(testTick);
    CHECK(oven.state == OVEN_STATE_FINISHED && !oven.heaterOn);

    resetTest();
    Oven_StartCycle();
    sensorWord = (uint16_t)(280U * 4U * 8U);
    testTick += OVEN_SENSOR_PERIOD_MS;
    OvenApp_Process();
    CHECK(oven.error == OVEN_ERROR_OVERTEMP && !oven.heaterOn);
    keyK0Button.pressEvent = true;
    ProcessButtons();
    CHECK(oven.state == OVEN_STATE_ERROR);
    oven.currentTempC = 269.0f;
    keyK0Button.pressEvent = true;
    ProcessButtons();
    CHECK(oven.state == OVEN_STATE_IDLE);

    resetTest();
    Oven_StartCycle();
    displayResult = HAL_TIMEOUT;
    ST7789_FillScreen(ST7789_BLACK);
    CHECK(ST7789_HasError() && displayTransfers == 1U);
    CHECK(testTick == 1000U + OVEN_DISPLAY_SPI_TIMEOUT_MS);
    /* O corte de emergencia usa BRR, nao depende da HAL. */
    CHECK(GPIOB->BRR == SSR_CMD_Pin);
    OvenApp_Process();
    CHECK(oven.error == OVEN_ERROR_DISPLAY && !oven.heaterOn);
    ST7789_FillScreen(ST7789_BLACK);
    CHECK(displayTransfers == 1U);
    keyK0Button.pressEvent = true;
    ProcessButtons();
    CHECK(oven.state == OVEN_STATE_ERROR);
    Oven_StartCycle();
    CHECK(!oven.heaterOn);

    resetTest();
    RCC->CSR = RCC_CSR_IWDGRSTF;
    OvenApp_Init();
    CHECK(oven.error == OVEN_ERROR_WATCHDOG && !oven.heaterOn);

    resetTest();
    pressedPins = KEY_K0_Pin;
    OvenApp_Process();
    testTick += OVEN_BUTTON_DEBOUNCE_MS - 1U;
    OvenApp_Process();
    CHECK(oven.state == OVEN_STATE_IDLE);
    testTick += 1U;
    OvenApp_Process();
    CHECK(oven.state == OVEN_STATE_HEATING);
    testTick += 100U;
    OvenApp_Process();
    CHECK(oven.state == OVEN_STATE_HEATING); /* Segurar nao gera STOP. */
    pressedPins = 0U;
    OvenApp_Process();
    testTick += OVEN_BUTTON_DEBOUNCE_MS;
    OvenApp_Process();
    pressedPins = KEY_K0_Pin;
    OvenApp_Process();
    testTick += OVEN_BUTTON_DEBOUNCE_MS;
    OvenApp_Process();
    CHECK(oven.state == OVEN_STATE_IDLE && !oven.heaterOn);

    resetTest();
    testTick = UINT32_MAX - 100U;
    lastValidSensorTick = testTick;
    Oven_StartCycle();
    testTick += 200U;
    lastValidSensorTick = testTick;
    ProcessControl(testTick);
    CHECK(oven.state == OVEN_STATE_HEATING);
    testTick += OVEN_HEATING_TIMEOUT_MS;
    lastValidSensorTick = testTick;
    ProcessControl(testTick);
    CHECK(oven.error == OVEN_ERROR_HEATING_TIMEOUT && !oven.heaterOn);

    resetTest();
    /* Ruido curto no PUSH nao deve trocar o campo. */
    pressedPins = ENC_PUSH_Pin;
    ProcessRealtime();
    testTick += 5U;
    pressedPins = 0U;
    ProcessRealtime();
    testTick += OVEN_BUTTON_DEBOUNCE_MS;
    ProcessRealtime();
    CHECK(oven.selectedField == OVEN_FIELD_TEMPERATURE);
    pressedPins = ENC_PUSH_Pin;
    ProcessRealtime();
    testTick += OVEN_BUTTON_DEBOUNCE_MS;
    ProcessRealtime();
    CHECK(oven.selectedField == OVEN_FIELD_TIME);
    testTick += 100U;
    ProcessRealtime();
    CHECK(oven.selectedField == OVEN_FIELD_TIME);

    resetTest();
    TIM4->CNT = 3U;
    ProcessRealtime();
    CHECK(oven.setpointC == OVEN_DEFAULT_SETPOINT_C);
    encPushButton.pressEvent = true;
    ProcessButtons();
    TIM4->CNT = 4U;
    ProcessRealtime();
    CHECK(oven.setTimeMin == OVEN_DEFAULT_TIME_MIN);
    TIM4->CNT = 7U;
    ProcessRealtime();
    CHECK(oven.setTimeMin == OVEN_DEFAULT_TIME_MIN + 1U);

    resetTest();
    encoderLastCounter = 65534U;
    TIM4->CNT = 2U;
    ProcessRealtime();
    CHECK(oven.setpointC == OVEN_DEFAULT_SETPOINT_C + 1U);
    TIM4->CNT = 65534U;
    ProcessRealtime();
    CHECK(oven.setpointC == OVEN_DEFAULT_SETPOINT_C);

    resetTest();
    Oven_EnterError(OVEN_ERROR_DISPLAY);
    unsigned previousAlarms = alarms;
    Oven_EnterError(OVEN_ERROR_OVERTEMP);
    CHECK(oven.error == OVEN_ERROR_DISPLAY && alarms == previousAlarms);

    resetTest();
    Oven_EnterError(OVEN_ERROR_SENSOR);
    testTick += OVEN_SENSOR_STALE_MS;
    keyK0Button.pressEvent = true;
    ProcessButtons();
    CHECK(oven.state == OVEN_STATE_ERROR);

    testCompleted = 1U;
    __asm volatile("bkpt #0");
    while (1) {}
}

#ifdef OVEN_TEST_REAL_UI
volatile unsigned measuredCardBytes;
volatile unsigned measuredStartLatency;
volatile unsigned measuredStopLatency;

static void DrawWithInput(uint16_t pin) {
    simulateWire = true;
    scheduledPin = pin;
    pressStart = testTick + 10U;
    pressEnd = pressStart + 80U;
    uiDirty = true;
    OvenUI_Invalidate();
    lastUiTick = testTick - OVEN_UI_PERIOD_MS;
    ProcessUi(testTick);
    scheduledPin = 0U;
    pressedPins = 0U;
}

void input_test_start(void) {
    resetTest();
    DrawWithInput(KEY_K0_Pin);
    CHECK(oven.state == OVEN_STATE_HEATING && oven.heaterOn);
    measuredStartLatency = heaterTransitionTick - pressStart;
    CHECK(measuredStartLatency <= OVEN_BUTTON_DEBOUNCE_MS + 3U);
    CHECK(uiDirty && uiInvalid);
    CHECK(lastState == OVEN_STATE_IDLE); /* Snapshot da tela que estava em curso. */
    ProcessUi(testTick);
    CHECK(lastState == OVEN_STATE_HEATING);

    resetTest();
    Oven_StartCycle();
    DrawWithInput(KEY_K0_Pin);
    CHECK(oven.state == OVEN_STATE_IDLE && !oven.heaterOn);
    measuredStopLatency = heaterTransitionTick - pressStart;
    CHECK(measuredStopLatency <= OVEN_BUTTON_DEBOUNCE_MS + 3U);
    CHECK(uiDirty && uiInvalid);
    ProcessUi(testTick);
    CHECK(lastState == OVEN_STATE_IDLE);

    resetTest();
    DrawWithInput(ENC_PUSH_Pin);
    CHECK(oven.selectedField == OVEN_FIELD_TIME);
    CHECK(uiDirty && lastField == OVEN_FIELD_TEMPERATURE);
    ProcessUi(testTick);
    CHECK(lastField == OVEN_FIELD_TIME);

    resetTest();
    simulateEncoder = true;
    injectedSteps = 0U;
    encoderStart = testTick + 10U;
    DrawWithInput(0U);
    simulateEncoder = false;
    CHECK(injectedSteps == 10U);
    CHECK(oven.setpointC == OVEN_DEFAULT_SETPOINT_C + 10U);
    CHECK(uiDirty);
    ProcessUi(testTick);
    CHECK(lastSetpoint == oven.setpointC);

    resetTest();
    Oven_StartCycle();
    sensorWord = 4U;
    /* Proxima leitura vence 50 ms depois do inicio do desenho. */
    lastSensorTick = testTick - OVEN_SENSOR_PERIOD_MS + 50U;
    DrawWithInput(0U);
    CHECK(oven.state == OVEN_STATE_ERROR && oven.error == OVEN_ERROR_SENSOR);
    CHECK(!oven.heaterOn && heaterTransitionTick <= 1053U);
    CHECK(uiInvalid && uiDirty);

    resetTest();
    unsigned before = displayBytes;
    oven.setpointC++;
    OvenStatus_t snapshot = oven;
    OvenUI_Render(&snapshot);
    measuredCardBytes = displayBytes - before;
    CHECK(measuredCardBytes > 0U && measuredCardBytes < 3000U);

    /* Confere os pixels produzidos pela nova expansao da fonte. */
    ST7789_SetServiceCallback(NULL);
    for (uint8_t scale = 1U; scale <= 5U; scale++) {
        ST7789_DrawChar(0U, 0U, 'A', ST7789_WHITE, ST7789_BLACK, scale);
        for (uint16_t y = 0U; y < 8U * scale; y++) {
            for (uint16_t x = 0U; x < 6U * scale; x++) {
                unsigned col = x / scale;
                unsigned row = y / scale;
                bool foreground = col < 5U && row < 7U &&
                    (ST7789_GetGlyph('A')[col] & (1U << row)) != 0U;
                unsigned index = 2U * (y * 6U * scale + x);
                CHECK(st7789CharBuffer[index] == (foreground ? 255U : 0U));
                CHECK(st7789CharBuffer[index + 1U] == (foreground ? 255U : 0U));
            }
        }
    }
    testCompleted = 1U;
    __asm volatile("bkpt #0");
    while (1) {}
}
#endif
