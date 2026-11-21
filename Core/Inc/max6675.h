#ifndef INC_MAX6675_H_
#define INC_MAX6675_H_

#include "main.h"
#include <stdbool.h>

typedef enum
{
    MAX6675_STATUS_OK = 0,
    MAX6675_STATUS_OPEN_THERMOCOUPLE,
    MAX6675_STATUS_SPI_ERROR
} MAX6675_Status_t;

void MAX6675_Init(void);
bool MAX6675_ReadTemperature(float *temperatureC, MAX6675_Status_t *status);

#endif /* INC_MAX6675_H_ */
