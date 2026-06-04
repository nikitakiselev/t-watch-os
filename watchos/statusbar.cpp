#include "statusbar.h"
#include "hw.h"
#include "theme.h"
#include "wifi.h"

void statusbarDraw()
{
    tft->fillRect(0, 0, SCR_W, STATUSBAR_H, COL_BG);

    int p = hwBattPercent();

    // Батарея с секциями (нарисованная).
    const int bx = 6, by = 5, bw = 50, bh = 14, segs = 5;
    uint16_t col = (p <= 20) ? COL_AMBER : COL_GREEN;

    tft->drawRoundRect(bx, by, bw, bh, 2, col);          // корпус
    tft->fillRect(bx + bw + 1, by + 4, 3, bh - 8, col);  // колпачок

    int filled = (p * segs + 50) / 100;                  // сколько секций залить
    int cellW  = (bw - 4) / segs;
    for (int i = 0; i < filled && i < segs; i++) {
        int sx = bx + 2 + i * cellW;
        tft->fillRect(sx, by + 2, cellW - 1, bh - 4, col);
    }

    // Процент рядом.
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", p);
    tft->setTextDatum(ML_DATUM);
    tft->setTextColor(col, COL_BG);
    tft->drawString(buf, bx + bw + 10, by + bh / 2, 2);

    // Уровень сигнала Wi-Fi справа, если радио активно (как в списке сетей).
    // Нет связи → все столбики тусклые (rssi -100).
    if (wifiActive()) {
        drawSignalBars(*tft, SCR_W - 22, 19, wifiConnected() ? wifiRssi() : -100,
                       COL_GREEN, COL_GREEN_DIM);
    }

    tft->drawFastHLine(0, STATUSBAR_H - 1, SCR_W, COL_FRAME);
}
