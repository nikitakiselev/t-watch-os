#include "keyboard.h"
#include "hw.h"
#include "input.h"
#include "theme.h"
#include "power.h"
#include <string.h>

// Раскладка: 3×3 буквенно-цифровая сетка + нижний ряд из 4 управляющих клавиш.
static const int KB_TOP = 64;
static const int KH     = 43;          // высота ряда
static const int TITLE_Y = 20;
static const int FIELD_Y = 46;

static const uint32_t MULTITAP_MS = 900;

// Наборы символов для 9 клавиш сетки (set 0..8) + пробел/0 (set 9).
static const char *SETS[] = {
    "1.,-_@#$%&*+=:/!?",   // 0: цифра 1 + символы
    "abc2", "def3",        // 1, 2
    "ghi4", "jkl5", "mno6",// 3, 4, 5
    "pqrs7", "tuv8", "wxyz9", // 6, 7, 8
    " 0",                  // 9: пробел / 0
};
static const char *GRID_LO[] = { "1#", "abc", "def", "ghi", "jkl", "mno", "pqrs", "tuv", "wxyz" };
static const char *GRID_UP[] = { "1#", "ABC", "DEF", "GHI", "JKL", "MNO", "PQRS", "TUV", "WXYZ" };

static char    *buf;
static int      maxLen;
static int      len;
static bool     upper;
static int      pendingSet;
static int      pendingIdx;
static uint32_t lastTap;

static char applyCase(char c) { return (upper && c >= 'a' && c <= 'z') ? (char)(c - 32) : c; }

static void drawKey(int x, int y, int w, int h, const char *label, uint16_t col)
{
    tft->drawRoundRect(x + 2, y + 2, w - 4, h - 4, 4, COL_FRAME);
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(col, COL_BG);
    tft->drawString(label, x + w / 2, y + h / 2, 2);
}

static void drawField()
{
    tft->fillRect(0, FIELD_Y - 12, SCR_W, 24, COL_BG);
    const char *disp = buf;
    if (len > 18) disp = buf + len - 18;     // показываем хвост
    char line[24];
    snprintf(line, sizeof(line), "%s_", disp);
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_AMBER, COL_BG);
    tft->drawString(line, SCR_W / 2, FIELD_Y, 2);
}

static void drawKeyboard(const char *title)
{
    tft->fillScreen(COL_BG);
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_GREEN, COL_BG);
    tft->drawString(title, SCR_W / 2, TITLE_Y, 2);
    drawField();

    const char *const *G = upper ? GRID_UP : GRID_LO;
    for (int s = 0; s < 9; s++) {
        int row = s / 3, col = s % 3;
        drawKey(col * (SCR_W / 3), KB_TOP + row * KH, SCR_W / 3, KH, G[s], COL_GREEN);
    }
    int y3 = KB_TOP + 3 * KH, w4 = SCR_W / 4;
    drawKey(0 * w4, y3, w4, KH, upper ? "AB" : "ab", COL_GREEN);
    drawKey(1 * w4, y3, w4, KH, "spc",  COL_GREEN);
    drawKey(2 * w4, y3, w4, KH, "<",    COL_GREEN);
    drawKey(3 * w4, y3, w4, KH, "OK",   COL_AMBER);
}

static void typeKey(int set)
{
    const char *s = SETS[set];
    int slen = strlen(s);
    uint32_t now = millis();
    if (set == pendingSet && (now - lastTap) <= MULTITAP_MS && len > 0) {
        pendingIdx = (pendingIdx + 1) % slen;       // циклируем текущий символ
        buf[len - 1] = applyCase(s[pendingIdx]);
    } else {
        if (len >= maxLen - 1) { lastTap = now; return; }
        pendingSet = set;
        pendingIdx = 0;
        buf[len++] = applyCase(s[0]);
        buf[len] = 0;
    }
    lastTap = now;
    drawField();
}

static void backspace()
{
    if (len > 0) { buf[--len] = 0; pendingSet = -1; drawField(); }
}

bool keyboardPrompt(const char *title, char *out, int outLen)
{
    buf = out;
    maxLen = outLen;
    len = 0;
    buf[0] = 0;
    upper = false;
    pendingSet = -1;
    pendingIdx = 0;
    lastTap = 0;

    drawKeyboard(title);

    for (;;) {
        int16_t x, y;
        InputEvent e = inputPoll(x, y);
        if (e != EVT_NONE) powerNoteActivity();     // модальный экран — держим таймер сна

        if (e == EVT_BACK) return false;            // долгое нажатие — отмена

        if (e == EVT_TAP) {
            if (y >= KB_TOP) {
                int row = (y - KB_TOP) / KH;
                if (row >= 0 && row < 3) {
                    int col = x / (SCR_W / 3); if (col > 2) col = 2;
                    typeKey(row * 3 + col);
                } else if (row == 3) {
                    int col = x / (SCR_W / 4); if (col > 3) col = 3;
                    switch (col) {
                    case 0: upper = !upper; pendingSet = -1; drawKeyboard(title); break;
                    case 1: typeKey(9); break;       // пробел / 0
                    case 2: backspace(); break;
                    case 3: return true;             // OK
                    }
                }
            }
        }

        // Тайм-аут мультитапа: завершить текущий символ.
        if (pendingSet >= 0 && (millis() - lastTap) > MULTITAP_MS) pendingSet = -1;

        delay(20);
    }
}
