#include "power.h"
#include "hw.h"
#include "input.h"
#include "program.h"
#include <esp_sleep.h>
#include <driver/gpio.h>

// Пин прерывания кнопки (T-Watch 2020 V3). Из сна будим ТОЛЬКО кнопкой:
// акселерометр давал ложные срабатывания, тач держит INT в LOW в покое.
static const gpio_num_t PIN_AXP_IRQ = GPIO_NUM_35;   // кнопка (AXP202 PEK), активный LOW

static uint32_t lastActivity = 0;

void powerBegin()
{
    pinMode(PIN_AXP_IRQ, INPUT);   // линия AXP IRQ; внешняя подтяжка на плате, в покое HIGH

    // Оставляем включённым ТОЛЬКО прерывание кнопки (PEK). Прочие прерывания AXP
    // (заряд/VBUS/батарея) держали бы линию в LOW и будили из сна сразу.
    watch->power->enableIRQ(0xFFFFFFFFFFFFFFFFULL, false);
    watch->power->enableIRQ(AXP202_PEK_SHORTPRESS_IRQ | AXP202_PEK_LONGPRESS_IRQ, true);
    watch->power->clearIRQ();

    lastActivity = millis();
}

void powerNoteActivity()
{
    lastActivity = millis();
}

// Приложение-исключение не даёт устройству уснуть (секундомер идёт, радио играет).
static bool appKeepAwake()
{
    const Program *p = kernelCurrent();
    return p && p->keepAwake && p->keepAwake();
}

static void fadeBacklight(bool down)
{
    if (down) for (int b = 255; b >= 0; b -= 17) { watch->setBrightness(b < 0 ? 0 : b); delay(8); }
    else      for (int b = 0; b <= 255; b += 17) { watch->setBrightness(b > 255 ? 255 : b); delay(8); }
}

// Полная остановка устройства: гасим экран и усыпляем CPU. Пробуждение — кнопкой.
static void fullSleep()
{
    fadeBacklight(true);
    watch->closeBL();
    watch->displaySleep();

    watch->power->clearIRQ();      // снять защёлку, чтобы GPIO35 был HIGH (иначе мгновенное пробуждение)
    gpio_wakeup_enable(PIN_AXP_IRQ, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();

    esp_light_sleep_start();       // ← здесь устройство стоит до нажатия кнопки

    // ── проснулись по кнопке ──
    watch->displayWakeup();
    delay(120);                    // ST7789 после sleep-out
    watch->power->clearIRQ();      // не считать кнопку-пробуждение за нажатие
    inputBegin();                  // сбросить состояние жеста тача
    kernelRedraw();                // перерисовать текущую программу
    watch->openBL();
    fadeBacklight(false);
    lastActivity = millis();
}

void powerTick()
{
    if (appKeepAwake()) {          // исключение: вообще не спим
        lastActivity = millis();
        return;
    }
    if (millis() - lastActivity >= SLEEP_IDLE_MS) fullSleep();
}
