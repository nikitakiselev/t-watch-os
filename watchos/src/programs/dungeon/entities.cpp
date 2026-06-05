#include "entities.h"
#include "worldgen.h"
#include "player.h"
#include <stdio.h>

// Спрайт гоблина в атласе: SPR_GOBLIN=121 (кадры 121/122). spr — смещение от SPR_MON_BASE(101) → 20.
#define GOBLIN_SPR 20

// Типы монстров: имя, spr (смещение спрайта от SPR_MON_BASE), anim (кадров: 1 статич./2 idle),
// hp, atk, def, dodge%, xp, gold, слабость, сопротивление, мин. глубина.
// Для базовых монстров spr == их «сквозной» индекс спрайта (атлас 101..109), anim=1.
// minDepth — с какой глубины этажа (|этаж|) монстр начинает встречаться. Должна НЕ убывать по индексу
// (выбор типа опирается на это: глубже открываются всё более тяжёлые монстры). Гоблины вставлены
// в это упорядочение по своим minDepth (10/30/50), но делят один спрайт (GOBLIN_SPR).
struct Def { const char *name; uint8_t spr, anim; int hp, atk, def, dodge, xp, gold; int8_t weak, resist; uint8_t minDepth; };
static const Def DEFS[12] = {
    { "Slime",           0,1,  18,  4, 1,  5,   8,   3, DMG_FIRE,   DMG_PHYS,    0 },  // студень гасит удары, горит
    { "Blue Slime",      1,1,  26,  5, 2,  8,  11,   5, DMG_LIGHT,  DMG_POISON,  0 },  // проводит ток, токсинам всё равно
    { "Skeleton",        2,1,  32,  7, 3,  8,  15,   7, DMG_PHYS,   DMG_POISON,  0 },  // кости крошатся, плоти нет
    { "Zombie",          3,1,  46,  9, 4,  3,  22,  10, DMG_FIRE,   DMG_POISON,  0 },  // нежить горит, ядом не взять
    { "Snowman",         4,1,  52, 10, 5,  6,  26,  12, DMG_FIRE,   DMG_PHYS,    5 },  // тает от огня, снег держит удар
    { "Tsargo",          5,1,  64, 12, 6,  9,  32,  16, DMG_LIGHT,  DMG_FIRE,    8 },  // огнеупорный, боится тока
    { "Goblin Cutthroat", GOBLIN_SPR,2,  56, 15, 5, 22,  40,  22, DMG_LIGHT, DMG_POISON, 10 },  // ловок и хитёр; молнию не обойти, яд нипочём
    { "Troll",           6,1,  75, 14, 7,  5,  38,  20, DMG_FIRE,   DMG_PHYS,   12 },  // регенерирует, огонь жжёт
    { "Yeti",            7,1,  90, 16, 8,  7,  48,  28, DMG_FIRE,   DMG_LIGHT,  16 },  // ледяной, шерсть изолирует ток
    { "Cyclops",         8,1, 110, 19,10,  8,  62,  40, DMG_POISON, DMG_PHYS,   22 },  // толстокожий, но травится ядом
    { "Goblin Marauder", GOBLIN_SPR,2,  92, 21, 9, 20,  70,  40, DMG_LIGHT, DMG_POISON, 30 },  // матёрый налётчик, тот же приём
    { "Goblin Warlord",  GOBLIN_SPR,2, 150, 30,14, 14, 120,  80, DMG_LIGHT, DMG_PHYS,   50 },  // вожак в броне: режет физ-урон, но молния бьёт
};
#define MON_N ((int)(sizeof(DEFS) / sizeof(DEFS[0])))

const char *dmgTypeName(int t)
{
    switch (t) {
        case DMG_PHYS:   return "Phys";
        case DMG_FIRE:   return "Fire";
        case DMG_LIGHT:  return "Light";
        case DMG_POISON: return "Poison";
        default:         return "-";
    }
}

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

void floorReset()
{
    clrN = clrHead = 0;        // зачищенные клетки
    chN  = chHead  = 0;        // открытые сундуки
    alN  = alHead  = 0;        // использованные алтари
    evN  = evHead  = 0;        // сработавшие руны
}

bool monsterActiveAt(int32_t x, int32_t y, Monster &out)
{
    if (tileAt(x, y) != TILE_FLOOR) return false;     // только на чистом полу
    if (inCamp(x, y)) return false;                   // в кампе безопасно — без монстров

    int depth = -gFloor;                              // глубина этажа (0 наверху … 128 внизу)
    if (depth < 0) depth = 0;

    uint32_t h = h2(x, y);
    int density = 10 + depth;                         // плотность монстров растёт с глубиной
    if (density > 40) density = 40;                   // потолок ~40% полов
    if (h % 100u >= (uint32_t)density) return false;
    if (isCleared(x, y)) return false;

    // Уровень монстра задаётся ЭТАЖОМ (не игроком): на 1 глубже — на 1 сильнее, ± разброс (±2).
    int variance = (int)((h >> 4) % 5u) - 2;          // -2..+2
    int level = 1 + depth + variance;
    if (level < 1) level = 1;

    // Типы, доступные на этой глубине, — префикс [0..maxType] (minDepth не убывает по индексу).
    int maxType = 0;
    for (int i = 0; i < MON_N; i++) if (DEFS[i].minDepth <= depth) maxType = i;
    // Берём из «окна» последних 4 открытых тиров — глубже вытесняет слабую мелочь.
    int lo = maxType - 3; if (lo < 0) lo = 0;
    int type = lo + (int)((h >> 8) % (uint32_t)(maxType - lo + 1));

    bool boss = (depth >= 10 && (h >> 16) % 18u == 0u);
    if (boss) type = maxType;                          // боссом становится самый тяжёлый доступный тип

    const Def &D = DEFS[type];
    static char bossName[24];
    out.spriteId = D.spr;
    out.anim     = D.anim;
    if (boss) { snprintf(bossName, sizeof(bossName), "%s!", D.name); out.name = bossName; }
    else      out.name = D.name;
    out.level = level;
    out.hpMax = out.hp = (D.hp + level * 5) * (boss ? 2 : 1);
    out.atk   = (D.atk + level * 2) * (boss ? 3 : 2) / 2;    // boss x1.5
    out.def   = D.def + level;
    out.dodge = D.dodge;
    // Множитель награды за глубину (поверх роста от уровня): +6% за этаж вниз.
    int rmul = 100 + depth * 6;
    out.xp    = (D.xp + level * 8) * (boss ? 2 : 1) * rmul / 100;
    out.gold  = (D.gold + level * 5) * (boss ? 2 : 1) * rmul / 100;
    out.weak   = D.weak;
    out.resist = D.resist;
    return true;
}
