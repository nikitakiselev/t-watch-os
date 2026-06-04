#include "prog_home.h"
#include "prog_menu.h"
#include "../../hw.h"
#include "../../statusbar.h"
#include "../../theme.h"

// Вертикальные позиции (вписаны в CONTENT_TOP..CONTENT_BOTTOM).
static const int MSK_LABEL_Y = STATUSBAR_H + 14;   // [ MSK ]
static const int DATE_Y      = STATUSBAR_H + 32;   // дата (между меткой и часами)
static const int MSK_TIME_Y  = 92;                 // крупные часы (font 7)
static const int DIV_Y       = 126;
static const int UTC_Y       = 152;
static const int EST_Y       = 180;

static int lastMin = -1;   // на главном показываем HH:MM → обновляем раз в минуту
static int lastDay = -1;

// День недели по дате (алгоритм Сакамото), 0 = воскресенье.
static int weekday(int y, int m, int d)
{
    static const int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    if (m < 3) y -= 1;
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

static void homeBand(int y0, int h);   // определена ниже

static void drawDate(const RTC_Date &d)
{
    static const char *const WD[] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };
    static const char *const MO[] = { "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                      "JUL", "AUG", "SEP", "OCT", "NOV", "DEC" };
    char buf[24];
    snprintf(buf, sizeof(buf), "%s  %02d %s %04d",
             WD[weekday(d.year, d.month, d.day)], d.day, MO[(d.month - 1) % 12], d.year);
    homeBand(DATE_Y - 9, 18);              // очистка + восстановление сетки
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_GREEN_DIM);      // прозрачный фон → сетка видна
    tft->drawString(buf, SCR_W / 2, DATE_Y, 2);
}

// Очистить горизонтальную полосу и восстановить фон (скан-линии + сетку) —
// чтобы цифры рисовались поверх сетки, а не затирали её чёрным.
static void homeBand(int y0, int h)
{
    tft->fillRect(0, y0, SCR_W, h, COL_BG);
    for (int x = 0; x <= SCR_W; x += 24) tft->drawFastVLine(x, y0, h, COL_GRID);
    for (int gy = ((y0 + 23) / 24) * 24; gy <= y0 + h; gy += 24)
        tft->drawFastHLine(0, gy, SCR_W, COL_GRID);
}

static void drawSmallClock(const char *zone, int y, int offsetMin, const RTC_Date &base)
{
    int h, m, s;
    hwTimeInZone(base, offsetMin, h, m, s);
    char buf[24];
    snprintf(buf, sizeof(buf), "%s  %02d:%02d", zone, h, m);

    homeBand(y - 13, 26);
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_GREEN);          // прозрачный фон → сетка видна
    tft->drawString(buf, SCR_W / 2, y, 4);
}

static void homeEnter()
{
    themeBackdrop();
    drawGrid();                            // сетка — фон главного экрана (само приложение её рисует)
    statusbarDraw();

    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_AMBER);          // прозрачный фон
    tft->drawString("[ MSK ]", SCR_W / 2, MSK_LABEL_Y, 2);

    tft->drawFastHLine(24, DIV_Y, SCR_W - 48, COL_FRAME);

    RTC_Date now = hwNow();
    drawDate(now);

    lastMin = -1;
    lastDay = now.day;
}

static void homeTick()
{
    RTC_Date now = hwNow();
    int h, m, s;
    hwTimeInZone(now, TZ_MSK_OFFSET_MIN, h, m, s);
    if (m == lastMin) return;        // минута не сменилась — не перерисовываем (нет моргания)
    lastMin = m;

    if (now.day != lastDay) {        // смена суток — обновить дату
        lastDay = now.day;
        drawDate(now);
    }

    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", h, m);

    homeBand(MSK_TIME_Y - 28, 56);
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_AMBER);          // прозрачный фон → сетка под цифрами
    tft->drawString(buf, SCR_W / 2, MSK_TIME_Y, 7);

    drawSmallClock("UTC", UTC_Y, TZ_UTC_OFFSET_MIN, now);
    drawSmallClock("EST", EST_Y, TZ_EST_OFFSET_MIN, now);

    statusbarDraw();
}

static void homeEvent(InputEvent e, int16_t x, int16_t y)
{
    if (e == EVT_CLICK) kernelOpen(&menuProgram);
}

static void openApps() { kernelOpen(&menuProgram); }
static const NavButton homeNav[] = { { "Apps", openApps } };

const Program homeProgram = {
    "Home", homeEnter, homeTick, homeEvent, nullptr, homeNav, 1
};
