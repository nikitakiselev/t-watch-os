#include "hw.h"
#include <time.h>
#include <math.h>
#include <Preferences.h>

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

// ─────────────────────────── Яркость дисплея ───────────────────────────
static const uint8_t BRIGHT_MIN = 20;     // ниже — экран практически не виден
static uint8_t       gBright     = 255;    // текущий уровень (общий на всю ОС)

uint8_t hwBrightness() { return gBright; }

void hwSetBrightness(uint8_t level)
{
    if (level < BRIGHT_MIN) level = BRIGHT_MIN;
    gBright = level;
    if (watch) watch->setBrightness(gBright);

    Preferences p;                          // сохранить в NVS (namespace "settings")
    if (p.begin("settings", false)) { p.putUChar("bright", gBright); p.end(); }
}

void hwVibrate(int ms)
{
    // Не tone (onec = ~50% скважность, слишком сильно), а слабый ШИМ: мотор на
    // канале ledc настроен на 1 кГц / 8 бит, низкая скважность → мягкий импульс.
    static const uint8_t VIBE_DUTY = 45;     // 0..255 — сила (меньше = слабее)
    static bool inited = false;
    if (!inited) { watch->motor_begin(); inited = true; }
    if (!watch->motor) return;
    watch->motor->adjust(VIBE_DUTY);
    delay(ms);
    watch->motor->adjust(0);
}

static void brightnessBegin()               // загрузить из NVS и применить (при старте)
{
    Preferences p;
    if (p.begin("settings", true)) { gBright = p.getUChar("bright", 255); p.end(); }
    if (gBright < BRIGHT_MIN) gBright = BRIGHT_MIN;
    watch->setBrightness(gBright);
}

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

// Синхронизация RTC по NTP. Wi-Fi должен быть уже поднят вызывающим.
bool hwNtpSync(uint32_t timeoutMs)
{
    configTime(0, 0, "pool.ntp.org", "time.google.com", "time.nist.gov");  // UTC
    uint32_t start = millis();
    time_t now = 0;
    while (millis() - start < timeoutMs) {
        now = time(nullptr);
        if (now > 1700000000) break;          // получили реальное время (после ~2023-11)
        delay(150);
    }
    if (now <= 1700000000) return false;

    time_t msk = now + (time_t)3 * 3600;      // RTC хранит MSK = UTC+3
    struct tm bt;
    gmtime_r(&msk, &bt);
    watch->rtc->setDateTime(RTC_Date(bt.tm_year + 1900, bt.tm_mon + 1, bt.tm_mday,
                                     bt.tm_hour, bt.tm_min, bt.tm_sec));
    return true;
}

void hwBegin()
{
    watch = TTGOClass::getWatch();
    watch->begin();               // AXP202 + ST7789 + датчики
    watch->openBL();              // подсветка
    brightnessBegin();            // яркость из NVS (общая на ОС), применить
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

// ─────────────────────────── Акселерометр (BMA423) ───────────────────────
static bool accelReady = false;

void hwAccelBegin()
{
    if (accelReady) return;
    Acfg cfg;
    cfg.odr       = BMA4_OUTPUT_DATA_RATE_100HZ;
    cfg.range     = BMA4_ACCEL_RANGE_2G;
    cfg.bandwidth = BMA4_ACCEL_NORMAL_AVG4;
    cfg.perf_mode = BMA4_CONTINUOUS_MODE;
    watch->bma->accelConfig(cfg);
    watch->bma->enableAccel();
    accelReady = true;
}

bool hwAccelRead(int16_t &x, int16_t &y, int16_t &z)
{
    hwAccelBegin();
    Accel a;
    if (!watch->bma->getAccel(a)) return false;
    x = a.x; y = a.y; z = a.z;
    return true;
}

// Поправка ориентации датчика под экран T-Watch 2020 V3 (оси повёрнуты на 90°):
//   горизонталь экрана (вправо) ← +Y,  вертикаль (вниз) ← -X.
void hwAccelToScreen(int16_t x, int16_t y, int16_t z, float &sx, float &sy)
{
    float mag = sqrtf((float)((int32_t)x * x + (int32_t)y * y + (int32_t)z * z));
    if (mag < 1.0f) mag = 1.0f;
    sx =  y / mag;
    sy = -x / mag;
}

const char *hwAccelDirection(int16_t x, int16_t y, int16_t z)
{
    int ax = abs(x), ay = abs(y), az = abs(z);
    if (az > ax && az > ay) return z > 0 ? "FACE DOWN" : "FACE UP";
    if (ax > ay && ax > az) return x < 0 ? "BOTTOM DN" : "TOP DN";
    return y > 0 ? "RIGHT DN" : "LEFT DN";
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
