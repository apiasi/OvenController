#ifndef INC_ST7789_H_
#define INC_ST7789_H_

#include "main.h"
#include <stdbool.h>

#define ST7789_WIDTH   320
#define ST7789_HEIGHT  240

/* RGB565 */
#define ST7789_BLACK    0x0000
#define ST7789_WHITE    0xFFFF
#define ST7789_RED      0xF800
#define ST7789_GREEN    0x07E0
#define ST7789_BLUE     0x001F
#define ST7789_YELLOW   0xFFE0
#define ST7789_CYAN     0x07FF
#define ST7789_MAGENTA  0xF81F
#define ST7789_GRAY     0x8410
#define ST7789_DARKGRAY 0x4208
#define ST7789_ORANGE   0xFD20

void ST7789_Init(void);
bool ST7789_HasError(void);
void ST7789_FillScreen(uint16_t color);
void ST7789_FillRect(uint16_t x,
                     uint16_t y,
                     uint16_t width,
                     uint16_t height,
                     uint16_t color);
void ST7789_DrawPixel(uint16_t x,
                      uint16_t y,
                      uint16_t color);
void ST7789_DrawChar(uint16_t x,
                     uint16_t y,
                     char c,
                     uint16_t color,
                     uint16_t bgcolor,
                     uint8_t scale);
void ST7789_WriteString(uint16_t x,
                        uint16_t y,
                        const char *str,
                        uint16_t color,
                        uint16_t bgcolor,
                        uint8_t scale);

#endif /* INC_ST7789_H_ */
