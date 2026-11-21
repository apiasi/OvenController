#ifndef INC_OVEN_UI_H_
#define INC_OVEN_UI_H_

#include "oven_types.h"

void OvenUI_Init(void);
void OvenUI_Invalidate(void);
void OvenUI_Render(const OvenStatus_t *status);

#endif /* INC_OVEN_UI_H_ */
