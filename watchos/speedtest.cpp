#include "speedtest.h"
#include "config.h"
#include "hw.h"          // tft, watch
#include "theme.h"       // COL_*, RGB565
#include "statusbar.h"   // statusbarDraw
#include "input.h"       // InputEvent, inputPoll
#include "modal.h"       // modalBegin, modalPoll
#include "wifi.h"        // wifiConnected
#include "aiclient.h"    // aiApiKey
#include <Arduino.h>
#include <WiFi.h>        // WiFiClient
#include <string.h>

// ───────────────────── Геометрия ─────────────────────
static const int TITLE_Y  = STATUSBAR_H + 14;
static const int HOST_Y   = STATUSBAR_H + 32;
static const int CENTER_Y = (CONTENT_TOP + CONTENT_BOTTOM) / 2;   // ~114
static const int BTN_R    = 52;                                   // круг START
static const int REP_X0   = SCR_W / 2 - 60, REP_Y0 = 150, REP_W = 120, REP_H = 28;

// ───────────────────── Сеть ─────────────────────
static const int UP_BUF = 16384;
static uint8_t  *buf = nullptr;          // общий буфер чтения/записи (PSRAM)

typedef void (*ProgressCb)(float mbs);

// Строка запроса + общие заголовки (Host + X-API-Key, если ключ задан).
static void writeReqHead(WiFiClient &c, const char *method, const char *path)
{
    c.printf("%s %s HTTP/1.1\r\n", method, path);
    c.printf("Host: %s\r\n", SPEEDTEST_HOST);
    String key = aiApiKey();
    if (key.length()) { c.print("X-API-Key: "); c.print(key); c.print("\r\n"); }
}

// Пропустить заголовки ответа до пустой строки "\r\n\r\n". true — дошли до тела.
static bool skipHeaders(WiFiClient &c, uint32_t deadline)
{
    int st = 0;
    while (millis() < deadline) {
        while (c.available()) {
            int ch = c.read();
            if      (st == 0 && ch == '\r') st = 1;
            else if (st == 1 && ch == '\n') st = 2;
            else if (st == 2 && ch == '\r') st = 3;
            else if (st == 3 && ch == '\n') return true;
            else st = (ch == '\r') ? 1 : 0;
        }
        if (!c.connected() && !c.available()) return false;
        delay(1);
    }
    return false;
}

// Неблокирующая проверка прерывания боковой кнопкой во время фазы.
static bool abortPressed()
{
    int16_t tx, ty;
    InputEvent e = inputPoll(tx, ty);
    return e == EVT_BACK || e == EVT_CLICK;
}

// Ping: SPEEDTEST_PINGS проб, в результат — минимум (мс). -1 если ни одна не удалась.
static int stPing()
{
    int best = -1;
    for (int i = 0; i < SPEEDTEST_PINGS; i++) {
        WiFiClient c;
        uint32_t t = millis();
        if (!c.connect(SPEEDTEST_HOST, SPEEDTEST_PORT, 3000)) { c.stop(); continue; }
        writeReqHead(c, "GET", "/speedtest/ping");
        c.print("Connection: close\r\n\r\n");
        bool got = false;
        uint32_t dl = millis() + 3000;
        while (millis() < dl) {
            if (c.available()) { got = true; break; }
            if (!c.connected()) break;
            delay(1);
        }
        int ms = (int)(millis() - t);
        c.stop();
        if (got && (best < 0 || ms < best)) best = ms;
    }
    return best;
}

// Download: читает SPEEDTEST_SECS секунд, среднее МБ/с. <0 — ошибка/прерывание.
static float stDownload(ProgressCb cb, bool *aborted)
{
    *aborted = false;
    WiFiClient c;
    if (!c.connect(SPEEDTEST_HOST, SPEEDTEST_PORT, 8000)) return -1;
    writeReqHead(c, "GET", "/speedtest/down");
    c.print("Connection: close\r\n\r\n");
    if (!skipHeaders(c, millis() + 8000)) { c.stop(); return -1; }

    uint32_t t0 = millis(), tEnd = t0 + SPEEDTEST_SECS * 1000;
    uint32_t winT = t0, winBytes = 0;
    uint64_t total = 0;
    while (millis() < tEnd) {
        int n = c.read(buf, UP_BUF);
        if (n > 0) { total += n; winBytes += n; }
        else if (!c.connected()) break;
        uint32_t now = millis();
        if (now - winT >= 250) {
            if (cb) cb(winBytes / 1e6f / ((now - winT) / 1000.0f));
            winBytes = 0; winT = now;
            if (abortPressed()) { *aborted = true; break; }
        }
    }
    uint32_t dt = millis() - t0;
    c.stop();
    if (*aborted) return -1;
    return total / 1e6f / (dt / 1000.0f);
}

// Upload: шлёт chunked SPEEDTEST_SECS секунд, среднее МБ/с. <0 — ошибка/прерывание.
static float stUpload(ProgressCb cb, bool *aborted)
{
    *aborted = false;
    WiFiClient c;
    if (!c.connect(SPEEDTEST_HOST, SPEEDTEST_PORT, 8000)) return -1;
    writeReqHead(c, "POST", "/speedtest/up");
    c.print("Transfer-Encoding: chunked\r\n");
    c.print("Content-Type: application/octet-stream\r\n");
    c.print("Connection: close\r\n\r\n");

    char hdr[12];
    int hdrLen = snprintf(hdr, sizeof(hdr), "%X\r\n", UP_BUF);

    uint32_t t0 = millis(), tEnd = t0 + SPEEDTEST_SECS * 1000;
    uint32_t winT = t0, winBytes = 0;
    uint64_t total = 0;
    while (millis() < tEnd) {
        c.write((const uint8_t *)hdr, hdrLen);
        int w = c.write(buf, UP_BUF);
        c.write((const uint8_t *)"\r\n", 2);
        if (w > 0) { total += w; winBytes += w; }
        else if (!c.connected()) break;
        uint32_t now = millis();
        if (now - winT >= 250) {
            if (cb) cb(winBytes / 1e6f / ((now - winT) / 1000.0f));
            winBytes = 0; winT = now;
            if (abortPressed()) { *aborted = true; break; }
        }
    }
    uint32_t dt = millis() - t0;
    c.print("0\r\n\r\n");                       // завершающий чанк
    uint32_t rd = millis() + 1000;             // коротко вычитать ответ
    while (millis() < rd && c.connected()) { while (c.available()) c.read(); delay(5); }
    c.stop();
    if (*aborted) return -1;
    return total / 1e6f / (dt / 1000.0f);
}

// ───────────────────── UI ─────────────────────
static void drawExitBtn()
{
    int y0 = CONTENT_BOTTOM, h = SCR_H - CONTENT_BOTTOM;
    tft->fillRect(0, y0, SCR_W, h, COL_BG);
    tft->drawRoundRect(8, y0 + 4, SCR_W - 16, h - 8, 6, COL_FRAME);
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_AMBER, COL_BG);
    tft->drawString("Exit", SCR_W / 2, y0 + h / 2, 2);
}

static void drawHeader()
{
    tft->fillRect(0, 0, SCR_W, CONTENT_BOTTOM, COL_BG);
    statusbarDraw();
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_AMBER, COL_BG);
    tft->drawString("SPEEDTEST", SCR_W / 2, TITLE_Y, 2);
    tft->setTextColor(COL_GREEN_DIM, COL_BG);
    tft->drawString("host: " SPEEDTEST_HOST, SCR_W / 2, HOST_Y, 1);
}

static void drawReady(const char *note)
{
    drawHeader();
    tft->drawCircle(SCR_W / 2, CENTER_Y, BTN_R, COL_GREEN);
    tft->drawCircle(SCR_W / 2, CENTER_Y, BTN_R - 1, COL_GREEN);
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_GREEN, COL_BG);
    tft->drawString("START", SCR_W / 2, CENTER_Y, 4);
    if (note && note[0]) {
        tft->setTextColor(COL_AMBER, COL_BG);
        tft->drawString(note, SCR_W / 2, CENTER_Y + BTN_R + 16, 2);
    }
    drawExitBtn();
}

static bool inStartBtn(int16_t x, int16_t y)
{
    int dx = x - SCR_W / 2, dy = y - CENTER_Y;
    return dx * dx + dy * dy <= BTN_R * BTN_R;
}

static void drawPhase(const char *label)
{
    drawHeader();
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_GREEN_HI, COL_BG);
    tft->drawString(label, SCR_W / 2, STATUSBAR_H + 52, 4);
    drawExitBtn();
}

// Живая цифра (мгновенная скорость). Перерисовывает только полосу цифры.
static void liveNumber(float mbs)
{
    char b[16];
    snprintf(b, sizeof(b), "%.2f", mbs < 0 ? 0 : mbs);
    tft->fillRect(0, CENTER_Y - 26, SCR_W, 64, COL_BG);
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_GREEN, COL_BG);
    tft->drawString(b, SCR_W / 2, CENTER_Y, 6);            // 48px
    tft->setTextColor(COL_GREEN_DIM, COL_BG);
    tft->drawString("MB/s", SCR_W / 2, CENTER_Y + 34, 2);
}

static void drawResults(float down, float up, int ping)
{
    drawHeader();
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_AMBER, COL_BG);
    tft->drawString("RESULTS", SCR_W / 2, STATUSBAR_H + 26, 2);

    char b[40];
    int y = STATUSBAR_H + 56, dy = 30;
    tft->setTextDatum(ML_DATUM);

    tft->setTextColor(COL_GREEN, COL_BG);
    snprintf(b, sizeof(b), "Down %.2f MB/s", down);
    tft->drawString(b, 18, y, 2);
    tft->setTextColor(COL_GREEN_DIM, COL_BG);
    snprintf(b, sizeof(b), "%.1f Mbps", down * 8);
    tft->drawString(b, 168, y, 1);
    y += dy;

    tft->setTextColor(COL_GREEN, COL_BG);
    snprintf(b, sizeof(b), "Up   %.2f MB/s", up);
    tft->drawString(b, 18, y, 2);
    tft->setTextColor(COL_GREEN_DIM, COL_BG);
    snprintf(b, sizeof(b), "%.1f Mbps", up * 8);
    tft->drawString(b, 168, y, 1);
    y += dy;

    tft->setTextColor(COL_GREEN, COL_BG);
    snprintf(b, sizeof(b), "Ping %d ms", ping);
    tft->drawString(b, 18, y, 2);

    tft->drawRoundRect(REP_X0, REP_Y0, REP_W, REP_H, 6, COL_GREEN);
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_GREEN_HI, COL_BG);
    tft->drawString("Repeat", SCR_W / 2, REP_Y0 + REP_H / 2, 2);

    drawExitBtn();
}

static bool inRepeatBtn(int16_t x, int16_t y)
{
    return x >= REP_X0 && x <= REP_X0 + REP_W && y >= REP_Y0 && y <= REP_Y0 + REP_H;
}

// Прогон ping→down→up. Возврат: true — успех (результаты в out-параметрах);
// false — ошибка/прерывание (note заполнен текстом для READY).
static bool runSequence(float &down, float &up, int &ping, const char **note)
{
    drawHeader();
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_AMBER, COL_BG);
    tft->drawString("ping...", SCR_W / 2, CENTER_Y, 4);
    drawExitBtn();
    ping = stPing();

    bool ab = false;
    drawPhase("DOWN"); liveNumber(0);
    down = stDownload(liveNumber, &ab);
    if (ab)        { *note = "aborted";           return false; }
    if (down < 0)  { *note = "server unreachable"; return false; }

    drawPhase("UP"); liveNumber(0);
    up = stUpload(liveNumber, &ab);
    if (ab)        { *note = "aborted";           return false; }
    if (up < 0)    { *note = "server unreachable"; return false; }

    return true;
}

// ───────────────────── Точка входа ─────────────────────
void speedtestRun()
{
    if (!buf) buf = (uint8_t *)ps_malloc(UP_BUF);
    if (buf)  memset(buf, 0xA5, UP_BUF);

    bool haveResults = false;
    float rDown = 0, rUp = 0; int rPing = 0;
    const char *note = wifiConnected() ? "" : "no wifi";

    modalBegin();
    drawReady(note);

    for (;;) {
        int16_t x, y; InputEvent e = modalPoll(x, y);
        if (e == EVT_BACK) break;
        if (e != EVT_TAP) continue;

        if (y >= CONTENT_BOTTOM) break;                       // нижний Exit
        bool start = (!haveResults && inStartBtn(x, y)) ||
                     ( haveResults && inRepeatBtn(x, y));
        if (!start) continue;

        if (!wifiConnected()) { note = "no wifi"; haveResults = false; drawReady(note); continue; }

        if (runSequence(rDown, rUp, rPing, &note)) {
            haveResults = true;
            drawResults(rDown, rUp, rPing);
        } else {
            haveResults = false;
            drawReady(note);
        }
    }

    if (buf) { free(buf); buf = nullptr; }
}
