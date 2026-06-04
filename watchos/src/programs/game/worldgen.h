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

uint8_t tileAt(int32_t x, int32_t y);   // тип клетки в мировых координатах
bool    isWalkable(int32_t x, int32_t y);
