#ifndef INC_OVEN_CONFIG_H_
#define INC_OVEN_CONFIG_H_

#define OVEN_FIRMWARE_VERSION          "3.0.0"
#define OVEN_HEATING_TIMEOUT_MS        (30UL * 60UL * 1000UL)
#define OVEN_SENSOR_STALE_MS           1000U
#define OVEN_SENSOR_SPI_TIMEOUT_MS     5U
#define OVEN_DISPLAY_SPI_TIMEOUT_MS    10U

/* =============================
 * Configuracao funcional do forno
 * ============================= */

/* Valores iniciais apresentados ao ligar */
#define OVEN_DEFAULT_SETPOINT_C       80U
#define OVEN_DEFAULT_TIME_MIN         20U

/* Limites de configuracao pelo encoder */
#define OVEN_SETPOINT_MIN_C           30U
#define OVEN_SETPOINT_MAX_C           250U
#define OVEN_TIME_MIN_MIN             1U
#define OVEN_TIME_MAX_MIN             180U

/* Controle ON/OFF com histerese durante a temporizacao */
#define OVEN_HYSTERESIS_C             1.5f

/* Corte de seguranca por software. Nao substitui protecao termica fisica. */
#define OVEN_HARD_MAX_C               280.0f
#define OVEN_ERROR_RESET_MARGIN_C     10.0f

/* Periodos das tarefas cooperativas */
#define OVEN_SENSOR_PERIOD_MS         250U
#define OVEN_UI_PERIOD_MS             500U

/* EC11 normalmente gera 4 contagens por detent em Encoder Mode TI12.
 * Se o seu encoder der 2 ou 1 contagem por clique, ajuste aqui. */
#define OVEN_ENCODER_COUNTS_PER_STEP  4

/* Troque para -1 caso o sentido do encoder fique invertido. */
#define OVEN_ENCODER_DIRECTION        1

/* Debounce verdadeiro dos botoes PUSH e K0.
 * O nivel precisa permanecer estavel este tempo tanto ao pressionar
 * quanto ao liberar antes de uma nova pressao ser aceita. */
#define OVEN_BUTTON_DEBOUNCE_MS       60U

#endif /* INC_OVEN_CONFIG_H_ */
