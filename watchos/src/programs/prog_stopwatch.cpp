#include "prog_stopwatch.h"
#include "../../hw.h"
#include "../../statusbar.h"
#include "../../theme.h"

static const int TITLE_Y = STATUSBAR_H + 16;
static const int TIME_Y  = 108;
static const int STATE_Y = 158;

static bool          running = false;
static unsigned long startMs = 0;
static unsigned long accMs   = 0;

static unsigned long elapsed()
{
    return accMs + (running ? (millis() - startMs) : 0);
}

static void drawTime()
{
    unsigned long e  = elapsed();
    unsigned long cs = (e / 10) % 100;
    unsigned long s  = (e / 1000) % 60;
    unsigned long m  = e / 60000;

    char b[16];
    snprintf(b, sizeof(b), "%02lu:%02lu.%02lu", m, s, cs);
    tft->fillRect(0, TIME_Y - 26, SCR_W, 52, COL_BG);
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(running ? COL_GREEN : COL_GREEN_DIM, COL_BG);
    tft->drawString(b, SCR_W / 2, TIME_Y, 6);
}

static void drawState()
{
    tft->fillRect(0, STATE_Y - 10, SCR_W, 20, COL_BG);
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(running ? COL_AMBER : COL_GREEN_DIM, COL_BG);
    tft->drawString(running ? "> RUNNING" : "[ STOPPED ]", SCR_W / 2, STATE_Y, 2);
}

static void swEnter()
{
    themeBackdrop();
    statusbarDraw();
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_AMBER, COL_BG);
    tft->drawString("[ STOPWATCH ]", SCR_W / 2, TITLE_Y, 2);
    drawTime();
    drawState();
}

static void swTick()
{
    if (running) drawTime();
}

static void swToggle()
{
    if (running) { accMs = elapsed(); running = false; }
    else         { startMs = millis(); running = true; }
    drawState();
    drawTime();
}

static void swReset()
{
    running = false;
    accMs = 0;
    startMs = 0;
    drawState();
    drawTime();
}

// ASCII-иконка — секундомер.
static const char *const SW_ART[] = { " ^ ", "(o)", "'-'" };
static void swDrawIcon(TFT_eSPI &g, int cx, int cy, int r)
{
    drawAsciiArt(g, SW_ART, 3, cx, cy, COL_GREEN, r / 8);
}

static const NavButton swNav[] = {
    { "Back",  kernelBack },
    { "Run",   swToggle },
    { "Reset", swReset },
};

// Пока секундомер идёт — не усыплять CPU (экран всё равно гаснет на простое).
static bool swKeepAwake() { return running; }

const Program stopwatchProgram = {
    "Stopwatch", swEnter, swTick, nullptr, swDrawIcon, swNav, 3, swKeepAwake
};
