#include "prog_radio.h"
#include "../../hw.h"
#include "../../statusbar.h"
#include "../../theme.h"
#include "../../wifi.h"
#include "../../sound.h"
#include "../../stations.h"
#include "../../webedit.h"
#include "vu_gen.h"
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

// ─────────────────── VU-метр (стрелочный, поверх ретро-подложки) ───────────────────
// Подложка VU_BG (240×130 RGB565) лежит во всю ширину, верх — там же, где полосы спектра.
// Ось стрелки задана в координатах картинки (нижний центр); стрелку рисуем поверх и
// стираем восстановлением фона из того же VU_BG (одинаковый путь drawPixel — без рассинхрона).
static const int   VU_X0   = (SCR_W - VU_W) / 2;       // = 0 (240 во всю ширину)
static const int   VU_Y0   = SPEC_Y0;                  // = 50, как верх полос спектра
static const float VU_PIVX = 120.0f, VU_PIVY = 111.0f; // ось стрелки в координатах VU_BG
static const float VU_LEN  = 80.0f;                    // длина стрелки, px
static const float VU_HALF = 52.0f * (float)M_PI / 180.0f;  // размах ±52° от вертикали
static const float VU_REF  = 9000.0f;                  // опорный RMS «full-scale» (чувствительность)
static const uint16_t COL_NEEDLE = 0x0000;             // чёрная стрелка — классика на янтаре

static int   vizMode    = 0;        // подрежим экрана спектра: 0 — спектр, 1 — VU-метр
static float vuLevel   = 0;        // сглаженный уровень 0..1 (баллистика VU)
static float vuDrawnAng = 999.0f;  // последний отрисованный угол (999 = стрелки на экране нет)

// Растеризует стрелку под углом ang (рад, от вертикали вверх; + вправо) в массив
// экранных пикселей xs/ys. Возвращает их число. Толщина ~3px.
#define VU_MAXPX 320
static int vuRaster(float ang, int16_t *xs, int16_t *ys)
{
    float dx = sinf(ang), dy = -cosf(ang);             // направление от оси «вверх»
    float px = -dy, py = dx;                           // перпендикуляр (для толщины)
    int n = 0;
    for (float t = 5.0f; t <= VU_LEN; t += 1.0f) {     // от 5px (за ступицей) до конца
        float cx = VU_PIVX + dx * t, cy = VU_PIVY + dy * t;
        for (int o = -1; o <= 1; o++) {
            int ix = (int)lroundf(cx + px * o);
            int iy = (int)lroundf(cy + py * o);
            if (ix < 0 || ix >= VU_W || iy < 0 || iy >= VU_H) continue;
            if (n >= VU_MAXPX) return n;
            xs[n] = VU_X0 + ix; ys[n++] = VU_Y0 + iy;
        }
    }
    return n;
}

static int16_t vu_ox[VU_MAXPX], vu_oy[VU_MAXPX];       // пиксели стрелки прошлого кадра
static int     vu_on = 0;

// Дифференциальная перерисовка: новую стрелку рисуем зелёным (общие со старой пиксели
// остаются гореть непрерывно — не мерцают), затем стираем только пиксели старой, которых
// нет в новой (восстановление фона из VU_BG). Колор-путь — тот же drawPixel.
static void vuRenderNeedle(float ang)
{
    int16_t nx[VU_MAXPX], ny[VU_MAXPX];
    int nn = vuRaster(ang, nx, ny);
    for (int i = 0; i < nn; i++) tft->drawPixel(nx[i], ny[i], COL_NEEDLE);   // новая поверх
    for (int i = 0; i < vu_on; i++) {                                        // старая \ новая → фон
        bool keep = false;
        for (int j = 0; j < nn; j++) if (vu_ox[i] == nx[j] && vu_oy[i] == ny[j]) { keep = true; break; }
        if (!keep) {
            int bx = vu_ox[i] - VU_X0, by = vu_oy[i] - VU_Y0;
            tft->drawPixel(vu_ox[i], vu_oy[i], VU_BG[by * VU_W + bx]);
        }
    }
    for (int i = 0; i < nn; i++) { vu_ox[i] = nx[i]; vu_oy[i] = ny[i]; }
    vu_on = nn;
}

// Пересчёт уровня (RMS последних SPEC_N сэмплов) + перерисовка стрелки с баллистикой VU.
static void vuUpdate(bool active)
{
    float target = 0;
    if (active) {
        int16_t s[SPEC_N];
        audioSpectrumCopy(s);
        float sum = 0;
        for (int i = 0; i < SPEC_N; i++) sum += (float)s[i] * (float)s[i];
        float rms = sqrtf(sum / SPEC_N);
        target = rms / VU_REF;
        if (target > 1) target = 1;
        target = sqrtf(target);                        // приподнять тихие уровни
    }
    // Баллистика VU, независимая от fps: коэффициент пересчитывается под реальный dt
    // (опорные 0.35 подъём / 0.12 спад заданы для кадра ~40 мс — feel не зависит от частоты).
    static uint32_t vu_t = 0;
    uint32_t now = millis();
    float dt = vu_t ? (float)(now - vu_t) : 40.0f;
    if (dt > 200.0f) dt = 200.0f;                      // защита от больших пауз (вход на экран)
    vu_t = now;
    float k0 = (target > vuLevel) ? 0.35f : 0.12f;
    float k = 1.0f - powf(1.0f - k0, dt / 40.0f);
    vuLevel += (target - vuLevel) * k;

    float ang = -VU_HALF + vuLevel * (2.0f * VU_HALF); // -VU_HALF (тишина) .. +VU_HALF (пик)
    if (vuDrawnAng < 100.0f && fabsf(ang - vuDrawnAng) < 0.004f) return;   // < ~0.2° — не дёргать
    vuRenderNeedle(ang);
    vuDrawnAng = ang;
}

static void drawVuScreen()
{
    tft->fillRect(0, 0, SCR_W, CONTENT_BOTTOM, COL_BG);
    statusbarDraw();
    drawTitle();
    // Подложка одним блочным pushImage (быстро). swapBytes=true → байты на шине те же,
    // что у drawPixel — фон совпадает по цвету со стиранием стрелки (drawPixel из VU_BG).
    bool sw = tft->getSwapBytes();
    tft->setSwapBytes(true);
    tft->pushImage(VU_X0, VU_Y0, VU_W, VU_H, VU_BG);
    tft->setSwapBytes(sw);
    tft->fillCircle(VU_X0 + (int)VU_PIVX, VU_Y0 + (int)VU_PIVY, 4, COL_NEEDLE);  // ступица
    vuDrawnAng = 999.0f; vu_on = 0;                    // фон свежий — стрелки ещё нет
    vuUpdate(audioIsPlaying() && !paused);
    drawPageDots(3, screen, CONTENT_BOTTOM - 8);
}

// ─────────────────── Осциллограф (форма волны, временная область) ───────────────────
// Рисуем 64 сэмпла моно как зелёный луч. Перерисовка дельтой: стираем прошлый луч по его же
// точкам (фон + базовая линия), затем рисуем новый — без полной очистки, без мигания.
static const int   SCOPE_Y0  = SPEC_Y0;                 // 50
static const int   SCOPE_H   = 130;                     // поле осциллографа
static const int   SCOPE_MID = SCOPE_Y0 + SCOPE_H / 2;  // ось (115)
static const int   SCOPE_AMP = SCOPE_H / 2 - 6;         // макс. отклонение в px
static float       sc_ref    = 4000.0f;                 // авто-усиление (плавающий пик)
static int         sc_x[SPEC_N];                        // x-координаты сэмплов (фикс.)
static int         sc_y[SPEC_N];                        // прошлые y (для стирания)
static bool        sc_have   = false;                   // на экране уже есть луч

static void scopeBaseline() { tft->drawFastHLine(0, SCOPE_MID, SCR_W, COL_GREEN_DIM); }

static void scopeUpdate(bool active)
{
    int16_t s[SPEC_N];
    if (active) audioSpectrumCopy(s);
    else        for (int i = 0; i < SPEC_N; i++) s[i] = 0;

    float peak = 1.0f;
    for (int i = 0; i < SPEC_N; i++) { float a = fabsf((float)s[i]); if (a > peak) peak = a; }
    if (peak > sc_ref) sc_ref += (peak - sc_ref) * 0.30f;   // быстрый подъём
    else               sc_ref += (peak - sc_ref) * 0.05f;   // плавный спад
    if (sc_ref < 1500.0f) sc_ref = 1500.0f;                 // пол — тишина не раздувается

    int ny[SPEC_N];
    for (int i = 0; i < SPEC_N; i++) {
        int d = (int)((float)s[i] / sc_ref * SCOPE_AMP);
        if (d >  SCOPE_AMP) d =  SCOPE_AMP;
        if (d < -SCOPE_AMP) d = -SCOPE_AMP;
        ny[i] = SCOPE_MID - d;
    }
    if (sc_have)                                            // стереть прошлый луч
        for (int i = 1; i < SPEC_N; i++)
            tft->drawLine(sc_x[i - 1], sc_y[i - 1], sc_x[i], sc_y[i], COL_BG);
    scopeBaseline();                                        // восстановить ось (стирание могло задеть)
    for (int i = 1; i < SPEC_N; i++)                        // нарисовать новый
        tft->drawLine(sc_x[i - 1], ny[i - 1], sc_x[i], ny[i], COL_GREEN);
    for (int i = 0; i < SPEC_N; i++) sc_y[i] = ny[i];
    sc_have = true;
}

static void drawScopeScreen()
{
    tft->fillRect(0, 0, SCR_W, CONTENT_BOTTOM, COL_BG);
    statusbarDraw();
    drawTitle();
    for (int i = 0; i < SPEC_N; i++) sc_x[i] = i * (SCR_W - 1) / (SPEC_N - 1);
    sc_have = false;                                        // фон свежий — прошлого луча нет
    scopeBaseline();
    scopeUpdate(audioIsPlaying() && !paused);
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
    else if (screen == 1) { if (vizMode == 0) drawSpectrumScreen(); else if (vizMode == 1) drawVuScreen(); else drawScopeScreen(); }
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
    if (screen == 0)           { drawTitle(); drawTransport(); }
    else if (vizMode == 0)      { drawTitle(); drawSpecLabels(); }   // частота дискретизации могла смениться
    else                       { drawTitle(); }                     // VU: подписи частот не нужны
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
// Идемпотентное удержание Wi-Fi: acquire/release ровно по разу за «жизнь»
// программы. Иначе повторный onEnter (пробуждение из сна — onResume не задан)
// утёк бы refCount, и Wi-Fi не выключился бы при выходе из радио.
static bool radioWifiHeld = false;

static void radioEnter()
{
    if (!radioWifiHeld) { wifiAcquire(); radioWifiHeld = true; }

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
    if (radioWifiHeld) { wifiRelease(); radioWifiHeld = false; }
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
        // Спектр / VU / осциллограф ~60 fps (только на этой странице).
        if (millis() - sp_last > 16) {
            sp_last = millis();
            if      (vizMode == 0) drawSpectrum(audioIsPlaying() && !paused);
            else if (vizMode == 1) vuUpdate(audioIsPlaying() && !paused);
            else                   scopeUpdate(audioIsPlaying() && !paused);
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
        } else if (screen == 1) {                                          // тап — спектр → VU → осциллограф
            vizMode = (vizMode + 1) % 3;
            drawScreen();
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
