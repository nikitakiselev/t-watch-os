#include "prog_menu.h"
#include "../../hw.h"
#include "../../statusbar.h"
#include "../../theme.h"
#include "../../apps.h"

// ─────────────────────────── Раскладка ───────────────────────────
static const int ARROW_TOP_CY = STATUSBAR_H + 12;   // 36
static const int AREA_TOP     = STATUSBAR_H + 24;   // 48 — верх карточки
static const int AREA_H       = 130;                // низ = 178
static const int ARROW_BOT_CY = 192;

static const int ICON_DY = 48;
static const int NAME_DY = 90;
static const int ICON_R  = 18;   // → drawAsciiArt size = r/8 = 2

static int selected = 0;

// ───────────────────────── Отрисовка карточки ─────────────────────
static void drawDefaultIcon(TFT_eSPI &g, int cx, int cy, const char *name)
{
    char s[2] = { (name && name[0]) ? name[0] : '?', 0 };
    g.setTextDatum(MC_DATUM);
    g.setTextColor(COL_GREEN);
    g.drawString(s, cx, cy, 6);
}

static void drawCard(int idx)
{
    int cx = SCR_W / 2;
    if (APPS[idx]->drawIcon) APPS[idx]->drawIcon(*tft, cx, AREA_TOP + ICON_DY, ICON_R);
    else                     drawDefaultIcon(*tft, cx, AREA_TOP + ICON_DY, APPS[idx]->name);

    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_AMBER);
    tft->drawString(APPS[idx]->name, cx, AREA_TOP + NAME_DY, 4);
}

// Вертикальная колонка точек слева: по точке на приложение, выбранное —
// ярче и чуть больше (индикатор позиции в списке).
static const int DOTS_X = 9;

static const int DOT_GAP = 14;   // расстояние между точками (плотная колонка)

static void drawDots()
{
    if (APPS_COUNT <= 1) return;
    tft->fillRect(0, AREA_TOP, 18, AREA_H, COL_BG);     // очистить колонку
    int y0 = AREA_TOP + AREA_H / 2 - (APPS_COUNT - 1) * DOT_GAP / 2;   // центрировать по высоте
    for (int i = 0; i < APPS_COUNT; i++) {
        int y = y0 + i * DOT_GAP;
        if (i == selected) tft->fillCircle(DOTS_X, y, 4, COL_AMBER);
        else               tft->fillCircle(DOTS_X, y, 2, COL_GREEN_DIM);
    }
}

static void renderCurrent()
{
    tft->fillRect(0, AREA_TOP, SCR_W, AREA_H, COL_BG);
    drawCard(selected);
    drawDots();
}

static void drawArrows()
{
    if (APPS_COUNT <= 1) return;
    int cx = SCR_W / 2;
    uint16_t c = COL_AMBER;
    tft->fillTriangle(cx, ARROW_TOP_CY - 6, cx - 12, ARROW_TOP_CY + 6, cx + 12, ARROW_TOP_CY + 6, c);
    tft->fillTriangle(cx - 12, ARROW_BOT_CY - 6, cx + 12, ARROW_BOT_CY - 6, cx, ARROW_BOT_CY + 6, c);
}

// Лёгкий glitch-переход по области карточки (в духе темы).
static void cardGlitch()
{
    for (int i = 0; i < 6; i++) {
        int y = AREA_TOP + random(0, AREA_H - 6);
        int h = random(2, 8);
        tft->fillRect(0, y, SCR_W, h, (i & 1) ? COL_AMBER : COL_GREEN);
        delay(12);
    }
}

static void launchSelected()
{
    if (APPS_COUNT > 0 && APPS[selected]) kernelOpen(APPS[selected]);
}

static void scrollNext()
{
    if (APPS_COUNT <= 1) return;
    selected = (selected + 1) % APPS_COUNT;
    cardGlitch();
    renderCurrent();
}

static void scrollPrev()
{
    if (APPS_COUNT <= 1) return;
    selected = (selected - 1 + APPS_COUNT) % APPS_COUNT;
    cardGlitch();
    renderCurrent();
}

// ──────────────────────────── Программа ───────────────────────────
static void menuEnter()
{
    // selected НЕ сбрасываем — запоминаем позицию между входами.
    if (selected >= APPS_COUNT) selected = 0;
    themeBackdrop();
    statusbarDraw();

    if (APPS_COUNT == 0) {
        tft->setTextDatum(MC_DATUM);
        tft->setTextColor(COL_GREEN_DIM, COL_BG);
        tft->drawString("no apps", SCR_W / 2, (CONTENT_TOP + CONTENT_BOTTOM) / 2, 4);
        return;
    }

    drawArrows();
    renderCurrent();
}

static void menuEvent(InputEvent e, int16_t x, int16_t y)
{
    if (APPS_COUNT == 0) return;

    switch (e) {
    case EVT_UP:    scrollPrev(); break;
    case EVT_DOWN:  scrollNext(); break;
    case EVT_TAP:
        if (y < AREA_TOP)               scrollPrev();
        else if (y > AREA_TOP + AREA_H) scrollNext();
        else                            launchSelected();
        break;
    default:
        break;
    }
}

const Program menuProgram = { "Apps", menuEnter, nullptr, menuEvent, nullptr };
