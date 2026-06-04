#include "prog_radio.h"
#include "../../hw.h"
#include "../../statusbar.h"
#include "../../theme.h"
#include "../../wifi.h"
#include "../../sound.h"
#include "../../stations.h"
#include "../../webedit.h"
#include <string.h>
#include <math.h>

// ─────────────────────────── Раскладка ───────────────────────────
static const int TITLE_Y      = STATUSBAR_H + 12;  // "station N/M" (оба экрана)
static const int MARQUEE_Y    = STATUSBAR_H + 38;  // бегущая строка (главный)
static const int TRANSPORT_CY = 118;               // prev / play-pause / next (главный)
static const int VOL_Y        = 170;               // громкость (главный)
static const int PREV_CX = 42, PP_CX = 120, NEXT_CX = 198;

// Страница спектроанализатора (экран 2):
static const int SPEC_Y0      = STATUSBAR_H + 26;  // верх полос (50)
static const int SPEC_H       = 116;               // высота полос → база 166
static const int LABEL_Y      = SPEC_Y0 + SPEC_H + 10;  // подписи частот под полосами

static int      screen = 0;                        // 0 — плеер, 1 — спектр

// Состояние плеера (объявлено до блока спектра — он ссылается на paused/ok).
static bool     ok        = false;                 // подключились и играем
static int      cur       = 0;                     // индекс станции
static bool     paused    = false;
static char     mqShown[256] = "";
static int      mqOffset  = 0;
static uint32_t mqLast    = 0;

static void drawTitle();                           // forward (нужна странице спектра)

// ─────────────────── Спектроанализатор (настоящий FFT) ───────────────────
#define SP_BARS 8         // 8 полос — метки частот (X.YK) влезают в один ряд
#define FN      SPEC_N            // длина FFT (= размер буфера захвата)
static float    sp_re[FN], sp_im[FN], sp_win[FN];
static float    sp_val[SP_BARS], sp_peak[SP_BARS];
static int      sp_lo[SP_BARS], sp_hi[SP_BARS];     // диапазоны бинов на полосу (лог-шкала)
static float    sp_ref = 4000.0f;                   // авто-усиление (плавающий максимум)
static bool     sp_init = false;
static uint32_t sp_last = 0;
static int      sp_drawn[SP_BARS];                  // текущая отрисованная высота полосы (для дельты)
static int      sp_pkY[SP_BARS];                    // y отрисованной шапочки (-1 = нет)

static void spectrumInit()
{
    for (int i = 0; i < FN; i++) sp_win[i] = 0.5f - 0.5f * cosf(2.0f * (float)M_PI * i / (FN - 1));  // Hann
    // 12 полос логарифмически по бинам 1..FN/2-1
    int kmax = FN / 2 - 1;
    for (int b = 0; b < SP_BARS; b++) {
        sp_lo[b] = 1 + (int)(powf((float)kmax, (float)b / SP_BARS));
        sp_hi[b] = 1 + (int)(powf((float)kmax, (float)(b + 1) / SP_BARS));
        if (sp_hi[b] <= sp_lo[b]) sp_hi[b] = sp_lo[b] + 1;
        if (sp_hi[b] > kmax + 1) sp_hi[b] = kmax + 1;
        sp_val[b] = sp_peak[b] = 0;
    }
    sp_init = true;
}

// Итеративный radix-2 FFT (комплексный, in-place).
static void fft(float *re, float *im)
{
    for (int i = 1, j = 0; i < FN; i++) {            // bit-reversal
        int bit = FN >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { float t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; }
    }
    for (int len = 2; len <= FN; len <<= 1) {
        float ang = -2.0f * (float)M_PI / len;
        float wr = cosf(ang), wi = sinf(ang);
        for (int i = 0; i < FN; i += len) {
            float cr = 1, ci = 0;
            for (int k = 0; k < len / 2; k++) {
                float a = re[i + k + len / 2], b = im[i + k + len / 2];
                float vr = a * cr - b * ci, vi = a * ci + b * cr;
                float ur = re[i + k], ui = im[i + k];
                re[i + k] = ur + vr; im[i + k] = ui + vi;
                re[i + k + len / 2] = ur - vr; im[i + k + len / 2] = ui - vi;
                float ncr = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = ncr;
            }
        }
    }
}

static void drawSpectrum(bool active)
{
    if (!sp_init) spectrumInit();
    const int baseY = SPEC_Y0 + SPEC_H;                // абсолютные координаты экрана
    const int margin = 8, gap = 3;
    const int barW = (SCR_W - 2 * margin - (SP_BARS - 1) * gap) / SP_BARS;
    const int TH = SPEC_H * 6 / 10;                    // высота смены цвета green→amber

    if (active) {
        int16_t s[FN];
        audioSpectrumCopy(s);
        for (int i = 0; i < FN; i++) { sp_re[i] = s[i] * sp_win[i]; sp_im[i] = 0; }
        fft(sp_re, sp_im);
        // Сырые энергии полос + текущий максимум для авто-усиления.
        float raw[SP_BARS], fmax = 1.0f;
        for (int b = 0; b < SP_BARS; b++) {
            float sum = 0;
            for (int k = sp_lo[b]; k < sp_hi[b]; k++)
                sum += sqrtf(sp_re[k] * sp_re[k] + sp_im[k] * sp_im[k]);
            raw[b] = sum;
            if (sum > fmax) fmax = sum;
        }
        // AGC: быстрый подъём опорного уровня, медленный спад → шкала «дышит» под музыку.
        if (fmax > sp_ref) sp_ref += (fmax - sp_ref) * 0.30f;
        else               sp_ref += (fmax - sp_ref) * 0.03f;
        if (sp_ref < 3000.0f) sp_ref = 3000.0f;        // пол — чтобы тишина не раздувалась
        for (int b = 0; b < SP_BARS; b++) {
            float ratio = raw[b] / sp_ref; if (ratio > 1) ratio = 1; if (ratio < 0) ratio = 0;
            float h = SPEC_H * sqrtf(ratio);           // sqrt — приподнять средние полосы
            if (h >= sp_val[b]) sp_val[b] = h;         // мгновенный подъём
            else { sp_val[b] -= 4.0f; if (sp_val[b] < 0) sp_val[b] = 0; }   // гравитация
        }
    } else {
        for (int b = 0; b < SP_BARS; b++) { sp_val[b] -= 5.0f; if (sp_val[b] < 0) sp_val[b] = 0; }
        sp_ref += (3000.0f - sp_ref) * 0.05f; if (sp_ref < 3000.0f) sp_ref = 3000.0f;
    }

    // Dirty-rect: рисуем прямо в экран, трогая только дельту высоты каждой полосы и шапочку.
    for (int b = 0; b < SP_BARS; b++) {
        if (sp_val[b] > sp_peak[b]) sp_peak[b] = sp_val[b];    // пиковая «шапочка»
        else { sp_peak[b] -= 1.5f; if (sp_peak[b] < sp_val[b]) sp_peak[b] = sp_val[b]; }

        int x = margin + b * (barW + gap);
        int newH = (int)sp_val[b], oldH = sp_drawn[b];
        if (newH > oldH) {                                     // полоса выросла — дорисовать прирост
            int gTop = newH < TH ? newH : TH;                  // зелёная часть [oldH..min(newH,TH)]
            if (gTop > oldH) tft->fillRect(x, baseY - gTop, barW, gTop - oldH, COL_GREEN);
            int aBot = oldH > TH ? oldH : TH;                  // янтарная часть [max(oldH,TH)..newH]
            if (newH > aBot) tft->fillRect(x, baseY - newH, barW, newH - aBot, COL_AMBER);
        } else if (newH < oldH) {                              // упала — стереть верх
            tft->fillRect(x, baseY - oldH, barW, oldH - newH, COL_BG);
        }
        sp_drawn[b] = newH;

        int newPk = (int)sp_peak[b];
        int pkY = baseY - newPk - 2;                           // 2px шапочка
        if (sp_pkY[b] != pkY) {                                // стереть старую, если она выше полосы
            if (sp_pkY[b] >= 0 && sp_pkY[b] + 2 <= baseY - newH)
                tft->fillRect(x, sp_pkY[b], barW, 2, COL_BG);
            sp_pkY[b] = pkY;
        }
        if (newPk > 0) tft->fillRect(x, pkY, barW, 2, COL_GREEN_HI);
    }
}

// Подписи центральных частот под каждой полосой (кГц). Рисуются один раз при входе на экран.
static void drawSpecLabels()
{
    if (!sp_init) spectrumInit();
    const int margin = 8, gap = 3;
    const int barW = (SCR_W - 2 * margin - (SP_BARS - 1) * gap) / SP_BARS;
    uint32_t sr = audioSampleRate(); if (!sr) sr = 44100;
    float effSR = sr / 4.0f;                            // в библиотеке отвод каждого 4-го сэмпла
    tft->fillRect(0, LABEL_Y - 6, SCR_W, 14, COL_BG);
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_GREEN_DIM, COL_BG);
    char b[8];
    for (int i = 0; i < SP_BARS; i++) {
        int center = (sp_lo[i] + sp_hi[i]) / 2;
        int freq = (int)(center * effSR / FN);          // Гц
        if (freq < 1000) snprintf(b, sizeof(b), "%d", freq);                       // Гц
        else             snprintf(b, sizeof(b), "%d.%dK", freq / 1000, (freq % 1000) / 100);  // кГц
        int xc = margin + i * (barW + gap) + barW / 2;
        tft->drawString(b, xc, LABEL_Y, 1);
    }
}

static void drawSpectrumScreen()
{
    tft->fillRect(0, 0, SCR_W, CONTENT_BOTTOM, COL_BG);
    statusbarDraw();
    drawTitle();
    drawSpecLabels();
    for (int b = 0; b < SP_BARS; b++) { sp_drawn[b] = 0; sp_pkY[b] = -1; }   // экран очищен → дельта с нуля
    drawSpectrum(audioIsPlaying() && !paused);
    drawPageDots(3, screen, CONTENT_BOTTOM - 8);
}

static const int MQ_SIZE = 2;                       // масштаб шрифта бегущей строки (font1)
static const int MQ_WIN  = 19;                      // символов в окне (240 / (6*2))
static const int MQ_GAP  = 4;                       // пробелы между повторами
static const uint32_t MQ_STEP_MS = 220;

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
    drawPageDots(3, screen, CONTENT_BOTTOM - 8);
}

// ─────────────────── Экран 3: веб-сервер редактора станций ───────────────────
static const int SRV_BX = 40, SRV_BY = 96, SRV_BW = 160, SRV_BH = 50;

static void drawServerScreen()
{
    tft->fillRect(0, 0, SCR_W, CONTENT_BOTTOM, COL_BG);
    statusbarDraw();
    char b[40];
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_AMBER, COL_BG);
    tft->drawString("STATION EDITOR", SCR_W / 2, TITLE_Y, 2);

    bool run = webEditRunning();
    // Кнопка START/STOP
    tft->fillRoundRect(SRV_BX, SRV_BY, SRV_BW, SRV_BH, 10, COL_BG);
    tft->drawRoundRect(SRV_BX, SRV_BY, SRV_BW, SRV_BH, 10, run ? COL_AMBER : COL_GREEN);
    tft->setTextColor(run ? COL_AMBER : COL_GREEN, COL_BG);
    tft->drawString(run ? "STOP SERVER" : "START SERVER", SCR_W / 2, SRV_BY + SRV_BH / 2, 2);

    // Статус / адрес
    if (run) {
        tft->setTextColor(COL_GREEN_DIM, COL_BG);
        tft->drawString("open in browser:", SCR_W / 2, SRV_BY + SRV_BH + 22, 1);
        tft->setTextColor(COL_GREEN_HI, COL_BG);
        snprintf(b, sizeof(b), "http://%s", webEditIP().c_str());
        tft->drawString(b, SCR_W / 2, SRV_BY + SRV_BH + 40, 2);
        tft->setTextColor(COL_GREEN_DIM, COL_BG);
        tft->drawString("same Wi-Fi network", SCR_W / 2, SRV_BY + SRV_BH + 58, 1);
    } else {
        tft->setTextColor(COL_GREEN_DIM, COL_BG);
        tft->drawString("edit stations from", SCR_W / 2, SRV_BY + SRV_BH + 26, 1);
        tft->drawString("phone / PC", SCR_W / 2, SRV_BY + SRV_BH + 40, 1);
    }
    drawPageDots(3, screen, CONTENT_BOTTOM - 8);
}

static void drawScreen()
{
    if      (screen == 0) drawPlayer();
    else if (screen == 1) drawSpectrumScreen();
    else                  drawServerScreen();
}

static void toggleServer()
{
    if (webEditRunning()) webEditStop();
    else                  webEditStart();
    drawServerScreen();
}

// ─────────────────── Действия ───────────────────
static void playStation(int i)
{
    cur = (i + stationsCount()) % stationsCount();
    paused = false;
    mqShown[0] = 0;
    mqOffset = 0;
    audioStart(stationsUrl(cur));
    if (screen == 0) { drawTitle(); drawTransport(); }
    else             { drawTitle(); drawSpecLabels(); }   // частота дискретизации могла смениться
}

static void togglePlay()
{
    if (paused) { audioStart(stationsUrl(cur)); paused = false; }
    else        { audioStop(); paused = true; }
    if (screen == 0) drawTransport();
}

static void volStep(int d)
{
    audioSetVolume(audioVolume() + d);
    if (screen == 0) drawVolume();
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
    audioSetVolume(18);
    screen = 0;
    playStation(cur);          // продолжаем с текущей станции
    drawScreen();
}

static void radioExit()
{
    webEditStop();             // сервер обязан гаснуть при выходе из радио
    audioStop();
    paused = false;
    ok = false;
    wifiRelease();
}

static void radioTick()
{
    if (!ok) return;

    if (webEditRunning()) webEditTick();           // обслуживать сервер на любом экране, пока поднят

    if (screen == 0) {
        // Бегущая строка (только на главном экране).
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
    } else if (screen == 1) {
        // Спектроанализатор ~30 fps (только на странице спектра).
        if (millis() - sp_last > 33) {
            sp_last = millis();
            drawSpectrum(audioIsPlaying() && !paused);
        }
    }
    // screen == 2 (сервер) — статичный экран, поллинг сервера выше.
}

static void radioEvent(InputEvent e, int16_t x, int16_t y)
{
    if (!ok) return;
    switch (e) {
    case EVT_LEFT:  if (screen < 2) { screen++; drawScreen(); } break;     // листать вправо
    case EVT_RIGHT: if (screen > 0) { screen--; drawScreen(); } break;     // листать влево
    case EVT_CLICK: togglePlay(); break;                                   // пауза (любой экран)
    case EVT_UP:    if (screen == 0) volStep(+1); break;                   // громче (главный)
    case EVT_DOWN:  if (screen == 0) volStep(-1); break;
    case EVT_TAP:
        if (screen == 0) {
            if (y >= TRANSPORT_CY - 22 && y <= TRANSPORT_CY + 22) {
                if (x < 80)       playStation(cur - 1);
                else if (x < 160) togglePlay();
                else              playStation(cur + 1);
            } else if (y >= VOL_Y - 18 && y <= VOL_Y + 18) {
                if (x < 44)            volStep(-1);
                else if (x > SCR_W-44) volStep(+1);
            }
        } else if (screen == 2) {                                          // тап по кнопке сервера
            if (x >= SRV_BX && x <= SRV_BX + SRV_BW && y >= SRV_BY && y <= SRV_BY + SRV_BH)
                toggleServer();
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
