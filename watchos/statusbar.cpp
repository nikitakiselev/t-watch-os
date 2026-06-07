#include "statusbar.h"
#include "hw.h"
#include "theme.h"
#include "wifi.h"
#include "program.h"     // kernelCurrent

// Какая программа последней нарисовала статусбар — чтобы поминутный тик не лез
// рисовать поверх полноэкранных программ (игра и т.п.), которые статусбар не зовут.
static const Program *g_sbProg = nullptr;
static int            sbLastMin = -1;
static bool           g_sbShowTime = true;   // главный экран отключает (дублирует часы)

// Время MSK слева (HH:MM). Обновляется раз в минуту.
static void drawStatusTime()
{
    RTC_Date now = hwNow();
    int h, m, s;
    hwTimeInZone(now, TZ_MSK_OFFSET_MIN, h, m, s);
    char buf[6];
    snprintf(buf, sizeof(buf), "%02d:%02d", h, m);

    tft->fillRect(4, 2, 56, STATUSBAR_H - 4, COL_BG);
    tft->setTextDatum(ML_DATUM);
    tft->setTextColor(COL_GREEN, COL_BG);
    tft->drawString(buf, 6, STATUSBAR_H / 2, 2);
    sbLastMin = m;
}

// Батарея-шкала справа: [сегменты + точечный «пустой»] NN%.
static void drawBatteryGauge()
{
    int p = hwBattPercent();
    uint16_t col = (p <= 20) ? COL_AMBER : COL_GREEN;

    char pct[8];
    snprintf(pct, sizeof(pct), "%d%%", p);
    tft->setTextDatum(MR_DATUM);
    tft->setTextColor(col, COL_BG);
    int pctX = SCR_W - 6;
    tft->drawString(pct, pctX, STATUSBAR_H / 2, 2);
    int pctW = tft->textWidth(pct, 2);

    // Ширину держим кратной числу сегментов — иначе на 100% справа остаётся хвост.
    const int segs = 10, cellW = 5, gw = segs * cellW;     // 50px
    const int gh = 12, gy = (STATUSBAR_H - gh) / 2;
    const int gRight = pctX - pctW - 8;
    const int gx = gRight - gw;

    tft->setTextDatum(MR_DATUM);
    tft->setTextColor(col, COL_BG);
    tft->drawString("]", gRight + 7, STATUSBAR_H / 2, 2);
    tft->setTextDatum(ML_DATUM);
    tft->drawString("[", gx - 7, STATUSBAR_H / 2, 2);

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

void statusbarDraw(bool showTime)
{
    tft->fillRect(0, 0, SCR_W, STATUSBAR_H, COL_BG);
    g_sbShowTime = showTime;
    if (showTime) drawStatusTime();
    drawBatteryGauge();

    // Пунктирный разделитель снизу (в духе макета).
    for (int x = 0; x < SCR_W; x += 8)
        tft->drawFastHLine(x, STATUSBAR_H - 1, 4, COL_AMBER_DIM);

    g_sbProg = kernelCurrent();
}

void statusbarTick()
{
    if (!g_sbShowTime) return;                       // на этом экране время не показываем
    if (kernelCurrent() != g_sbProg) return;        // текущая программа не показывает статусбар
    // Раз в секунду проверяем смену минуты (RTC по I2C — не дёргаем каждый кадр).
    static uint32_t lastCheck = 0;
    uint32_t nowMs = millis();
    if (nowMs - lastCheck < 1000) return;
    lastCheck = nowMs;

    RTC_Date now = hwNow();
    int h, m, s;
    hwTimeInZone(now, TZ_MSK_OFFSET_MIN, h, m, s);
    if (m != sbLastMin) drawStatusTime();
}
