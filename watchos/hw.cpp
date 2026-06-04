#include "hw.h"
#include <time.h>

// Реальное время хоста на момент сборки (UNIX-эпоха, UTC) — подставляется
// генератором build_time.h перед компиляцией. 0 — фоллбэк на время компиляции.
#if defined(__has_include)
#  if __has_include("build_time.h")
#    include "build_time.h"
#  endif
#endif
#ifndef BUILD_UNIX_TIME
#  define BUILD_UNIX_TIME 0
#endif

TTGOClass *watch = nullptr;
TFT_eSPI  *tft   = nullptr;

// (год,мес,день,ч,м,с) a позже b ?
static bool dateAfter(const struct tm &a, const RTC_Date &b)
{
    long ai[6] = { a.tm_year + 1900, a.tm_mon + 1, a.tm_mday, a.tm_hour, a.tm_min, a.tm_sec };
    long bi[6] = { b.year, b.month, b.day, b.hour, b.minute, b.second };
    for (int i = 0; i < 6; i++) {
        if (ai[i] != bi[i]) return ai[i] > bi[i];
    }
    return false;
}

// Выставляем RTC (базовый пояс — MSK) из реального времени сборки, если оно
// новее текущего RTC. RTC батарейный — выставив однажды, дальше идёт сам.
static void syncClock()
{
    RTC_Date r = watch->rtc->getDateTime();

#if (BUILD_UNIX_TIME > 0)
    time_t msk = (time_t)BUILD_UNIX_TIME + (time_t)3 * 3600;   // MSK = UTC+3
    struct tm bt;
    gmtime_r(&msk, &bt);
    if (r.year < 2024 || dateAfter(bt, r)) {
        watch->rtc->setDateTime(RTC_Date(bt.tm_year + 1900, bt.tm_mon + 1, bt.tm_mday,
                                         bt.tm_hour, bt.tm_min, bt.tm_sec));
    }
#else
    if (r.year < 2024) {
        watch->rtc->setDateTime(RTC_Date(__DATE__, __TIME__));   // фоллбэк
    }
#endif
}

void hwBegin()
{
    watch = TTGOClass::getWatch();
    watch->begin();               // AXP202 + ST7789 + датчики
    watch->openBL();              // подсветка
    watch->setBrightness(255);
    tft = watch->tft;

    // Включаем ADC-каналы AXP202 для измерения тока/мощности (по умолчанию
    // библиотека их не включает → getBattChargeCurrent/Inpower возвращают 0).
    watch->power->adc1Enable(AXP202_BATT_VOL_ADC1 | AXP202_BATT_CUR_ADC1 |
                             AXP202_VBUS_VOL_ADC1 | AXP202_VBUS_CUR_ADC1, true);

    syncClock();

    // Кнопка питания (AXP202 PEK): короткое и долгое нажатие через IRQ.
    watch->power->enableIRQ(AXP202_PEK_SHORTPRESS_IRQ | AXP202_PEK_LONGPRESS_IRQ, true);
    watch->power->clearIRQ();
}

int hwBattPercent()
{
    // Встроенный getBattPercentage() AXP202 на этом железе ненадёжен (залипает на 100%),
    // поэтому оцениваем заряд по напряжению батареи через таблицу Li-ion.
    if (!watch->power->isBatteryConnect()) return 100;   // нет батареи (только USB)
    float v = watch->power->getBattVoltage();            // мВ
    static const int   N = 11;
    static const float vt[N] = { 4150, 4050, 3970, 3900, 3850, 3800, 3760, 3720, 3680, 3500, 3300 };
    static const int   pt[N] = {  100,   90,   80,   70,   60,   50,   40,   30,   20,   10,    0 };
    if (v >= vt[0]) return 100;
    for (int i = 1; i < N; i++)
        if (v >= vt[i]) {
            float f = (v - vt[i]) / (vt[i - 1] - vt[i]);
            return (int)(pt[i] + f * (pt[i - 1] - pt[i]) + 0.5f);
        }
    return 0;
}

RTC_Date hwNow()
{
    return watch->rtc->getDateTime();
}

void hwTimeInZone(const RTC_Date &base, int offsetMin, int &h, int &m, int &s)
{
    long total = (long)base.hour * 3600 + (long)base.minute * 60 + base.second;
    total += (long)offsetMin * 60;
    total %= 86400L;
    if (total < 0) total += 86400L;   // перенос через полночь назад
    h = total / 3600;
    m = (total % 3600) / 60;
    s = total % 60;
}
