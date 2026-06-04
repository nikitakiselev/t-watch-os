#include "listnav.h"

bool listNavEvent(ListNav &l, InputEvent e)
{
    if (l.count <= 0) return false;
    int s = l.sel, t = l.top;
    if (e == EVT_UP) {
        if (l.sel > 0) l.sel--;
    } else if (e == EVT_DOWN) {
        if (l.sel < l.count - 1) l.sel++;
    } else {
        return false;
    }
    if (l.vis > 0) {                                  // подстроить окно прокрутки
        if (l.sel < l.top) l.top = l.sel;
        else if (l.sel >= l.top + l.vis) l.top = l.sel - l.vis + 1;
    }
    return l.sel != s || l.top != t;
}
