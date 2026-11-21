#include "st7789.h"
#include "oven_config.h"
#include <stdbool.h>

/* SPI1 criado pelo CubeMX em main.c */
extern SPI_HandleTypeDef hspi1;

/* Falha fica retida ate reiniciar; nenhuma nova transferencia e tentada. */
static bool spiFailed;

bool ST7789_HasError(void) {
    return spiFailed;
}

static bool ST7789_Transmit(const uint8_t *data, uint16_t size) {
    if (spiFailed) {
        return false;
    }
    if (HAL_SPI_Transmit(&hspi1, (uint8_t *)data, size,
                         OVEN_DISPLAY_SPI_TIMEOUT_MS) != HAL_OK) {
        spiFailed = true;
        Safety_HeaterOff();
        return false;
    }
    return true;
}

/* Comandos ST7789 */
#define ST7789_SWRESET  0x01
#define ST7789_SLPOUT   0x11
#define ST7789_NORON    0x13
#define ST7789_INVON    0x21
#define ST7789_DISPON   0x29
#define ST7789_CASET    0x2A
#define ST7789_RASET    0x2B
#define ST7789_RAMWR    0x2C
#define ST7789_MADCTL   0x36
#define ST7789_COLMOD   0x3A

static void ST7789_Select(void)
{
    HAL_GPIO_WritePin(TFT_CS_GPIO_Port, TFT_CS_Pin, GPIO_PIN_RESET);
}

static void ST7789_Unselect(void)
{
    HAL_GPIO_WritePin(TFT_CS_GPIO_Port, TFT_CS_Pin, GPIO_PIN_SET);
}

static void ST7789_WriteCommand(uint8_t cmd)
{
    HAL_GPIO_WritePin(TFT_DC_GPIO_Port, TFT_DC_Pin, GPIO_PIN_RESET);
    ST7789_Select();
    (void)ST7789_Transmit(&cmd, 1U);
    ST7789_Unselect();
}

static void ST7789_WriteData(const uint8_t *data, uint16_t size)
{
    HAL_GPIO_WritePin(TFT_DC_GPIO_Port, TFT_DC_Pin, GPIO_PIN_SET);
    ST7789_Select();
    (void)ST7789_Transmit(data, size);
    ST7789_Unselect();
}

static void ST7789_Reset(void)
{
    HAL_GPIO_WritePin(TFT_RST_GPIO_Port, TFT_RST_Pin, GPIO_PIN_RESET);
    HAL_Delay(20);

    HAL_GPIO_WritePin(TFT_RST_GPIO_Port, TFT_RST_Pin, GPIO_PIN_SET);
    HAL_Delay(120);
}

static void ST7789_SetAddressWindow(uint16_t x0,
                                    uint16_t y0,
                                    uint16_t x1,
                                    uint16_t y1)
{
    uint8_t data[4];

    ST7789_WriteCommand(ST7789_CASET);
    data[0] = (uint8_t)(x0 >> 8);
    data[1] = (uint8_t)(x0 & 0xFF);
    data[2] = (uint8_t)(x1 >> 8);
    data[3] = (uint8_t)(x1 & 0xFF);
    ST7789_WriteData(data, 4);

    ST7789_WriteCommand(ST7789_RASET);
    data[0] = (uint8_t)(y0 >> 8);
    data[1] = (uint8_t)(y0 & 0xFF);
    data[2] = (uint8_t)(y1 >> 8);
    data[3] = (uint8_t)(y1 & 0xFF);
    ST7789_WriteData(data, 4);

    ST7789_WriteCommand(ST7789_RAMWR);
}

void ST7789_Init(void)
{
    uint8_t data;

    ST7789_Unselect();
    ST7789_Reset();

    ST7789_WriteCommand(ST7789_SWRESET);
    HAL_Delay(150);

    ST7789_WriteCommand(ST7789_SLPOUT);
    HAL_Delay(120);

    /* RGB565: 16 bits por pixel */
    ST7789_WriteCommand(ST7789_COLMOD);
    data = 0x55;
    ST7789_WriteData(&data, 1);
    HAL_Delay(10);

    /* Landscape 320x240 */
    ST7789_WriteCommand(ST7789_MADCTL);
    data = 0x60;
    ST7789_WriteData(&data, 1);

    ST7789_WriteCommand(ST7789_INVON);
    ST7789_WriteCommand(ST7789_NORON);
    HAL_Delay(10);

    ST7789_WriteCommand(ST7789_DISPON);
    HAL_Delay(120);
}

void ST7789_DrawPixel(uint16_t x,
                      uint16_t y,
                      uint16_t color)
{
    uint8_t data[2];

    if ((x >= ST7789_WIDTH) || (y >= ST7789_HEIGHT))
    {
        return;
    }

    ST7789_SetAddressWindow(x, y, x, y);

    data[0] = (uint8_t)(color >> 8);
    data[1] = (uint8_t)(color & 0xFF);
    ST7789_WriteData(data, 2);
}

void ST7789_FillRect(uint16_t x,
                     uint16_t y,
                     uint16_t width,
                     uint16_t height,
                     uint16_t color)
{
    uint8_t buffer[256];
    uint32_t pixels;
    uint8_t high;
    uint8_t low;

    if ((width == 0U) || (height == 0U))
    {
        return;
    }

    if ((x >= ST7789_WIDTH) || (y >= ST7789_HEIGHT))
    {
        return;
    }

    if (((uint32_t)x + width) > ST7789_WIDTH)
    {
        width = ST7789_WIDTH - x;
    }

    if (((uint32_t)y + height) > ST7789_HEIGHT)
    {
        height = ST7789_HEIGHT - y;
    }

    high = (uint8_t)(color >> 8);
    low  = (uint8_t)(color & 0xFF);

    for (uint16_t i = 0; i < sizeof(buffer); i += 2U)
    {
        buffer[i]     = high;
        buffer[i + 1] = low;
    }

    ST7789_SetAddressWindow(
        x,
        y,
        (uint16_t)(x + width - 1U),
        (uint16_t)(y + height - 1U)
    );

    HAL_GPIO_WritePin(TFT_DC_GPIO_Port, TFT_DC_Pin, GPIO_PIN_SET);
    ST7789_Select();

    pixels = (uint32_t)width * (uint32_t)height;

    while (pixels > 0U)
    {
        uint32_t count = pixels;

        if (count > (sizeof(buffer) / 2U))
        {
            count = sizeof(buffer) / 2U;
        }

        if (!ST7789_Transmit(buffer, (uint16_t)(count * 2U))) {
            break;
        }

        pixels -= count;
    }

    ST7789_Unselect();
}

void ST7789_FillScreen(uint16_t color)
{
    ST7789_FillRect(0, 0, ST7789_WIDTH, ST7789_HEIGHT, color);
}

/*
 * Fonte 5x7 compacta.
 * Cada byte representa uma coluna; bits 0..6 representam as linhas.
 * Indices:
 *  0      = espaco
 *  1..10  = 0..9
 * 11..36  = A..Z
 * 37      = ':'
 * 38      = '-'
 * 39      = '.'
 * 40      = '/'
 * 41      = '%'
 */
static const uint8_t font5x7[][5] =
{
    {0x00,0x00,0x00,0x00,0x00}, /* space */

    {0x3E,0x51,0x49,0x45,0x3E}, /* 0 */
    {0x00,0x42,0x7F,0x40,0x00}, /* 1 */
    {0x42,0x61,0x51,0x49,0x46}, /* 2 */
    {0x21,0x41,0x45,0x4B,0x31}, /* 3 */
    {0x18,0x14,0x12,0x7F,0x10}, /* 4 */
    {0x27,0x45,0x45,0x45,0x39}, /* 5 */
    {0x3C,0x4A,0x49,0x49,0x30}, /* 6 */
    {0x01,0x71,0x09,0x05,0x03}, /* 7 */
    {0x36,0x49,0x49,0x49,0x36}, /* 8 */
    {0x06,0x49,0x49,0x29,0x1E}, /* 9 */

    {0x7E,0x11,0x11,0x11,0x7E}, /* A */
    {0x7F,0x49,0x49,0x49,0x36}, /* B */
    {0x3E,0x41,0x41,0x41,0x22}, /* C */
    {0x7F,0x41,0x41,0x22,0x1C}, /* D */
    {0x7F,0x49,0x49,0x49,0x41}, /* E */
    {0x7F,0x09,0x09,0x09,0x01}, /* F */
    {0x3E,0x41,0x49,0x49,0x7A}, /* G */
    {0x7F,0x08,0x08,0x08,0x7F}, /* H */
    {0x00,0x41,0x7F,0x41,0x00}, /* I */
    {0x20,0x40,0x41,0x3F,0x01}, /* J */
    {0x7F,0x08,0x14,0x22,0x41}, /* K */
    {0x7F,0x40,0x40,0x40,0x40}, /* L */
    {0x7F,0x02,0x0C,0x02,0x7F}, /* M */
    {0x7F,0x04,0x08,0x10,0x7F}, /* N */
    {0x3E,0x41,0x41,0x41,0x3E}, /* O */
    {0x7F,0x09,0x09,0x09,0x06}, /* P */
    {0x3E,0x41,0x51,0x21,0x5E}, /* Q */
    {0x7F,0x09,0x19,0x29,0x46}, /* R */
    {0x46,0x49,0x49,0x49,0x31}, /* S */
    {0x01,0x01,0x7F,0x01,0x01}, /* T */
    {0x3F,0x40,0x40,0x40,0x3F}, /* U */
    {0x1F,0x20,0x40,0x20,0x1F}, /* V */
    {0x7F,0x20,0x18,0x20,0x7F}, /* W */
    {0x63,0x14,0x08,0x14,0x63}, /* X */
    {0x03,0x04,0x78,0x04,0x03}, /* Y */
    {0x61,0x51,0x49,0x45,0x43}, /* Z */

    {0x00,0x36,0x36,0x00,0x00}, /* : */
    {0x08,0x08,0x08,0x08,0x08}, /* - */
    {0x00,0x60,0x60,0x00,0x00}, /* . */
    {0x20,0x10,0x08,0x04,0x02}, /* / */
    {0x63,0x13,0x08,0x64,0x63}  /* % */
};

static const uint8_t *ST7789_GetGlyph(char c)
{
    if (c == ' ')
    {
        return font5x7[0];
    }

    if ((c >= '0') && (c <= '9'))
    {
        return font5x7[1 + (c - '0')];
    }

    if ((c >= 'a') && (c <= 'z'))
    {
        c = (char)(c - ('a' - 'A'));
    }

    if ((c >= 'A') && (c <= 'Z'))
    {
        return font5x7[11 + (c - 'A')];
    }

    switch (c)
    {
        case ':': return font5x7[37];
        case '-': return font5x7[38];
        case '.': return font5x7[39];
        case '/': return font5x7[40];
        case '%': return font5x7[41];
        default:  return font5x7[0];
    }
}

#define ST7789_MAX_FONT_SCALE 5U
#define ST7789_CHAR_MAX_WIDTH  (6U * ST7789_MAX_FONT_SCALE)
#define ST7789_CHAR_MAX_HEIGHT (8U * ST7789_MAX_FONT_SCALE)
#define ST7789_CHAR_BUFFER_SIZE (ST7789_CHAR_MAX_WIDTH * ST7789_CHAR_MAX_HEIGHT * 2U)

/* Buffer estatico para desenhar um caractere inteiro em uma unica transferencia
 * SPI. Isso reduz drasticamente a cintilacao em comparacao com dezenas de
 * FillRect por caractere. */
static uint8_t st7789CharBuffer[ST7789_CHAR_BUFFER_SIZE];

void ST7789_DrawChar(uint16_t x,
                     uint16_t y,
                     char c,
                     uint16_t color,
                     uint16_t bgcolor,
                     uint8_t scale)
{
    const uint8_t *glyph;
    uint16_t charWidth;
    uint16_t charHeight;
    uint32_t index = 0U;
    uint8_t fgHi;
    uint8_t fgLo;
    uint8_t bgHi;
    uint8_t bgLo;

    if (scale == 0U)
    {
        scale = 1U;
    }

    if (scale > ST7789_MAX_FONT_SCALE)
    {
        scale = ST7789_MAX_FONT_SCALE;
    }

    charWidth = (uint16_t)(6U * scale);
    charHeight = (uint16_t)(8U * scale);

    if (((uint32_t)x + charWidth) > ST7789_WIDTH ||
        ((uint32_t)y + charHeight) > ST7789_HEIGHT)
    {
        return;
    }

    glyph = ST7789_GetGlyph(c);

    fgHi = (uint8_t)(color >> 8);
    fgLo = (uint8_t)(color & 0xFFU);
    bgHi = (uint8_t)(bgcolor >> 8);
    bgLo = (uint8_t)(bgcolor & 0xFFU);

    for (uint16_t py = 0U; py < charHeight; py++)
    {
        uint8_t row = (uint8_t)(py / scale);

        for (uint16_t px = 0U; px < charWidth; px++)
        {
            uint8_t col = (uint8_t)(px / scale);
            bool foreground = false;

            if ((col < 5U) && (row < 7U))
            {
                foreground = (glyph[col] & (1U << row)) != 0U;
            }

            if (foreground)
            {
                st7789CharBuffer[index++] = fgHi;
                st7789CharBuffer[index++] = fgLo;
            }
            else
            {
                st7789CharBuffer[index++] = bgHi;
                st7789CharBuffer[index++] = bgLo;
            }
        }
    }

    ST7789_SetAddressWindow(
        x,
        y,
        (uint16_t)(x + charWidth - 1U),
        (uint16_t)(y + charHeight - 1U)
    );

    HAL_GPIO_WritePin(TFT_DC_GPIO_Port, TFT_DC_Pin, GPIO_PIN_SET);
    ST7789_Select();
    (void)ST7789_Transmit(st7789CharBuffer,
                         (uint16_t)(charWidth * charHeight * 2U));
    ST7789_Unselect();
}

void ST7789_WriteString(uint16_t x,
                        uint16_t y,
                        const char *str,
                        uint16_t color,
                        uint16_t bgcolor,
                        uint8_t scale)
{
    uint16_t cursorX = x;
    uint16_t cursorY = y;

    if ((str == NULL) || (scale == 0U))
    {
        return;
    }

    if (scale > ST7789_MAX_FONT_SCALE)
    {
        scale = ST7789_MAX_FONT_SCALE;
    }

    while (*str != '\0')
    {
        if (*str == '\n')
        {
            cursorX = x;
            cursorY = (uint16_t)(cursorY + (8U * scale));
            str++;
            continue;
        }

        if ((cursorX + (6U * scale)) > ST7789_WIDTH)
        {
            cursorX = x;
            cursorY = (uint16_t)(cursorY + (8U * scale));
        }

        if ((cursorY + (8U * scale)) > ST7789_HEIGHT)
        {
            break;
        }

        ST7789_DrawChar(
            cursorX,
            cursorY,
            *str,
            color,
            bgcolor,
            scale
        );

        cursorX = (uint16_t)(cursorX + (6U * scale));
        str++;
    }
}
