#include "player.h"
#include "skills.h"

Player gp;
int gLearnedSkill = -1;

// HP/MP-пулы выводятся из характеристик: каждое очко STR = +10 HP, INT = +5 MP.
void playerRecalcMax()
{
    int newHpMax = 100 + (gp.str   - 5) * 10;
    int newMpMax = 30  + (gp.intel - 5) * 5;
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
    gp.hp = gp.mp = gp.hpMax = gp.mpMax = 0;
    playerRecalcMax();                        // hpMax=100, mpMax=30
    gp.hp = gp.hpMax; gp.mp = gp.mpMax;
}

long xpForNext(int level) { return 30L * level; }   // каждый уровень — больше опыта

static void levelUp()
{
    gp.level++;
    gp.statPoints += STAT_POINTS_PER_LEVEL;   // игрок распределит сам (STR→HP, INT→MP, DEX→уклон/крит)
    gp.hp = gp.hpMax;                         // полное восстановление при лвл-апе
    gp.mp = gp.mpMax;
    int s = skillLearnedAtLevel(gp.level);    // открылся новый навык?
    if (s >= 0) gLearnedSkill = s;
}

void playerGainXp(long amount)
{
    gLearnedSkill = -1;
    gp.xp += amount;
    while (gp.xp >= xpForNext(gp.level)) {
        gp.xp -= xpForNext(gp.level);
        levelUp();
    }
}
