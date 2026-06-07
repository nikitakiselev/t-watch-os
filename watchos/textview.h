#pragma once
#include "input.h"

// Прокручиваемый просмотрщик UTF-8 текста (кириллица) со скроллбаром справа.
// Сам грузит встроенный VLW-шрифт (notesfont) на время своих операций, чтобы
// глобальное состояние tft не ломало штатные шрифты статусбара/навбара.
#define TV_MAX_LINES 64

struct TextView {
    int16_t x, y, w, h;                  // прямоугольник контента
    const char *text;                    // источник (не владеет)
    int  top;                            // первая видимая строка
    int  lineCount, visLines, lineH;
    int16_t lineOff[TV_MAX_LINES];       // смещение начала строки в text (байт)
    int16_t lineLen[TV_MAX_LINES];       // длина строки (байт)
};

void textViewInit(TextView &tv, int x, int y, int w, int h);
void textViewSetText(TextView &tv, const char *text);   // word-wrap, top=0
void textViewDraw(TextView &tv);                        // строки + скроллбар
bool textViewEvent(TextView &tv, InputEvent e);         // свайп ↑↓; true=redraw

// Загрузка/выгрузка встроенного кириллического VLW-шрифта (notesfont).
// Переиспользуется списком заметок для рисования заголовков. Между Load и Unload
// drawString/textWidth работают этим шрифтом; после Unload — снова штатные.
void textFontLoad();
void textFontUnload();
