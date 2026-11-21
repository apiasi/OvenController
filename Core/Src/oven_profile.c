#include "oven_profile.h"

/* Pontos iniciais editaveis para ligas com chumbo; qualificar na placa real.
 * A permanencia e o tempo da rampa sao separados, em segundos. */
static const OvenProfilePoint_t sn63[OVEN_PROFILE_POINT_COUNT] = {
    {120U, 90U, 0U}, {150U, 45U, 60U}, {185U, 30U, 0U}, {210U, 30U, 30U}
};
static const OvenProfilePoint_t sn62[OVEN_PROFILE_POINT_COUNT] = {
    {120U, 90U, 0U}, {150U, 45U, 60U}, {185U, 30U, 0U}, {205U, 30U, 30U}
};

const char *OvenProfile_Name(OvenProfileId_t id) {
    switch (id) {
    case OVEN_PROFILE_MANUAL: return "MANUAL";
    case OVEN_PROFILE_SN63: return "SN63 / PB37";
    case OVEN_PROFILE_SN62: return "SN62 / PB36 / AG2";
    case OVEN_PROFILE_CUSTOM: return "PERSONALIZADO";
    default: return "INVALIDO";
    }
}

void OvenProfile_Load(OvenProfileId_t id, OvenProfilePoint_t *points) {
    const OvenProfilePoint_t *source = id == OVEN_PROFILE_SN62 ? sn62 : sn63;
    for (unsigned i = 0; i < OVEN_PROFILE_POINT_COUNT; i++) { points[i] = source[i]; }
}

uint32_t OvenProfile_Duration(const OvenProfilePoint_t *points) {
    uint32_t seconds = 0U;
    for (unsigned i = 0; i < OVEN_PROFILE_POINT_COUNT; i++) {
        seconds += points[i].rampSeconds + points[i].holdSeconds;
    }
    return seconds;
}

bool OvenProfile_Validate(const OvenProfilePoint_t *points) {
    for (unsigned i = 0; i < OVEN_PROFILE_POINT_COUNT; i++) {
        if (points[i].targetC < OVEN_SETPOINT_MIN_C || points[i].targetC > OVEN_SETPOINT_MAX_C
            || points[i].rampSeconds < OVEN_PROFILE_RAMP_MIN_S
            || points[i].rampSeconds > OVEN_PROFILE_RAMP_MAX_S
            || points[i].holdSeconds > OVEN_PROFILE_HOLD_MAX_S) { return false; }
    }
    return true;
}

bool OvenProfile_Evaluate(const OvenProfilePoint_t *points, float startTempC,
                         uint32_t elapsedMs, float *targetC, uint8_t *point,
                         bool *holding, uint32_t *remainingSeconds) {
    uint32_t cursor = 0U;
    float previous = startTempC;
    for (uint8_t i = 0U; i < OVEN_PROFILE_POINT_COUNT; i++) {
        uint32_t rampMs = (uint32_t)points[i].rampSeconds * 1000U;
        uint32_t end = cursor + rampMs + (uint32_t)points[i].holdSeconds * 1000U;
        if (elapsedMs < end) {
            uint32_t local = elapsedMs - cursor;
            *holding = local >= rampMs;
            *targetC = *holding ? (float)points[i].targetC : previous +
                ((float)points[i].targetC - previous) * (float)local / (float)rampMs;
            *point = i;
            *remainingSeconds = (end - elapsedMs + 999U) / 1000U;
            return true;
        }
        cursor = end;
        previous = (float)points[i].targetC;
    }
    *targetC = previous;
    *point = OVEN_PROFILE_POINT_COUNT - 1U;
    *holding = false;
    *remainingSeconds = 0U;
    return false;
}
