#include "modal.h"
#include "config.h"   // SCR_W, SCR_H
#include "hw.h"       // watch, tft
#include "power.h"    // powerNoteActivity
#include "theme.h"    // COL_BG
#include <Arduino.h>

void modalBegin()
{
    // Дождаться отпускания пальца (которым открыли диалог), но не дольше 1.5с.
    int16_t wx, wy; uint32_t t = millis();
    while (watch->getTouch(wx, wy) && millis() - t < 1500) delay(10);
    inputBegin();
}

InputEvent modalPoll(int16_t &x, int16_t &y)
{
    InputEvent e = inputPoll(x, y);
    if (e != EVT_NONE) powerNoteActivity();   // модальный цикл сам держит таймер сна
    else delay(20);                            // в простое — троттлим CPU
    return e;
}

void modalScrim()
{
    // Чёрная линия через строку → фон «темнеет» на ~50%. drawFastHLine быстрый (одна линия
    // = один проход), так что вся вуаль — это SCR_H/2 операций, без попиксельного блендинга.
    for (int y = 0; y < SCR_H; y += 2)
        tft->drawFastHLine(0, y, SCR_W, COL_BG);
}

void modalPanel(int x, int y, int w, int h, int r, uint16_t border)
{
    modalScrim();                          // затенить фон под окном (идемпотентно при перерисовке)
    tft->fillRoundRect(x, y, w, h, r, COL_BG);   // панель — поверх вуали, чистая
    tft->drawRoundRect(x, y, w, h, r, border);
}
