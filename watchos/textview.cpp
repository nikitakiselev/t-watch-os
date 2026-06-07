#include "textview.h"
#include "hw.h"
#include "theme.h"
#include "notesfont.h"
#include <string.h>

static const int SCROLLBAR_W = 6;
static const int LINE_PAD     = 4;        // межстрочный зазор

void textFontLoad()   { tft->loadFont(notesfont); }
void textFontUnload() { tft->unloadFont(); }

// Длина UTF-8 символа по первому байту.
static int utf8Len(uint8_t c)
{
    if (c < 0x80) return 1;
    if ((c >> 5) == 0x6) return 2;
    if ((c >> 4) == 0xE) return 3;
    if ((c >> 3) == 0x1E) return 4;
    return 1;
}

void textViewInit(TextView &tv, int x, int y, int w, int h)
{
    tv.x = x; tv.y = y; tv.w = w; tv.h = h;
    tv.text = ""; tv.top = 0; tv.lineCount = 0; tv.visLines = 0; tv.lineH = 0;
}

// Жадный word-wrap по ширине (textWidth с загруженным VLW). Слова длиннее
// строки рвутся по символам, не разрывая многобайтовый UTF-8.
void textViewSetText(TextView &tv, const char *text)
{
    tv.text = text ? text : "";
    tv.top  = 0;
    tv.lineCount = 0;

    textFontLoad();
    tv.lineH = tft->fontHeight() + LINE_PAD;
    if (tv.lineH <= 0) tv.lineH = 20;
    tv.visLines = tv.h / tv.lineH;
    if (tv.visLines < 1) tv.visLines = 1;
    const int maxW = tv.w - SCROLLBAR_W - 2;

    const int n = strlen(tv.text);
    int ls = 0;                 // байт начала текущей строки
    int i  = 0;                 // курсор
    int lastSpace = -1;         // позиция последнего пробела в строке

    char tmp[256];
    while (i < n && tv.lineCount < TV_MAX_LINES) {
        int clen = utf8Len((uint8_t)tv.text[i]);
        int segLen = i + clen - ls;
        if (segLen >= (int)sizeof(tmp)) segLen = sizeof(tmp) - 1;   // защита
        memcpy(tmp, tv.text + ls, segLen);
        tmp[segLen] = 0;
        int wpx = tft->textWidth(tmp);

        if (tv.text[i] == ' ') lastSpace = i;

        if (wpx > maxW && i > ls) {        // символ не влезает, в строке уже что-то есть
            int brk, nextStart;
            if (lastSpace > ls) { brk = lastSpace; nextStart = lastSpace + 1; }
            else                { brk = i;         nextStart = i; }   // жёсткий перенос
            tv.lineOff[tv.lineCount] = ls;
            tv.lineLen[tv.lineCount] = brk - ls;
            tv.lineCount++;
            ls = nextStart;
            i  = nextStart;
            lastSpace = -1;
            continue;
        }
        i += clen;
    }
    if (ls < n && tv.lineCount < TV_MAX_LINES) {
        tv.lineOff[tv.lineCount] = ls;
        tv.lineLen[tv.lineCount] = n - ls;
        tv.lineCount++;
    }
    if (tv.lineCount == 0) { tv.lineOff[0] = 0; tv.lineLen[0] = 0; tv.lineCount = 1; }
    textFontUnload();
}

void textViewDraw(TextView &tv)
{
    tft->fillRect(tv.x, tv.y, tv.w, tv.h, COL_BG);
    textFontLoad();
    tft->setTextDatum(TL_DATUM);
    tft->setTextColor(COL_GREEN, COL_BG);

    char buf[256];
    for (int r = 0; r < tv.visLines; r++) {
        int li = tv.top + r;
        if (li >= tv.lineCount) break;
        int len = tv.lineLen[li];
        if (len > (int)sizeof(buf) - 1) len = sizeof(buf) - 1;
        memcpy(buf, tv.text + tv.lineOff[li], len);
        buf[len] = 0;
        while (len > 0 && buf[len - 1] == ' ') buf[--len] = 0;   // обрезать хвостовой пробел
        tft->drawString(buf, tv.x, tv.y + r * tv.lineH);
    }
    textFontUnload();

    // скроллбар справа, только если текст не помещается
    int sbx = tv.x + tv.w - SCROLLBAR_W;
    tft->fillRect(sbx, tv.y, SCROLLBAR_W, tv.h, COL_BG);
    if (tv.lineCount > tv.visLines) {
        tft->drawRect(sbx, tv.y, SCROLLBAR_W, tv.h, COL_FRAME);
        int thumbH = tv.h * tv.visLines / tv.lineCount;
        if (thumbH < 6) thumbH = 6;
        int maxTop = tv.lineCount - tv.visLines;
        int thumbY = tv.y + (tv.h - thumbH) * tv.top / (maxTop > 0 ? maxTop : 1);
        tft->fillRect(sbx + 1, thumbY + 1, SCROLLBAR_W - 2, thumbH - 2, COL_GREEN);
    }
}

bool textViewEvent(TextView &tv, InputEvent e)
{
    if (tv.lineCount <= tv.visLines) return false;
    int maxTop = tv.lineCount - tv.visLines;
    int step = tv.visLines - 1; if (step < 1) step = 1;
    int old = tv.top;
    if (e == EVT_DOWN)      tv.top += step;
    else if (e == EVT_UP)   tv.top -= step;
    else return false;
    if (tv.top < 0) tv.top = 0;
    if (tv.top > maxTop) tv.top = maxTop;
    return tv.top != old;
}
