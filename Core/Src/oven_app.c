#include "oven_app.h"
#include "oven_config.h"
#include "oven_ui.h"
#include "max6675.h"
#include "buzzer.h"
#include "main.h"
#include "st7789.h"
#include "oven_profile.h"
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
static OvenProfilePoint_t customPoints[OVEN_PROFILE_POINT_COUNT];
static uint32_t coolingStartTick;
static uint32_t pushPressedTick;
static bool pushLongHandled;
static uint8_t checkedPoints;

static void ProcessRealtime(void);

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
			|| (oven.state == OVEN_STATE_RUNNING) || (oven.state == OVEN_STATE_COOLING);
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
	oven.screen = OVEN_SCREEN_HOME;
	encoderAccumulator = 0;
	oven.remainingSeconds = (uint32_t) oven.setTimeMin * 60U;
	uiDirty = true;
	OvenUI_Invalidate();
}

static void Oven_EnterError(OvenError_t error) {
	bool newError = (oven.state != OVEN_STATE_ERROR) || (oven.error != error);

	Heater_OffImmediate();
	/* A falha retida do display nao pode alternar com outra falha a cada
	 * leitura do sensor, reiniciando o alarme e invalidando a UI sem parar. */
	if (oven.state == OVEN_STATE_ERROR && oven.error == OVEN_ERROR_DISPLAY
			&& error != OVEN_ERROR_DISPLAY) {
		return;
	}
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
	if (oven.profileId != OVEN_PROFILE_MANUAL) {
		if (!OvenProfile_Validate(oven.points)) {
			Oven_EnterError(OVEN_ERROR_PROFILE);
			return;
		}
		if (oven.currentTempC > OVEN_PROFILE_START_MAX_C) {
			Oven_EnterError(OVEN_ERROR_START_HOT);
			return;
		}
	}

	oven.state = oven.profileId == OVEN_PROFILE_MANUAL ? OVEN_STATE_HEATING : OVEN_STATE_RUNNING;
	oven.screen = OVEN_SCREEN_HOME;
	oven.elapsedSeconds = 0U;
	oven.activePoint = 0U;
	oven.holding = false;
	oven.traceCount = 0U;
	oven.profileStartTempC = oven.currentTempC;
	oven.targetTempC = oven.profileId == OVEN_PROFILE_MANUAL ? (float)oven.setpointC : oven.currentTempC;
	oven.profileTotalSeconds = OvenProfile_Duration(oven.points);
	cycleStartTick = HAL_GetTick();
	checkedPoints = 0U;
	encoderAccumulator = 0;
	heatingStartTick = HAL_GetTick();
	oven.error = OVEN_ERROR_NONE;
	oven.remainingSeconds = (uint32_t) oven.setTimeMin * 60U;
	/* Forno ja quente nao recebe um pulso de aquecimento ao iniciar. */
	Heater_Set(oven.profileId == OVEN_PROFILE_MANUAL && oven.currentTempC < (float)oven.setpointC);
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
	if (oven.screen == OVEN_SCREEN_EDITOR) {
		uint8_t row = oven.editCell / 3U;
		uint8_t column = oven.editCell % 3U;
		uint16_t *field = column == 0U ? &oven.points[row].targetC :
			(column == 1U ? &oven.points[row].rampSeconds : &oven.points[row].holdSeconds);
		int32_t minimum = column == 0U ? OVEN_SETPOINT_MIN_C :
			(column == 1U ? OVEN_PROFILE_RAMP_MIN_S : 0U);
		int32_t maximum = column == 0U ? OVEN_SETPOINT_MAX_C :
			(column == 1U ? OVEN_PROFILE_RAMP_MAX_S : OVEN_PROFILE_HOLD_MAX_S);
		value = (int32_t)*field + steps;
		if (value < minimum) { value = minimum; }
		if (value > maximum) { value = maximum; }
		if ((uint16_t)value != *field) {
			*field = (uint16_t)value;
			oven.profileId = OVEN_PROFILE_CUSTOM;
			for (unsigned i = 0; i < OVEN_PROFILE_POINT_COUNT; i++) { customPoints[i] = oven.points[i]; }
			oven.profileTotalSeconds = OvenProfile_Duration(oven.points);
			oven.traceCount = 0U;
			oven.elapsedSeconds = 0U;
			uiDirty = true;
			Buzzer_Click();
		}
		return;
	}
	if (oven.selectedField == OVEN_FIELD_PROFILE) {
		value = (int32_t)oven.profileId + steps;
		if (value < 0) { value = 0; }
		if (value >= OVEN_PROFILE_COUNT) { value = OVEN_PROFILE_COUNT - 1; }
		if ((OvenProfileId_t)value != oven.profileId) {
			oven.profileId = (OvenProfileId_t)value;
			oven.traceCount = 0U;
			oven.elapsedSeconds = 0U;
			if (oven.profileId == OVEN_PROFILE_CUSTOM) {
				for (unsigned i = 0; i < OVEN_PROFILE_POINT_COUNT; i++) { oven.points[i] = customPoints[i]; }
			} else { OvenProfile_Load(oven.profileId, oven.points); }
			oven.profileTotalSeconds = OvenProfile_Duration(oven.points);
			uiDirty = true;
			Buzzer_Click();
		}
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
			/* Nao leva uma fracao de detent do campo anterior para o novo. */
			encoderAccumulator = 0;
			if (oven.screen == OVEN_SCREEN_EDITOR) {
				oven.editCell++;
				if (oven.editCell >= OVEN_PROFILE_POINT_COUNT * 3U) {
					oven.screen = OVEN_SCREEN_HOME;
					oven.selectedField = OVEN_FIELD_PROFILE;
				}
			} else if (oven.profileId != OVEN_PROFILE_MANUAL) {
				oven.screen = OVEN_SCREEN_EDITOR;
				oven.editCell = 0U;
			} else {
				oven.selectedField = oven.selectedField == OVEN_FIELD_PROFILE ? OVEN_FIELD_TEMPERATURE :
					(oven.selectedField == OVEN_FIELD_TEMPERATURE ? OVEN_FIELD_TIME : OVEN_FIELD_PROFILE);
			}

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
	case OVEN_STATE_COOLING:
		Oven_StopCycle();
		break;

	case OVEN_STATE_FINISHED:
		Oven_EnterIdle();
		Buzzer_Confirm();
		break;

	case OVEN_STATE_ERROR:
		if (!ST7789_HasError() && oven.sensorValid
				&& (uint32_t)(HAL_GetTick() - lastValidSensorTick) < OVEN_SENSOR_STALE_MS
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

static void ProcessLongPush(uint32_t now) {
	if (!encPushButton.stablePressed) {
		pushPressedTick = now;
		pushLongHandled = false;
	} else if (!pushLongHandled && (uint32_t)(now - pushPressedTick) >= OVEN_PUSH_LONG_MS) {
		pushLongHandled = true;
		if (oven.state == OVEN_STATE_IDLE) {
			oven.screen = OVEN_SCREEN_HOME;
			oven.selectedField = OVEN_FIELD_PROFILE;
			encoderAccumulator = 0;
			uiDirty = true;
			Buzzer_Confirm();
		}
	}
}

static void RecordTrace(uint32_t now) {
	if (oven.profileId == OVEN_PROFILE_MANUAL || !oven.sensorValid) { return; }
	uint32_t elapsed = (now - cycleStartTick) / 1000U;
	uint32_t interval = (oven.profileTotalSeconds + 120U + OVEN_TRACE_CAPACITY - 2U) /
		(OVEN_TRACE_CAPACITY - 1U);
	uint8_t n = oven.traceCount;
	if (n != 0U && elapsed < oven.traceTime[n - 1U] + interval) { return; }
	if (n == OVEN_TRACE_CAPACITY) { n--; } else { oven.traceCount++; }
	oven.traceTime[n] = (uint16_t)elapsed;
	oven.traceTemp[n] = (uint16_t)(oven.currentTempC + 0.5f);
	uiDirty = true;
}

static void ProcessProfile(uint32_t now) {
	uint32_t elapsed = now - cycleStartTick;
	uint32_t boundary = 0U;
	uint16_t peak = 0U;
	for (unsigned i = 0U; i < OVEN_PROFILE_POINT_COUNT; i++) {
		if (oven.points[i].targetC > peak) { peak = oven.points[i].targetC; }
	}
	if (oven.currentTempC > (float)peak + OVEN_PROFILE_TOLERANCE_C) {
		Oven_EnterError(OVEN_ERROR_TRACKING);
		return;
	}
	/* Nao declara ciclo conforme se o forno nao alcancou as etapas. */
	for (uint8_t i = 0; i < OVEN_PROFILE_POINT_COUNT; i++) {
		boundary += ((uint32_t)oven.points[i].rampSeconds + oven.points[i].holdSeconds) * 1000U;
		if (i >= checkedPoints && elapsed >= boundary) {
			float difference = oven.currentTempC - (float)oven.points[i].targetC;
			if (difference > OVEN_PROFILE_TOLERANCE_C || difference < -OVEN_PROFILE_TOLERANCE_C) {
				Oven_EnterError(OVEN_ERROR_TRACKING);
				return;
			}
			checkedPoints = i + 1U;
		}
	}
	uint32_t seconds = elapsed / 1000U;
	float target;
	uint8_t point;
	bool holding;
	uint32_t remaining;
	bool running = OvenProfile_Evaluate(oven.points, oven.profileStartTempC,
		elapsed, &target, &point, &holding, &remaining);
	if (seconds != oven.elapsedSeconds || point != oven.activePoint || holding != oven.holding
		|| (uint16_t)(target * 4.0f) != (uint16_t)(oven.targetTempC * 4.0f)) { uiDirty = true; }
	oven.targetTempC = target;
	oven.elapsedSeconds = seconds;
	oven.activePoint = point;
	oven.holding = holding;
	oven.remainingSeconds = remaining;
	RecordTrace(now);
	if (!running) {
		Heater_OffImmediate();
		oven.state = OVEN_STATE_COOLING;
		coolingStartTick = now;
		uiDirty = true;
		OvenUI_Invalidate();
		Buzzer_Confirm();
		return;
	}
	/* Uma rampa descendente nunca solicita aquecimento. */
	float previous = point == 0U ? oven.profileStartTempC : (float)oven.points[point - 1U].targetC;
	if (!holding && (float)oven.points[point].targetC < previous) {
		Heater_OffImmediate();
	} else if (oven.currentTempC <= target - OVEN_HYSTERESIS_C) { Heater_Set(true); }
	else if (oven.currentTempC >= target + OVEN_HYSTERESIS_C) { Heater_Set(false); }
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
	if (Oven_IsActive() && (!oven.sensorValid || (uint32_t)(now - lastValidSensorTick)
			>= OVEN_SENSOR_STALE_MS)) {
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
		if (oven.profileId != OVEN_PROFILE_MANUAL) {
			ProcessProfile(now);
			break;
		}
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

	case OVEN_STATE_COOLING:
		Heater_OffImmediate();
		RecordTrace(now);
		if (oven.elapsedSeconds != (now - cycleStartTick) / 1000U) { uiDirty = true; }
		oven.elapsedSeconds = (now - cycleStartTick) / 1000U;
		if (oven.currentTempC <= OVEN_COOL_TARGET_C) { Oven_FinishCycle(); }
		else if ((uint32_t)(now - coolingStartTick) >= OVEN_COOL_TIMEOUT_MS) {
			Oven_EnterError(OVEN_ERROR_COOLING_TIMEOUT);
		}
		break;

	default:
		Oven_EnterError(OVEN_ERROR_SENSOR);
		break;
	}
}

static void ProcessUi(uint32_t now) {
	OvenStatus_t snapshot;
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
	/* As entradas continuam mudando oven durante o desenho. A UI recebe
	 * uma copia coerente; eventos durante o render mantem uiDirty=true. */
	snapshot = oven;
	OvenUI_Render(&snapshot);
	if (ST7789_HasError()) {
		Oven_EnterError(OVEN_ERROR_DISPLAY);
	}
}

void OvenApp_Init(void) {
	uint32_t now = HAL_GetTick();
	ST7789_SetServiceCallback(NULL);

	oven.state = OVEN_STATE_IDLE;
	oven.selectedField = OVEN_FIELD_TEMPERATURE;
	oven.error = OVEN_ERROR_NONE;
	oven.currentTempC = 0.0f;
	oven.sensorValid = false;
	oven.heaterOn = false;
	oven.setpointC = OVEN_DEFAULT_SETPOINT_C;
	oven.setTimeMin = OVEN_DEFAULT_TIME_MIN;
	oven.remainingSeconds = (uint32_t) oven.setTimeMin * 60U;
	oven.profileId = OVEN_PROFILE_SN63;
	OvenProfile_Load(oven.profileId, oven.points);
	OvenProfile_Load(OVEN_PROFILE_SN63, customPoints);
	oven.profileTotalSeconds = OvenProfile_Duration(oven.points);
	oven.screen = OVEN_SCREEN_HOME;
	oven.selectedField = OVEN_FIELD_PROFILE;
	oven.editCell = 0U;
	oven.activePoint = 0U;
	oven.holding = false;
	oven.targetTempC = 0.0f;
	oven.profileStartTempC = 25.0f;
	oven.elapsedSeconds = 0U;
	oven.traceCount = 0U;
	pushPressedTick = now;
	pushLongHandled = false;

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
	ST7789_SetServiceCallback(ProcessRealtime);
}

/* Nunca chama a UI. Pode ser executada entre blocos do driver ST7789. */
static void ProcessRealtime(void) {
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
	ProcessLongPush(HAL_GetTick());

	/* Atualiza o relogio depois das operacoes SPI. */
	ProcessControl(HAL_GetTick());
}

void OvenApp_Process(void) {
	ProcessRealtime();
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
