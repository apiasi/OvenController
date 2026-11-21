#ifndef INC_OVEN_TYPES_H_
#define INC_OVEN_TYPES_H_

#include <stdint.h>
#include <stdbool.h>
#include "oven_config.h"

typedef enum
{
    OVEN_STATE_IDLE = 0,
    OVEN_STATE_HEATING,
    OVEN_STATE_RUNNING,
    OVEN_STATE_FINISHED,
    OVEN_STATE_ERROR,
    OVEN_STATE_COOLING
} OvenState_t;

typedef enum
{
    OVEN_FIELD_TEMPERATURE = 0,
    OVEN_FIELD_TIME,
    OVEN_FIELD_PROFILE
} OvenField_t;

typedef enum
{
    OVEN_ERROR_NONE = 0,
    OVEN_ERROR_SENSOR,
    OVEN_ERROR_OVERTEMP,
    OVEN_ERROR_HEATING_TIMEOUT,
    OVEN_ERROR_DISPLAY,
    OVEN_ERROR_WATCHDOG,
    OVEN_ERROR_PROFILE,
    OVEN_ERROR_START_HOT,
    OVEN_ERROR_TRACKING,
    OVEN_ERROR_COOLING_TIMEOUT
} OvenError_t;

typedef enum {
    OVEN_PROFILE_MANUAL = 0,
    OVEN_PROFILE_SN63,
    OVEN_PROFILE_SN62,
    OVEN_PROFILE_CUSTOM,
    OVEN_PROFILE_COUNT
} OvenProfileId_t;

typedef struct {
    uint16_t targetC;
    uint16_t rampSeconds;
    uint16_t holdSeconds;
} OvenProfilePoint_t;

typedef enum { OVEN_SCREEN_HOME = 0, OVEN_SCREEN_EDITOR } OvenScreen_t;

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

    OvenProfileId_t profileId;
    OvenProfilePoint_t points[OVEN_PROFILE_POINT_COUNT];
    OvenScreen_t screen;
    uint8_t editCell;
    uint8_t activePoint;
    bool holding;
    float targetTempC;
    float profileStartTempC;
    uint32_t elapsedSeconds;
    uint32_t profileTotalSeconds;
    uint16_t traceTime[OVEN_TRACE_CAPACITY];
    uint16_t traceTemp[OVEN_TRACE_CAPACITY];
    uint8_t traceCount;
} OvenStatus_t;

#endif /* INC_OVEN_TYPES_H_ */
