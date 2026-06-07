#include "prog_notes.h"
#include "../../hw.h"
#include "../../statusbar.h"
#include "../../theme.h"
#include "../../listnav.h"
#include "../../modal.h"
#include "../../textview.h"
#include <Arduino.h>
#include <SPIFFS.h>
#include <string.h>

// ───────────────────────── Хранилище ─────────────────────────
#define NOTES_MAX 16
#define NOTE_CAP  512

static char notes[NOTES_MAX][NOTE_CAP];
static int  noteCount = 0;
static bool loaded = false;

static void notesLoad()
{
    noteCount = 0;
    SPIFFS.begin(true);
    fs::File f = SPIFFS.open("/notes.txt", "r");
    if (f) {
        while (f.available() && noteCount < NOTES_MAX) {
            String line = f.readStringUntil('\n');
            line.trim();
            if (line.length() == 0) continue;
            line.toCharArray(notes[noteCount], NOTE_CAP);
            noteCount++;
        }
        f.close();
    }
    loaded = true;
}

static void notesSave()
{
    fs::File f = SPIFFS.open("/notes.txt", "w");
    if (!f) return;
    for (int i = 0; i < noteCount; i++) {
        f.print(notes[i]);
        f.print('\n');
    }
    f.close();
}

// Записать text в notes[idx], заменив переводы строк пробелом. idx<0 — добавить.
// Возвращает индекс или -1 (нет места).
static int notesStore(int idx, const char *text)
{
    if (idx < 0) {
        if (noteCount >= NOTES_MAX) return -1;
        idx = noteCount++;
    }
    int j = 0;
    for (int i = 0; text[i] && j < NOTE_CAP - 1; i++) {
        char c = text[i];
        notes[idx][j++] = (c == '\n' || c == '\r') ? ' ' : c;
    }
    notes[idx][j] = 0;
    notesSave();
    return idx;
}

static void notesRemove(int idx)
{
    if (idx < 0 || idx >= noteCount) return;
    for (int i = idx; i < noteCount - 1; i++)
        memcpy(notes[i], notes[i + 1], NOTE_CAP);
    noteCount--;
    notesSave();
}

// Общее состояние между программами list/view/rec.
static int selected = 0;       // выделенная в списке
static int scrollTop = 0;
static int openIdx = 0;        // открытая в просмотрщике
static int recTargetIdx = -1;  // цель записи: -1 = новая, иначе перезапись

// Объявления соседних программ (реализованы ниже в этом файле, Task 5–7).
extern const Program notesViewProgram;
extern const Program notesRecProgram;

// ───────────────────────── Список ─────────────────────────
static const int TITLE_Y  = STATUSBAR_H + 12;
static const int LIST_TOP  = STATUSBAR_H + 30;
static const int ROW_H     = 30;
static const int VISIBLE   = 4;

// Обрезать UTF-8 строку по ширине px (с загруженным шрифтом), добавить «…».
static void fitTitle(const char *src, char *dst, int dstCap, int maxW)
{
    int n = strlen(src);
    if (n > dstCap - 1) n = dstCap - 1;
    memcpy(dst, src, n); dst[n] = 0;
    if (tft->textWidth(dst) <= maxW) return;
    while (n > 0) {
        int k = n - 1;                                  // отступить на один UTF-8 символ
        while (k > 0 && ((uint8_t)dst[k] & 0xC0) == 0x80) k--;
        n = k;
        char t[256];
        int m = n; if (m > (int)sizeof(t) - 4) m = sizeof(t) - 4;
        memcpy(t, dst, m); t[m] = 0;
        strcat(t, "...");
        if (tft->textWidth(t) <= maxW) { memcpy(dst, t, strlen(t) + 1); return; }
    }
    dst[0] = 0;
}

static void drawList()
{
    tft->fillRect(0, LIST_TOP, SCR_W, CONTENT_BOTTOM - LIST_TOP, COL_BG);
    if (noteCount == 0) {
        tft->setTextDatum(MC_DATUM);
        tft->setTextColor(COL_GREEN_DIM, COL_BG);
        tft->drawString("no notes", SCR_W / 2, (LIST_TOP + CONTENT_BOTTOM) / 2, 4);
        return;
    }
    textFontLoad();
    tft->setTextDatum(ML_DATUM);
    char title[256];
    for (int i = scrollTop; i < scrollTop + VISIBLE && i < noteCount; i++) {
        int y = LIST_TOP + (i - scrollTop) * ROW_H;
        bool sel = (i == selected);
        if (sel) tft->fillRoundRect(6, y + 2, SCR_W - 12, ROW_H - 4, 4, COL_GREEN_DIM);
        tft->setTextColor(sel ? COL_GREEN_HI : COL_GREEN, sel ? COL_GREEN_DIM : COL_BG);
        fitTitle(notes[i], title, sizeof(title), SCR_W - 28);
        tft->drawString(title, 14, y + ROW_H / 2);
    }
    textFontUnload();
}

static void drawAll()
{
    tft->fillRect(0, 0, SCR_W, CONTENT_BOTTOM, COL_BG);
    statusbarDraw();
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_AMBER, COL_BG);
    tft->drawString("[ NOTES ]", SCR_W / 2, TITLE_Y, 2);
    drawList();
}

static void openSelected();   // реализация в Task 6
static void newNote();        // реализация в Task 5

static void notesEnter()
{
    if (!loaded) notesLoad();
    if (selected >= noteCount) selected = noteCount > 0 ? noteCount - 1 : 0;
    if (selected < 0) selected = 0;
    drawAll();
}

static void notesEvent(InputEvent e, int16_t x, int16_t y)
{
    if (noteCount == 0 && e != EVT_NONE) return;
    switch (e) {
    case EVT_UP:
    case EVT_DOWN: {
        ListNav l{noteCount, VISIBLE, selected, scrollTop};
        if (listNavEvent(l, e)) { selected = l.sel; scrollTop = l.top; drawList(); }
        break;
    }
    case EVT_TAP:
        openSelected();           // тап активирует ВЫДЕЛЕННУЮ (правило ОС)
        break;
    default: break;
    }
}

static const NavButton notesNav[] = {
    { "Exit",     kernelBack },
    { "New Note", newNote },
};

static void notesIcon(TFT_eSPI &g, int cx, int cy, int r)
{
    g.drawRoundRect(cx - r / 2, cy - r * 2 / 3, r, r * 4 / 3, r / 6, COL_GREEN);
    for (int i = -1; i <= 1; i++)
        g.drawFastHLine(cx - r / 3, cy + i * (r / 3), r * 2 / 3, COL_GREEN);
}

const Program notesProgram = {
    "Notes", notesEnter, nullptr, notesEvent, notesIcon, notesNav, 2, nullptr
};

// ── временные заглушки (будут заменены в Task 5/6/7) ──
static void newNote() {}
static void openSelected() {}
const Program notesViewProgram = { "Note", nullptr, nullptr, nullptr };
const Program notesRecProgram  = { "Rec",  nullptr, nullptr, nullptr };
