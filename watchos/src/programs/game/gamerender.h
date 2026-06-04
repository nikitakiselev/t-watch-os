#pragma once
#include <stdint.h>

// Геометрия вьюпорта.
// Навбара у игры нет → карта занимает всё под HUD: 240×216. Тайл 24px:
// 10×9 клеток (240×216), ровно без полей.
#define TILE   24
#define VW     10          // ширина вьюпорта в клетках
#define VH     9           // высота
#define MAP_X  0
#define MAP_Y  24          // под HUD
#define ARROW_OFF 15       // отступ центра стрелки от грани карты

void gameRenderInit();                  // создать off-screen спрайт карты
void gameRenderFree();                  // освободить
void gameRenderMap(int32_t px, int32_t py);  // нарисовать вьюпорт (игрок в центре)
