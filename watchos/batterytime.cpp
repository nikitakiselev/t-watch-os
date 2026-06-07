#include "batterytime.h"
#include "hw.h"
#include <Preferences.h>
#include <time.h>

static Preferences prefs;
static uint32_t baseEpoch  = 0;       // момент последнего отключения USB (UNIX, RTC)
static bool     wasPlugged = false;   // последнее известное состояние USB
static uint32_t lastPoll   = 0;

// Текущее время RTC → UNIX-секунды. Часовой пояс несущественен: считаем разницу.
static uint32_t nowEpoch()
{
    RTC_Date d = hwNow();
    struct tm t = {};
    t.tm_year  = d.year - 1900;
    t.tm_mon   = d.month - 1;
    t.tm_mday  = d.day;
    t.tm_hour  = d.hour;
    t.tm_min   = d.minute;
    t.tm_sec   = d.second;
    t.tm_isdst = 0;
    time_t e = mktime(&t);
    return (e < 0) ? 0 : (uint32_t)e;
}

void battTimeBegin()
{
    prefs.begin("batt", false);
    bool plugNow = watch->power->isVBUSPlug();

    if (!prefs.isKey("base")) {            // первый запуск — инициализируем
        baseEpoch  = nowEpoch();
        wasPlugged = plugNow;
        prefs.putULong("base", baseEpoch);
        prefs.putBool("plug", wasPlugged);
        return;
    }

    baseEpoch  = prefs.getULong("base", nowEpoch());
    wasPlugged = prefs.getBool("plug", plugNow);

    // Догоняем переход, пропущенный пока часы были выключены/спали:
    if (wasPlugged && !plugNow) {          // сняли с зарядки во сне → старт отсчёта
        baseEpoch  = nowEpoch();
        wasPlugged = false;
        prefs.putULong("base", baseEpoch);
        prefs.putBool("plug", false);
    } else if (!wasPlugged && plugNow) {   // поставили на зарядку
        wasPlugged = true;
        prefs.putBool("plug", true);
    }
}

void battTimePoll()
{
    uint32_t ms = millis();
    if (ms - lastPoll < 2000) return;      // раз в ~2 с достаточно
    lastPoll = ms;

    bool plugNow = watch->power->isVBUSPlug();
    if (plugNow == wasPlugged) return;     // состояние не менялось

    if (!plugNow) {                        // сняли с зарядки → начинаем отсчёт
        baseEpoch = nowEpoch();
        prefs.putULong("base", baseEpoch);
    }
    wasPlugged = plugNow;
    prefs.putBool("plug", plugNow);
}

bool battTimeCharging()
{
    return watch->power->isVBUSPlug();
}

uint32_t battTimeSinceChargeSec()
{
    if (watch->power->isVBUSPlug()) return 0;
    uint32_t now = nowEpoch();
    return (now > baseEpoch) ? (now - baseEpoch) : 0;
}
