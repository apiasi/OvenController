#include "max6675.h"
#include "oven_config.h"

extern SPI_HandleTypeDef hspi2;

void MAX6675_Init(void)
{
    /* CS alto mantem o conversor em conversao / deselecionado. */
    HAL_GPIO_WritePin(MAX6675_CS_GPIO_Port, MAX6675_CS_Pin, GPIO_PIN_SET);
}

bool MAX6675_ReadTemperature(float *temperatureC, MAX6675_Status_t *status)
{
    uint8_t tx[2] = {0xFFU, 0xFFU};
    uint8_t rx[2] = {0U, 0U};
    uint16_t raw;
    HAL_StatusTypeDef halStatus;

    if (temperatureC == NULL)
    {
        if (status != NULL)
        {
            *status = MAX6675_STATUS_SPI_ERROR;
        }
        return false;
    }

    HAL_GPIO_WritePin(MAX6675_CS_GPIO_Port, MAX6675_CS_Pin, GPIO_PIN_RESET);

    halStatus = HAL_SPI_TransmitReceive(
        &hspi2,
        tx,
        rx,
        2U,
        OVEN_SENSOR_SPI_TIMEOUT_MS
    );

    HAL_GPIO_WritePin(MAX6675_CS_GPIO_Port, MAX6675_CS_Pin, GPIO_PIN_SET);

    if (halStatus != HAL_OK)
    {
        if (status != NULL)
        {
            *status = MAX6675_STATUS_SPI_ERROR;
        }
        return false;
    }

    raw = ((uint16_t)rx[0] << 8) | (uint16_t)rx[1];

    /* D2 = 1 indica termopar aberto. */
    if ((raw & 0x0004U) != 0U)
    {
        if (status != NULL)
        {
            *status = MAX6675_STATUS_OPEN_THERMOCOUPLE;
        }
        return false;
    }

    /* D14..D3: 12 bits, 0,25 C por LSB. */
    *temperatureC = (float)((raw >> 3) & 0x0FFFU) * 0.25f;

    if (status != NULL)
    {
        *status = MAX6675_STATUS_OK;
    }

    return true;
}
