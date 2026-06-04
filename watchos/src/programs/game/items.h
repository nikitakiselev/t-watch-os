#pragma once
#include <stdint.h>

enum ItemKind : uint8_t { IT_NONE = 0, IT_WEAPON, IT_ARMOR, IT_HP_POTION, IT_MP_POTION };
enum Rarity   : uint8_t { R_COMMON = 0, R_RARE, R_EPIC, R_LEGENDARY };

struct Item {
    uint8_t kind;
    uint8_t rarity;
    uint8_t count;        // зелья: количество в стопке; снаряжение = 1
    int16_t power;        // оружие: +атака; броня: +защита; зелье: восстановление
    char    name[22];
};

Item        itemRandom(uint32_t seed, long dist);   // детерминированно из seed+дистанции
const char *rarityName(uint8_t r);
uint16_t    rarityColor(uint8_t r);
bool        itemIsGear(const Item &it);             // оружие/броня (надевается)
