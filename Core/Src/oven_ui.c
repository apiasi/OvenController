#include "oven_ui.h"
#include "oven_profile.h"
#include "st7789.h"
#include <stdio.h>
#include <string.h>

/* RGB565: painel escuro, dados claros, ciano para selecao e ambar para calor. */
#define UI_BG       0x0863U
#define UI_PANEL    0x10C5U
#define UI_SELECTED 0x194AU
#define UI_LINE     0x2969U
#define UI_FG       0xEF7DU
#define UI_MUTED    0x8C51U
#define UI_ACCENT   0x3E9CU
#define UI_WARN     0xFD49U
#define UI_OK       0x6E51U
#define UI_ERROR    0xFAAAU

#if OVEN_PROFILE_POINT_COUNT != 4U
#error "O editor e o grafico desta interface exigem quatro pontos"
#endif

static bool uiInvalid = true;
static uint32_t invalidationGeneration;
static bool hasLast;
static OvenStatus_t last;

static const char *StateName(OvenState_t state) {
    switch (state) {
    case OVEN_STATE_IDLE: return "PRONTO";
    case OVEN_STATE_HEATING: return "AQUECENDO";
    case OVEN_STATE_RUNNING: return "EXECUTANDO";
    case OVEN_STATE_COOLING: return "RESFRIANDO";
    case OVEN_STATE_FINISHED: return "CONCLUIDO";
    default: return "FALHA";
    }
}

static void Text(uint16_t x, uint16_t y, const char *text, uint16_t fg,
                 uint16_t bg, uint8_t scale) {
    ST7789_WriteString(x, y, text, fg, bg, scale);
}

static void TimeText(char *out, size_t size, uint32_t seconds) {
    (void)snprintf(out, size, "%02lu:%02lu", (unsigned long)(seconds / 60U),
                   (unsigned long)(seconds % 60U));
}

static void Header(const OvenStatus_t *s) {
    ST7789_FillRect(0, 0, 320, 26, UI_PANEL);
    ST7789_FillRect(0, 0, 3, 26, UI_ACCENT);
    Text(12, 6, "REFLOW CTRL", UI_FG, UI_PANEL, 2);
    uint16_t color = s->state == OVEN_STATE_ERROR ? UI_ERROR :
        (s->state == OVEN_STATE_IDLE || s->state == OVEN_STATE_FINISHED ? UI_OK : UI_WARN);
    Text(226, 9, StateName(s->state), color, UI_PANEL, 1);
}

static void Footer(const OvenStatus_t *s) {
    ST7789_FillRect(0, 217, 320, 23, UI_PANEL);
    const char *line1;
    const char *line2;
    if (s->state == OVEN_STATE_ERROR) {
        line1 = "CORRIJA A CAUSA ANTES DE CONTINUAR";
        line2 = "K0: RECONHECER FALHA";
    } else if (s->state == OVEN_STATE_FINISHED) {
        line1 = "CICLO ENCERRADO / AQUECIMENTO OFF";
        line2 = "K0: VOLTAR A CONFIGURACAO";
    } else if (s->state != OVEN_STATE_IDLE) {
        if (s->state == OVEN_STATE_COOLING) {
            line1 = "RESFRIAMENTO PASSIVO / SSR OFF";
        } else if (s->profileId == OVEN_PROFILE_MANUAL) {
            line1 = s->state == OVEN_STATE_HEATING ? "TEMPO INICIA AO ATINGIR O ALVO" :
                "CONTROLE DE TEMPERATURA CONSTANTE";
        } else {
            line1 = "CURVA CIANO / MEDICAO AMBAR";
        }
        line2 = "K0: INTERROMPER CICLO";
    } else if (s->screen == OVEN_SCREEN_EDITOR) {
        line1 = "PUSH: PROXIMO / SEGURE: VOLTAR";
        line2 = "GIRE: AJUSTAR / K0: INICIAR";
    } else {
        line1 = s->profileId == OVEN_PROFILE_MANUAL ? "PUSH: PERFIL / TEMPERATURA / TEMPO" : "GIRE: PERFIL / PUSH: EDITAR 4 PONTOS";
        line2 = "K0: INICIAR / AJUSTES EM RAM";
    }
    Text(10, 220, line1, UI_FG, UI_PANEL, 1);
    Text(10, 231, line2, UI_MUTED, UI_PANEL, 1);
}

static void ProfileBar(const OvenStatus_t *s) {
    uint16_t bg = s->state == OVEN_STATE_IDLE && s->screen == OVEN_SCREEN_HOME &&
        s->selectedField == OVEN_FIELD_PROFILE ? UI_SELECTED : UI_PANEL;
    ST7789_FillRect(10, 32, 300, 23, bg);
    ST7789_FillRect(10, 32, 3, 23, UI_ACCENT);
    Text(20, 36, s->profileId == OVEN_PROFILE_MANUAL ? "MANUAL / SECAGEM" :
         OvenProfile_Name(s->profileId), UI_FG, bg, 2);
}

static void CurrentTemperature(const OvenStatus_t *s, bool full) {
    char text[18];
    if (full) {
        ST7789_FillRect(10, 61, 166, 53, UI_PANEL);
        Text(18, 65, "TEMPERATURA / C", UI_MUTED, UI_PANEL, 1);
    }
    if (s->sensorValid) {
        unsigned value = (unsigned)(s->currentTempC * 10.0f + 0.5f);
        (void)snprintf(text, sizeof(text), "%3u.%u", value / 10U, value % 10U);
    } else { (void)snprintf(text, sizeof(text), " --.-"); }
    Text(18, 79, text, s->sensorValid ? UI_FG : UI_ERROR, UI_PANEL, 4);
}

static unsigned DisplayTarget(const OvenStatus_t *s) {
    if (s->state == OVEN_STATE_COOLING) { return (unsigned)OVEN_COOL_TARGET_C; }
    if (s->profileId == OVEN_PROFILE_MANUAL) { return s->setpointC; }
    if (s->state == OVEN_STATE_IDLE) { return s->points[3].targetC; }
    return (unsigned)(s->targetTempC + 0.5f);
}

static void Target(const OvenStatus_t *s, bool full) {
    char text[16];
    if (full) {
        ST7789_FillRect(184, 61, 126, 53, UI_PANEL);
        Text(194, 65, s->state == OVEN_STATE_COOLING ? "META RESFR. / C" :
         (s->state == OVEN_STATE_IDLE && s->profileId != OVEN_PROFILE_MANUAL ? "PICO / C" : "ALVO / C"),
         UI_MUTED, UI_PANEL, 1);
    }
    (void)snprintf(text, sizeof(text), "%3u", DisplayTarget(s));
    Text(194, 81, text, UI_ACCENT, UI_PANEL, 3);
}

static void ManualCard(const OvenStatus_t *s, bool time, bool rebuild) {
    uint16_t x = time ? 165U : 10U;
    bool selected = s->state == OVEN_STATE_IDLE && s->selectedField ==
        (time ? OVEN_FIELD_TIME : OVEN_FIELD_TEMPERATURE);
    uint16_t bg = selected ? UI_SELECTED : UI_PANEL;
    char text[18];
    if (rebuild) {
        ST7789_FillRect(x, 128, 145, 65, bg);
        ST7789_FillRect(x, 128, 3, 65, selected ? UI_ACCENT : UI_LINE);
        Text(x + 12U, 136, time ? (s->state == OVEN_STATE_RUNNING ? "RESTANTE / MIN:S" : "DURACAO / MIN") :
             "CONSTANTE / C", UI_MUTED, bg, 1);
    }
    if (time && s->state == OVEN_STATE_RUNNING) {
        TimeText(text, sizeof(text), s->remainingSeconds);
    } else {
        (void)snprintf(text, sizeof(text), "%3u", time ? s->setTimeMin : s->setpointC);
    }
    Text(x + 12U, 155, text, selected ? UI_ACCENT : UI_FG, bg, 3);
}

static void Line(int x0, int y0, int x1, int y1, uint16_t color) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        ST7789_DrawPixel((uint16_t)x0, (uint16_t)y0, color);
        if (x0 == x1 && y0 == y1) { break; }
        int twice = 2 * error;
        if (twice >= dy) { error += dy; x0 += sx; }
        if (twice <= dx) { error += dx; y0 += sy; }
    }
}

static int PlotX(uint32_t seconds, uint32_t horizon) {
    if (seconds > horizon) { seconds = horizon; }
    return 20 + (int)(seconds * 178U / horizon);
}
static int PlotY(unsigned temperature) {
    if (temperature > 250U) { temperature = 250U; }
    return 188 - (int)(temperature * 62U / 250U);
}

static void Plot(const OvenStatus_t *s) {
    uint32_t horizon = OvenProfile_Duration(s->points) + 120U;
    uint32_t cursor = 0U;
    unsigned previous = s->state == OVEN_STATE_IDLE ? 25U : (unsigned)s->profileStartTempC;
    char text[18];
    ST7789_FillRect(10, 120, 194, 81, UI_BG);
    for (int y = 126; y <= 188; y += 31) { ST7789_FillRect(20, (uint16_t)y, 179, 1, UI_LINE); }
    for (int x = 20; x <= 198; x += 44) { ST7789_FillRect((uint16_t)x, 126, 1, 63, UI_LINE); }
    for (unsigned i = 0U; i < OVEN_PROFILE_POINT_COUNT; i++) {
        uint32_t next = cursor + s->points[i].rampSeconds;
        Line(PlotX(cursor, horizon), PlotY(previous), PlotX(next, horizon), PlotY(s->points[i].targetC), UI_ACCENT);
        cursor = next;
        next += s->points[i].holdSeconds;
        Line(PlotX(cursor, horizon), PlotY(s->points[i].targetC), PlotX(next, horizon), PlotY(s->points[i].targetC), UI_ACCENT);
        cursor = next;
        previous = s->points[i].targetC;
    }
    /* Resfriamento ilustrativo: sem atuador ou prazo garantido de queda. */
    for (unsigned i = 0; i < 120U; i += 10U) {
        int value = (int)previous + ((int)OVEN_COOL_TARGET_C - (int)previous) * (int)i / 120;
        ST7789_DrawPixel((uint16_t)PlotX(cursor + i, horizon), (uint16_t)PlotY((unsigned)value), UI_MUTED);
    }
    for (unsigned i = 1U; i < s->traceCount; i++) {
        Line(PlotX(s->traceTime[i - 1U], horizon), PlotY(s->traceTemp[i - 1U]),
             PlotX(s->traceTime[i], horizon), PlotY(s->traceTemp[i]), UI_WARN);
    }
    if (s->state != OVEN_STATE_IDLE) {
        int x = PlotX(s->elapsedSeconds, horizon);
        ST7789_FillRect((uint16_t)x, 125, 1, 65, UI_MUTED);
    }
    Text(10, 117, "250", UI_MUTED, UI_BG, 1);
    Text(14, 192, "0", UI_MUTED, UI_BG, 1);
    TimeText(text, sizeof(text), horizon);
    Text(160, 192, text, UI_MUTED, UI_BG, 1);
}

static void PointList(const OvenStatus_t *s) {
    static const char *names[] = {"PRE", "SOAK", "SUBIDA", "PICO"};
    Text(214, 119, "ETAPAS / C", UI_MUTED, UI_BG, 1);
    for (unsigned i = 0U; i < OVEN_PROFILE_POINT_COUNT; i++) {
        char text[20];
        bool active = s->state == OVEN_STATE_RUNNING && s->activePoint == i;
        uint16_t bg = active ? UI_SELECTED : UI_BG;
        ST7789_FillRect(211, (uint16_t)(133U + i * 16U), 99, 14, bg);
        (void)snprintf(text, sizeof(text), "%u %-6s %3u", i + 1U, names[i], s->points[i].targetC);
        Text(215, (uint16_t)(136U + i * 16U), text, active ? UI_ACCENT : UI_FG, bg, 1);
    }
}

static void ProcessLine(const OvenStatus_t *s) {
    char text[52];
    char time[16];
    ST7789_FillRect(10, 203, 300, 10, UI_BG);
    if (s->state == OVEN_STATE_IDLE) {
        if (s->profileId == OVEN_PROFILE_MANUAL) {
            (void)snprintf(text, sizeof(text), "TEMPO INICIA QUANDO ATINGIR O ALVO");
        } else {
            TimeText(time, sizeof(time), OvenProfile_Duration(s->points));
            (void)snprintf(text, sizeof(text), "4 PONTOS / %s / RESFRIAMENTO", time);
        }
    } else if (s->state == OVEN_STATE_COOLING) {
        (void)snprintf(text, sizeof(text), "RESFRIAMENTO PASSIVO ATE %u C", (unsigned)OVEN_COOL_TARGET_C);
    } else if (s->profileId == OVEN_PROFILE_MANUAL) {
        (void)snprintf(text, sizeof(text), "AQUECIMENTO %s", s->heaterOn ? "ON" : "OFF");
    } else {
        TimeText(time, sizeof(time), s->remainingSeconds);
        (void)snprintf(text, sizeof(text), "P%u %-7s %s / SSR %s", s->activePoint + 1U,
                       s->holding ? "PATAMAR" : "RAMPA", time, s->heaterOn ? "ON" : "OFF");
    }
    Text(10, 204, text, s->heaterOn ? UI_WARN : UI_MUTED, UI_BG, 1);
}

static void Editor(const OvenStatus_t *s, bool full) {
    if (full) {
        Text(12, 65, "PONTO", UI_MUTED, UI_BG, 1);
        Text(65, 65, "ALVO / C", UI_MUTED, UI_BG, 1);
        Text(148, 65, "RAMPA / S", UI_MUTED, UI_BG, 1);
        Text(231, 65, "MANTER / S", UI_MUTED, UI_BG, 1);
        for (unsigned row = 0U; row < OVEN_PROFILE_POINT_COUNT; row++) {
            char label[5];
            (void)snprintf(label, sizeof(label), "P%u", row + 1U);
            Text(18, (uint16_t)(92U + row * 29U), label, UI_MUTED, UI_BG, 2);
        }
        Text(12, 204, "EDITAR PRESET CRIA PERFIL PERSONALIZADO", UI_MUTED, UI_BG, 1);
    }
    for (unsigned cell = 0U; cell < OVEN_PROFILE_POINT_COUNT * 3U; cell++) {
        unsigned row = cell / 3U;
        unsigned col = cell % 3U;
        unsigned value = col == 0U ? s->points[row].targetC :
            (col == 1U ? s->points[row].rampSeconds : s->points[row].holdSeconds);
        unsigned previous = col == 0U ? last.points[row].targetC :
            (col == 1U ? last.points[row].rampSeconds : last.points[row].holdSeconds);
        if (!full && value == previous && cell != s->editCell && cell != last.editCell) { continue; }
        if (!full && value == previous && s->editCell == last.editCell) { continue; }
        uint16_t x = (uint16_t)(62U + col * 83U);
        uint16_t y = (uint16_t)(86U + row * 29U);
        uint16_t bg = cell == s->editCell ? UI_SELECTED : UI_PANEL;
        ST7789_FillRect(x, y, 73, 25, bg);
        ST7789_FillRect(x, y, 3, 25, cell == s->editCell ? UI_ACCENT : UI_LINE);
        char text[12];
        (void)snprintf(text, sizeof(text), "%3u", value);
        Text(x + 12U, y + 5U, text, cell == s->editCell ? UI_ACCENT : UI_FG, bg, 2);
    }
}

static void Fault(const OvenStatus_t *s) {
    const char *title;
    const char *detail;
    switch (s->error) {
    case OVEN_ERROR_SENSOR: title = "FALHA NO SENSOR"; detail = "VERIFIQUE TERMOPAR E CONEXOES"; break;
    case OVEN_ERROR_OVERTEMP: title = "SOBRETEMPERATURA"; detail = "AGUARDE A TEMPERATURA BAIXAR"; break;
    case OVEN_ERROR_HEATING_TIMEOUT: title = "TEMPO EXCEDIDO"; detail = "ALVO NAO ATINGIDO NO PRAZO"; break;
    case OVEN_ERROR_WATCHDOG: title = "REINICIO POR FALHA"; detail = "CICLO INTERROMPIDO PELO WATCHDOG"; break;
    case OVEN_ERROR_START_HOT: title = "FORNO AINDA QUENTE"; detail = "REFLOW INICIA COM ATE 50 C"; break;
    case OVEN_ERROR_TRACKING: title = "DESVIO TERMICO"; detail = "FORNO NAO ACOMPANHOU O PERFIL"; break;
    case OVEN_ERROR_PROFILE: title = "PERFIL INVALIDO"; detail = "REVISE TEMPERATURAS E TEMPOS"; break;
    case OVEN_ERROR_COOLING_TIMEOUT: title = "RESFRIAMENTO LENTO"; detail = "VERIFIQUE O FORNO E O TERMOPAR"; break;
    default: title = "FALHA DE COMUNICACAO"; detail = "VERIFIQUE O DISPLAY / REINICIE"; break;
    }
    Text(14, 48, title, UI_ERROR, UI_BG, 2);
    Text(14, 76, detail, UI_MUTED, UI_BG, 1);
    ST7789_FillRect(14, 108, 292, 1, UI_LINE);
    Text(14, 125, "AQUECIMENTO BLOQUEADO", UI_WARN, UI_BG, 2);
    Text(14, 158, "SSR OFF", UI_FG, UI_BG, 3);
}

void OvenUI_Init(void) {
    uiInvalid = true;
    hasLast = false;
}

void OvenUI_Invalidate(void) {
    uiInvalid = true;
    invalidationGeneration++;
}

void OvenUI_Render(const OvenStatus_t *s) {
    if (s == NULL) { return; }
    uint32_t generation = invalidationGeneration;
    bool manual = s->profileId == OVEN_PROFILE_MANUAL;
    bool full = uiInvalid || !hasLast || s->state != last.state || s->screen != last.screen ||
        manual != (last.profileId == OVEN_PROFILE_MANUAL) ||
        (s->state == OVEN_STATE_ERROR && s->error != last.error);
    bool pointsChanged = !hasLast || memcmp(s->points, last.points, sizeof(s->points)) != 0;
    if (full) {
        ST7789_FillScreen(UI_BG);
        Header(s);
        Footer(s);
    }
    if (s->state == OVEN_STATE_ERROR) {
        if (full || s->error != last.error) { Fault(s); }
        if (full || s->currentTempC != last.currentTempC || s->sensorValid != last.sensorValid) {
            char temperature[24];
            if (s->sensorValid) {
                (void)snprintf(temperature, sizeof(temperature), "MEDIDO: %3u C  ", (unsigned)(s->currentTempC + 0.5f));
            } else { (void)snprintf(temperature, sizeof(temperature), "MEDIDO: --- C  "); }
            Text(14, 192, temperature, UI_MUTED, UI_BG, 1);
        }
    } else {
        if (full || s->profileId != last.profileId || s->selectedField != last.selectedField) { ProfileBar(s); }
        if (s->screen == OVEN_SCREEN_EDITOR && s->state == OVEN_STATE_IDLE) {
            Editor(s, full);
        } else {
            if (full || s->sensorValid != last.sensorValid || s->currentTempC != last.currentTempC) { CurrentTemperature(s, full); }
            if (full || DisplayTarget(s) != DisplayTarget(&last)) { Target(s, full); }
            if (manual) {
                if (full || s->setpointC != last.setpointC || s->selectedField != last.selectedField) {
                    ManualCard(s, false, full || s->selectedField != last.selectedField);
                }
                if (full || s->setTimeMin != last.setTimeMin || s->selectedField != last.selectedField ||
                    s->remainingSeconds != last.remainingSeconds) {
                    ManualCard(s, true, full || s->selectedField != last.selectedField);
                }
            } else {
                if (full || pointsChanged || s->elapsedSeconds != last.elapsedSeconds || s->traceCount != last.traceCount) { Plot(s); }
                if (full || pointsChanged || s->activePoint != last.activePoint) { PointList(s); }
            }
            if (full || pointsChanged || s->remainingSeconds != last.remainingSeconds ||
                s->heaterOn != last.heaterOn || s->holding != last.holding) { ProcessLine(s); }
        }
    }
    uiInvalid = generation != invalidationGeneration;
    last = *s;
    hasLast = true;
}
