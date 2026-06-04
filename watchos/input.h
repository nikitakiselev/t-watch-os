#pragma once
#include "config.h"

// Нормализованные события ввода для всех программ.
enum InputEvent {
    EVT_NONE = 0,
    EVT_CLICK,   // короткое нажатие боковой кнопки  → войти / выбрать
    EVT_BACK,    // долгое нажатие боковой кнопки     → назад / домой
    EVT_UP,      // свайп вверх
    EVT_DOWN,    // свайп вниз
    EVT_LEFT,    // свайп влево
    EVT_RIGHT,   // свайп вправо
    EVT_TAP      // короткое касание (координаты в tapX/tapY)
};

void       inputBegin();
// Возвращает одно событие за вызов. Для EVT_TAP заполняет координаты касания.
InputEvent inputPoll(int16_t &tapX, int16_t &tapY);
