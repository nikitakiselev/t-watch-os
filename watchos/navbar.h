#pragma once
#include "program.h"

// Нижняя панель кнопок (как в Android). Рисуется фреймворком для каждой
// программы: либо её собственные кнопки, либо дефолтная «Back».
void navbarDraw(const Program *p);

// Обработать тап: если попал в панель — вызвать действие кнопки и вернуть true.
bool navbarHandleTap(const Program *p, int16_t x, int16_t y);
