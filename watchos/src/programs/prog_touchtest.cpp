#include "prog_touchtest.h"
#include "../../hw.h"
#include "../../statusbar.h"
#include "../../theme.h"

static const int TITLE_Y    = STATUSBAR_H + 12;
static const int COORD_Y    = STATUSBAR_H + 34;
static const int CANVAS_TOP = STATUSBAR_H + 52;

static void clearCanvas()
{
    tft->fillRect(0, CANVAS_TOP, SCR_W, CONTENT_BOTTOM - CANVAS_TOP, COL_BG);
}

static void ttEnter()
{
    themeBackdrop();
    statusbarDraw();
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_AMBER, COL_BG);
    tft->drawString("[ TOUCH TEST ]", SCR_W / 2, TITLE_Y, 2);
    clearCanvas();
}

static void ttTick()
{
    int16_t x, y;
    if (!watch->getTouch(x, y)) return;

    if (y >= CANVAS_TOP && y < CONTENT_BOTTOM) {
        tft->fillCircle(x, y, 3, COL_GREEN);
    }

    char b[24];
    snprintf(b, sizeof(b), "x:%3d  y:%3d", x, y);
    tft->fillRect(0, COORD_Y - 8, SCR_W, 16, COL_BG);
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_GREEN, COL_BG);
    tft->drawString(b, SCR_W / 2, COORD_Y, 2);
}

// ASCII-иконка — прицел.
static const char *const TT_ART[] = { " | ", "-+-", " | " };
static void ttDrawIcon(TFT_eSPI &g, int cx, int cy, int r)
{
    drawAsciiArt(g, TT_ART, 3, cx, cy, COL_GREEN, r / 8);
}

static const NavButton ttNav[] = {
    { "Back",  kernelBack },
    { "Clear", clearCanvas },
};

const Program touchtestProgram = {
    "Touch", ttEnter, ttTick, nullptr, ttDrawIcon, ttNav, 2
};
