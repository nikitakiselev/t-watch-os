#pragma once
#include "config.h"

// ───────────────────────── Палитра: Matrix green + amber ─────────────────────────
#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

#define COL_BG         TFT_BLACK
#define COL_GREEN      RGB565(0x33, 0xFF, 0x66)   // основной неон
#define COL_GREEN_HI   RGB565(0xC0, 0xFF, 0xC0)   // яркий акцент/блик
#define COL_GREEN_DIM  RGB565(0x0A, 0x55, 0x22)   // тусклый (рамки)
#define COL_AMBER      RGB565(0xFF, 0xB0, 0x00)   // янтарный акцент
#define COL_AMBER_DIM  RGB565(0x5A, 0x3A, 0x00)
#define COL_GRID       RGB565(0x06, 0x20, 0x12)   // сетка фона

#define COL_TEXT       COL_GREEN
#define COL_ACCENT     COL_AMBER
#define COL_FRAME      COL_GREEN_DIM

// ───────────────────────── Общесистемные эффекты (вкл/выкл) ─────────────────────────
// Фон (сетка и т.п.) каждое приложение рисует себе само — глобальных флагов фона нет.
#define FX_GLITCH      1   // glitch при смене экранов
#define FX_TYPEWRITER  1   // печатающийся текст (бут-сплеш)

// ───────────────────────── Хелперы отрисовки ─────────────────────────
void themeBackdrop();                 // базовый фон: заливка + HUD-рамка
void drawGrid();                      // сетка (приложение вызывает само, если нужна)
void drawHudFrame();                  // угловые HUD-скобки

// Индикатор страниц для экранов с листанием влево/вправо: count точек по центру на
// высоте y; активная (current) — яркая. Правило для всех свайп-страничных экранов.
void drawPageDots(int count, int current, int y);

// ASCII-арт: массив строк, отцентрован по (cx,cy). Моноширинный font 1, масштаб size.
// Цель g — экран (*tft) или спрайт (наследует TFT_eSPI).
void drawAsciiArt(TFT_eSPI &g, const char *const *lines, int n,
                  int cx, int cy, uint16_t color, uint8_t size);

void glitchFlash();                   // короткий glitch-эффект (для переходов)
void typeOut(const char *s, int x, int y, uint16_t color, uint8_t font, int chDelayMs);

// Иконка Wi-Fi (дуги веером + точка). (cx, cyBottom) — низ значка (точка-источник).
void drawWifiGlyph(TFT_eSPI &g, int cx, int cyBottom, int r, uint16_t color);

// Индикатор уровня сигнала: 4 столбика по RSSI (dBm). Левый-нижний угол — (x, yBottom),
// ширина 16px, высота до 12px. Занятые столбики — colOn, пустые — colOff.
void drawSignalBars(TFT_eSPI &g, int x, int yBottom, int rssi, uint16_t colOn, uint16_t colOff);
