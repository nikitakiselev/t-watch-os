# Как писать приложения для watchos

Приложение — это обычная `Program` (см. `../../program.h`). Тот же интерфейс, что у главного
экрана и списка приложений. Файлы приложений лежат здесь, в `watchos/src/programs/`.

## 1. Интерфейс `Program`

```c
struct Program {
    const char *name;                                    // имя в списке приложений
    void (*onEnter)();                                   // открытие/перерисовка экрана
    void (*onTick)();                                    // периодический тик (может быть nullptr)
    void (*onEvent)(InputEvent e, int16_t x, int16_t y); // ввод (может быть nullptr)
    void (*drawIcon)(TFT_eSPI &g, int cx, int cy, int r);// иконка для списка (может быть nullptr)
    const NavButton *nav;                                // кнопки нижней панели (nullptr → «Back»)
    int              navCount;
    bool (*keepAwake)();                                 // не усыплять CPU, пока true (может быть nullptr)
};
```

Жизненный цикл (всё делает ядро `kernel.cpp`):

- `onEnter()` — при открытии приложения (`kernelOpen`) и при возврате на него (`kernelBack`), а
  также после пробуждения экрана. Здесь рисуем статичную часть экрана **с нуля**.
- `onTick()` — каждый цикл (~40 мс) **только пока приложение на переднем плане**. Здесь обновляем
  динамику (время, счётчики). НЕ делай тут `fillScreen` — рисуй точечно, иначе мерцание.
- `onEvent(e, x, y)` — на событие ввода (кроме тех, что забрал навбар и кроме `BACK`).

Важно: свёрнутое приложение (ушли по «Back») **перестаёт тикать** — `onTick` зовётся только у
текущей программы. Фоновой работы у обычного приложения нет (см. §4 про радио).

## 2. Ввод

События (`InputEvent` из `../../input.h`):

| Событие | Откуда | Типичный смысл |
|---|---|---|
| `EVT_CLICK` | короткое нажатие боковой кнопки | войти / выбрать |
| `EVT_BACK`  | долгое нажатие боковой кнопки | назад — **обрабатывает ядро**, до приложения не доходит |
| `EVT_UP` / `EVT_DOWN` | свайп вниз / вверх по тачу | листание |
| `EVT_TAP`   | короткое касание (есть координаты x, y) | нажатие по элементу |

`EVT_BACK` ловит ядро (делает pop стека) — в `onEvent` его обрабатывать не нужно. Тап по нижней
панели кнопок тоже перехватывается **до** `onEvent`.

## 3. Нижняя панель кнопок (как в Android)

По умолчанию у приложения одна кнопка **«Back»** (возврат). Можно переопределить — до `NAV_MAX` (3)
кнопок. Кнопка = `NavButton { label, onPress }`, где `onPress` — обычная `void()`-функция.

```c
static void doRun()   { /* ... */ }
static void doReset() { /* ... */ }

static const NavButton myNav[] = {
    { "Back",  kernelBack },   // kernelBack — штатный возврат
    { "Run",   doRun },
    { "Reset", doReset },
};
// ...в Program: ..., myNav, 3, ...
```

Подписи короткие (рисуются font 2, ширина экрана делится на число кнопок). Кнопка «Back» по
соглашению красится янтарным. Тап по панели вызывает `onPress` соответствующей кнопки.

## 4. Энергосбережение (`keepAwake`)

Модель **бинарная**: на простое (`SLEEP_IDLE_MS`) устройство **полностью останавливается** (экран
гаснет + CPU в light sleep), пробуждение **только боковой кнопкой**.

`keepAwake()` — исключение: верни `true`, чтобы **вообще не давать устройству уснуть** (секундомер
идёт, радио играет). Тогда устройство остаётся полностью активным (экран включён, CPU работает).
Промежуточного «экран выключен, CPU крутится» состояния нет.

```c
static bool myKeepAwake() { return running; }   // напр. пока секундомер запущен
// ...в Program: ..., myKeepAwake, ...
```

`keepAwake()` опрашивается только у **текущего** приложения → выход по «Back» снимает удержание.
Ресурсы освобождай в `onExit()` (вызывается ядром при выходе).

> Фоновое радио (играет, пока ходишь по другим экранам): одного `keepAwake` мало — свёрнутое
> приложение не тикает. Понадобится явный wake-lock + фоновая FreeRTOS-задача. Проектировать
> вместе с плеером.

## 5. Отрисовка

- Глобальные `tft` (`TFT_eSPI*`) и `watch` (`TTGOClass*`) — из `../../hw.h`.
- Рисовать строго в области контента: между `CONTENT_TOP` (низ статусбара) и `CONTENT_BOTTOM`
  (верх навбара). Статусбар и навбар рисует фреймворк — не залезать.
- Цвета темы из `../../theme.h`: `COL_GREEN`, `COL_AMBER`, `COL_GREEN_DIM`, `COL_BG`, `COL_FRAME`.
  Фон: `themeBackdrop()` в начале `onEnter`, затем `statusbarDraw()`.
- Текст: `tft->setTextColor(col)` (1 арг) — прозрачный фон; `setTextColor(fg, COL_BG)` — с заливкой.
- Иконка приложения (`drawIcon`) — ASCII-арт через `drawAsciiArt(g, lines, n, cx, cy, color, r/8)`.
  **Только ASCII** (font 1 моноширинный); кириллицы в шрифтах нет.
- Время/батарея: `hwNow()` (RTC, база MSK), `hwTimeInZone(now, offsetMin, h, m, s)`, `hwBattPercent()`.

## 6. Регистрация

В `../../apps.cpp`: подключить заголовок и добавить указатель в массив:

```c
#include "src/programs/prog_myapp.h"
static const Program *const appList[] = { ..., &myAppProgram };
```

---

## Готовый шаблон

`prog_myapp.h`:

```c
#pragma once
#include "../../program.h"
extern const Program myAppProgram;
```

`prog_myapp.cpp`:

```c
#include "prog_myapp.h"
#include "../../hw.h"
#include "../../statusbar.h"
#include "../../theme.h"

static int counter = 0;

static void appEnter()
{
    themeBackdrop();
    statusbarDraw();
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_AMBER);
    tft->drawString("[ MY APP ]", SCR_W / 2, STATUSBAR_H + 16, 2);
}

static void appEvent(InputEvent e, int16_t x, int16_t y)
{
    if (e == EVT_TAP || e == EVT_CLICK) counter++;
    // EVT_BACK сюда не приходит — им рулит ядро
}

static void appTick()
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", counter);
    tft->fillRect(0, 110, SCR_W, 40, COL_BG);          // точечная очистка, не fillScreen
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_GREEN, COL_BG);
    tft->drawString(buf, SCR_W / 2, 130, 6);
}

static const char *const ICON[] = { ".--.", "|##|", "'--'" };
static void appIcon(TFT_eSPI &g, int cx, int cy, int r)
{
    drawAsciiArt(g, ICON, 3, cx, cy, COL_GREEN, r / 8);
}

// Дефолтная панель («Back») — nav=nullptr, navCount=0. keepAwake не нужен — nullptr.
const Program myAppProgram = {
    "My App", appEnter, appTick, appEvent, appIcon, nullptr, 0, nullptr
};
```
