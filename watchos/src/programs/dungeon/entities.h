#pragma once
#include <stdint.h>

// Типы урона (для слабостей/сопротивлений монстров).
enum DmgType { DMG_PHYS = 0, DMG_FIRE, DMG_LIGHT, DMG_POISON, DMG_N };

const char *dmgTypeName(int t);    // короткое имя типа ("Phys"/"Fire"/...)

// Монстр в конкретной точке мира (порождается детерминированно из координат,
// статы масштабируются с удалением от (0,0)).
struct Monster {
    const char *name;
    uint8_t     spriteId;          // смещение спрайта от SPR_MON_BASE (атлас = SPR_MON_BASE + spriteId)
    uint8_t     anim;              // число кадров анимации (1 = статичный; 2 = idle-анимация, кадры spriteId, spriteId+1)
    int         level;
    int         hp, hpMax;
    int         atk, def, dodge;   // dodge — шанс уклона, %
    int         xp, gold;
    int8_t      weak, resist;      // DmgType: слабость (×2) и сопротивление (÷2); -1 = нет
};

// Есть ли (непобеждённый) монстр в клетке. Заполняет out.
bool monsterActiveAt(int32_t x, int32_t y, Monster &out);

// Сбросить состояние этажа (зачищенные клетки, сундуки, алтари, руны) — новый этаж свежий.
void floorReset();

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
