#include "prog_notes.h"
#include "../../hw.h"
#include "../../statusbar.h"
#include "../../theme.h"
#include "../../listnav.h"
#include "../../modal.h"
#include "../../textview.h"
#include "../../wifi.h"
#include "../../sound.h"
#include "../../aiclient.h"
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

// ───────────────────────── Запись голоса ─────────────────────────
enum RState { RS_IDLE, RS_REC, RS_SENDING, RS_NOWIFI };
static RState rState = RS_IDLE;
static char    recSession[40] = "";
static int16_t *recBuf = nullptr;
static int      recSamples = 0;
static const int REC_MAX_SECONDS = 15;
static const int REC_SR = 16000;
static const int REC_CAP = REC_SR * REC_MAX_SECONDS;

static const int RBTN_CX = SCR_W / 2, RBTN_CY = 116, RBTN_R = 66;
static const int RTIMER_Y = RBTN_CY - RBTN_R - 12;

static void recGenSession()
{
    uint32_t a = esp_random(), b = esp_random(), c = esp_random(), d = esp_random();
    snprintf(recSession, sizeof(recSession),
             "%08x-%04x-4%03x-%04x-%08x%04x",
             a, b & 0xffff, (c & 0x0fff), (d & 0x3fff) | 0x8000, b, d & 0xffff);
}

static void recMicIcon(int cx, int cy, uint16_t c)
{
    tft->fillRoundRect(cx - 9, cy - 22, 18, 30, 9, c);
    tft->drawFastHLine(cx - 13, cy + 14, 26, c);
    tft->drawFastVLine(cx, cy + 8, 6, c);
}
static void recDotsIcon(int cx, int cy, uint16_t c)
{
    tft->fillCircle(cx - 16, cy, 4, c);
    tft->fillCircle(cx,      cy, 4, c);
    tft->fillCircle(cx + 16, cy, 4, c);
}

static void recDrawButton(uint16_t col)
{
    tft->fillRect(0, CONTENT_TOP, SCR_W, CONTENT_BOTTOM - CONTENT_TOP, COL_BG);
    tft->drawCircle(RBTN_CX, RBTN_CY, RBTN_R, col);
    tft->drawCircle(RBTN_CX, RBTN_CY, RBTN_R - 1, col);
    if (rState == RS_SENDING) recDotsIcon(RBTN_CX, RBTN_CY, col);
    else                      recMicIcon(RBTN_CX, RBTN_CY, col);
}

static void recDraw()
{
    tft->fillRect(0, 0, SCR_W, CONTENT_BOTTOM, COL_BG);
    statusbarDraw();
    if (rState != RS_REC) {
        tft->setTextDatum(MC_DATUM);
        tft->setTextColor(COL_AMBER, COL_BG);
        tft->drawString(recTargetIdx < 0 ? "NEW NOTE" : "EDIT NOTE",
                        SCR_W / 2, STATUSBAR_H + 12, 2);
    }
    switch (rState) {
    case RS_IDLE:    recDrawButton(COL_GREEN);     break;
    case RS_REC:     recDrawButton(COL_AMBER);     break;
    case RS_SENDING: recDrawButton(COL_GREEN_DIM); break;
    case RS_NOWIFI:
        tft->setTextDatum(MC_DATUM);
        tft->setTextColor(COL_AMBER, COL_BG);
        tft->drawString("no wifi", SCR_W / 2, RBTN_CY, 4);
        break;
    }
}

static bool recTouchDown() { int16_t x, y; return watch->getTouch(x, y); }
static bool recTouchInBtn()
{
    int16_t x, y;
    if (!watch->getTouch(x, y)) return false;
    int dx = x - RBTN_CX, dy = y - RBTN_CY;
    return dx * dx + dy * dy <= RBTN_R * RBTN_R;
}

static void recStart()
{
    if (!recBuf) { soundBeep(400, 120); return; }
    if (!micCaptureBegin()) { soundBeep(400, 120); return; }
    recSamples = 0;
    rState = RS_REC;
    recDraw();
}

static void recStop()
{
    micCaptureEnd();
    if (recSamples < REC_SR * 3 / 10) { rState = RS_IDLE; recDraw(); return; }

    rState = RS_SENDING; recDraw();
    if (!wifiConnected()) wifiAutoConnect(8000, nullptr);
    if (!wifiConnected()) { soundBeep(300, 120); rState = RS_IDLE; recDraw(); return; }

    AiText res = aiTranscribe(recSession, recBuf, recSamples);   // блокирует ~5-10 c
    if (res.code == 200 && res.text.length() > 0) {
        int idx = notesStore(recTargetIdx, res.text.c_str());
        if (idx >= 0 && recTargetIdx < 0) selected = idx;        // выделить новую
        kernelBack();                                            // назад (список/просмотр)
    } else {
        soundBeep(res.code == 204 ? 600 : 300, 120);
        rState = RS_IDLE; recDraw();
    }
}

// Идемпотентное удержание Wi-Fi: acquire ровно один раз за «жизнь» программы,
// release ровно один раз. Иначе повторный onEnter (пробуждение из сна, т.к.
// onResume не задан) утёк бы refCount и Wi-Fi не выключился при выходе.
static bool recWifiHeld = false;

static void recEnter()
{
    if (!recWifiHeld) { wifiAcquire(); recWifiHeld = true; }
    if (!wifiConnected()) {
        tft->fillRect(0, 0, SCR_W, CONTENT_BOTTOM, COL_BG);
        statusbarDraw();
        tft->setTextDatum(MC_DATUM);
        tft->setTextColor(COL_AMBER, COL_BG);
        tft->drawString("connecting...", SCR_W / 2, RBTN_CY, 4);
        wifiAutoConnect(12000, nullptr);
    }
    if (!wifiConnected()) { rState = RS_NOWIFI; recDraw(); return; }

    recGenSession();
    if (!recBuf) recBuf = (int16_t *)ps_malloc((size_t)REC_CAP * sizeof(int16_t));
    rState = RS_IDLE; recSamples = 0;
    recDraw();
}

// Пробуждение из сна: только перерисовать — НЕ трогать Wi-Fi/буфер заново.
static void recResume()
{
    recDraw();
}

static void recExit()
{
    if (rState == RS_REC) micCaptureEnd();
    if (recBuf) { free(recBuf); recBuf = nullptr; }
    rState = RS_IDLE;
    if (recWifiHeld) { wifiRelease(); recWifiHeld = false; }
}

static void recTick()
{
    if (rState == RS_IDLE && recTouchInBtn()) { recStart(); return; }
    if (rState == RS_REC) {
        if (recSamples < REC_CAP)
            recSamples += micCaptureRead(recBuf + recSamples, REC_CAP - recSamples);
        static int lastSec = -1;
        int sec = recSamples / REC_SR;
        if (sec != lastSec) {
            lastSec = sec;
            char b[8]; snprintf(b, sizeof(b), "%ds", sec);
            tft->fillRect(RBTN_CX - 40, RTIMER_Y - 14, 80, 28, COL_BG);
            tft->setTextDatum(MC_DATUM);
            tft->setTextColor(COL_AMBER, COL_BG);
            tft->drawString(b, RBTN_CX, RTIMER_Y, 4);
        }
        if (!recTouchDown() || recSamples >= REC_CAP) recStop();
    }
}

static bool recKeepAwake() { return rState != RS_IDLE && rState != RS_NOWIFI; }

static const NavButton recNav[] = { { "Cancel", kernelBack } };

const Program notesRecProgram = {
    "Rec", recEnter, recTick, nullptr, nullptr, recNav, 1, recKeepAwake, recExit, recResume
};

// New Note: открыть запись для новой заметки.
static void newNote()
{
    if (noteCount >= NOTES_MAX) {           // нет места — оверлей «full»
        tft->fillRect(0, LIST_TOP, SCR_W, CONTENT_BOTTOM - LIST_TOP, COL_BG);
        tft->setTextDatum(MC_DATUM);
        tft->setTextColor(COL_AMBER, COL_BG);
        tft->drawString("full", SCR_W / 2, (LIST_TOP + CONTENT_BOTTOM) / 2, 4);
        delay(700);
        drawList();
        return;
    }
    recTargetIdx = -1;
    kernelOpen(&notesRecProgram);
}

// ── Подтверждение удаления: true, если подтвердили ──
static bool confirmDelete()
{
    const int w = 200, h = 120, x0 = (SCR_W - w) / 2, y0 = 50;
    const int bh = 30, gap = 12, by0 = y0 + 46;
    auto draw = [&](int sel) {
        modalPanel(x0, y0, w, h, 10, COL_AMBER);
        tft->setTextDatum(MC_DATUM);
        tft->setTextColor(COL_AMBER, COL_BG);
        tft->drawString("Delete note?", SCR_W / 2, y0 + 22, 2);
        const char *it[2] = { "Delete", "Cancel" };
        const int bx = x0 + 14, bw = w - 28;
        for (int i = 0; i < 2; i++) {
            int by = by0 + i * (bh + gap);
            if (i == sel) tft->fillRoundRect(bx, by, bw, bh, 6, COL_GREEN_DIM);
            tft->drawRoundRect(bx, by, bw, bh, 6, i == sel ? COL_AMBER : COL_FRAME);
            tft->setTextColor(COL_GREEN, i == sel ? COL_GREEN_DIM : COL_BG);
            tft->drawString(it[i], SCR_W / 2, by + bh / 2, 2);
        }
    };
    ListNav l{2, 0, 1, 0};                 // по умолчанию выделен Cancel (безопасно)
    modalBegin();
    draw(l.sel);
    for (;;) {
        int16_t x, y; InputEvent e = modalPoll(x, y);
        if (e == EVT_BACK) return false;
        if (e == EVT_UP || e == EVT_DOWN) { if (listNavEvent(l, e)) draw(l.sel); continue; }
        if (e == EVT_TAP) {
            if (x < x0 || x > x0 + w || y < y0 || y > y0 + h) return false;  // мимо — отмена
        } else if (e != EVT_CLICK) continue;
        return l.sel == 0;                 // Delete выбран?
    }
}

// ── Контекстное меню заметки: Open / Delete ──
static void noteMenu()
{
    const int w = 190, h = 120, x0 = (SCR_W - w) / 2, y0 = 50;
    const int bh = 30, gap = 12, by0 = y0 + 40;
    auto draw = [&](int sel) {
        modalPanel(x0, y0, w, h, 10, COL_GREEN);
        const char *it[2] = { "Open", "Delete" };
        const int bx = x0 + 14, bw = w - 28;
        tft->setTextDatum(MC_DATUM);
        for (int i = 0; i < 2; i++) {
            int by = by0 + i * (bh + gap);
            if (i == sel) tft->fillRoundRect(bx, by, bw, bh, 6, COL_GREEN_DIM);
            tft->drawRoundRect(bx, by, bw, bh, 6, i == sel ? COL_AMBER : COL_FRAME);
            tft->setTextColor(COL_GREEN, i == sel ? COL_GREEN_DIM : COL_BG);
            tft->drawString(it[i], SCR_W / 2, by + bh / 2, 2);
        }
    };
    ListNav l{2, 0, 0, 0};
    modalBegin();
    draw(l.sel);
    for (;;) {
        int16_t x, y; InputEvent e = modalPoll(x, y);
        if (e == EVT_BACK) { kernelRedraw(); return; }
        if (e == EVT_UP || e == EVT_DOWN) { if (listNavEvent(l, e)) draw(l.sel); continue; }
        if (e == EVT_TAP) {
            if (x < x0 || x > x0 + w || y < y0 || y > y0 + h) { kernelRedraw(); return; }
        } else if (e != EVT_CLICK) continue;
        if (l.sel == 0) {                  // Open
            openIdx = selected;
            kernelOpen(&notesViewProgram);
            return;
        }
        // Delete
        if (confirmDelete()) {
            notesRemove(selected);
            if (selected >= noteCount) selected = noteCount > 0 ? noteCount - 1 : 0;
        }
        kernelRedraw();
        return;
    }
}

static void openSelected()
{
    if (noteCount == 0) return;
    noteMenu();
}

// ───────────────────────── Просмотрщик заметки ─────────────────────────
static TextView noteTV;
static const int VIEW_TITLE_Y = STATUSBAR_H + 12;
static const int VIEW_TOP     = STATUSBAR_H + 26;

static void viewEnter()
{
    tft->fillRect(0, 0, SCR_W, CONTENT_BOTTOM, COL_BG);
    statusbarDraw();
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_AMBER, COL_BG);
    tft->drawString("[ NOTE ]", SCR_W / 2, VIEW_TITLE_Y, 2);

    textViewInit(noteTV, 8, VIEW_TOP, SCR_W - 12, CONTENT_BOTTOM - VIEW_TOP - 4);
    textViewSetText(noteTV, (openIdx >= 0 && openIdx < noteCount) ? notes[openIdx] : "");
    textViewDraw(noteTV);
}

static void viewEvent(InputEvent e, int16_t x, int16_t y)
{
    if (e == EVT_UP || e == EVT_DOWN) {
        if (textViewEvent(noteTV, e)) textViewDraw(noteTV);
    }
}

// Edit: перезаписать текущую заметку голосом.
static void editNote()
{
    recTargetIdx = openIdx;
    kernelOpen(&notesRecProgram);
}

static const NavButton viewNav[] = {
    { "Back", kernelBack },
    { "Edit", editNote },
};

const Program notesViewProgram = {
    "Note", viewEnter, nullptr, viewEvent, nullptr, viewNav, 2, nullptr
};
