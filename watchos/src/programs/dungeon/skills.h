#pragma once
#include <stdint.h>
#include "entities.h"     // DmgType

// Активные навыки + пассивные мастерства. Ранг 0 = не изучен (не показывается в бою).
// Очки навыков начисляются за уровень и тратятся на 3-й вкладке экрана персонажа.
enum SkillId {
    SK_FIREBALL = 0, SK_FLAMENOVA,        // огонь
    SK_LIGHTNING, SK_THUNDER,             // молния (игнор брони)
    SK_POISON, SK_PLAGUE,                 // яд (урон по времени)
    SK_POWERSTRIKE,                       // физ. удар магией
    SK_LIFEDRAIN,                         // вампиризм: урон + лечение от нанесённого
    SK_HEAL, SK_REGEN, SK_SHIELD,         // утилити
    SK_M_FIRE, SK_M_LIGHT, SK_M_POISON,   // пассивные мастерства (+% урона типа)
    SK_M_VAMP,                            // пассивный вампиризм (+% HP от физ. урона)
    SK_COUNT
};

enum SkillKind {
    SKK_DAMAGE,   // разовый урон
    SKK_DOT,      // урон по времени
    SKK_DRAIN,    // урон + лечение игрока (вампиризм)
    SKK_HEAL,     // мгновенное лечение
    SKK_HOT,      // лечение по времени
    SKK_SHIELD,   // входящий урон ×½ на N ходов
    SKK_PASSIVE,  // мастерство: +% урона типа (в бою не выбирается)
};

#define SKILL_MAX_RANK 50

struct SkillDef {
    const char *name;
    uint8_t kind;        // SkillKind
    int8_t  dmgType;     // DmgType для урона/мастерства, -1 для лечения/щита
    uint8_t ignoreDef;   // 1 — почти игнорирует броню (молния)
    int     cost;        // MP (0 для пассивок)
    int     basePow;     // эффект на ранге 1 (до INT)
    int     perRank;     // прибавка эффекта за каждый ранг сверх 1-го
    int     intelMul;    // + INT*это к силе (0 для пассивок/щита)
    int     turns;       // базовая длительность для DOT/HOT/SHIELD
};

extern const SkillDef SKILLS[SK_COUNT];

int  skillRank(int id);              // текущий ранг навыка (gp.skillRank[id])
bool skillUsable(int id);            // активный навык с рангом > 0 (пассивки исключены)
int  playerMasteryPct(int dmgType);  // суммарный бонус % к урону типа от пассивок
int  playerLifestealPct();           // пассивный вампиризм: % HP от нанесённого физ. урона

int  skillPowerAt(int id, int rank);     // сила эффекта на ранге (урон/лечение/за ход)
int  skillDurationAt(int id, int rank);  // длительность в ходах (DoT/HoT/щит), иначе 0
bool skillPassive(int id);               // true — пассивное мастерство
