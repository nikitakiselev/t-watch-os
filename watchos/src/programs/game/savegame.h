#pragma once
#include <stdint.h>

// Сохранение/загрузка прогресса игры в NVS (ESP32 Preferences, namespace "dungeon").
// Сохраняются игрок, инвентарь, экипировка, позиция и последний лагерь-чекпоинт.

bool saveExists();
void gameSave(int32_t px, int32_t py, int32_t campX, int32_t campY, bool hasCamp);
bool gameLoad(int32_t &px, int32_t &py, int32_t &campX, int32_t &campY, bool &hasCamp);
