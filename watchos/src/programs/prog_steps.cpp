#include "prog_steps.h"
#include "../../hw.h"
#include "../../statusbar.h"
#include "../../theme.h"
#include <Arduino.h>

// ───────────────────────────── Геометрия ─────────────────────────────
static const int TITLE_Y = STATUSBAR_H + 14;            // [ STEPS ]
static const int NUM_Y   = STATUSBAR_H + 56;            // крупное число шагов (font 7)
static const int BAR_Y   = STATUSBAR_H + 88;            // прогресс к цели
static const int BAR_H   = 10;
static const int ROW_Y0  = STATUSBAR_H + 118;           // строки статистики
static const int ROW_H   = 24;
static const int MARGIN  = 20;
static const int GOAL    = 6000;                        // дневная цель шагов

// Оценочные коэффициенты (как у фитнес-браслетов).
static const float STRIDE_M   = 0.72f;                  // средний шаг, м
static const float KCAL_STEP  = 0.04f;                  // ккал на шаг

static uint32_t lastShown = 0xFFFFFFFFu;
static uint32_t lastPoll  = 0;

static void drawRow(int idx, const char *label, const char *val)
{
    int cy = ROW_Y0 + idx * ROW_H;
    tft->fillRect(MARGIN, cy - 9, SCR_W - 2 * MARGIN, 18, COL_BG);
    tft->setTextDatum(ML_DATUM);
    tft->setTextColor(COL_GREEN_DIM, COL_BG);
    tft->drawString(label, MARGIN, cy, 2);
    tft->setTextDatum(MR_DATUM);
    tft->setTextColor(COL_GREEN, COL_BG);
    tft->drawString(val, SCR_W - MARGIN, cy, 2);
}

static void drawSteps(bool full)
{
    uint32_t steps = hwStepCount();
    if (!full && steps == lastShown) return;
    lastShown = steps;

    // Крупное число шагов (font 7 — семисегментный, только цифры).
    char b[12];
    snprintf(b, sizeof(b), "%lu", (unsigned long)steps);
    tft->fillRect(0, NUM_Y - 26, SCR_W, 52, COL_BG);
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_GREEN, COL_BG);
    tft->drawString(b, SCR_W / 2, NUM_Y, 7);

    // Прогресс к цели.
    int bw = SCR_W - 2 * MARGIN;
    tft->fillRect(MARGIN, BAR_Y, bw, BAR_H, COL_BG);
    tft->drawRoundRect(MARGIN, BAR_Y, bw, BAR_H, 3, COL_GREEN_DIM);
    uint32_t cl = steps > (uint32_t)GOAL ? (uint32_t)GOAL : steps;
    int fill = (int)((long)(bw - 4) * cl / GOAL);
    if (fill > 0)
        tft->fillRoundRect(MARGIN + 2, BAR_Y + 2, fill, BAR_H - 4, 2,
                           steps >= (uint32_t)GOAL ? COL_AMBER : COL_GREEN);

    // Производная статистика.
    char v[16];
    snprintf(v, sizeof(v), "%.2f km", steps * STRIDE_M / 1000.0f);
    drawRow(0, "Distance", v);
    snprintf(v, sizeof(v), "%d", (int)(steps * KCAL_STEP));
    drawRow(1, "Calories", v);
    snprintf(v, sizeof(v), "%d", GOAL);
    drawRow(2, "Goal", v);
}

static void stepsEnter()
{
    hwStepBegin();
    tft->fillRect(0, 0, SCR_W, CONTENT_BOTTOM, COL_BG);
    statusbarDraw();
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_AMBER, COL_BG);
    tft->drawString("[ STEPS ]", SCR_W / 2, TITLE_Y, 2);
    lastShown = 0xFFFFFFFFu;
    drawSteps(true);
}

static void stepsTick()
{
    if (millis() - lastPoll < 1000) return;     // опрос BMA ~раз в секунду
    lastPoll = millis();
    drawSteps(false);
}

static void stepsReset()
{
    hwStepReset();
    lastShown = 0xFFFFFFFFu;
    drawSteps(true);
}

static void stepsIcon(TFT_eSPI &g, int cx, int cy, int r)
{
    g.fillCircle(cx - r / 3, cy - r / 4, r / 4, COL_GREEN);   // следы
    g.fillCircle(cx + r / 3, cy + r / 4, r / 4, COL_GREEN);
}

static const NavButton stepsNav[] = {
    { "Back",  kernelBack },
    { "Reset", stepsReset },
};

const Program stepsProgram = {
    "Steps", stepsEnter, stepsTick, nullptr, stepsIcon, stepsNav, 2, nullptr
};
