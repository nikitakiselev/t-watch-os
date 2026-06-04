#include "theme.h"
#include "hw.h"

void drawGrid()
{
    for (int x = 0; x <= SCR_W; x += 24) tft->drawFastVLine(x, 0, SCR_H, COL_GRID);
    for (int y = 0; y <= SCR_H; y += 24) tft->drawFastHLine(0, y, SCR_W, COL_GRID);
}

// Точки-индикатор страниц (свайп влево/вправо): активная — яркая, прочие — тусклые.
void drawPageDots(int count, int current, int y)
{
    if (count < 2) return;
    const int spacing = 12, r = 2;
    int total = (count - 1) * spacing;
    int x0 = SCR_W / 2 - total / 2;
    tft->fillRect(x0 - r - 2, y - r - 2, total + 2 * r + 4, 2 * r + 4, COL_BG);
    for (int i = 0; i < count; i++)
        tft->fillCircle(x0 + i * spacing, y, r, i == current ? COL_GREEN_HI : COL_GREEN_DIM);
}

void drawHudFrame()
{
    const int m = 2, L = 16;
    // Углы-«скобки» в стиле HUD.
    // верх-лево
    tft->drawFastHLine(m, m, L, COL_FRAME);          tft->drawFastVLine(m, m, L, COL_FRAME);
    // верх-право
    tft->drawFastHLine(SCR_W - m - L, m, L, COL_FRAME); tft->drawFastVLine(SCR_W - m - 1, m, L, COL_FRAME);
    // низ-лево
    tft->drawFastHLine(m, SCR_H - m - 1, L, COL_FRAME); tft->drawFastVLine(m, SCR_H - m - L, L, COL_FRAME);
    // низ-право
    tft->drawFastHLine(SCR_W - m - L, SCR_H - m - 1, L, COL_FRAME);
    tft->drawFastVLine(SCR_W - m - 1, SCR_H - m - L, L, COL_FRAME);
}

void themeBackdrop()
{
    // Чистый фон для всех экранов: заливка + угловые HUD-скобки.
    // Сетка и скан-линии — только на главном экране (см. prog_home).
    tft->fillScreen(COL_BG);
    drawHudFrame();
}

void drawAsciiArt(TFT_eSPI &g, const char *const *lines, int n,
                  int cx, int cy, uint16_t color, uint8_t size)
{
    if (size < 1) size = 1;
    g.setTextSize(size);
    g.setTextColor(color);            // одноцветный → прозрачный фон
    g.setTextDatum(MC_DATUM);
    int lineH = 8 * size;
    int top   = cy - (n * lineH) / 2 + lineH / 2;
    for (int i = 0; i < n; i++) g.drawString(lines[i], cx, top + i * lineH, 1);
    g.setTextSize(1);
}

void glitchFlash()
{
#if FX_GLITCH
    for (int i = 0; i < 7; i++) {
        int y = random(0, SCR_H - 6);
        int h = random(2, 9);
        uint16_t c = (i & 1) ? COL_AMBER : COL_GREEN;
        tft->fillRect(0, y, SCR_W, h, c);
        delay(11);
    }
#endif
}

void drawWifiGlyph(TFT_eSPI &g, int cx, int cyBottom, int r, uint16_t color)
{
    g.fillCircle(cx, cyBottom, 1, color);                 // точка-источник
    // Дуги веером вверх (верхняя половина окружности = top-left|top-right = 0x3).
    for (int k = 1; k <= 3; k++) {
        g.drawCircleHelper(cx, cyBottom, (r * k) / 3, 0x3, color);
    }
}

void drawSignalBars(TFT_eSPI &g, int x, int yBottom, int rssi, uint16_t colOn, uint16_t colOff)
{
    int bars;
    if      (rssi >= -55) bars = 4;
    else if (rssi >= -65) bars = 3;
    else if (rssi >= -75) bars = 2;
    else if (rssi >= -85) bars = 1;
    else                  bars = 0;
    for (int b = 0; b < 4; b++) {
        int bh = 3 + b * 3;                  // 3, 6, 9, 12
        g.fillRect(x + b * 4, yBottom - bh, 3, bh, (b < bars) ? colOn : colOff);
    }
}

void typeOut(const char *s, int x, int y, uint16_t color, uint8_t font, int chDelayMs)
{
    tft->setTextDatum(TL_DATUM);
    tft->setTextColor(color, COL_BG);
    char buf[64];
    int n = strlen(s);
    if (n > 63) n = 63;
    for (int i = 0; i < n; i++) {
        buf[i] = s[i];
        buf[i + 1] = 0;
        tft->drawString(buf, x, y, font);
        delay(chDelayMs);
    }
}
