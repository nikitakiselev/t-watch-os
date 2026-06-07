#include "prog_home.h"
#include "prog_menu.h"
#include "../../hw.h"
#include "../../statusbar.h"
#include "../../theme.h"
#include "../../power.h"

// ───────── Вёрстка по дизайн-макету (240×240, контент 24..240) ─────────
static const int WORDMARK_Y = STATUSBAR_H / 2;     // «T-WATCH» в статусбаре слева
static const int DATE_Y     = 44;                  // SUN · 2026.06.07
static const int ZONE_Y     = 72;                  // ◆ MSK ............ UTC+3
static const int BIG_Y      = 116;                 // крупные часы (font 7) + ореол
static const int SECBAR_TOP = 156;                 // циановая шкала секунд
static const int SECBAR_H   = 10;
static const int ALT_Y      = 192;                 // EST ..  UTC ..
static const int APPS_Y     = 224;                 // > APPS ▤ ........ [ tap ]

static const int MARGIN = 14;

static int lastMin = -1;
static int lastSec = -1;
static int lastDay = -1;

// День недели по дате (Сакамото), 0 = воскресенье.
static int weekday(int y, int m, int d)
{
    static const int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    if (m < 3) y -= 1;
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

// Очистить полосу и восстановить сетку фона (чтобы текст лёг поверх сетки).
static void homeBand(int y0, int h)
{
    tft->fillRect(0, y0, SCR_W, h, COL_BG);
    for (int x = 0; x <= SCR_W; x += 24) tft->drawFastVLine(x, y0, h, COL_GRID);
    for (int gy = ((y0 + 23) / 24) * 24; gy <= y0 + h; gy += 24)
        tft->drawFastHLine(0, gy, SCR_W, COL_GRID);
}

// Wordmark «T-WATCH» в левой части статусбара (статусбар его не рисует сам).
static void drawWordmark()
{
    tft->setTextDatum(ML_DATUM);
    tft->setTextColor(COL_AMBER, COL_BG);
    tft->drawString("T-WATCH", 6, WORDMARK_Y, 2);
}

static void homeStatusbar()
{
    statusbarDraw();
    drawWordmark();
}

// SUN · 2026.06.07  (день недели — маджента, дата — зелёная).
static void drawDate(const RTC_Date &d)
{
    static const char *const WD[] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };
    homeBand(DATE_Y - 14, 28);

    tft->setTextDatum(ML_DATUM);
    tft->setTextColor(COL_MAGENTA);
    tft->drawString(WD[weekday(d.year, d.month, d.day)], MARGIN, DATE_Y, 4);
    int wdW = tft->textWidth(WD[0], 4);

    char buf[20];
    snprintf(buf, sizeof(buf), "%04d.%02d.%02d", d.year, d.month, d.day);
    tft->fillCircle(MARGIN + wdW + 11, DATE_Y, 2, COL_GREEN_DIM);   // разделитель-точка
    tft->setTextColor(COL_GREEN);
    tft->drawString(buf, MARGIN + wdW + 22, DATE_Y, 4);
}

// ◆ MSK слева (циан-ромб) … UTC+3 справа (тускло).
static void drawZoneRow()
{
    homeBand(ZONE_Y - 10, 20);
    int cy = ZONE_Y;
    int dx = MARGIN + 5;
    tft->fillTriangle(dx, cy - 6, dx - 6, cy, dx, cy + 6, COL_CYAN);  // левая половина ромба
    tft->fillTriangle(dx, cy - 6, dx + 6, cy, dx, cy + 6, COL_CYAN);  // правая
    tft->setTextDatum(ML_DATUM);
    tft->setTextColor(COL_GREEN);
    tft->drawString("MSK", dx + 16, cy, 2);

    tft->setTextDatum(MR_DATUM);
    tft->setTextColor(COL_GREEN_DIM);
    tft->drawString("UTC+3", SCR_W - MARGIN, cy, 2);
}

// Крупные часы с фейк-свечением: тусклый ореол со сдвигом ±1px, затем яркие цифры.
static void drawBigClock(const char *hhmm)
{
    homeBand(BIG_Y - 28, 56);
    tft->setTextDatum(MC_DATUM);
    int cx = SCR_W / 2;
    tft->setTextColor(COL_GREEN_GLOW);                      // ореол (прозрачный фон → сетка видна)
    tft->drawString(hhmm, cx - 1, BIG_Y,     7);
    tft->drawString(hhmm, cx + 1, BIG_Y,     7);
    tft->drawString(hhmm, cx,     BIG_Y - 1, 7);
    tft->drawString(hhmm, cx,     BIG_Y + 1, 7);
    tft->setTextColor(COL_GREEN);                           // яркие цифры поверх
    tft->drawString(hhmm, cx, BIG_Y, 7);
}

// Циановая шкала секунд + «NN SEC» справа.
static void drawSecBar(int sec)
{
    const int labelW = 52;
    const int bx = MARGIN, by = SECBAR_TOP;
    const int bw = SCR_W - MARGIN - labelW - bx, bh = SECBAR_H;

    tft->fillRect(bx, by, bw, bh, COL_BG);
    tft->drawRoundRect(bx, by, bw, bh, 3, COL_CYAN);
    int fill = (bw - 4) * sec / 59;
    if (fill > 0) tft->fillRoundRect(bx + 2, by + 2, fill, bh - 4, 2, COL_CYAN);

    // «NN SEC»: число циан (font 2) + SEC тускло (font 1).
    tft->fillRect(SCR_W - labelW, by - 3, labelW, bh + 6, COL_BG);
    tft->setTextDatum(MR_DATUM);
    tft->setTextColor(COL_GREEN_DIM, COL_BG);
    tft->drawString("SEC", SCR_W - MARGIN, by + bh / 2, 1);
    int secW = tft->textWidth("SEC", 1);
    char sb[4]; snprintf(sb, sizeof(sb), "%d", sec);
    tft->setTextColor(COL_CYAN, COL_BG);
    tft->drawString(sb, SCR_W - MARGIN - secW - 5, by + bh / 2, 2);
}

// EST 14:27   UTC 19:27  (метки цветные, время зелёное).
static void drawAltClocks(const RTC_Date &base)
{
    homeBand(ALT_Y - 14, 28);
    int he, me, se, hu, mu, su;
    hwTimeInZone(base, TZ_EST_OFFSET_MIN, he, me, se);
    hwTimeInZone(base, TZ_UTC_OFFSET_MIN, hu, mu, su);
    char est[8], utc[8];
    snprintf(est, sizeof(est), "%02d:%02d", he, me);
    snprintf(utc, sizeof(utc), "%02d:%02d", hu, mu);

    tft->setTextDatum(ML_DATUM);
    tft->setTextColor(COL_MAGENTA);
    tft->drawString("EST", MARGIN, ALT_Y, 2);
    tft->setTextColor(COL_GREEN);
    tft->drawString(est, MARGIN + 34, ALT_Y, 4);

    tft->setTextColor(COL_CYAN);
    tft->drawString("UTC", SCR_W / 2 + 8, ALT_Y, 2);
    tft->setTextColor(COL_GREEN);
    tft->drawString(utc, SCR_W / 2 + 42, ALT_Y, 4);
}

// > APPS ▤ ............ [ tap ]
static void drawAppsBar()
{
    tft->fillRect(0, APPS_Y - 12, SCR_W, 24, COL_BG);
    tft->setTextDatum(ML_DATUM);
    tft->setTextColor(COL_GREEN);
    tft->drawString("> APPS", MARGIN, APPS_Y, 2);
    int w = tft->textWidth("> APPS", 2);
    // мини-иконка списка (3 полоски).
    int ix = MARGIN + w + 8, iy = APPS_Y - 5;
    for (int i = 0; i < 3; i++) tft->fillRect(ix, iy + i * 4, 12, 2, COL_GREEN);

    tft->setTextDatum(MR_DATUM);
    tft->setTextColor(COL_GREEN_DIM);
    tft->drawString("[ tap ]", SCR_W - MARGIN, APPS_Y, 2);
}

static void homeEnter()
{
    themeBackdrop();
    drawGrid();
    homeStatusbar();

    RTC_Date now = hwNow();
    drawDate(now);
    drawZoneRow();
    drawAppsBar();

    lastMin = -1;
    lastSec = -1;
    lastDay = now.day;
}

static void homeTick()
{
    RTC_Date now = hwNow();
    int h, m, s;
    hwTimeInZone(now, TZ_MSK_OFFSET_MIN, h, m, s);

    if (s != lastSec) {                     // секунда сменилась — обновить ТОЛЬКО шкалу
        lastSec = s;
        drawSecBar(s);
    }

    if (m == lastMin) return;               // минута та же — крупные часы не трогаем
    lastMin = m;

    if (now.day != lastDay) { lastDay = now.day; drawDate(now); }

    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", h, m);
    drawBigClock(buf);
    drawAltClocks(now);
    homeStatusbar();
}

// Тап по экрану — открыть список приложений ([ tap ] в макете).
static void homeEvent(InputEvent e, int16_t x, int16_t y)
{
    if (e == EVT_TAP) kernelOpen(&menuProgram);
}

// Короткий клик боковой кнопкой на корне — уйти в сон.
static void homeButton() { powerSleepNow(); }

// navCount = -1 → программа занимает весь экран (без нижней панели ядра);
// «APPS»-строку и тап-обработку home делает сам.
const Program homeProgram = {
    "Home", homeEnter, homeTick, homeEvent, nullptr, nullptr, -1, nullptr, nullptr, nullptr, homeButton
};
