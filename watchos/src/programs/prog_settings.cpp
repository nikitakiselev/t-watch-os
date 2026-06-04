#include "prog_settings.h"
#include "../../hw.h"
#include "../../statusbar.h"
#include "../../theme.h"

static void setEnter()
{
    themeBackdrop();
    statusbarDraw();

    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_AMBER, COL_BG);
    tft->drawString("[ SETTINGS ]", SCR_W / 2, STATUSBAR_H + 16, 2);

    tft->setTextColor(COL_GREEN_DIM, COL_BG);
    tft->drawString("> coming soon_", SCR_W / 2, (CONTENT_TOP + CONTENT_BOTTOM) / 2, 4);
}

// ASCII-иконка — «ползунки».
static const char *const SET_ART[] = { "-o--", "--o-", "o---" };
static void setDrawIcon(TFT_eSPI &g, int cx, int cy, int r)
{
    drawAsciiArt(g, SET_ART, 3, cx, cy, COL_GREEN, r / 8);
}

const Program settingsProgram = {
    "Settings", setEnter, nullptr, nullptr, setDrawIcon, nullptr, 0
};
