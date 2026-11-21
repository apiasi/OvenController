#ifndef INC_OVEN_PROFILE_H_
#define INC_OVEN_PROFILE_H_

#include "oven_types.h"

const char *OvenProfile_Name(OvenProfileId_t id);
void OvenProfile_Load(OvenProfileId_t id, OvenProfilePoint_t *points);
uint32_t OvenProfile_Duration(const OvenProfilePoint_t *points);
bool OvenProfile_Validate(const OvenProfilePoint_t *points);
/* Requer pontos validados. false indica fim do programa; resfriamento na app. */
bool OvenProfile_Evaluate(const OvenProfilePoint_t *points, float startTempC,
                         uint32_t elapsedMs, float *targetC, uint8_t *point,
                         bool *holding, uint32_t *remainingSeconds);

#endif
