#include "skills.h"
#include "player.h"

// Навыки: имя, тип, тип урона, игнор-брони, стоимость MP, базовая сила, прибавка/ранг,
// множитель характеристики (STR для физ., иначе INT), длительность.
// Ранг повышается за очки навыков (по 1 за уровень); макс. SKILL_MAX_RANK.
const SkillDef SKILLS[SK_COUNT] = {
    /* FIREBALL   */ { "Fireball",   SKK_DAMAGE,  DMG_FIRE,   0,  8,  6, 2, 1, 0 },
    /* FLAMENOVA  */ { "Flame Nova", SKK_DAMAGE,  DMG_FIRE,   0, 16, 12, 3, 1, 0 },
    /* LIGHTNING  */ { "Lightning",  SKK_DAMAGE,  DMG_LIGHT,  1, 12,  8, 2, 1, 0 },
    /* THUNDER    */ { "Thunderclap",SKK_DAMAGE,  DMG_LIGHT,  1, 22, 14, 3, 1, 0 },
    /* POISON     */ { "Poison",     SKK_DOT,     DMG_POISON, 0, 10,  4, 1, 0, 4 },
    /* PLAGUE     */ { "Plague",     SKK_DOT,     DMG_POISON, 0, 18,  7, 1, 0, 6 },
    /* POWERSTRIKE*/ { "Power Strike",SKK_DAMAGE, DMG_PHYS,   0, 10, 10, 2, 1, 0 },
    /* LIFEDRAIN  */ { "Lifedrain",  SKK_DRAIN,   DMG_POISON, 0, 14,  8, 2, 1, 0 },
    /* HEAL       */ { "Heal",       SKK_HEAL,    -1,         0, 12, 12, 2, 1, 0 },
    /* REGEN      */ { "Regen",      SKK_HOT,     -1,         0, 14,  5, 1, 0, 4 },
    /* SHIELD     */ { "Shield",     SKK_SHIELD,  -1,         0, 10,  0, 0, 0, 3 },
    /* M_FIRE     */ { "Fire Mastery",   SKK_PASSIVE, DMG_FIRE,   0, 0, 0, 0, 0, 0 },
    /* M_LIGHT    */ { "Storm Mastery",  SKK_PASSIVE, DMG_LIGHT,  0, 0, 0, 0, 0, 0 },
    /* M_POISON   */ { "Venom Mastery",  SKK_PASSIVE, DMG_POISON, 0, 0, 0, 0, 0, 0 },
    /* M_VAMP     */ { "Vampirism",      SKK_PASSIVE, -1,         0, 0, 0, 0, 0, 0 },
};

static_assert(SK_COUNT <= SKILL_SLOTS, "Слишком много навыков для Player.skillRank[]");

int skillRank(int id)
{
    if (id < 0 || id >= SK_COUNT) return 0;
    return gp.skillRank[id];
}

bool skillUsable(int id)
{
    return id >= 0 && id < SK_COUNT && SKILLS[id].kind != SKK_PASSIVE && gp.skillRank[id] > 0;
}

// Сила навыка на ранге: base + perRank*(rank-1) + статХар*intelMul (STR для физ., иначе INT).
int skillPowerAt(int id, int rank)
{
    if (id < 0 || id >= SK_COUNT) return 0;
    const SkillDef &s = SKILLS[id];
    int stat = (s.dmgType == DMG_PHYS) ? gp.str : gp.intel;
    int p = s.basePow + s.perRank * (rank - 1) + stat * s.intelMul;
    return p < 1 ? 1 : p;
}

// Длительность эффекта в ходах: ранг немного удлиняет.
int skillDurationAt(int id, int rank)
{
    if (id < 0 || id >= SK_COUNT) return 0;
    const SkillDef &s = SKILLS[id];
    if (s.kind == SKK_SHIELD) return s.turns + rank / 10;
    if (s.kind == SKK_DOT || s.kind == SKK_HOT) return s.turns + rank / 8;
    return 0;
}

bool skillPassive(int id) { return id >= 0 && id < SK_COUNT && SKILLS[id].kind == SKK_PASSIVE; }

// +1% к урону типа за каждый ранг соответствующего мастерства (до +50% на ранге 50).
int playerMasteryPct(int dmgType)
{
    if (dmgType < 0) return 0;
    int pct = 0;
    for (int i = 0; i < SK_COUNT; i++)
        if (SKILLS[i].kind == SKK_PASSIVE && SKILLS[i].dmgType == dmgType)
            pct += gp.skillRank[i];
    return pct;
}

// Пассивный вампиризм: +1% HP от нанесённого физ. урона за ранг (до +50% на ранге 50).
int playerLifestealPct() { return gp.skillRank[SK_M_VAMP]; }
