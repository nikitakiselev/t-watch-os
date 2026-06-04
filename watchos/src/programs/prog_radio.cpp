#include "prog_radio.h"
#include "../../hw.h"
#include "../../statusbar.h"
#include "../../theme.h"
#include "../../wifi.h"
#include "../../sound.h"
#include "../../stations.h"
#include <string.h>

// ─────────────────────────── Раскладка ───────────────────────────
static const int TITLE_Y     = STATUSBAR_H + 12;   // "station N/M"
static const int MARQUEE_Y   = STATUSBAR_H + 38;   // бегущая строка
static const int TRANSPORT_CY = 118;               // prev / play-pause / next
static const int VOL_Y       = 170;                // громкость
static const int PREV_CX = 42, PP_CX = 120, NEXT_CX = 198;

static const int MQ_SIZE = 2;                       // масштаб шрифта бегущей строки (font1)
static const int MQ_WIN  = 19;                      // символов в окне (240 / (6*2))
static const int MQ_GAP  = 4;                       // пробелы между повторами
static const uint32_t MQ_STEP_MS = 220;

static bool     ok        = false;                  // подключились и играем
static int      cur       = 0;                      // индекс станции
static bool     paused    = false;
static char     mqShown[256] = "";
static int      mqOffset  = 0;
static uint32_t mqLast    = 0;

// ─────────────────── Транспорт-иконки ───────────────────
static void iconPlay (int cx, int cy, uint16_t c) { tft->fillTriangle(cx - 8, cy - 11, cx - 8, cy + 11, cx + 11, cy, c); }
static void iconPause(int cx, int cy, uint16_t c) { tft->fillRect(cx - 9, cy - 11, 6, 22, c); tft->fillRect(cx + 3, cy - 11, 6, 22, c); }
static void iconPrev (int cx, int cy, uint16_t c) { tft->fillRect(cx - 13, cy - 10, 4, 20, c); tft->fillTriangle(cx + 11, cy - 10, cx + 11, cy + 10, cx - 5, cy, c); }
static void iconNext (int cx, int cy, uint16_t c) { tft->fillTriangle(cx - 11, cy - 10, cx - 11, cy + 10, cx + 5, cy, c); tft->fillRect(cx + 9, cy - 10, 4, 20, c); }

static void drawTransport()
{
    tft->fillRect(0, TRANSPORT_CY - 16, SCR_W, 34, COL_BG);
    iconPrev(PREV_CX, TRANSPORT_CY, COL_GREEN);
    if (paused) iconPlay(PP_CX, TRANSPORT_CY, COL_AMBER);
    else        iconPause(PP_CX, TRANSPORT_CY, COL_AMBER);
    iconNext(NEXT_CX, TRANSPORT_CY, COL_GREEN);
}

static void drawVolume()
{
    int vol = audioVolume();                         // 0..21
    tft->fillRect(0, VOL_Y - 12, SCR_W, 24, COL_BG);
    // [-]   bar   [+]
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_GREEN, COL_BG);
    tft->drawString("-", 22, VOL_Y, 4);
    tft->drawString("+", SCR_W - 22, VOL_Y, 4);
    int bx = 44, bw = SCR_W - 88, bh = 10;
    tft->drawRect(bx, VOL_Y - bh / 2, bw, bh, COL_FRAME);
    int fillw = (bw - 2) * vol / 21;
    if (fillw > 0) tft->fillRect(bx + 1, VOL_Y - bh / 2 + 1, fillw, bh - 2, COL_GREEN);
}

static void drawTitle()
{
    tft->fillRect(0, TITLE_Y - 9, SCR_W, 18, COL_BG);
    char buf[24];
    snprintf(buf, sizeof(buf), "station %d/%d", cur + 1, stationsCount());
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_AMBER, COL_BG);
    tft->drawString(buf, SCR_W / 2, TITLE_Y, 2);
}

// Текст бегущей строки в зависимости от состояния.
static void marqueeText(char *out, int n)
{
    if (paused)               { strncpy(out, "paused", n); }
    else if (!audioIsPlaying()) { strncpy(out, "buffering...", n); }
    else {
        const char *t = audioTitle();
        strncpy(out, (t && t[0]) ? t : stationsUrl(cur), n);
    }
    out[n - 1] = 0;
}

static void drawMarquee()
{
    int len = strlen(mqShown);
    char win[MQ_WIN + 1];
    if (len <= MQ_WIN) {
        snprintf(win, sizeof(win), "%-*s", MQ_WIN, mqShown);   // влезает целиком
    } else {
        int period = len + MQ_GAP;
        for (int i = 0; i < MQ_WIN; i++) {
            int p = (mqOffset + i) % period;
            win[i] = (p < len) ? mqShown[p] : ' ';
        }
        win[MQ_WIN] = 0;
    }
    tft->fillRect(0, MARQUEE_Y - 9, SCR_W, 20, COL_BG);
    tft->setTextSize(MQ_SIZE);
    tft->setTextDatum(ML_DATUM);
    tft->setTextColor(COL_GREEN, COL_BG);
    tft->drawString(win, 4, MARQUEE_Y, 1);
    tft->setTextSize(1);
}

static void drawPlayer()
{
    tft->fillRect(0, 0, SCR_W, CONTENT_BOTTOM, COL_BG);   // навбар не трогаем
    statusbarDraw();
    tft->drawFastHLine(16, STATUSBAR_H + 26, SCR_W - 32, COL_FRAME);
    drawTitle();
    drawMarquee();
    drawTransport();
    drawVolume();
}

// ─────────────────── Действия ───────────────────
static void playStation(int i)
{
    cur = (i + stationsCount()) % stationsCount();
    paused = false;
    mqShown[0] = 0;
    mqOffset = 0;
    audioStart(stationsUrl(cur));
    drawTitle();
    drawTransport();
}

static void togglePlay()
{
    if (paused) { audioStart(stationsUrl(cur)); paused = false; }
    else        { audioStop(); paused = true; }
    drawTransport();
}

static void volStep(int d)
{
    audioSetVolume(audioVolume() + d);
    drawVolume();
}

// ─────────────────── Connect-flow ───────────────────
static void connectCb(const char *ssid)
{
    tft->fillRect(0, CONTENT_TOP, SCR_W, CONTENT_BOTTOM - CONTENT_TOP, COL_BG);
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_AMBER, COL_BG);
    char buf[40];
    snprintf(buf, sizeof(buf), "connecting:");
    tft->drawString(buf, SCR_W / 2, (CONTENT_TOP + CONTENT_BOTTOM) / 2 - 14, 2);
    tft->setTextColor(COL_GREEN, COL_BG);
    tft->drawString(ssid, SCR_W / 2, (CONTENT_TOP + CONTENT_BOTTOM) / 2 + 12, 4);
}

static void centerMsg(const char *msg, uint16_t col)
{
    tft->fillRect(0, 0, SCR_W, CONTENT_BOTTOM, COL_BG);
    statusbarDraw();
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(col, COL_BG);
    tft->drawString(msg, SCR_W / 2, (CONTENT_TOP + CONTENT_BOTTOM) / 2, 4);
}

// ─────────────────── Программа ───────────────────
static void radioEnter()
{
    wifiAcquire();

    if (!wifiConnected()) {
        centerMsg("connecting...", COL_AMBER);
        wifiAutoConnect(12000, connectCb);
    }
    if (!wifiConnected()) { ok = false; centerMsg("no wifi", COL_AMBER); return; }

    if (stationsCount() == 0) { ok = false; centerMsg("no stations", COL_AMBER); return; }

    ok = true;
    audioSetVolume(12);
    playStation(cur);          // продолжаем с текущей станции
    drawPlayer();
}

static void radioExit()
{
    audioStop();
    paused = false;
    ok = false;
    wifiRelease();
}

static void radioTick()
{
    if (!ok) return;

    // Обновляем текст бегущей строки при изменении состояния/метаданных.
    char want[256];
    marqueeText(want, sizeof(want));
    if (strncmp(want, mqShown, sizeof(mqShown)) != 0) {
        strncpy(mqShown, want, sizeof(mqShown));
        mqShown[sizeof(mqShown) - 1] = 0;
        mqOffset = 0;
        drawMarquee();
    } else if (strlen(mqShown) > MQ_WIN && (millis() - mqLast) > MQ_STEP_MS) {
        mqLast = millis();
        mqOffset = (mqOffset + 1) % (strlen(mqShown) + MQ_GAP);
        drawMarquee();
    }
}

static void radioEvent(InputEvent e, int16_t x, int16_t y)
{
    if (!ok) return;
    switch (e) {
    case EVT_CLICK: togglePlay(); break;
    case EVT_UP:    volStep(+1);  break;       // свайп вверх — громче
    case EVT_DOWN:  volStep(-1);  break;
    case EVT_TAP:
        if (y >= TRANSPORT_CY - 22 && y <= TRANSPORT_CY + 22) {
            if (x < 80)       playStation(cur - 1);
            else if (x < 160) togglePlay();
            else              playStation(cur + 1);
        } else if (y >= VOL_Y - 18 && y <= VOL_Y + 18) {
            if (x < 44)            volStep(-1);
            else if (x > SCR_W-44) volStep(+1);
        }
        break;
    default: break;
    }
}

static void radioIcon(TFT_eSPI &g, int cx, int cy, int r)
{
    g.fillCircle(cx - r / 3, cy + r / 3, r / 4, COL_GREEN);
    g.drawFastVLine(cx - r / 3 + r / 4, cy - r / 2, r * 5 / 6, COL_GREEN);
    g.fillRect(cx - r / 3 + r / 4, cy - r / 2, r / 2, 3, COL_GREEN);
}

static bool radioKeepAwake() { return audioIsPlaying(); }

const Program radioProgram = {
    "Radio", radioEnter, radioTick, radioEvent, radioIcon, nullptr, 0, radioKeepAwake, radioExit
};
