#include "prog_assistant.h"
#include "../../hw.h"
#include "../../statusbar.h"
#include "../../theme.h"
#include "../../wifi.h"
#include "../../sound.h"
#include "../../aiclient.h"
#include <Arduino.h>
#include <string.h>

enum AState { ST_IDLE, ST_RECORDING, ST_SENDING, ST_SPEAKING, ST_NOWIFI };
static AState state = ST_IDLE;

static char    sessionId[40] = "";
static int16_t *recBuf = nullptr;            // PSRAM-буфер записи
static int      recSamples = 0;
static const int REC_MAX_SECONDS = 15;
static const int SR = 16000;
static const int REC_CAP = SR * REC_MAX_SECONDS;   // макс. сэмплов

// Кнопка-микрофон в центре экрана.
static const int BTN_CX = SCR_W / 2, BTN_CY = 116, BTN_R = 66;

static void genSession()
{
    uint32_t a = esp_random(), b = esp_random(), c = esp_random(), d = esp_random();
    snprintf(sessionId, sizeof(sessionId),
             "%08x-%04x-4%03x-%04x-%08x%04x",
             a, b & 0xffff, (c & 0x0fff), (d & 0x3fff) | 0x8000, b, d & 0xffff);
}

static void drawButton(uint16_t col, const char *label)
{
    tft->fillRect(0, CONTENT_TOP, SCR_W, CONTENT_BOTTOM - CONTENT_TOP, COL_BG);
    tft->drawCircle(BTN_CX, BTN_CY, BTN_R, col);
    tft->drawCircle(BTN_CX, BTN_CY, BTN_R - 1, col);
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(col, COL_BG);
    tft->drawString(label, BTN_CX, BTN_CY, 4);
}

static void drawScreen()
{
    tft->fillRect(0, 0, SCR_W, CONTENT_BOTTOM, COL_BG);
    statusbarDraw();
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_AMBER, COL_BG);
    tft->drawString("AI ASSISTANT", SCR_W / 2, STATUSBAR_H + 12, 2);
    switch (state) {
    case ST_IDLE:      drawButton(COL_GREEN,    "TALK");  break;
    case ST_RECORDING: drawButton(COL_AMBER,    "REC");   break;
    case ST_SENDING:   drawButton(COL_GREEN_DIM,"...");   break;
    case ST_SPEAKING:  drawButton(RGB565(0x33,0xcc,0xff), "SPK"); break;
    case ST_NOWIFI:
        tft->setTextColor(COL_AMBER, COL_BG);
        tft->drawString("no wifi", SCR_W / 2, BTN_CY, 4);
        break;
    }
}

static void assistantEnter()
{
    wifiAcquire();
    if (!wifiConnected()) {
        tft->fillRect(0, 0, SCR_W, CONTENT_BOTTOM, COL_BG);
        statusbarDraw();
        tft->setTextDatum(MC_DATUM);
        tft->setTextColor(COL_AMBER, COL_BG);
        tft->drawString("connecting...", SCR_W / 2, BTN_CY, 4);
        wifiAutoConnect(12000, nullptr);
    }
    if (!wifiConnected()) { state = ST_NOWIFI; drawScreen(); return; }

    genSession();
    if (!recBuf) recBuf = (int16_t *)ps_malloc((size_t)REC_CAP * sizeof(int16_t));
    state = ST_IDLE;
    recSamples = 0;
    drawScreen();
}

static void assistantExit()
{
    if (state == ST_RECORDING) micCaptureEnd();
    audioStop();
    if (recBuf) { free(recBuf); recBuf = nullptr; }
    state = ST_IDLE;
    wifiRelease();
}

static bool assistantKeepAwake() { return state != ST_IDLE && state != ST_NOWIFI; }

static void assistantIcon(TFT_eSPI &g, int cx, int cy, int r)
{
    g.fillRoundRect(cx - r / 4, cy - r / 2, r / 2, r * 3 / 4, r / 6, COL_GREEN);   // капсула мик
    g.drawFastHLine(cx - r / 3, cy + r / 2, r * 2 / 3, COL_GREEN);                 // подставка
    g.drawFastVLine(cx, cy + r / 4, r / 4, COL_GREEN);
}

const Program assistantProgram = {
    "AI Assistant", assistantEnter, nullptr, nullptr, assistantIcon,
    nullptr, 0, assistantKeepAwake, assistantExit
};
