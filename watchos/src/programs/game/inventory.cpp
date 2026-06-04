#include "inventory.h"
#include "player.h"

Item inv[INV_MAX];
int  invCount = 0;
Item equWeapon; bool hasWeapon = false;
Item equArmor;  bool hasArmor  = false;

void inventoryInit()
{
    invCount = 0;
    hasWeapon = hasArmor = false;
}

static bool isPotion(const Item &it) { return it.kind == IT_HP_POTION || it.kind == IT_MP_POTION; }

bool invAdd(const Item &it)
{
    if (isPotion(it)) {                              // зелья — стакаются в одну строку
        for (int i = 0; i < invCount; i++)
            if (inv[i].kind == it.kind && inv[i].power == it.power) {
                inv[i].count += (it.count ? it.count : 1);
                return true;
            }
    }
    if (invCount >= INV_MAX) return false;
    inv[invCount] = it;
    if (inv[invCount].count == 0) inv[invCount].count = 1;
    invCount++;
    return true;
}

static void invRemoveAt(int i)
{
    if (i < 0 || i >= invCount) return;
    for (int j = i; j < invCount - 1; j++) inv[j] = inv[j + 1];
    invCount--;
}

void invEquip(int i)
{
    if (i < 0 || i >= invCount) return;
    Item it = inv[i];
    if (it.kind == IT_WEAPON) {
        invRemoveAt(i);
        if (hasWeapon) invAdd(equWeapon);   // снятое — обратно в инвентарь
        equWeapon = it; hasWeapon = true;
    } else if (it.kind == IT_ARMOR) {
        invRemoveAt(i);
        if (hasArmor) invAdd(equArmor);
        equArmor = it; hasArmor = true;
    }
}

bool invUse(int i)
{
    if (i < 0 || i >= invCount) return false;
    Item it = inv[i];
    if (it.kind == IT_HP_POTION) {
        if (gp.hp >= gp.hpMax) return false;         // HP полное — не тратим
        gp.hp += it.power; if (gp.hp > gp.hpMax) gp.hp = gp.hpMax;
    } else if (it.kind == IT_MP_POTION) {
        if (gp.mp >= gp.mpMax) return false;         // MP полное — не тратим
        gp.mp += it.power; if (gp.mp > gp.mpMax) gp.mp = gp.mpMax;
    } else return false;
    if (inv[i].count > 1) inv[i].count--;            // расходуем одну из стопки
    else invRemoveAt(i);
    return true;
}

int playerAtkBonus() { return hasWeapon ? equWeapon.power : 0; }
int playerDefBonus() { return hasArmor ? equArmor.power : 0; }

int invFindHpPotion()
{
    for (int i = 0; i < invCount; i++) if (inv[i].kind == IT_HP_POTION) return i;
    return -1;
}

int itemSellValue(const Item &it)
{
    if (itemIsGear(it)) return it.power * 3 + it.rarity * 12 + 5;   // ~половина закупки
    return 10;                                                     // зелья
}

int invSell(int i)
{
    if (i < 0 || i >= invCount) return 0;
    int price = itemSellValue(inv[i]);
    if (inv[i].count > 1) inv[i].count--;            // продаём одну из стопки
    else invRemoveAt(i);
    gp.gold += price;
    return price;
}
