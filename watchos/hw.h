#pragma once
#include "config.h"

// Обёртка над TTGOClass: единая точка инициализации железа и доступа к нему.
// Глобальные указатели определены в hw.cpp.
extern TTGOClass *watch;   // экземпляр часов
extern TFT_eSPI  *tft;     // дисплей (принадлежит watch)

// Инициализация: питание (AXP202), дисплей (ST7789), подсветка, RTC, кнопка.
void hwBegin();

// Заряд батареи в процентах (0..100).
int  hwBattPercent();

// Текущее время RTC (базовый пояс — MSK).
RTC_Date hwNow();

// Перевести время RTC в нужный пояс по смещению в минутах.
// Возвращает часы/минуты/секунды этого пояса (с переносом через сутки).
void hwTimeInZone(const RTC_Date &base, int offsetMin, int &h, int &m, int &s);
