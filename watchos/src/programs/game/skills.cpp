#include "skills.h"
#include "player.h"

// Навыки: имя, стоимость маны, уровень изучения.
const SkillDef SKILLS[SK_COUNT] = {
    { "Fireball",  10, 1 },   // SK_FIREBALL — урон магией
    { "Heal",      12, 2 },   // SK_HEAL     — лечение
    { "Lightning", 18, 3 },   // SK_LIGHTNING— сильный урон, игнор брони
    { "Shield",    10, 4 },   // SK_SHIELD   — урон по тебе ×½ на 3 хода
    { "Poison",    14, 5 },   // SK_POISON   — урон врагу 4 хода
};

bool skillKnown(int id)
{
    return id >= 0 && id < SK_COUNT && gp.level >= SKILLS[id].unlock;
}

int skillLearnedAtLevel(int level)
{
    for (int i = 0; i < SK_COUNT; i++)
        if (SKILLS[i].unlock == level) return i;
    return -1;
}
