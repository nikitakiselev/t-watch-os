#include "apps.h"
#include "src/programs/prog_clock.h"
#include "src/programs/prog_radio.h"
#include "src/programs/dungeon/prog_dungeon.h"
#include "src/programs/prog_settings.h"
#include "src/programs/prog_assistant.h"
#include "src/programs/prog_notes.h"
#include "src/programs/prog_steps.h"

// ─────────────────────── Реестр приложений ───────────────────────
// Приложение — это обычная Program. Файлы программ лежат в src/programs/.
//
// Чтобы добавить приложение:
//   1) создайте src/programs/prog_myapp.{h,cpp} с `const Program myAppProgram = {...};`
//   2) подключите заголовок выше (#include "src/programs/prog_myapp.h");
//   3) добавьте &myAppProgram в массив appList ниже.

static const Program *const appList[] = {
    &clockProgram,
    &radioProgram,
    &assistantProgram,
    &notesProgram,
    &stepsProgram,
    &dungeonProgram,
    &settingsProgram,
};

const Program *const *APPS       = appList;
const int             APPS_COUNT = sizeof(appList) / sizeof(appList[0]);
