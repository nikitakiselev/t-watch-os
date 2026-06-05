#pragma once
#include "entities.h"

enum CombatResult { CR_WIN, CR_LOSE, CR_FLEE };

// Модальный пошаговый бой с монстром mon. Меняет глобального игрока gp.
// Затирает экран — вызывающий после возврата перерисовывает свой экран.
CombatResult combatRun(Monster &mon);
