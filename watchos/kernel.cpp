#include "program.h"
#include "navbar.h"
#include "theme.h"

// Стек программ: вершина — активная программа. Корень (дно стека) —
// главный экран, из него «назад» уже некуда.
#define KERNEL_STACK_MAX 8
static const Program *stack[KERNEL_STACK_MAX];
static int top = -1;

// Перерисовать программу целиком: её контент + нижняя панель кнопок.
static void enter(const Program *p)
{
    glitchFlash();              // glitch-переход (если FX_GLITCH выключен — ничего не делает)
    if (p->onEnter) p->onEnter();
    navbarDraw(p);
}

void kernelOpen(const Program *p)
{
    if (!p) return;
    if (top < KERNEL_STACK_MAX - 1) {
        stack[++top] = p;
        enter(p);
    }
}

void kernelBack()
{
    if (top > 0) {                  // корень не закрываем
        const Program *leaving = stack[top];
        if (leaving->onExit) leaving->onExit();   // освободить ресурсы (Wi-Fi и т.п.)
        top--;
        enter(stack[top]);          // перерисовать программу, на которую вернулись
    }
}

void kernelRedraw()
{
    const Program *p = kernelCurrent();
    if (!p) return;
    if (p->onEnter) p->onEnter();
    navbarDraw(p);
}

const Program *kernelCurrent()
{
    return (top >= 0) ? stack[top] : nullptr;
}
