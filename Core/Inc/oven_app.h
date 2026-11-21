#ifndef INC_OVEN_APP_H_
#define INC_OVEN_APP_H_

#include "oven_types.h"

void OvenApp_Init(void);
void OvenApp_Process(void);
const OvenStatus_t *OvenApp_GetStatus(void);

#endif /* INC_OVEN_APP_H_ */
