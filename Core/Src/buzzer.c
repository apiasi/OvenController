#include "buzzer.h"
#include <stdbool.h>

typedef struct {
	bool active;
	bool outputOn;
	uint16_t onMs;
	uint16_t offMs;
	uint8_t repeatsRemaining;
	uint32_t nextTick;
} BuzzerPattern_t;

static BuzzerPattern_t buzzer;

static void Buzzer_SetOutput(bool on) {
	HAL_GPIO_WritePin(
	BUZZER_GPIO_Port,
	BUZZER_Pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void Buzzer_Play(uint16_t onMs, uint16_t offMs, uint8_t repeats) {
	if (repeats == 0U) {
		return;
	}

	buzzer.active = true;
	buzzer.outputOn = true;
	buzzer.onMs = onMs;
	buzzer.offMs = offMs;
	buzzer.repeatsRemaining = repeats;
	buzzer.nextTick = HAL_GetTick() + onMs;

	Buzzer_SetOutput(true);
}

void Buzzer_Init(void) {
	buzzer.active = false;
	buzzer.outputOn = false;
	buzzer.onMs = 0U;
	buzzer.offMs = 0U;
	buzzer.repeatsRemaining = 0U;
	buzzer.nextTick = 0U;
	Buzzer_SetOutput(false);
}

void Buzzer_Stop(void) {
	buzzer.active = false;
	buzzer.outputOn = false;
	buzzer.repeatsRemaining = 0U;
	Buzzer_SetOutput(false);
}

void Buzzer_Process(void) {
	uint32_t now;

	if (!buzzer.active) {
		return;
	}

	now = HAL_GetTick();

	if ((int32_t) (now - buzzer.nextTick) < 0) {
		return;
	}

	if (buzzer.outputOn) {
		Buzzer_SetOutput(false);
		buzzer.outputOn = false;

		if (buzzer.repeatsRemaining > 0U) {
			buzzer.repeatsRemaining--;
		}

		if (buzzer.repeatsRemaining == 0U) {
			buzzer.active = false;
			return;
		}

		buzzer.nextTick = now + buzzer.offMs;
	} else {
		Buzzer_SetOutput(true);
		buzzer.outputOn = true;
		buzzer.nextTick = now + buzzer.onMs;
	}
}

void Buzzer_Click(void) {
	/* Nao reinicia o bip a cada contagem se o encoder estiver girando rapido. */
	if (!buzzer.active) {
		Buzzer_Play(25U, 0U, 1U);
	}
}

void Buzzer_Confirm(void) {
	Buzzer_Play(70U, 0U, 1U);
}

void Buzzer_StartCycle(void) {
	Buzzer_Play(90U, 90U, 2U);
}

void Buzzer_StopCycle(void) {
	Buzzer_Play(220U, 0U, 1U);
}

void Buzzer_FinishCycle(void) {
	Buzzer_Play(180U, 120U, 3U);
}

void Buzzer_Error(void) {
	Buzzer_Play(300U, 150U, 4U);
}
