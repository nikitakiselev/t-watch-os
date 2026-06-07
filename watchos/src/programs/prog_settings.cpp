#include "prog_settings.h"
#include "prog_brightness.h"
#include "prog_wifi.h"
#include "prog_system.h"
#include "../../hw.h"
#include "../../statusbar.h"
#include "../../theme.h"
#include "../../listnav.h"

// ─────────────────────────── Под-приложения настроек ───────────────────────
// Эти программы НЕ входят в главный список приложений (apps.cpp) — они видны и
// запускаются только отсюда. Добавить пункт: завести Program и дописать сюда.
static const Program *const SETTINGS_APPS[] = {
    &brightnessProgram,
    &wifiProgram,
    &systemProgram,
};
static const int SETTINGS_COUNT = sizeof(SETTINGS_APPS) / sizeof(SETTINGS_APPS[0]);

// ─────────────────────────────── Раскладка ─────────────────────────────────
static const int TITLE_Y  = STATUSBAR_H + 16;
static const int LIST_TOP  = STATUSBAR_H + 36;
static const int ROW_H     = 34;
static const int VISIBLE   = (CONTENT_BOTTOM - LIST_TOP) / ROW_H;

static ListNav nav = { SETTINGS_COUNT, 0, 0, 0 };

static void drawRow(int idx, int y)
{
    bool sel = (idx == nav.sel);
    if (sel) tft->fillRoundRect(6, y, SCR_W - 12, ROW_H - 4, 6, COL_GREEN_DIM);

    uint16_t fg = sel ? COL_GREEN_HI : COL_GREEN;
    tft->setTextDatum(ML_DATUM);
    tft->setTextColor(fg, sel ? COL_GREEN_DIM : COL_BG);
    tft->drawString(SETTINGS_APPS[idx]->name, 18, y + (ROW_H - 4) / 2, 4);

    tft->setTextDatum(MR_DATUM);              // шеврон «открыть»
    tft->setTextColor(COL_AMBER, sel ? COL_GREEN_DIM : COL_BG);
    tft->drawString(">", SCR_W - 18, y + (ROW_H - 4) / 2, 4);
}

static void drawList()
{
    nav.vis = VISIBLE;
    tft->fillRect(0, LIST_TOP, SCR_W, CONTENT_BOTTOM - LIST_TOP, COL_BG);
    for (int i = nav.top; i < nav.top + VISIBLE && i < SETTINGS_COUNT; i++)
        drawRow(i, LIST_TOP + (i - nav.top) * ROW_H);
}

static void setEnter()
{
    nav.count = SETTINGS_COUNT;
    if (nav.sel >= SETTINGS_COUNT) nav.sel = 0;

    themeBackdrop();
    statusbarDraw();
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_AMBER, COL_BG);
    tft->drawString("[ SETTINGS ]", SCR_W / 2, TITLE_Y, 2);

    drawList();
}

// Свайп ↑↓ выбирает пункт, тап активирует ВЫДЕЛЕННЫЙ (как в остальных списках ОС).
static void setEvent(InputEvent e, int16_t x, int16_t y)
{
    if (SETTINGS_COUNT == 0) return;
    switch (e) {
    case EVT_UP:
    case EVT_DOWN:
        if (listNavEvent(nav, e)) drawList();
        break;
    case EVT_TAP:
        if (y >= LIST_TOP && y < CONTENT_BOTTOM)
            kernelOpen(SETTINGS_APPS[nav.sel]);
        break;
    default: break;
    }
}

// ASCII-иконка — «ползунки».
static const char *const SET_ART[] = { "-o--", "--o-", "o---" };
static void setDrawIcon(TFT_eSPI &g, int cx, int cy, int r)
{
    drawAsciiArt(g, SET_ART, 3, cx, cy, COL_GREEN, r / 8);
}

const Program settingsProgram = {
    "Settings", setEnter, nullptr, setEvent, setDrawIcon, nullptr, 0
};
