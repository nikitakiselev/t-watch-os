#pragma once

// Характеристики игрока (в RAM; сохранение во Flash — это M5).
struct Player {
    int  hp, hpMax;
    int  mp, mpMax;
    int  str, dex, intel;
    int  level;
    long xp;
    int  gold;
    int  statPoints;              // нераспределённые очки характеристик
};

extern Player gp;                 // единственный игрок
extern int gLearnedSkill;         // навык, изученный при последнем playerGainXp (-1 если нет)

const int STAT_POINTS_PER_LEVEL = 3;

void playerInit();                // стартовые значения
long xpForNext(int level);        // опыт до следующего уровня
void playerGainXp(long amount);   // начислить опыт (+ обработать лвл-апы)
void playerRecalcMax();           // пересчитать hpMax/mpMax из STR/INT (прирост идёт в текущее)
