#include "oven_app.h"
#include "oven_config.h"
#include "oven_ui.h"
#include "max6675.h"
#include "buzzer.h"
#include "main.h"
#include "st7789.h"
#include <stdbool.h>
#include <stdint.h>

extern TIM_HandleTypeDef htim4;

typedef struct {
	GPIO_TypeDef *port;
	uint16_t pin;
	bool rawPressed;
	bool stablePressed;
	bool pressEvent;
	uint32_t rawChangedTick;
} DebouncedButton_t;

static OvenStatus_t oven;

static uint32_t lastSensorTick;
static uint32_t lastUiTick;
static uint32_t cycleStartTick;
static uint32_t heatingStartTick;
static uint32_t lastValidSensorTick;

static uint16_t encoderLastCounter;
static int32_t encoderAccumulator;

static DebouncedButton_t encPushButton;
static DebouncedButton_t keyK0Button;

static bool uiDirty;

static bool Button_ReadPressed(const DebouncedButton_t *button) {
	return HAL_GPIO_ReadPin(button->port, button->pin) == GPIO_PIN_RESET;
}

static void Button_Init(DebouncedButton_t *button, GPIO_TypeDef *port,
		uint16_t pin, uint32_t now) {
	button->port = port;
	button->pin = pin;
	button->rawPressed = Button_ReadPressed(button);
	button->stablePressed = button->rawPressed;
	button->pressEvent = false;
	button->rawChangedTick = now;
}

static void Button_Process(DebouncedButton_t *button, uint32_t now) {
	bool raw = Button_ReadPressed(button);

	if (raw != button->rawPressed) {
		button->rawPressed = raw;
		button->rawChangedTick = now;
	}

	if ((raw != button->stablePressed)
			&& ((uint32_t) (now - button->rawChangedTick)
					>= OVEN_BUTTON_DEBOUNCE_MS)) {
		button->stablePressed = raw;

		/* Gera somente um evento por pressionamento completo. A nova
		 * leitura so sera aceita depois que o botao for liberado e a
		 * liberacao tambem permanecer estavel pelo tempo de debounce. */
		if (button->stablePressed) {
			button->pressEvent = true;
		}
	}
}

static bool Button_GetPressEvent(DebouncedButton_t *button) {
	bool event = button->pressEvent;
	button->pressEvent = false;
	return event;
}

static bool Oven_IsActive(void) {
	return (oven.state == OVEN_STATE_HEATING)
			|| (oven.state == OVEN_STATE_RUNNING);
}

static void Heater_Set(bool on) {
	if (oven.heaterOn == on) {
		return;
	}

	oven.heaterOn = on;

	HAL_GPIO_WritePin(
	SSR_CMD_GPIO_Port,
	SSR_CMD_Pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET);

	uiDirty = true;
}

static void Heater_OffImmediate(void) {
	bool changed = oven.heaterOn;

	oven.heaterOn = false;
	HAL_GPIO_WritePin(SSR_CMD_GPIO_Port, SSR_CMD_Pin, GPIO_PIN_RESET);

	if (changed) {
		uiDirty = true;
	}
}

static void Oven_EnterIdle(void) {
	Heater_OffImmediate();
	oven.state = OVEN_STATE_IDLE;
	oven.error = OVEN_ERROR_NONE;
	oven.remainingSeconds = (uint32_t) oven.setTimeMin * 60U;
	uiDirty = true;
	OvenUI_Invalidate();
}

static void Oven_EnterError(OvenError_t error) {
	bool newError = (oven.state != OVEN_STATE_ERROR) || (oven.error != error);

	Heater_OffImmediate();
	oven.state = OVEN_STATE_ERROR;
	oven.error = error;
	uiDirty = true;

	if (newError) {
		OvenUI_Invalidate();
		Buzzer_Error();
	}
}

static void Oven_StartCycle(void) {
	if (ST7789_HasError()) {
		Oven_EnterError(OVEN_ERROR_DISPLAY);
		return;
	}
	if (!oven.sensorValid || ((uint32_t)(HAL_GetTick() - lastValidSensorTick)
			>= OVEN_SENSOR_STALE_MS)) {
		Buzzer_Error();
		uiDirty = true;
		return;
	}

	if (oven.currentTempC >= OVEN_HARD_MAX_C) {
		Oven_EnterError(OVEN_ERROR_OVERTEMP);
		return;
	}

	oven.state = OVEN_STATE_HEATING;
	heatingStartTick = HAL_GetTick();
	oven.error = OVEN_ERROR_NONE;
	oven.remainingSeconds = (uint32_t) oven.setTimeMin * 60U;
	/* Forno ja quente nao recebe um pulso de aquecimento ao iniciar. */
	Heater_Set(oven.currentTempC < (float)oven.setpointC);
	uiDirty = true;
	OvenUI_Invalidate();
	Buzzer_StartCycle();
}

static void Oven_StopCycle(void) {
	Heater_OffImmediate();
	Oven_EnterIdle();
	Buzzer_StopCycle();
}

static void Oven_FinishCycle(void) {
	Heater_OffImmediate();
	oven.state = OVEN_STATE_FINISHED;
	oven.remainingSeconds = 0U;
	uiDirty = true;
	OvenUI_Invalidate();
	Buzzer_FinishCycle();
}

static void ProcessSensor(uint32_t now) {
	float temp;
	float previousTemp;
	bool previousValid;
	MAX6675_Status_t sensorStatus;
	bool ok;

	if ((uint32_t) (now - lastSensorTick) < OVEN_SENSOR_PERIOD_MS) {
		return;
	}

	lastSensorTick = now;
	previousValid = oven.sensorValid;
	previousTemp = oven.currentTempC;

	ok = MAX6675_ReadTemperature(&temp, &sensorStatus);

	if (!ok) {
		oven.sensorValid = false;

		if (previousValid) {
			uiDirty = true;
		}

		if (Oven_IsActive()) {
			Oven_EnterError(OVEN_ERROR_SENSOR);
		}
		return;
	}

	oven.sensorValid = true;
	lastValidSensorTick = HAL_GetTick();
	oven.currentTempC = temp;

	/* O MAX6675 muda em passos de 0,25 C. So sinaliza atualizacao da UI
	 * quando existe alteracao real de valor ou de validade. */
	if ((!previousValid) || (oven.currentTempC != previousTemp)) {
		uiDirty = true;
	}

	if (oven.currentTempC >= OVEN_HARD_MAX_C) {
		Oven_EnterError(OVEN_ERROR_OVERTEMP);
	}
}

static int32_t ProcessEncoder(void) {
	uint16_t current;
	int16_t delta;
	int32_t steps = 0;

	current = (uint16_t) __HAL_TIM_GET_COUNTER(&htim4);
	delta = (int16_t) (current - encoderLastCounter);
	encoderLastCounter = current;

	encoderAccumulator += ((int32_t) delta * OVEN_ENCODER_DIRECTION);

	while (encoderAccumulator >= OVEN_ENCODER_COUNTS_PER_STEP) {
		encoderAccumulator -= OVEN_ENCODER_COUNTS_PER_STEP;
		steps++;
	}

	while (encoderAccumulator <= -OVEN_ENCODER_COUNTS_PER_STEP) {
		encoderAccumulator += OVEN_ENCODER_COUNTS_PER_STEP;
		steps--;
	}

	return steps;
}

static void AdjustConfiguration(int32_t steps) {
	int32_t value;

	if ((steps == 0) || (oven.state != OVEN_STATE_IDLE)) {
		return;
	}

	if (oven.selectedField == OVEN_FIELD_TEMPERATURE) {
		value = (int32_t) oven.setpointC + steps;

		if (value < (int32_t) OVEN_SETPOINT_MIN_C) {
			value = OVEN_SETPOINT_MIN_C;
		}
		if (value > (int32_t) OVEN_SETPOINT_MAX_C) {
			value = OVEN_SETPOINT_MAX_C;
		}

		if ((uint16_t) value != oven.setpointC) {
			oven.setpointC = (uint16_t) value;
			Buzzer_Click();
			uiDirty = true;
		}
	} else {
		value = (int32_t) oven.setTimeMin + steps;

		if (value < (int32_t) OVEN_TIME_MIN_MIN) {
			value = OVEN_TIME_MIN_MIN;
		}
		if (value > (int32_t) OVEN_TIME_MAX_MIN) {
			value = OVEN_TIME_MAX_MIN;
		}

		if ((uint16_t) value != oven.setTimeMin) {
			oven.setTimeMin = (uint16_t) value;
			oven.remainingSeconds = (uint32_t) oven.setTimeMin * 60U;
			Buzzer_Click();
			uiDirty = true;
		}
	}
}

static void ProcessButtons(void) {
	bool push = Button_GetPressEvent(&encPushButton);
	bool k0 = Button_GetPressEvent(&keyK0Button);

	if (push) {
		if (oven.state == OVEN_STATE_IDLE) {
			oven.selectedField =
					(oven.selectedField == OVEN_FIELD_TEMPERATURE) ?
							OVEN_FIELD_TIME : OVEN_FIELD_TEMPERATURE;

			Buzzer_Confirm();
			uiDirty = true;
			/* Nao invalida a tela inteira. Apenas os dois cards de ajuste
			 * precisam ser redesenhados, evitando uma piscada completa. */
		} else {
			Buzzer_Click();
		}
	}

	if (!k0) {
		return;
	}

	switch (oven.state) {
	case OVEN_STATE_IDLE:
		Oven_StartCycle();
		break;

	case OVEN_STATE_HEATING:
	case OVEN_STATE_RUNNING:
		Oven_StopCycle();
		break;

	case OVEN_STATE_FINISHED:
		Oven_EnterIdle();
		Buzzer_Confirm();
		break;

	case OVEN_STATE_ERROR:
		if (!ST7789_HasError() && oven.sensorValid
				&& (oven.currentTempC
						< (OVEN_HARD_MAX_C - OVEN_ERROR_RESET_MARGIN_C))) {
			Oven_EnterIdle();
			Buzzer_Confirm();
		} else {
			Buzzer_Error();
		}
		break;

	default:
		Oven_EnterIdle();
		break;
	}
}

static void ProcessControl(uint32_t now) {
	uint32_t durationMs;
	uint32_t elapsedMs;
	uint32_t remainingMs;
	uint32_t newRemainingSeconds;

	if (ST7789_HasError()) {
		if (oven.state != OVEN_STATE_ERROR || oven.error != OVEN_ERROR_DISPLAY) {
			Oven_EnterError(OVEN_ERROR_DISPLAY);
		}
		return;
	}
	if (Oven_IsActive() && (uint32_t)(now - lastValidSensorTick)
			>= OVEN_SENSOR_STALE_MS) {
		oven.sensorValid = false;
		Oven_EnterError(OVEN_ERROR_SENSOR);
		return;
	}

	switch (oven.state) {
	case OVEN_STATE_IDLE:
	case OVEN_STATE_FINISHED:
	case OVEN_STATE_ERROR:
		Heater_OffImmediate();
		break;

	case OVEN_STATE_HEATING:
		if ((uint32_t)(now - heatingStartTick) >= OVEN_HEATING_TIMEOUT_MS) {
			Oven_EnterError(OVEN_ERROR_HEATING_TIMEOUT);
			break;
		}
		if (!oven.sensorValid) {
			Oven_EnterError(OVEN_ERROR_SENSOR);
			break;
		}

		if (oven.currentTempC >= (float) oven.setpointC) {
			Heater_OffImmediate();
			oven.state = OVEN_STATE_RUNNING;
			cycleStartTick = now;
			oven.remainingSeconds = (uint32_t) oven.setTimeMin * 60U;
			uiDirty = true;
			OvenUI_Invalidate();
			Buzzer_Confirm();
		} else {
			Heater_Set(true);
		}
		break;

	case OVEN_STATE_RUNNING:
		if (!oven.sensorValid) {
			Oven_EnterError(OVEN_ERROR_SENSOR);
			break;
		}

		durationMs = (uint32_t) oven.setTimeMin * 60U * 1000U;
		elapsedMs = (uint32_t) (now - cycleStartTick);

		if (elapsedMs >= durationMs) {
			Oven_FinishCycle();
			break;
		}

		remainingMs = durationMs - elapsedMs;
		newRemainingSeconds = (remainingMs + 999U) / 1000U;

		if (newRemainingSeconds != oven.remainingSeconds) {
			oven.remainingSeconds = newRemainingSeconds;
			uiDirty = true;
		}

		if (oven.currentTempC <= ((float) oven.setpointC - OVEN_HYSTERESIS_C)) {
			Heater_Set(true);
		} else if (oven.currentTempC
				>= ((float) oven.setpointC + OVEN_HYSTERESIS_C)) {
			Heater_Set(false);
		}
		/* Dentro da banda de histerese mantem o estado atual. */
		break;

	default:
		Oven_EnterError(OVEN_ERROR_SENSOR);
		break;
	}
}

static void ProcessUi(uint32_t now) {
	/* Falha retida: nao tenta desenhar novamente em um barramento com erro. */
	if (ST7789_HasError()) {
		return;
	}
	/* A versao anterior redesenhava a UI periodicamente mesmo quando nada
	 * havia mudado. Isso causava cintilacao perceptivel no ST7789. */
	if (!uiDirty) {
		return;
	}

	if ((uint32_t) (now - lastUiTick) < OVEN_UI_PERIOD_MS) {
		return;
	}

	lastUiTick = now;
	uiDirty = false;
	OvenUI_Render(&oven);
	if (ST7789_HasError()) {
		Oven_EnterError(OVEN_ERROR_DISPLAY);
	}
}

void OvenApp_Init(void) {
	uint32_t now = HAL_GetTick();

	oven.state = OVEN_STATE_IDLE;
	oven.selectedField = OVEN_FIELD_TEMPERATURE;
	oven.error = OVEN_ERROR_NONE;
	oven.currentTempC = 0.0f;
	oven.sensorValid = false;
	oven.heaterOn = false;
	oven.setpointC = OVEN_DEFAULT_SETPOINT_C;
	oven.setTimeMin = OVEN_DEFAULT_TIME_MIN;
	oven.remainingSeconds = (uint32_t) oven.setTimeMin * 60U;

	lastSensorTick = now - OVEN_SENSOR_PERIOD_MS;
	lastUiTick = now - OVEN_UI_PERIOD_MS;
	cycleStartTick = 0U;
	heatingStartTick = 0U;
	lastValidSensorTick = now;

	encoderAccumulator = 0;

	Heater_OffImmediate();
	Buzzer_Init();
	MAX6675_Init();

	if (HAL_TIM_Encoder_Start(&htim4, TIM_CHANNEL_ALL) != HAL_OK) {
		Error_Handler();
	}
	encoderLastCounter = (uint16_t) __HAL_TIM_GET_COUNTER(&htim4);

	Button_Init(&encPushButton, ENC_PUSH_GPIO_Port, ENC_PUSH_Pin, now);
	Button_Init(&keyK0Button, KEY_K0_GPIO_Port, KEY_K0_Pin, now);

	OvenUI_Init();
	uiDirty = true;
	if ((RCC->CSR & RCC_CSR_IWDGRSTF) != 0U) {
		Oven_EnterError(OVEN_ERROR_WATCHDOG);
	}
	__HAL_RCC_CLEAR_RESET_FLAGS();
	if (ST7789_HasError()) {
		Oven_EnterError(OVEN_ERROR_DISPLAY);
	}
}

void OvenApp_Process(void) {
	uint32_t now = HAL_GetTick();
	int32_t encoderSteps;

	Buzzer_Process();

	/* Debounce real: valida nivel estavel tanto na pressao quanto na
	 * liberacao. Nao depende de multiplas bordas EXTI. */
	Button_Process(&encPushButton, now);
	Button_Process(&keyK0Button, now);

	ProcessSensor(now);

	encoderSteps = ProcessEncoder();
	AdjustConfiguration(encoderSteps);
	ProcessButtons();

	/* Atualiza o relogio depois das operacoes SPI. */
	ProcessControl(HAL_GetTick());
	ProcessUi(HAL_GetTick());
}

const OvenStatus_t* OvenApp_GetStatus(void) {
	return &oven;
}

/* PB8/PB9 podem continuar configurados como EXTI no CubeMX. A aplicacao
 * deliberadamente nao usa a borda para aceitar o botao; a decisao e feita
 * pelo polling com estado estavel acima. Isso elimina duplo START/STOP por
 * bounce. */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
	(void) GPIO_Pin;
}
