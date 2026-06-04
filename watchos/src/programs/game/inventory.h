#pragma once
#include "items.h"

#define INV_MAX 16

extern Item inv[INV_MAX];
extern int  invCount;
extern Item equWeapon; extern bool hasWeapon;
extern Item equArmor;  extern bool hasArmor;

void inventoryInit();
bool invAdd(const Item &it);     // false, если инвентарь полон
void invEquip(int i);            // надеть оружие/броню (снятое возвращается в инвентарь)
bool invUse(int i);              // использовать зелье; false — если бесполезно (HP/MP полные)
int  playerAtkBonus();           // +атака от оружия
int  playerDefBonus();           // +защита от брони
int  invFindHpPotion();          // индекс первого зелья HP или -1
int  itemSellValue(const Item &it);   // цена продажи предмета
int  invSell(int i);             // продать 1 ед. предмета, вернуть полученное золото
