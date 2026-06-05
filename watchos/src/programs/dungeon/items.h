#pragma once
#include <stdint.h>

enum ItemKind : uint8_t { IT_NONE = 0, IT_WEAPON, IT_ARMOR, IT_HP_POTION, IT_MP_POTION };
enum Rarity   : uint8_t { R_COMMON = 0, R_RARE, R_EPIC, R_LEGENDARY };

// Эффекты (на оружии — при атаке; на броне — пассивные). Срабатывают только с RARE+.
enum ItemEffect : uint8_t {
    EF_NONE = 0,
    EF_POISON,     // яд: effMag урона/ход (оружие)
    EF_FIRE,       // +effMag урона к удару (оружие)
    EF_PIERCE,     // игнор effMag% защиты врага (оружие)
    EF_CRIT,       // +effMag% к криту (оружие)
    EF_LIFESTEAL,  // лечит effMag% от нанесённого (оружие)
    EF_VITALITY,   // +effMag к макс. HP (броня)
    EF_SPIRIT,     // +effMag к макс. MP (броня)
    EF_EVASION,    // +effMag% уклонения (броня)
    EF_SHOCK,      // +effMag урона светом к удару (оружие)
};

struct Item {
    uint8_t kind;
    uint8_t rarity;
    uint8_t count;        // зелья: количество в стопке; снаряжение = 1
    uint8_t effect;       // ItemEffect
    int16_t power;        // оружие: +атака; броня: +защита; зелье: восстановление
    int16_t effMag;       // величина эффекта
    int32_t price;        // цена покупки (продажа = 70%); растёт с глубиной → 32 бита
    char    name[22];
};

// Каталог предметов — «архетипы» (имя/тип/эффект/редкость + базовые числа).
// Реальные характеристики считаются под текущую глубину этажа (itemScaled).
struct ItemDef {
    uint8_t kind, rarity, effect;
    int16_t power;
    int32_t price;
    int16_t effMag;
    uint8_t minLvl;       // «тир» архетипа: окно доступности у торговца зависит от глубины
    const char *name;
};
extern const ItemDef ITEM_DB[];
extern const int      ITEM_DB_N;

Item        itemFromDef(const ItemDef &d);          // базовые числа (глубина 0)
Item        itemScaled(const ItemDef &d, int depth); // характеристики/цена под глубину
Item        itemRandom(uint32_t seed, int depth);   // случайный предмет под глубину
void        merchTierWindow(int depth, int &lo, int &hi);   // окно тиров архетипов для глубины
const char *rarityName(uint8_t r);
uint16_t    rarityColor(uint8_t r);
bool        itemIsGear(const Item &it);             // оружие/броня (надевается)
const char *effectName(uint8_t e);                  // короткое имя эффекта ("" если нет)
int         effectDmgType(uint8_t e);               // тип урона оружейной стихии (DmgType) или -1
int         itemSellValue(const Item &it);          // 70% от цены
