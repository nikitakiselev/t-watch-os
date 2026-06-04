#include "apps.h"
#include "src/programs/prog_clock.h"
#include "src/programs/prog_wifi.h"
#include "src/programs/prog_radio.h"
#include "src/programs/game/prog_game.h"
#include "src/programs/prog_touchtest.h"
#include "src/programs/prog_stopwatch.h"
#include "src/programs/prog_settings.h"
#include "src/programs/prog_system.h"

// ─────────────────────── Реестр приложений ───────────────────────
// Приложение — это обычная Program. Файлы программ лежат в src/programs/.
//
// Чтобы добавить приложение:
//   1) создайте src/programs/prog_myapp.{h,cpp} с `const Program myAppProgram = {...};`
//   2) подключите заголовок выше (#include "src/programs/prog_myapp.h");
//   3) добавьте &myAppProgram в массив appList ниже.

static const Program *const appList[] = {
    &clockProgram,
    &wifiProgram,
    &radioProgram,
    &gameProgram,
    &touchtestProgram,
    &stopwatchProgram,
    &settingsProgram,
    &systemProgram,
};

const Program *const *APPS       = appList;
const int             APPS_COUNT = sizeof(appList) / sizeof(appList[0]);
