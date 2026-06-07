/**
 * watchOS — простая модульная прошивка для LilyGo T-Watch-2020 V3.
 *
 * Всё построено как набор равноправных «программ» (Program): главный экран,
 * список приложений и любые приложения — однотипные модули с интерфейсом
 * { onEnter, onTick, onEvent }. Ядро (kernel.cpp) переключает их по стеку.
 *
 * Программы:
 *   prog_home — статусбар (заряд) + трое часов (MSK крупно, UTC, EST) + "Apps".
 *   prog_menu — список приложений с пагинацией (сейчас пуст).
 *   prog_*    — будущие приложения (каждое в своём файле, см. apps.cpp).
 *
 * Управление (тач + боковая кнопка AXP202):
 *   короткое нажатие кнопки → CLICK («назад» на предыдущий экран; на корне — спец-действие, напр. сон)
 *   долгое нажатие кнопки    → BACK  (ВСЕГДА на главный экран, с любого уровня; ядро)
 *   свайп вверх/вниз         → листание списка
 *   тап по "Apps"            → открыть меню
 */
#include "config.h"
#include "hw.h"
#include "input.h"
#include "program.h"
#include "navbar.h"
#include "theme.h"
#include "power.h"
#include "batterytime.h"
#include "wifi.h"
#include "sound.h"
#include "stations.h"
#include "src/programs/prog_home.h"

static void bootSplash()
{
#if FX_TYPEWRITER
    themeBackdrop();
    typeOut("> watchOS v0.1",  18, 92,  COL_GREEN, 2, 38);
    typeOut("> booting_",      18, 122, COL_AMBER, 2, 38);
    delay(450);
#endif
}

void setup()
{
    Serial.begin(115200);
    hwBegin();
    battTimeBegin();   // учёт времени от батареи (после hwBegin: RTC готов)
    wifiBegin();
    stationsBegin();
    audioBegin();
    inputBegin();
    powerBegin();
    bootSplash();
    kernelOpen(&homeProgram);   // корневая программа
}

void loop()
{
    int16_t tx = 0, ty = 0;
    InputEvent e = inputPoll(tx, ty);

    if (e != EVT_NONE) powerNoteActivity();   // любой ввод сбрасывает таймер сна

    const Program *p = kernelCurrent();

    if (e == EVT_TAP && navbarHandleTap(p, tx, ty)) {
        // тап по нижней панели кнопок — обработан фреймворком
    } else if (e == EVT_BACK) {
        kernelHome();                       // ДОЛГОЕ нажатие — всегда на главный экран (с любого уровня)
    } else if (e == EVT_CLICK) {
        // КОРОТКИЙ клик — «назад» на предыдущий экран. На корне (назад некуда)
        // выполняем спец-действие программы (главный экран → сон), если задано.
        if (kernelAtRoot()) { if (p && p->onButton) p->onButton(); }
        else                kernelBack();
    } else if (e != EVT_NONE && p && p->onEvent) {
        p->onEvent(e, tx, ty);
    }

    // Тик выполняем для текущей программы (она могла смениться выше).
    p = kernelCurrent();
    if (p && p->onTick) p->onTick();

    battTimePoll();   // отследить отключение/подключение USB
    powerTick();      // при простое уснёт здесь (light sleep)

    // Активная программа (keepAwake: радио играет, секундомер) — мы и так не спим,
    // крутим цикл часто для плавной анимации. Иначе бережём CPU (~25 fps).
    delay(p && p->keepAwake && p->keepAwake() ? 5 : 40);
}
