#pragma once
#include <stdint.h>

#define SKILL_SLOTS 16            // размер массива рангов навыков (>= SK_COUNT в skills.h)

// Характеристики игрока.
struct Player {
    int  hp, hpMax;
    int  mp, mpMax;
    int  str, dex, intel;
    int  level;
    long xp;
    int  gold;
    int  statPoints;              // нераспределённые очки характеристик
    int  skillPoints;             // нераспределённые очки навыков
    uint8_t skillRank[SKILL_SLOTS];   // ранг каждого навыка (0 = не изучен)
};

extern Player gp;                 // единственный игрок

const int STAT_POINTS_PER_LEVEL  = 3;
const int SKILL_POINTS_PER_LEVEL = 1;

void playerInit();                // стартовые значения
long xpForNext(int level);        // опыт до следующего уровня
void playerGainXp(long amount);   // начислить опыт (+ обработать лвл-апы)
void playerRecalcMax();           // пересчитать hpMax/mpMax из STR/INT (прирост идёт в текущее)
