#include "input.h"
#include "hw.h"

// Состояние жеста тача (между вызовами inputPoll).
static bool    touching = false;
static int16_t startX, startY, lastX, lastY;

void inputBegin()
{
    touching = false;
}

static InputEvent pollButton()
{
    // Опрашиваем статус IRQ AXP202: короткое / долгое нажатие PEK.
    watch->power->readIRQ();
    bool shortPress = watch->power->isPEKShortPressIRQ();
    bool longPress  = watch->power->isPEKLongPressIRQ();
    watch->power->clearIRQ();

    if (longPress)  return EVT_BACK;    // долгое — приоритетнее
    if (shortPress) return EVT_CLICK;
    return EVT_NONE;
}

static InputEvent pollTouch(int16_t &tapX, int16_t &tapY)
{
    int16_t x, y;
    bool now = watch->getTouch(x, y);

    if (now) {
        if (!touching) {            // палец только что коснулся
            touching = true;
            startX = lastX = x;
            startY = lastY = y;
        } else {
            lastX = x;
            lastY = y;
        }
        return EVT_NONE;            // событие выдаём только при отпускании
    }

    // Палец отпущен — разбираем жест.
    if (touching) {
        touching = false;
        int dx = lastX - startX;
        int dy = lastY - startY;

        if (abs(dy) >= SWIPE_MIN_DELTA && abs(dy) > abs(dx)) {
            return (dy < 0) ? EVT_UP : EVT_DOWN;          // вертикальный свайп
        }
        if (abs(dx) >= SWIPE_MIN_DELTA && abs(dx) > abs(dy)) {
            return (dx < 0) ? EVT_LEFT : EVT_RIGHT;       // горизонтальный свайп
        }
        if (abs(dx) < TAP_MAX_DELTA && abs(dy) < TAP_MAX_DELTA) {
            tapX = startX;
            tapY = startY;
            return EVT_TAP;
        }
    }
    return EVT_NONE;
}

InputEvent inputPoll(int16_t &tapX, int16_t &tapY)
{
    InputEvent e = pollButton();      // кнопка приоритетнее тача
    if (e != EVT_NONE) return e;
    return pollTouch(tapX, tapY);
}
