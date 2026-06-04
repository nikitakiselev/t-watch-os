#include "items.h"
#include "entities.h"   // DmgType
#include <stdio.h>
#include <string.h>

static const char *RAR[4]    = { "", "Rare ", "Epic ", "Legend " };
static const uint16_t RARCOL[4] = { 0xFFFF, 0x07FF, 0xF81F, 0xFD20 };  // бел/циан/маджента/золото

const char *rarityName(uint8_t r) { return r < 4 ? (r == 0 ? "Common" : RAR[r]) : "?"; }
uint16_t    rarityColor(uint8_t r) { return r < 4 ? RARCOL[r] : 0xFFFF; }
bool        itemIsGear(const Item &it) { return it.kind == IT_WEAPON || it.kind == IT_ARMOR; }

const char *effectName(uint8_t e)
{
    switch (e) {
        case EF_POISON:    return "psn";
        case EF_FIRE:      return "fire";
        case EF_PIERCE:    return "pierce";
        case EF_CRIT:      return "crit";
        case EF_LIFESTEAL: return "leech";
        case EF_VITALITY:  return "+HP";
        case EF_SPIRIT:    return "+MP";
        case EF_EVASION:   return "dodge";
        case EF_SHOCK:     return "shock";
        default:           return "";
    }
}

// Оружейная стихия → тип урона (для слабостей/мастерства). Яд — это DoT, прямого урона нет.
int effectDmgType(uint8_t e)
{
    switch (e) {
        case EF_FIRE:   return DMG_FIRE;
        case EF_SHOCK:  return DMG_LIGHT;
        case EF_POISON: return DMG_POISON;
        default:        return -1;
    }
}

// ── Каталог. Цены монотонны по силе; common — без эффекта, rare+ — с эффектом. ──
const ItemDef ITEM_DB[] = {
    // kind        rarity       effect        power price effMag minLvl  name
    // ── Оружие (power = +ATK) ──
    { IT_WEAPON,   R_COMMON,    EF_NONE,        2,   20,   0,   1, "Rusty Dagger" },
    { IT_WEAPON,   R_COMMON,    EF_NONE,        4,   45,   0,   2, "Dagger" },
    { IT_WEAPON,   R_COMMON,    EF_NONE,        6,   80,   0,   3, "Shortsword" },
    { IT_WEAPON,   R_RARE,      EF_POISON,      5,  110,   4,   3, "Venom Dagger" },
    { IT_WEAPON,   R_COMMON,    EF_NONE,        9,  150,   0,   4, "Sword" },
    { IT_WEAPON,   R_RARE,      EF_FIRE,        8,  210,   5,   5, "Flame Sword" },
    { IT_WEAPON,   R_RARE,      EF_SHOCK,       8,  220,   5,   5, "Spark Blade" },
    { IT_WEAPON,   R_COMMON,    EF_NONE,       12,  240,   0,   6, "Mace" },
    { IT_WEAPON,   R_RARE,      EF_PIERCE,     11,  300,  30,   6, "Piercer" },
    { IT_WEAPON,   R_COMMON,    EF_NONE,       15,  360,   0,   7, "Battle Axe" },
    { IT_WEAPON,   R_RARE,      EF_CRIT,       13,  430,  12,   7, "Assassin Blade" },
    { IT_WEAPON,   R_EPIC,      EF_LIFESTEAL,  14,  520,  20,   8, "Vampiric Sword" },
    { IT_WEAPON,   R_COMMON,    EF_NONE,       19,  560,   0,   9, "Greatsword" },
    { IT_WEAPON,   R_EPIC,      EF_FIRE,       18,  720,  10,  10, "Inferno Blade" },
    { IT_WEAPON,   R_EPIC,      EF_SHOCK,      18,  730,  10,  10, "Storm Hammer" },
    { IT_WEAPON,   R_LEGENDARY, EF_PIERCE,     24,  950,  45,  12, "Dragon Slayer" },
    // ── Броня (power = +DEF) ──
    { IT_ARMOR,    R_COMMON,    EF_NONE,        1,   18,   0,   1, "Cloth" },
    { IT_ARMOR,    R_COMMON,    EF_NONE,        2,   40,   0,   2, "Leather" },
    { IT_ARMOR,    R_COMMON,    EF_NONE,        4,   90,   0,   3, "Chainmail" },
    { IT_ARMOR,    R_RARE,      EF_VITALITY,    3,  130,  30,   3, "Vital Vest" },
    { IT_ARMOR,    R_COMMON,    EF_NONE,        6,  180,   0,   5, "Plate" },
    { IT_ARMOR,    R_RARE,      EF_SPIRIT,      4,  220,  25,   5, "Mage Robe" },
    { IT_ARMOR,    R_RARE,      EF_EVASION,     5,  300,  12,   6, "Shadow Cloak" },
    { IT_ARMOR,    R_COMMON,    EF_NONE,        9,  380,   0,   7, "Knight Plate" },
    { IT_ARMOR,    R_EPIC,      EF_VITALITY,    8,  520,  55,   8, "Guardian" },
    { IT_ARMOR,    R_LEGENDARY, EF_EVASION,    13,  900,  18,  12, "Dragon Mail" },
    // ── Зелья (power = восстановление) ──
    { IT_HP_POTION, R_COMMON,   EF_NONE,       35,   18,   0,   1, "Small HP" },
    { IT_HP_POTION, R_COMMON,   EF_NONE,       70,   40,   0,   1, "HP Potion" },
    { IT_HP_POTION, R_RARE,     EF_NONE,      120,   75,   0,   1, "Large HP" },
    { IT_MP_POTION, R_COMMON,   EF_NONE,       25,   16,   0,   1, "Small MP" },
    { IT_MP_POTION, R_COMMON,   EF_NONE,       55,   36,   0,   1, "MP Potion" },
    { IT_MP_POTION, R_RARE,     EF_NONE,       95,   65,   0,   1, "Large MP" },
};
const int ITEM_DB_N = sizeof(ITEM_DB) / sizeof(ITEM_DB[0]);

Item itemFromDef(const ItemDef &d)
{
    Item it;
    it.kind = d.kind; it.rarity = d.rarity; it.count = 1; it.effect = d.effect;
    it.power = d.power; it.effMag = d.effMag; it.price = d.price;
    strncpy(it.name, d.name, sizeof(it.name)); it.name[sizeof(it.name) - 1] = 0;
    return it;
}

// Плоские (числовые) эффекты растут с глубиной; процентные (пробитие/крит/уклон/вампиризм) — нет.
static bool effectIsFlat(uint8_t e)
{
    return e == EF_FIRE || e == EF_SHOCK || e == EF_POISON || e == EF_VITALITY || e == EF_SPIRIT;
}

Item itemScaled(const ItemDef &d, int depth)
{
    if (depth < 0) depth = 0;
    Item it = itemFromDef(d);
    int m10 = 10 + depth;                                 // множитель ×(1 + depth/10)
    it.power  = (int16_t)((long)d.power  * m10 / 10);
    it.effMag = (int16_t)(effectIsFlat(d.effect) ? (long)d.effMag * m10 / 10 : d.effMag);
    it.price  = (int32_t)((long)d.price * m10 / 10 * m10 / 10);   // цена ~ глубина² → сток золота
    return it;
}

// Тир-окно архетипов для текущей глубины (как у монстров: низкие тиры вытесняются).
// Каталог: minLvl 1..12. На больших глубинах показываем верхние тиры (масштаб по depth).
void merchTierWindow(int depth, int &lo, int &hi)
{
    if (depth < 0) depth = 0;
    hi = depth; if (hi < 3) hi = 3; if (hi > 12) hi = 12;
    lo = hi - 7; if (lo < 1) lo = 1;
}

Item itemRandom(uint32_t seed, int depth)
{
    int lo, hi; merchTierWindow(depth, lo, hi);
    int idxs[64], n = 0;
    for (int i = 0; i < ITEM_DB_N; i++)
        if (ITEM_DB[i].minLvl >= lo && ITEM_DB[i].minLvl <= hi) idxs[n++] = i;
    if (n == 0) for (int i = 0; i < ITEM_DB_N; i++) idxs[n++] = i;
    uint32_t h = seed * 2654435761u; h ^= h >> 15;
    return itemScaled(ITEM_DB[idxs[h % (uint32_t)n]], depth);
}

int itemSellValue(const Item &it) { return (int)((long)it.price * 70 / 100); }   // продажа 70% от цены
