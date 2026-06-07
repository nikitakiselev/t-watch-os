#include "prog_brightness.h"
#include "../../hw.h"
#include "../../statusbar.h"
#include "../../theme.h"

// Яркость — 10 уровней (10%..100%). Уровень → подсветка raw 0..255 = level*255/10.
// Значение применяется на всю ОС немедленно (hwSetBrightness) и сохраняется в NVS.
static const int LEVELS  = 10;
static const int TITLE_Y = STATUSBAR_H + 16;
static const int VAL_CY  = (CONTENT_TOP + CONTENT_BOTTOM) / 2;   // 114
static const int BTN_R   = 26;
static const int MINUS_CX = 40;
static const int PLUS_CX  = SCR_W - 40;

static int level = 10;   // 1..10

static int levelFromBrightness(uint8_t b)
{
    int l = (b * LEVELS + 127) / 255;   // округление к ближайшему уровню
    if (l < 1) l = 1;
    if (l > LEVELS) l = LEVELS;
    return l;
}

static void applyLevel()
{
    hwSetBrightness((uint8_t)(level * 255 / LEVELS));
}

// Кнопка − / + : рамка-кружок и глиф полосами.
// Кнопка − / + : рамка-кружок и глиф полосами. pressed — подсветка при тапе.
static void drawButton(int cx, bool plus, bool enabled, bool pressed)
{
    uint16_t glyph = !enabled ? COL_GREEN_DIM : (pressed ? COL_GREEN_HI : COL_GREEN);
    tft->fillRoundRect(cx - BTN_R, VAL_CY - BTN_R, 2 * BTN_R, 2 * BTN_R, 8,
                       pressed ? COL_GREEN_DIM : COL_BG);
    tft->drawRoundRect(cx - BTN_R, VAL_CY - BTN_R, 2 * BTN_R, 2 * BTN_R, 8,
                       pressed ? COL_AMBER : COL_FRAME);
    tft->fillRect(cx - 12, VAL_CY - 2, 24, 5, glyph);          // горизонтальная полоса
    if (plus) tft->fillRect(cx - 2, VAL_CY - 12, 5, 24, glyph); // + добавляет вертикаль
}

static void drawButtons()
{
    drawButton(MINUS_CX, false, level > 1,      false);
    drawButton(PLUS_CX,  true,  level < LEVELS, false);
}

// Число (font 7) и знак «%» ОТДЕЛЬНОЙ строкой ниже — иначе «100%» не помещается.
static void drawValue()
{
    char num[5];
    snprintf(num, sizeof(num), "%d", level * 10);

    int x0 = MINUS_CX + BTN_R + 2;
    int w  = PLUS_CX - MINUS_CX - 2 * BTN_R - 4;
    tft->fillRect(x0, VAL_CY - 30, w, 80, COL_BG);

    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_AMBER, COL_BG);
    tft->drawString(num, SCR_W / 2, VAL_CY - 6, 7);
    tft->setTextColor(COL_GREEN, COL_BG);
    tft->drawString("%", SCR_W / 2, VAL_CY + 32, 4);
}

static void brightnessEnter()
{
    level = levelFromBrightness(hwBrightness());

    themeBackdrop();
    statusbarDraw();
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_AMBER, COL_BG);
    tft->drawString("[ BRIGHTNESS ]", SCR_W / 2, TITLE_Y, 2);

    drawButtons();
    drawValue();
}

// Нажатие кнопки: подсветка + лёгкая вибрация, затем изменение уровня.
static void press(int cx, bool plus, int d)
{
    drawButton(cx, plus, true, true);     // подсветить нажатую кнопку
    hwVibrate(25);                        // лёгкий тактильный отклик
    delay(70);                            // чтобы подсветка была заметна

    int n = level + d;
    if (n < 1) n = 1;
    if (n > LEVELS) n = LEVELS;
    bool changed = (n != level);
    level = n;
    if (changed) applyLevel();
    drawButtons();                        // снять подсветку + обновить доступность
    if (changed) drawValue();
}

static void brightnessEvent(InputEvent e, int16_t x, int16_t y)
{
    switch (e) {
    case EVT_UP:
    case EVT_RIGHT: press(PLUS_CX,  true,  +1); break;
    case EVT_DOWN:
    case EVT_LEFT:  press(MINUS_CX, false, -1); break;
    case EVT_TAP:
        if (y >= CONTENT_TOP && y < CONTENT_BOTTOM) {
            if (x < SCR_W / 3)          press(MINUS_CX, false, -1);   // левая зона — минус
            else if (x > SCR_W * 2 / 3) press(PLUS_CX,  true,  +1);   // правая зона — плюс
        }
        break;
    default: break;
    }
}

// ASCII-иконка — солнышко.
static const char *const BR_ART[] = { " \\|/ ", "-(O)-", " /|\\ " };
static void brightnessIcon(TFT_eSPI &g, int cx, int cy, int r)
{
    drawAsciiArt(g, BR_ART, 3, cx, cy, COL_AMBER, r / 8);
}

// Это под-экран настроек — нижняя кнопка «Back» (возврат к списку Settings).
static const NavButton brightnessNav[] = { { "Back", kernelBack } };

const Program brightnessProgram = {
    "Brightness", brightnessEnter, nullptr, brightnessEvent, brightnessIcon, brightnessNav, 1
};
