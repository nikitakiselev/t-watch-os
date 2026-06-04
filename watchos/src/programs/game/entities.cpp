#include "entities.h"
#include "worldgen.h"
#include "player.h"

// Типы монстров (порядок = MON_TILES): имя, hp, atk, def, dodge%, xp, gold.
struct Def { const char *name; int hp, atk, def, dodge, xp, gold; };
static const Def DEFS[9] = {
    { "Slime",      18,  4, 1,  5,   8,   3 },   // 0 slime-green
    { "Blue Slime", 26,  5, 2,  8,  11,   5 },   // 1 slime-blue
    { "Skeleton",   32,  7, 3,  8,  15,   7 },   // 2 skeleton
    { "Zombie",     46,  9, 4,  3,  22,  10 },   // 3 zombie
    { "Snowman",    52, 10, 5,  6,  26,  12 },   // 4 evil-snowman
    { "Tsargo",     64, 12, 6,  9,  32,  16 },   // 5 tsargo
    { "Troll",      75, 14, 7,  5,  38,  20 },   // 6 troll
    { "Yeti",       90, 16, 8,  7,  48,  28 },   // 7 yeti
    { "Cyclops",   110, 19,10,  8,  62,  40 },   // 8 cyclops
};

static uint32_t h2(int32_t x, int32_t y)
{
    uint32_t h = 0xA53F1B7u;
    h ^= (uint32_t)x * 2654435761u; h = (h ^ (h >> 15)) * 2246822519u;
    h ^= (uint32_t)y * 3266489917u; h = (h ^ (h >> 13));
    return h;
}

// Кольцевой буфер зачищенных клеток (помним только ближние — без хранения карты).
#define CLR_MAX 64
static int32_t clrX[CLR_MAX], clrY[CLR_MAX];
static int     clrN = 0, clrHead = 0;

static bool isCleared(int32_t x, int32_t y)
{
    for (int i = 0; i < clrN; i++) if (clrX[i] == x && clrY[i] == y) return true;
    return false;
}

void monsterMarkCleared(int32_t x, int32_t y)
{
    clrX[clrHead] = x; clrY[clrHead] = y;
    clrHead = (clrHead + 1) % CLR_MAX;
    if (clrN < CLR_MAX) clrN++;
}

// Открытые сундуки — отдельный кольцевой буфер.
static int32_t chX[CLR_MAX], chY[CLR_MAX];
static int     chN = 0, chHead = 0;

bool chestOpenedAt(int32_t x, int32_t y)
{
    for (int i = 0; i < chN; i++) if (chX[i] == x && chY[i] == y) return true;
    return false;
}

void markChestOpened(int32_t x, int32_t y)
{
    chX[chHead] = x; chY[chHead] = y;
    chHead = (chHead + 1) % CLR_MAX;
    if (chN < CLR_MAX) chN++;
}

// Использованные алтари и сработавшие события — ещё два кольцевых буфера.
static int32_t alX[CLR_MAX], alY[CLR_MAX]; static int alN = 0, alHead = 0;
static int32_t evX[CLR_MAX], evY[CLR_MAX]; static int evN = 0, evHead = 0;

bool altarUsedAt(int32_t x, int32_t y)
{
    for (int i = 0; i < alN; i++) if (alX[i] == x && alY[i] == y) return true;
    return false;
}
void markAltarUsed(int32_t x, int32_t y)
{
    alX[alHead] = x; alY[alHead] = y;
    alHead = (alHead + 1) % CLR_MAX;
    if (alN < CLR_MAX) alN++;
}

bool eventTriggeredAt(int32_t x, int32_t y)
{
    for (int i = 0; i < evN; i++) if (evX[i] == x && evY[i] == y) return true;
    return false;
}
void markEventTriggered(int32_t x, int32_t y)
{
    evX[evHead] = x; evY[evHead] = y;
    evHead = (evHead + 1) % CLR_MAX;
    if (evN < CLR_MAX) evN++;
}

bool monsterActiveAt(int32_t x, int32_t y, Monster &out)
{
    if (tileAt(x, y) != TILE_FLOOR) return false;     // только на чистом полу
    uint32_t h = h2(x, y);
    if (h % 100u >= 10u) return false;                // ~10% полов
    if (isCleared(x, y)) return false;

    long ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
    long d  = ax > ay ? ax : ay;                      // дистанция от (0,0)
    // Базовый уровень монстра растёт ВМЕСТЕ с игроком, + за удалённость, ± разброс.
    int  variance = (int)((h >> 4) % 3u) - 1;         // -1..+1
    int  level = gp.level + (int)(d / 24) + variance;
    if (level < 1) level = 1;

    int type;
    if      (level <= 2) type = (int)((h >> 8) & 1u);        // slime / blue slime
    else if (level <= 4) type = 2 + (int)((h >> 8) & 1u);    // skeleton / zombie
    else if (level <= 6) type = 4 + (int)((h >> 8) & 1u);    // snowman / tsargo
    else if (level <= 8) type = 6 + (int)((h >> 8) & 1u);    // troll / yeti
    else                 type = 8;                           // cyclops
    bool boss = (level >= 10 && (h >> 16) % 18u == 0u);
    if (boss) type = 8;

    const Def &D = DEFS[type];
    out.spriteId = (uint8_t)type;
    out.name  = boss ? "Cyclops!" : D.name;
    out.level = level;
    out.hpMax = out.hp = (D.hp + level * 5) * (boss ? 2 : 1);
    out.atk   = (D.atk + level * 2) * (boss ? 3 : 2) / 2;    // boss x1.5
    out.def   = D.def + level;
    out.dodge = D.dodge;
    out.xp    = (D.xp + level * 8) * (boss ? 2 : 1);     // награда сильнее растёт с уровнем
    out.gold  = (D.gold + level * 5) * (boss ? 2 : 1);
    return true;
}
