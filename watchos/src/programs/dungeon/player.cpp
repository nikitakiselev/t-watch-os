#include "player.h"
#include "skills.h"
#include "inventory.h"
#include <string.h>

Player gp;

// HP/MP-пулы выводятся из характеристик: каждое очко STR = +10 HP, INT = +5 MP.
void playerRecalcMax()
{
    int newHpMax = 100 + (gp.str   - 5) * 10;
    int newMpMax = 30  + (gp.intel - 5) * 5;
    if (hasArmor && equArmor.effect == EF_VITALITY) newHpMax += equArmor.effMag;   // +HP от брони
    if (hasArmor && equArmor.effect == EF_SPIRIT)   newMpMax += equArmor.effMag;   // +MP от брони
    if (newHpMax < 1) newHpMax = 1;
    if (newMpMax < 0) newMpMax = 0;
    gp.hp += newHpMax - gp.hpMax;             // прирост максимума идёт и в текущее
    gp.mp += newMpMax - gp.mpMax;
    gp.hpMax = newHpMax; gp.mpMax = newMpMax;
    if (gp.hp > gp.hpMax) gp.hp = gp.hpMax;
    if (gp.hp < 1) gp.hp = 1;
    if (gp.mp > gp.mpMax) gp.mp = gp.mpMax;
    if (gp.mp < 0) gp.mp = 0;
}

void playerInit()
{
    gp.str = gp.dex = gp.intel = 5;
    gp.level = 1;
    gp.xp = 0;
    gp.gold = 0;
    gp.statPoints = 0;
    gp.skillPoints = 0;
    memset(gp.skillRank, 0, sizeof(gp.skillRank));   // все навыки на ранге 0
    gp.hp = gp.mp = gp.hpMax = gp.mpMax = 0;
    playerRecalcMax();                        // hpMax=100, mpMax=30
    gp.hp = gp.hpMax; gp.mp = gp.mpMax;
}

// Квадратичный рост: на старте ~30 (level 1), дальше резко больше, чтобы один сильный
// монстр на глубоком этаже не давал сразу несколько уровней.
long xpForNext(int level) { return 25L * level + 5L * level * level; }

static void levelUp()
{
    gp.level++;
    gp.statPoints  += STAT_POINTS_PER_LEVEL;   // распределить в STR/DEX/INT
    gp.skillPoints += SKILL_POINTS_PER_LEVEL;  // распределить в навыки (3-я вкладка)
    gp.hp = gp.hpMax;                          // полное восстановление при лвл-апе
    gp.mp = gp.mpMax;
}

void playerGainXp(long amount)
{
    gp.xp += amount;
    while (gp.xp >= xpForNext(gp.level)) {
        gp.xp -= xpForNext(gp.level);
        levelUp();
    }
}
