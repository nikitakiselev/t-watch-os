#pragma once
#include "config.h"
#include "input.h"

// Единая абстракция «программа». Главный экран, список приложений и любое
// приложение — всё это равноправные Program. Ядро (kernel) переключается
// между ними по стеку.
// Кнопка нижней панели навигации.
struct NavButton {
    const char *label;
    void (*onPress)();
};
#define NAV_MAX 3   // максимум кнопок в панели

struct Program {
    const char *name;
    void (*onEnter)();                                   // открытие/перерисовка
    void (*onTick)();                                    // периодический тик (может быть nullptr)
    void (*onEvent)(InputEvent e, int16_t x, int16_t y); // обработка ввода (может быть nullptr)
    // Иконка для списка приложений: рисует себя по центру (cx,cy) радиусом r.
    // Цель — TFT_eSPI или TFT_eSprite (спрайт наследует TFT_eSPI). Может быть nullptr.
    void (*drawIcon)(TFT_eSPI &g, int cx, int cy, int r);
    // Переопределение кнопок нижней панели (до NAV_MAX). Если nav==nullptr или
    // navCount==0 — фреймворк рисует одну кнопку по умолчанию: «Back».
    const NavButton *nav;
    int              navCount;
    // Энергопрофиль: вернуть true, если приложению нужно продолжать работать на
    // простое (radio играет, секундомер идёт). Тогда гасится только экран, а CPU
    // не уходит в light sleep. nullptr → можно спать как обычно.
    bool (*keepAwake)();
    // Вызывается ядром при выходе из программы (kernelBack), до снятия со стека.
    // Здесь освобождают ресурсы (например, Wi-Fi). nullptr — ничего не делать.
    void (*onExit)();
    // Пробуждение из сна: ТОЛЬКО перерисовать экран, БЕЗ повторной настройки
    // ресурсов (в отличие от onEnter). Если nullptr — ядро падает обратно на onEnter.
    void (*onResume)();
};

// ─────────────────────────────── Ядро ───────────────────────────────
void           kernelOpen(const Program *p);  // открыть программу (push в стек)
void           kernelBack();                  // вернуться назад (pop)
void           kernelRedraw();                // перерисовать текущую программу (без glitch)
void           kernelResume();                // пробуждение: onResume (или onEnter, если нет)
const Program *kernelCurrent();               // текущая программа (вершина стека)
