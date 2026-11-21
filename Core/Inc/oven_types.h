#ifndef INC_OVEN_TYPES_H_
#define INC_OVEN_TYPES_H_

#include <stdint.h>
#include <stdbool.h>

typedef enum
{
    OVEN_STATE_IDLE = 0,
    OVEN_STATE_HEATING,
    OVEN_STATE_RUNNING,
    OVEN_STATE_FINISHED,
    OVEN_STATE_ERROR
} OvenState_t;

typedef enum
{
    OVEN_FIELD_TEMPERATURE = 0,
    OVEN_FIELD_TIME
} OvenField_t;

typedef enum
{
    OVEN_ERROR_NONE = 0,
    OVEN_ERROR_SENSOR,
    OVEN_ERROR_OVERTEMP,
    OVEN_ERROR_HEATING_TIMEOUT,
    OVEN_ERROR_DISPLAY,
    OVEN_ERROR_WATCHDOG
} OvenError_t;

typedef struct
{
    OvenState_t state;
    OvenField_t selectedField;
    OvenError_t error;

    float currentTempC;
    bool sensorValid;
    bool heaterOn;

    uint16_t setpointC;
    uint16_t setTimeMin;
    uint32_t remainingSeconds;
} OvenStatus_t;

#endif /* INC_OVEN_TYPES_H_ */
