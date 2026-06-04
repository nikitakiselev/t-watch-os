#include "items.h"
#include <stdio.h>
#include <string.h>

static const char *RAR[4] = { "", "Rare ", "Epic ", "Legend " };
static const uint16_t RARCOL[4] = { 0xFFFF, 0x07FF, 0xF81F, 0xFD20 };  // бел/циан/маджента/золото
static const char *WPN[3] = { "Sword", "Axe", "Staff" };
static const char *ARM[3] = { "Mail", "Plate", "Robe" };

const char *rarityName(uint8_t r) { return r < 4 ? (r == 0 ? "Common" : RAR[r]) : "?"; }
uint16_t    rarityColor(uint8_t r) { return r < 4 ? RARCOL[r] : 0xFFFF; }
bool        itemIsGear(const Item &it) { return it.kind == IT_WEAPON || it.kind == IT_ARMOR; }

Item itemRandom(uint32_t seed, long dist)
{
    Item it; memset(&it, 0, sizeof(it));
    it.count = 1;
    uint32_t h = seed * 2654435761u; h ^= h >> 15;

    int kroll = h % 100u;
    if      (kroll < 35) it.kind = IT_WEAPON;
    else if (kroll < 65) it.kind = IT_ARMOR;
    else if (kroll < 85) it.kind = IT_HP_POTION;
    else                 it.kind = IT_MP_POTION;

    int rr = (int)((h >> 8) % 100u) + (int)(dist / 20);
    it.rarity = rr > 95 ? R_LEGENDARY : rr > 82 ? R_EPIC : rr > 58 ? R_RARE : R_COMMON;

    int lvl  = 1 + (int)(dist / 12);
    int rmul = it.rarity + 1;                       // 1..4

    switch (it.kind) {
    case IT_WEAPON:
        it.power = (2 + lvl) + it.rarity * 3;
        snprintf(it.name, sizeof(it.name), "%s%s", RAR[it.rarity], WPN[(h >> 16) % 3u]);
        break;
    case IT_ARMOR:
        it.power = (1 + lvl / 2) + it.rarity * 2;
        snprintf(it.name, sizeof(it.name), "%s%s", RAR[it.rarity], ARM[(h >> 16) % 3u]);
        break;
    case IT_HP_POTION:
        it.power = 30 + it.rarity * 20;
        snprintf(it.name, sizeof(it.name), "HP Potion");
        break;
    default:  // IT_MP_POTION
        it.power = 15 + it.rarity * 10;
        snprintf(it.name, sizeof(it.name), "MP Potion");
        break;
    }
    return it;
}
