/* Executa o codigo ARM da aplicacao com perifericos simulados. */
#include "../Core/Src/main.c"
#include "../Core/Src/oven_app.c"
#include "../Core/Src/st7789.c"
#include "../Core/Src/max6675.c"
#include "../Core/Src/stm32f1xx_it.c"

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

#define CHECK(condition) do { testCount++; if (!(condition)) { \
    failedLine = __LINE__; __asm volatile("bkpt #0"); } } while (0)

uint32_t HAL_GetTick(void) { return testTick; }
void HAL_Delay(uint32_t ms) { testTick += ms; }
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin) {
    (void)port;
    return (pressedPins & pin) ? GPIO_PIN_RESET : GPIO_PIN_SET;
}
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state) {
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

static void resetTest(void) {
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
    testTick += 59U;
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
    testTick += 60U;
    OvenApp_Process();
    pressedPins = KEY_K0_Pin;
    OvenApp_Process();
    testTick += 60U;
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

    testCompleted = 1U;
    __asm volatile("bkpt #0");
    while (1) {}
}
