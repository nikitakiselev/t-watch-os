#include "navbar.h"
#include "hw.h"
#include "theme.h"

// Кнопка по умолчанию — «Back» (возврат на предыдущую программу).
static const NavButton DEFAULT_NAV[] = { { "Back", kernelBack } };

// Возвращает действующий набор кнопок программы (или дефолтный).
static void resolveNav(const Program *p, const NavButton **btns, int *n)
{
    if (p && p->nav && p->navCount > 0) {
        *btns = p->nav;
        *n    = (p->navCount > NAV_MAX) ? NAV_MAX : p->navCount;
    } else {
        *btns = DEFAULT_NAV;
        *n    = 1;
    }
}

void navbarDraw(const Program *p)
{
    if (p && p->navCount < 0) return;       // программа сама занимает всё (без панели)

    const NavButton *btns;
    int n;
    resolveNav(p, &btns, &n);

    const int top = SCR_H - NAVBAR_H;
    tft->fillRect(0, top, SCR_W, NAVBAR_H, COL_BG);
    tft->drawFastHLine(0, top, SCR_W, COL_FRAME);

    const int w = SCR_W / n;
    tft->setTextDatum(MC_DATUM);
    for (int i = 0; i < n; i++) {
        int x0 = i * w;
        if (i > 0) tft->drawFastVLine(x0, top + 5, NAVBAR_H - 10, COL_FRAME);
        // «Назад» — янтарный акцент, остальные — зелёные.
        bool back = btns[i].label && btns[i].label[0] == 'B';
        tft->setTextColor(back ? COL_AMBER : COL_GREEN, COL_BG);
        tft->drawString(btns[i].label, x0 + w / 2, top + NAVBAR_H / 2, 2);
    }
}

bool navbarHandleTap(const Program *p, int16_t x, int16_t y)
{
    if (p && p->navCount < 0) return false;   // у программы нет панели
    const int top = SCR_H - NAVBAR_H;
    if (y < top) return false;          // тап не по панели

    const NavButton *btns;
    int n;
    resolveNav(p, &btns, &n);

    const int w = SCR_W / n;
    int idx = x / w;
    if (idx >= n) idx = n - 1;
    if (idx < 0)  idx = 0;

    if (btns[idx].onPress) btns[idx].onPress();
    return true;
}
