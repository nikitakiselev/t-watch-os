#pragma once
#include <stdint.h>

// Процедурный бесконечный мир: тип клетки вычисляется из координат + seed,
// ничего не хранится. Одинаковые координаты всегда дают одинаковый результат.
enum TileType : uint8_t {
    TILE_WALL = 0, TILE_FLOOR, TILE_STAIRS, TILE_CHEST,
    TILE_MERCHANT,    // торговец (магазин), многоразовый
    TILE_ALTAR,       // алтарь (одноразовое благословение)
    TILE_EVENT,       // руна случайного события (одноразовая)
    TILE_CAMP,        // лагерь-чекпоинт (сохранение + точка респавна)
};

// Текущий этаж: 0 (верх, легко) … -128 (низ, тяжело). Глубже — сильнее монстры и награда.
// Сила монстров теперь зависит от этажа, а не от уровня игрока (см. entities.cpp).
extern int gFloor;
#define FLOOR_MIN (-128)
#define FLOOR_MAX 0

uint8_t tileAt(int32_t x, int32_t y);   // тип клетки в мировых координатах
bool    isWalkable(int32_t x, int32_t y);
bool    inCamp(int32_t x, int32_t y);   // клетка внутри безопасной комнаты-кампа
