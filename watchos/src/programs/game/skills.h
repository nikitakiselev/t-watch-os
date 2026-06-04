#pragma once

enum SkillId { SK_FIREBALL = 0, SK_HEAL, SK_LIGHTNING, SK_SHIELD, SK_POISON, SK_COUNT };

struct SkillDef { const char *name; int cost; int unlock; };   // unlock — уровень изучения
extern const SkillDef SKILLS[SK_COUNT];

bool skillKnown(int id);              // изучен (gp.level >= unlock)
int  skillLearnedAtLevel(int level);  // навык, открываемый ровно на этом уровне (или -1)
