#include "oven_ui.h"
#include "st7789.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>

#define UI_BG          ST7789_BLACK
#define UI_FG          ST7789_WHITE
#define UI_ACCENT      ST7789_CYAN
#define UI_WARN        ST7789_YELLOW
#define UI_OK          ST7789_GREEN
#define UI_ERROR       ST7789_RED
#define UI_DIM         ST7789_DARKGRAY

static bool uiInvalid = true;
static uint32_t invalidationGeneration;
static OvenState_t lastState = (OvenState_t) 0xFF;
static OvenField_t lastField = (OvenField_t) 0xFF;
static int32_t lastTempQuarter = INT32_MIN;
static bool lastSensorValid = false;
static uint16_t lastSetpoint = UINT16_MAX;
static uint16_t lastTimeMin = UINT16_MAX;
static uint32_t lastRemaining = UINT32_MAX;
static bool lastHeaterOn = false;
static bool lastHeaterValid = false;

static void UI_ResetDynamicCache(void) {
	lastField = (OvenField_t) 0xFF;
	lastTempQuarter = INT32_MIN;
	lastSensorValid = false;
	lastSetpoint = UINT16_MAX;
	lastTimeMin = UINT16_MAX;
	lastRemaining = UINT32_MAX;
	lastHeaterOn = false;
	lastHeaterValid = false;
}

static int32_t UI_TemperatureKey(float temperatureC, bool valid) {
	if (!valid) {
		return INT32_MIN;
	}

	/* MAX6675 trabalha em passos de 0,25 C. */
	return (int32_t) ((temperatureC * 4.0f) + 0.5f);
}

static void UI_FormatTemperature(char *buffer, size_t size, float temperatureC,
		bool valid) {
	int32_t value10;
	int32_t whole;
	int32_t decimal;

	if (!valid) {
		(void) snprintf(buffer, size, "--.- C");
		return;
	}

	value10 = (int32_t) ((temperatureC * 10.0f) + 0.5f);
	whole = value10 / 10;
	decimal = value10 % 10;

	(void) snprintf(buffer, size, "%ld.%ld C", (long) whole, (long) decimal);
}

static void UI_FormatTime(char *buffer, size_t size, uint32_t seconds) {
	uint32_t minutes = seconds / 60U;
	uint32_t secs = seconds % 60U;

	(void) snprintf(buffer, size, "%02lu:%02lu", (unsigned long) minutes,
			(unsigned long) secs);
}

static void UI_Pad(char *out, size_t outSize, const char *in, unsigned width) {
	(void) snprintf(out, outSize, "%-*s", (int) width, in);
}

static void UI_DrawHeader(const char *title, uint16_t color) {
	ST7789_FillRect(0, 0, ST7789_WIDTH, 42, UI_DIM);
	ST7789_WriteString(12, 9, title, color, UI_DIM, 3);
}

static void UI_DrawFooter(const char *text) {
	ST7789_FillRect(0, 214, ST7789_WIDTH, 26, UI_DIM);
	ST7789_WriteString(8, 219, text, ST7789_WHITE, UI_DIM, 2);
}

static void UI_DrawIdleStatic(void) {
	ST7789_FillScreen(UI_BG);
	UI_DrawHeader("FORNO PCB", UI_WARN);

	ST7789_WriteString(12, 52, "ATUAL", UI_ACCENT, UI_BG, 2);
	ST7789_WriteString(12, 126, "AJUSTES", UI_ACCENT, UI_BG, 2);

	UI_DrawFooter("PUSH TROCA  K0 START");
}

static void UI_DrawHeatingStatic(void) {
	ST7789_FillScreen(UI_BG);
	UI_DrawHeader("AQUECENDO", UI_WARN);

	ST7789_WriteString(12, 52, "TEMPERATURA", UI_ACCENT, UI_BG, 2);
	ST7789_WriteString(12, 126, "ALVO", UI_ACCENT, UI_BG, 2);
	ST7789_WriteString(168, 126, "TEMPO", UI_ACCENT, UI_BG, 2);

	UI_DrawFooter("K0 STOP - TEMPO APOS ALVO");
}

static void UI_DrawRunningStatic(void) {
	ST7789_FillScreen(UI_BG);
	UI_DrawHeader("CONTROLANDO", UI_OK);

	ST7789_WriteString(12, 52, "TEMPERATURA", UI_ACCENT, UI_BG, 2);
	ST7789_WriteString(12, 126, "ALVO", UI_ACCENT, UI_BG, 2);
	ST7789_WriteString(168, 126, "RESTANTE", UI_ACCENT, UI_BG, 2);

	UI_DrawFooter("K0 STOP");
}

static void UI_DrawFinishedStatic(void) {
	ST7789_FillScreen(UI_BG);
	UI_DrawHeader("FINALIZADO", UI_OK);

	ST7789_WriteString(33, 82, "CICLO CONCLUIDO", UI_OK, UI_BG, 3);
	ST7789_WriteString(55, 142, "AQUECIMENTO OFF", UI_WARN, UI_BG, 2);

	UI_DrawFooter("K0 VOLTAR");
}

static void UI_DrawErrorStatic(OvenError_t error) {
	ST7789_FillScreen(UI_BG);
	UI_DrawHeader("ERRO", UI_ERROR);

	if (error == OVEN_ERROR_SENSOR) {
		ST7789_WriteString(38, 78, "FALHA NO SENSOR", UI_ERROR, UI_BG, 3);
		ST7789_WriteString(50, 118, "VERIFIQUE TERMOPAR", UI_WARN, UI_BG, 2);
	} else if (error == OVEN_ERROR_OVERTEMP) {
		ST7789_WriteString(50, 78, "SOBRETEMPERATURA", UI_ERROR, UI_BG, 2);
	} else if (error == OVEN_ERROR_HEATING_TIMEOUT) {
		ST7789_WriteString(26, 78, "TEMPO DE AQUECIMENTO", UI_ERROR, UI_BG, 2);
		ST7789_WriteString(62, 118, "LIMITE EXCEDIDO", UI_WARN, UI_BG, 2);
	} else if (error == OVEN_ERROR_WATCHDOG) {
		ST7789_WriteString(32, 78, "REINICIO POR FALHA", UI_ERROR, UI_BG, 2);
		ST7789_WriteString(38, 118, "VERIFIQUE O FORNO", UI_WARN, UI_BG, 2);
	} else if (error == OVEN_ERROR_DISPLAY) {
		ST7789_WriteString(50, 78, "FALHA DO DISPLAY", UI_ERROR, UI_BG, 2);
	} else {
		ST7789_WriteString(85, 78, "FALHA", UI_ERROR, UI_BG, 3);
	}

	ST7789_WriteString(50, 158, "AQUECIMENTO OFF", UI_WARN, UI_BG, 2);
	UI_DrawFooter("CORRIJA E PRESSIONE K0");
}

static void UI_UpdateCurrentTemperature(const OvenStatus_t *status) {
	char text[20];
	char padded[20];
	int32_t key = UI_TemperatureKey(status->currentTempC, status->sensorValid);

	if ((key == lastTempQuarter) && (status->sensorValid == lastSensorValid)) {
		return;
	}

	UI_FormatTemperature(text, sizeof(text), status->currentTempC,
			status->sensorValid);
	UI_Pad(padded, sizeof(padded), text, 9U);

	ST7789_WriteString(18, 77, padded, status->sensorValid ? UI_FG : UI_ERROR,
	UI_BG, 5);

	lastTempQuarter = key;
	lastSensorValid = status->sensorValid;
}

static void UI_UpdateHeaterStatus(bool heaterOn) {
	if (lastHeaterValid && (heaterOn == lastHeaterOn)) {
		return;
	}

	ST7789_WriteString(12, 184, heaterOn ? "HEAT ON  " : "HEAT OFF ",
			heaterOn ? UI_WARN : UI_OK,
			UI_BG, 2);

	lastHeaterOn = heaterOn;
	lastHeaterValid = true;
}

static void UI_UpdateIdleCards(const OvenStatus_t *status) {
	char text[20];
	char padded[20];
	uint16_t tempBg;
	uint16_t timeBg;
	uint16_t tempFg;
	uint16_t timeFg;
	bool selectionChanged = status->selectedField != lastField;

	if ((status->setpointC == lastSetpoint)
			&& (status->setTimeMin == lastTimeMin) && !selectionChanged) {
		return;
	}

	tempBg =
			(status->selectedField == OVEN_FIELD_TEMPERATURE) ?
					UI_ACCENT : UI_DIM;
	timeBg = (status->selectedField == OVEN_FIELD_TIME) ? UI_ACCENT : UI_DIM;
	tempFg =
			(status->selectedField == OVEN_FIELD_TEMPERATURE) ?
					ST7789_BLACK : UI_FG;
	timeFg = (status->selectedField == OVEN_FIELD_TIME) ? ST7789_BLACK : UI_FG;

	/* Girar o encoder atualiza apenas o valor do card afetado. Fundos e
	 * titulos precisam mudar somente quando a selecao muda. */
	if (selectionChanged) {
		ST7789_FillRect(12, 148, 145, 52, tempBg);
		ST7789_WriteString(20, 154, "TEMP", tempFg, tempBg, 2);
		ST7789_FillRect(163, 148, 145, 52, timeBg);
		ST7789_WriteString(171, 154, "TEMPO", timeFg, timeBg, 2);
	}
	if (selectionChanged || status->setpointC != lastSetpoint) {
		(void) snprintf(text, sizeof(text), "%u C", (unsigned) status->setpointC);
		UI_Pad(padded, sizeof(padded), text, 7U);
		ST7789_WriteString(20, 177, padded, tempFg, tempBg, 2);
	}
	if (selectionChanged || status->setTimeMin != lastTimeMin) {
		(void) snprintf(text, sizeof(text), "%u MIN", (unsigned) status->setTimeMin);
		UI_Pad(padded, sizeof(padded), text, 7U);
		ST7789_WriteString(171, 177, padded, timeFg, timeBg, 2);
	}

	lastSetpoint = status->setpointC;
	lastTimeMin = status->setTimeMin;
	lastField = status->selectedField;
}

static void UI_UpdateHeatingValues(const OvenStatus_t *status) {
	char text[20];
	char padded[20];

	if (status->setpointC != lastSetpoint) {
		(void) snprintf(text, sizeof(text), "%u C",
				(unsigned) status->setpointC);
		UI_Pad(padded, sizeof(padded), text, 7U);
		ST7789_WriteString(12, 151, padded, UI_FG, UI_BG, 3);
		lastSetpoint = status->setpointC;
	}

	if (status->setTimeMin != lastTimeMin) {
		(void) snprintf(text, sizeof(text), "%u MIN",
				(unsigned) status->setTimeMin);
		UI_Pad(padded, sizeof(padded), text, 7U);
		ST7789_WriteString(168, 151, padded, UI_FG, UI_BG, 3);
		lastTimeMin = status->setTimeMin;
	}

	UI_UpdateHeaterStatus(status->heaterOn);
}

static void UI_UpdateRunningValues(const OvenStatus_t *status) {
	char text[20];
	char padded[20];

	if (status->setpointC != lastSetpoint) {
		(void) snprintf(text, sizeof(text), "%u C",
				(unsigned) status->setpointC);
		UI_Pad(padded, sizeof(padded), text, 7U);
		ST7789_WriteString(12, 151, padded, UI_FG, UI_BG, 3);
		lastSetpoint = status->setpointC;
	}

	if (status->remainingSeconds != lastRemaining) {
		UI_FormatTime(text, sizeof(text), status->remainingSeconds);
		UI_Pad(padded, sizeof(padded), text, 7U);
		ST7789_WriteString(168, 151, padded, UI_WARN, UI_BG, 3);
		lastRemaining = status->remainingSeconds;
	}

	UI_UpdateHeaterStatus(status->heaterOn);
}

void OvenUI_Init(void) {
	ST7789_FillScreen(UI_BG);
	uiInvalid = true;
	lastState = (OvenState_t) 0xFF;
	UI_ResetDynamicCache();
}

void OvenUI_Invalidate(void) {
	uiInvalid = true;
	invalidationGeneration++;
}

void OvenUI_Render(const OvenStatus_t *status) {
	bool fullRedraw;
	uint32_t generation = invalidationGeneration;

	if (status == NULL) {
		return;
	}

	/* Mudanca do campo selecionado NAO apaga mais a tela inteira. */
	fullRedraw = uiInvalid || (status->state != lastState);

	if (fullRedraw) {
		switch (status->state) {
		case OVEN_STATE_IDLE:
			UI_DrawIdleStatic();
			break;

		case OVEN_STATE_HEATING:
			UI_DrawHeatingStatic();
			break;

		case OVEN_STATE_RUNNING:
			UI_DrawRunningStatic();
			break;

		case OVEN_STATE_FINISHED:
			UI_DrawFinishedStatic();
			break;

		case OVEN_STATE_ERROR:
		default:
			UI_DrawErrorStatic(status->error);
			break;
		}

		/* Uma mudanca de estado/erro durante o desenho pede outra tela. */
		uiInvalid = (generation != invalidationGeneration);
		lastState = status->state;
		UI_ResetDynamicCache();
	}

	switch (status->state) {
	case OVEN_STATE_IDLE:
		UI_UpdateCurrentTemperature(status);
		UI_UpdateIdleCards(status);
		break;

	case OVEN_STATE_HEATING:
		UI_UpdateCurrentTemperature(status);
		UI_UpdateHeatingValues(status);
		break;

	case OVEN_STATE_RUNNING:
		UI_UpdateCurrentTemperature(status);
		UI_UpdateRunningValues(status);
		break;

	case OVEN_STATE_FINISHED:
	case OVEN_STATE_ERROR:
	default:
		break;
	}
}
