#include "prog_clock.h"
#include "../../hw.h"
#include "../../statusbar.h"
#include "../../theme.h"

// ─────────────────────────────── Вкладки ───────────────────────────────
// Две вкладки в одном приложении (бывшие Clock и Stopwatch). Переключение —
// тапом по боковым стрелкам, как в System (точки-индикатор + стрелки).
enum { TAB_CLOCK = 0, TAB_STOPWATCH, NUM_TABS };
static const char *const TAB_NAME[NUM_TABS] = { "MSK", "STOPWATCH" };

static int curTab = 0;

// ─────────────────────────────── Геометрия ─────────────────────────────
static const int TITLE_Y  = STATUSBAR_H + 12;            // 36 — заголовок вкладки
static const int DOTS_Y   = STATUSBAR_H + 26;            // 50 — точки-индикатор
static const int BODY_TOP = STATUSBAR_H + 34;            // 58 — верх тела вкладки
static const int ARROW_CY = (BODY_TOP + CONTENT_BOTTOM) / 2;
static const int TAP_EDGE = 36;                          // ширина зоны тапа по стрелке

static const int CLK_TIME_Y = ARROW_CY;                  // HH:MM:SS по центру (font 6)
static const int TIME_Y = 122;                           // секундомер (font 6)
static const int STATE_Y = 172;                          // строка состояния секундомера

// ─────────────────────────────── Состояние ─────────────────────────────
static int lastSec = -1;   // часы: троттлинг перерисовки (раз в секунду)

static bool          running = false;   // секундомер
static unsigned long startMs = 0;
static unsigned long accMs   = 0;

static unsigned long elapsed()
{
    return accMs + (running ? (millis() - startMs) : 0);
}

// Боковые стрелки переключения вкладок (рисуются и в хроме, и поверх времени).
static void drawArrows()
{
    uint16_t lc = (curTab > 0)            ? COL_GREEN : COL_GREEN_DIM;
    uint16_t rc = (curTab < NUM_TABS - 1) ? COL_GREEN : COL_GREEN_DIM;
    tft->fillTriangle(6, ARROW_CY, 16, ARROW_CY - 9, 16, ARROW_CY + 9, lc);
    tft->fillTriangle(SCR_W - 6, ARROW_CY, SCR_W - 16, ARROW_CY - 9, SCR_W - 16, ARROW_CY + 9, rc);
}

// ─────────────────────────────── Тело: часы ────────────────────────────
// HH:MM:SS одной строкой, единым цифровым шрифтом font 6 (font 7 семисегментный
// не вмещает 8 символов в 240px). Обновляется раз в секунду.
static void clockBody(bool full)
{
    RTC_Date now = hwNow();
    int h, m, s;
    hwTimeInZone(now, TZ_MSK_OFFSET_MIN, h, m, s);

    if (full) lastSec = -1;
    if (s == lastSec) return;
    lastSec = s;

    char buf[12];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", h, m, s);
    tft->fillRect(TAP_EDGE, CLK_TIME_Y - 26, SCR_W - 2 * TAP_EDGE, 52, COL_BG);
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_AMBER, COL_BG);
    tft->drawString(buf, SCR_W / 2, CLK_TIME_Y, 6);
    drawArrows();          // время по центру может задеть колонки стрелок — рисуем их поверх
    statusbarDraw();
}

// ─────────────────────────── Тело: секундомер ──────────────────────────
static void swDrawTime()
{
    unsigned long e  = elapsed();
    unsigned long cs = (e / 10) % 100;
    unsigned long s  = (e / 1000) % 60;
    unsigned long m  = e / 60000;

    char b[16];
    snprintf(b, sizeof(b), "%02lu:%02lu.%02lu", m, s, cs);
    tft->fillRect(TAP_EDGE, TIME_Y - 26, SCR_W - 2 * TAP_EDGE, 52, COL_BG);
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(running ? COL_GREEN : COL_GREEN_DIM, COL_BG);
    tft->drawString(b, SCR_W / 2, TIME_Y, 6);
}

static void swDrawState()
{
    tft->fillRect(0, STATE_Y - 10, SCR_W, 20, COL_BG);
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(running ? COL_AMBER : COL_GREEN_DIM, COL_BG);
    tft->drawString(running ? "> RUNNING" : "[ STOPPED ]", SCR_W / 2, STATE_Y, 2);
}

static void stopwatchBody()
{
    swDrawTime();
    swDrawState();
}

// ─────────────────────────────── Хром вкладок ──────────────────────────
// Заголовок, точки-индикатор и боковые стрелки.
static void drawChrome()
{
    tft->fillRect(0, STATUSBAR_H, SCR_W, CONTENT_BOTTOM - STATUSBAR_H, COL_BG);
    statusbarDraw();

    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_AMBER, COL_BG);
    tft->drawString(TAB_NAME[curTab], SCR_W / 2, TITLE_Y, 2);

    // Точки-индикатор: активная — янтарная, остальные тусклые.
    int spacing = 12;
    int x0 = SCR_W / 2 - (NUM_TABS - 1) * spacing / 2;
    for (int i = 0; i < NUM_TABS; i++) {
        if (i == curTab) tft->fillCircle(x0 + i * spacing, DOTS_Y, 3, COL_AMBER);
        else             tft->fillCircle(x0 + i * spacing, DOTS_Y, 2, COL_GREEN_DIM);
    }

    drawArrows();   // боковые стрелки (тусклые на крайних вкладках, где идти некуда)
}

// Нижняя панель. Своя (navCount=-1): на секундомере нужны Run/Reset, на часах — Back.
static void drawNavbar()
{
    const int top = SCR_H - NAVBAR_H;
    tft->fillRect(0, top, SCR_W, NAVBAR_H, COL_BG);
    tft->drawFastHLine(0, top, SCR_W, COL_FRAME);
    tft->setTextDatum(MC_DATUM);

    if (curTab == TAB_STOPWATCH) {
        const int w = SCR_W / 3;
        tft->setTextColor(COL_AMBER, COL_BG);
        tft->drawString("Exit", w / 2, top + NAVBAR_H / 2, 2);
        tft->drawFastVLine(w, top + 5, NAVBAR_H - 10, COL_FRAME);
        tft->setTextColor(COL_GREEN, COL_BG);
        tft->drawString("Run", w + w / 2, top + NAVBAR_H / 2, 2);
        tft->drawFastVLine(2 * w, top + 5, NAVBAR_H - 10, COL_FRAME);
        tft->drawString("Reset", 2 * w + w / 2, top + NAVBAR_H / 2, 2);
    } else {
        tft->setTextColor(COL_AMBER, COL_BG);
        tft->drawString("Exit", SCR_W / 2, top + NAVBAR_H / 2, 2);
    }
}

static void drawBody()
{
    if (curTab == TAB_CLOCK) {
        clockBody(true);
    } else {
        stopwatchBody();
    }
}

static void switchTab(int dir)
{
    int next = curTab + dir;
    if (next < 0 || next >= NUM_TABS) return;
    curTab = next;
    drawChrome();
    drawBody();
    drawNavbar();
}

// ─────────────────────────── Действия секундомера ──────────────────────
static void swToggle()
{
    if (running) { accMs = elapsed(); running = false; }
    else         { startMs = millis(); running = true; }
    swDrawState();
    swDrawTime();
}

static void swReset()
{
    running = false;
    accMs = 0;
    startMs = 0;
    swDrawState();
    swDrawTime();
}

// ─────────────────────────────── Программа ─────────────────────────────
static void clockEnter()
{
    drawChrome();
    drawBody();
    drawNavbar();   // navCount=-1: фреймворк навбар не рисует, делаем сами
}

static void clockTick()
{
    if (curTab == TAB_CLOCK)       clockBody(false);
    else if (running)              swDrawTime();
}

static void clockEvent(InputEvent e, int16_t x, int16_t y)
{
    // Свайпы листают вкладки (влево — вперёд, вправо — назад), как пейджинг.
    if (e == EVT_LEFT)  { switchTab(+1); return; }
    if (e == EVT_RIGHT) { switchTab(-1); return; }
    if (e != EVT_TAP) return;

    if (y >= SCR_H - NAVBAR_H) {                  // тап по навбару (рисуем его сами)
        if (curTab == TAB_STOPWATCH) {
            const int w = SCR_W / 3;
            if (x < w)       kernelBack();
            else if (x < 2 * w) swToggle();
            else             swReset();
        } else {
            kernelBack();
        }
    } else if (x < TAP_EDGE) {
        switchTab(-1);                            // тап по левой стрелке
    } else if (x > SCR_W - TAP_EDGE) {
        switchTab(+1);                            // тап по правой стрелке
    }
}

// Секундомер идёт — не усыплять CPU (экран всё равно гаснет на простое).
static bool clockKeepAwake() { return curTab == TAB_STOPWATCH && running; }

// ASCII-иконка — циферблат.
static const char *const CLOCK_ART[] = { ".--.", "|<>|", "'--'" };
static void clockDrawIcon(TFT_eSPI &g, int cx, int cy, int r)
{
    drawAsciiArt(g, CLOCK_ART, 3, cx, cy, COL_GREEN, r / 8);
}

const Program clockProgram = {
    "Clock", clockEnter, clockTick, clockEvent, clockDrawIcon, nullptr, -1, clockKeepAwake
};
