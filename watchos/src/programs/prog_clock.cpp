#include "prog_clock.h"
#include "../../hw.h"
#include "../../statusbar.h"
#include "../../theme.h"

static const int LABEL_Y = STATUSBAR_H + 22;   // "MSK"
static const int HHMM_Y  = 104;                // HH:MM крупно (font 7)
static const int SEC_Y   = 160;                // секунды (font 6)

static int lastSec = -1;
static int lastMin = -1;

static void clockEnter()
{
    themeBackdrop();
    statusbarDraw();

    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_AMBER, COL_BG);
    tft->drawString("[ MSK ]", SCR_W / 2, LABEL_Y, 2);

    lastSec = -1;
    lastMin = -1;
}

static void clockTick()
{
    RTC_Date now = hwNow();
    int h, m, s;
    hwTimeInZone(now, TZ_MSK_OFFSET_MIN, h, m, s);

    if (m != lastMin) {
        lastMin = m;
        char buf[8];
        snprintf(buf, sizeof(buf), "%02d:%02d", h, m);
        tft->fillRect(0, HHMM_Y - 28, SCR_W, 56, COL_BG);
        tft->setTextDatum(MC_DATUM);
        tft->setTextColor(COL_AMBER, COL_BG);
        tft->drawString(buf, SCR_W / 2, HHMM_Y, 7);
    }

    if (s != lastSec) {
        lastSec = s;
        char buf[4];
        snprintf(buf, sizeof(buf), "%02d", s);
        tft->fillRect(0, SEC_Y - 26, SCR_W, 52, COL_BG);
        tft->setTextDatum(MC_DATUM);
        tft->setTextColor(COL_GREEN, COL_BG);
        tft->drawString(buf, SCR_W / 2, SEC_Y, 6);
        statusbarDraw();
    }
}

// ASCII-иконка — циферблат.
static const char *const CLOCK_ART[] = { ".--.", "|<>|", "'--'" };
static void clockDrawIcon(TFT_eSPI &g, int cx, int cy, int r)
{
    drawAsciiArt(g, CLOCK_ART, 3, cx, cy, COL_GREEN, r / 8);
}

const Program clockProgram = { "Clock", clockEnter, clockTick, nullptr, clockDrawIcon, nullptr, 0 };
