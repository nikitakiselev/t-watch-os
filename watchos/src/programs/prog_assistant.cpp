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

// Состояние воспроизведения ответа (борьба с гонкой audioIsPlaying):
// audioStart() лишь ставит флаг; аудио-задача поднимет поток позже, поэтому
// сразу после старта playing ещё false. Ждём, пока реально заиграет.
static uint32_t speakStartMs = 0;
static bool     playStarted  = false;
static const uint32_t SPEAK_CONNECT_TIMEOUT_MS = 10000;   // не заиграло за 10 c → сдаёмся

// Кнопка-микрофон в центре экрана.
static const int BTN_CX = SCR_W / 2, BTN_CY = 116, BTN_R = 66;
static const int INFO_Y = CONTENT_BOTTOM - 14;     // строка под кнопкой (громкость / секунды)

// Громкость воспроизведения ответа (0..21, как у радио). Меняется кнопками Vol-/Vol+.
static int aiVolume = 16;

static void drawVolume()
{
    const int bw = 130, bx = (SCR_W - bw) / 2, bh = 8;
    tft->fillRect(0, INFO_Y - 8, SCR_W, 18, COL_BG);
    tft->setTextDatum(MR_DATUM);
    tft->setTextColor(COL_GREEN_DIM, COL_BG);
    tft->drawString("VOL", bx - 6, INFO_Y, 1);
    tft->drawRect(bx, INFO_Y - bh / 2, bw, bh, COL_FRAME);
    int fill = (bw - 2) * aiVolume / 21;
    if (fill > 0) tft->fillRect(bx + 1, INFO_Y - bh / 2 + 1, fill, bh - 2, COL_GREEN);
}

static void volStep(int d)
{
    aiVolume += d;
    if (aiVolume < 0)  aiVolume = 0;
    if (aiVolume > 21) aiVolume = 21;
    audioSetVolume(aiVolume);        // применяется и к текущему воспроизведению
    drawVolume();
}
static void volUp()   { volStep(+2); }
static void volDown() { volStep(-2); }

static void genSession()
{
    uint32_t a = esp_random(), b = esp_random(), c = esp_random(), d = esp_random();
    snprintf(sessionId, sizeof(sessionId),
             "%08x-%04x-4%03x-%04x-%08x%04x",
             a, b & 0xffff, (c & 0x0fff), (d & 0x3fff) | 0x8000, b, d & 0xffff);
}

static void iconMic(int cx, int cy, uint16_t c)
{
    tft->fillRoundRect(cx - 9, cy - 22, 18, 30, 9, c);     // капсула
    tft->drawFastHLine(cx - 13, cy + 14, 26, c);           // подставка
    tft->drawFastVLine(cx, cy + 8, 6, c);
}
static void iconDots(int cx, int cy, uint16_t c)
{
    tft->fillCircle(cx - 16, cy, 4, c);
    tft->fillCircle(cx,      cy, 4, c);
    tft->fillCircle(cx + 16, cy, 4, c);
}
static void iconSpeaker(int cx, int cy, uint16_t c)
{
    tft->fillRect(cx - 16, cy - 7, 10, 14, c);             // корпус
    tft->fillTriangle(cx - 6, cy - 14, cx - 6, cy + 14, cx + 6, cy, c);
    tft->drawCircle(cx + 12, cy, 7, c);                    // звуковая волна
}

static void drawButton(uint16_t col)
{
    tft->fillRect(0, CONTENT_TOP, SCR_W, CONTENT_BOTTOM - CONTENT_TOP, COL_BG);
    tft->drawCircle(BTN_CX, BTN_CY, BTN_R, col);
    tft->drawCircle(BTN_CX, BTN_CY, BTN_R - 1, col);
    switch (state) {
    case ST_IDLE:      iconMic(BTN_CX, BTN_CY, col);     break;
    case ST_RECORDING: iconMic(BTN_CX, BTN_CY, col);     break;
    case ST_SENDING:   iconDots(BTN_CX, BTN_CY, col);    break;
    case ST_SPEAKING:  iconSpeaker(BTN_CX, BTN_CY, col); break;
    default: break;
    }
}

static void drawScreen()
{
    tft->fillRect(0, 0, SCR_W, CONTENT_BOTTOM, COL_BG);
    statusbarDraw();
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_AMBER, COL_BG);
    tft->drawString("AI ASSISTANT", SCR_W / 2, STATUSBAR_H + 12, 2);
    switch (state) {
    case ST_IDLE:      drawButton(COL_GREEN);                  break;
    case ST_RECORDING: drawButton(COL_AMBER);                  break;
    case ST_SENDING:   drawButton(COL_GREEN_DIM);              break;
    case ST_SPEAKING:  drawButton(RGB565(0x33, 0xcc, 0xff));   break;
    case ST_NOWIFI:
        tft->setTextDatum(MC_DATUM);
        tft->setTextColor(COL_AMBER, COL_BG);
        tft->drawString("no wifi", SCR_W / 2, BTN_CY, 4);
        break;
    }
    if (state != ST_RECORDING && state != ST_NOWIFI) drawVolume();   // в записи тут счётчик секунд
}

// onEnter — только при настоящем входе (kernelOpen/возврат), НЕ на пробуждении:
// пробуждение идёт через onResume (ядро). Поэтому здесь честно один acquire/genSession.
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

// Пробуждение из сна: только перерисовать текущий экран — ресурсы (Wi-Fi, буфер,
// сессия) живы, повторно их не трогаем. Wi-Fi после сна поднимет авто-reconnect ОС.
static void assistantResume()
{
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

// Идёт ли сейчас палец по экрану (для hold-to-talk опрашиваем тач напрямую).
static bool touchDown()
{
    int16_t x, y;
    return watch->getTouch(x, y);
}

static void startRecording()
{
    if (!recBuf) { soundBeep(400, 120); return; }   // PSRAM не выделился — записывать некуда
    if (!micCaptureBegin()) { soundBeep(400, 120); return; }
    recSamples = 0;
    state = ST_RECORDING;
    drawScreen();
}

static void stopRecording()
{
    micCaptureEnd();

    if (recSamples < SR * 3 / 10) {        // < 0.3 c — игнор
        state = ST_IDLE;
        drawScreen();
        return;
    }

    state = ST_SENDING;                    // рисуем «...» ДО блокирующего POST
    drawScreen();

    // После light-sleep Wi-Fi-модем гаснет и линк отваливается (значок при этом ещё
    // «горит»). Перед отправкой убеждаемся, что связь жива — иначе переподключаемся.
    if (!wifiConnected()) wifiAutoConnect(8000, nullptr);
    if (!wifiConnected()) {
        soundBeep(300, 120);
        state = ST_IDLE;
        drawScreen();
        return;
    }

    AiResult res = aiSend(sessionId, recBuf, recSamples);   // блокирует ~5-10 c

    if (res.code == 200 && res.url.length() > 0) {
        audioSetVolume(aiVolume);
        audioStart(res.url.c_str());       // connecttohost (http/https) — асинхронно
        speakStartMs = millis();
        playStarted  = false;
        state = ST_SPEAKING;
        drawScreen();
    } else {
        soundBeep(res.code == 204 ? 600 : 300, 120);   // 204 — не расслышал; иначе ошибка
        state = ST_IDLE;
        drawScreen();
    }
}

static void assistantTick()
{
    if (state == ST_IDLE && touchDown()) {
        startRecording();
        return;
    }
    if (state == ST_RECORDING) {
        if (recSamples < REC_CAP) {
            int got = micCaptureRead(recBuf + recSamples, REC_CAP - recSamples);
            recSamples += got;
        }
        // секунды записи под кнопкой (точечно, без мерцания)
        static int lastSec = -1;
        int sec = recSamples / SR;
        if (sec != lastSec) {
            lastSec = sec;
            char b[8];
            snprintf(b, sizeof(b), "%ds", sec);
            tft->fillRect(0, INFO_Y - 9, SCR_W, 20, COL_BG);   // та же строка, что и бар громкости
            tft->setTextDatum(MC_DATUM);
            tft->setTextColor(COL_AMBER, COL_BG);
            tft->drawString(b, BTN_CX, INFO_Y, 2);
        }
        if (!touchDown() || recSamples >= REC_CAP) {
            stopRecording();
        }
    }
    if (state == ST_SPEAKING) {
        if (audioIsPlaying()) {
            playStarted = true;                          // поток реально пошёл
        } else if (playStarted || millis() - speakStartMs > SPEAK_CONNECT_TIMEOUT_MS) {
            state = ST_IDLE;                             // доиграло, либо так и не стартовало за таймаут
            drawScreen();
        }
    }
}

// Нижняя панель: Exit (выход с очисткой) + регулировка громкости ответа.
static const NavButton assistantNav[] = {
    { "Exit", kernelBack },
    { "Vol-", volDown },
    { "Vol+", volUp },
};

const Program assistantProgram = {
    "AI Assistant", assistantEnter, assistantTick, nullptr, assistantIcon,
    assistantNav, 3, assistantKeepAwake, assistantExit, assistantResume
};
