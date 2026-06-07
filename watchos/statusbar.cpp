#include "statusbar.h"
#include "hw.h"
#include "theme.h"
#include "wifi.h"

// Батарея-шкала справа: [сегменты + точечный «пустой»] NN%. В стиле макета.
static void drawBatteryGauge()
{
    int p = hwBattPercent();
    uint16_t col = (p <= 20) ? COL_AMBER : COL_GREEN;

    // Процент — у правого края.
    char pct[8];
    snprintf(pct, sizeof(pct), "%d%%", p);
    tft->setTextDatum(MR_DATUM);
    tft->setTextColor(col, COL_BG);
    int pctX = SCR_W - 6;
    tft->drawString(pct, pctX, STATUSBAR_H / 2, 2);
    int pctW = tft->textWidth(pct, 2);

    // Шкала слева от процента, в квадратных скобках.
    // Ширину держим кратной числу сегментов — иначе на 100% справа остаётся
    // незакрытый «хвост».
    const int segs = 10, cellW = 5, gw = segs * cellW;     // 50px
    const int gh = 12, gy = (STATUSBAR_H - gh) / 2;
    const int gRight = pctX - pctW - 8;
    const int gx = gRight - gw;

    // Скобки [ ].
    tft->setTextDatum(MR_DATUM);
    tft->setTextColor(col, COL_BG);
    tft->drawString("]", gRight + 7, STATUSBAR_H / 2, 2);
    tft->setTextDatum(ML_DATUM);
    tft->drawString("[", gx - 7, STATUSBAR_H / 2, 2);

    // Сегменты: залитые слева, точечные справа (пустой остаток).
    int on = (p * segs + 50) / 100;
    for (int i = 0; i < segs; i++) {
        int sx = gx + i * cellW;
        if (i < on) tft->fillRect(sx, gy + 1, cellW - 1, gh - 2, col);
        else        tft->drawPixel(sx + cellW / 2, gy + gh / 2, COL_AMBER_DIM);
    }

    // Уровень Wi-Fi (если радио активно) — слева от шкалы.
    if (wifiActive()) {
        drawSignalBars(*tft, gx - 26, STATUSBAR_H - 5, wifiConnected() ? wifiRssi() : -100,
                       COL_GREEN, COL_GREEN_DIM);
    }
}

void statusbarDraw()
{
    tft->fillRect(0, 0, SCR_W, STATUSBAR_H, COL_BG);
    drawBatteryGauge();

    // Пунктирный разделитель снизу (в духе макета).
    for (int x = 0; x < SCR_W; x += 8)
        tft->drawFastHLine(x, STATUSBAR_H - 1, 4, COL_AMBER_DIM);
}
