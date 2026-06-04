#pragma once
#include <stdint.h>

// Монстр в конкретной точке мира (порождается детерминированно из координат,
// статы масштабируются с удалением от (0,0)).
struct Monster {
    const char *name;
    uint8_t     spriteId;          // индекс в MON_TILES
    int         level;
    int         hp, hpMax;
    int         atk, def, dodge;   // dodge — шанс уклона, %
    int         xp, gold;
};

// Есть ли (непобеждённый) монстр в клетке. Заполняет out.
bool monsterActiveAt(int32_t x, int32_t y, Monster &out);

// Пометить клетку как зачищенную (после победы) — кольцевой буфер ближних клеток.
void monsterMarkCleared(int32_t x, int32_t y);

// Открытые сундуки (кольцевой буфер ближних клеток).
bool chestOpenedAt(int32_t x, int32_t y);
void markChestOpened(int32_t x, int32_t y);

// Использованные алтари / сработавшие руны событий (одноразовые).
bool altarUsedAt(int32_t x, int32_t y);
void markAltarUsed(int32_t x, int32_t y);
bool eventTriggeredAt(int32_t x, int32_t y);
void markEventTriggered(int32_t x, int32_t y);
