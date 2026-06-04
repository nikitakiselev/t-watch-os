#include "gamerender.h"
#include "worldgen.h"
#include "entities.h"
#include "wall_sprites.h"
#include "player_sprites.h"
#include "trader_sprites.h"
#include "chest_sprites.h"
#include "mon_sprites.h"
#include "../../../hw.h"
#include "../../../theme.h"

// Прозрачный блит квадратного спрайта (ключ 0xF81F пропускаем).
static void blitKeyed(TFT_eSprite *s, int x, int y, int px, const uint16_t *d)
{
    for (int yy = 0; yy < px; yy++)
        for (int xx = 0; xx < px; xx++) {
            uint16_t c = d[yy * px + xx];
            if (c != 0xF81F) s->drawPixel(x + xx, y + yy, c);
        }
}


static TFT_eSprite *spr = nullptr;

static void drawTile(TFT_eSprite *s, int tx, int ty, uint8_t type)
{
    if (type == TILE_WALL) {
        s->fillRect(tx, ty, TILE, TILE, COL_GREEN_DIM);
        s->drawFastHLine(tx, ty + TILE / 2, TILE, COL_BG);          // «кирпичная» текстура
        s->drawFastVLine(tx + TILE / 2, ty, TILE / 2, COL_BG);
        s->drawFastVLine(tx + TILE / 4, ty + TILE / 2, TILE / 2, COL_BG);
    } else if (type == TILE_STAIRS) {
        for (int i = 0; i < 4; i++)
            s->fillRect(tx + 2 + i * 3, ty + 3 + i * 3, TILE - 4 - i * 3, 2, COL_GREEN);
    } else if (type == TILE_ALTAR) {           // алтарь со свечением
        int cx = tx + TILE / 2;
        s->fillRect(tx + 6, ty + TILE - 8, TILE - 12, 6, COL_GREEN_DIM);  // пьедестал
        s->fillRect(tx + 8, ty + 8, TILE - 16, TILE - 14, COL_GREEN);     // камень
        s->fillTriangle(cx, ty + 2, cx - 4, ty + 8, cx + 4, ty + 8, COL_AMBER);
    } else if (type == TILE_CAMP) {            // лагерь — костёр
        int cx = tx + TILE / 2, cy = ty + TILE / 2;
        s->drawLine(cx - 7, cy + 7, cx + 7, cy + 1, COL_GREEN_DIM);   // дрова крест-накрест
        s->drawLine(cx + 7, cy + 7, cx - 7, cy + 1, COL_GREEN_DIM);
        s->fillTriangle(cx, cy - 9, cx - 5, cy + 2, cx + 5, cy + 2, COL_AMBER);  // пламя
        s->fillTriangle(cx, cy - 3, cx - 3, cy + 2, cx + 3, cy + 2, COL_GREEN);
    } else if (type == TILE_EVENT) {           // руна события (ромб + «!»)
        int cx = tx + TILE / 2, cy = ty + TILE / 2;
        s->drawLine(cx, cy - 8, cx + 8, cy, COL_AMBER);
        s->drawLine(cx + 8, cy, cx, cy + 8, COL_AMBER);
        s->drawLine(cx, cy + 8, cx - 8, cy, COL_AMBER);
        s->drawLine(cx - 8, cy, cx, cy - 8, COL_AMBER);
        s->drawFastVLine(cx, cy - 3, 4, COL_AMBER);
        s->drawPixel(cx, cy + 4, COL_AMBER);
    } else {
        s->drawPixel(tx + TILE / 2, ty + TILE / 2, COL_GREEN_DIM);  // пол — точка
    }
}

static void drawPlayer(TFT_eSprite *s, int tx, int ty)
{
    blitKeyed(s, tx + (TILE - PLAYER_TILES_PX) / 2, ty + (TILE - PLAYER_TILES_PX) / 2,
              PLAYER_TILES_PX, PLAYER_TILES[0]);
}

void gameRenderInit()
{
    if (spr) return;
    spr = new TFT_eSprite(tft);
    spr->setColorDepth(16);
    spr->createSprite(VW * TILE, VH * TILE);
    spr->setSwapBytes(true);     // RGB565-картинки (pushImage) — со свопом байт
}

void gameRenderFree()
{
    if (spr) { spr->deleteSprite(); delete spr; spr = nullptr; }
}

#define ARROW_ALPHA 115     // 0..255 — доля цвета стрелки поверх подложки (~45%)

// Смешивание двух цветов RGB565: a — вес fg (0..255).
static uint16_t blend565(uint16_t fg, uint16_t bg, uint8_t a)
{
    uint8_t fr = (fg >> 11) & 0x1F, fgr = (fg >> 5) & 0x3F, fb = fg & 0x1F;
    uint8_t br = (bg >> 11) & 0x1F, bgr = (bg >> 5) & 0x3F, bb = bg & 0x1F;
    uint8_t r = (fr * a + br * (255 - a)) / 255;
    uint8_t g = (fgr * a + bgr * (255 - a)) / 255;
    uint8_t b = (fb * a + bb * (255 - a)) / 255;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static int edgeFn(int ax, int ay, int bx, int by, int px, int py)
{
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

// Полупрозрачный залитый треугольник: смешиваем с подложкой спрайта.
static void fillTriAlpha(TFT_eSprite *s, int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c)
{
    int minx = min(x0, min(x1, x2)), maxx = max(x0, max(x1, x2));
    int miny = min(y0, min(y1, y2)), maxy = max(y0, max(y1, y2));
    for (int py = miny; py <= maxy; py++)
        for (int px = minx; px <= maxx; px++) {
            int w0 = edgeFn(x1, y1, x2, y2, px, py);
            int w1 = edgeFn(x2, y2, x0, y0, px, py);
            int w2 = edgeFn(x0, y0, x1, y1, px, py);
            bool inside = (w0 >= 0 && w1 >= 0 && w2 >= 0) || (w0 <= 0 && w1 <= 0 && w2 <= 0);
            if (inside)
                s->drawPixel(px, py, blend565(c, s->readPixel(px, py), ARROW_ALPHA));
        }
}

// Стрелка-подсказка управления (dir: 0 вверх, 1 вниз, 2 влево, 3 вправо).
static void drawArrow(TFT_eSprite *s, int cx, int cy, int dir)
{
    const int r = 7;
    const uint16_t c = COL_AMBER;
    if      (dir == 0) fillTriAlpha(s, cx, cy - r, cx - r, cy + r, cx + r, cy + r, c);
    else if (dir == 1) fillTriAlpha(s, cx, cy + r, cx - r, cy - r, cx + r, cy - r, c);
    else if (dir == 2) fillTriAlpha(s, cx - r, cy, cx + r, cy - r, cx + r, cy + r, c);
    else               fillTriAlpha(s, cx + r, cy, cx - r, cy - r, cx - r, cy + r, c);
}

void gameRenderMap(int32_t px, int32_t py)
{
    if (!spr) return;
    spr->fillSprite(COL_BG);
    Monster m;
    for (int vy = 0; vy < VH; vy++)
        for (int vx = 0; vx < VW; vx++) {
            int32_t wx = px + vx - VW / 2, wy = py + vy - VH / 2;
            uint8_t t = tileAt(wx, wy);
            uint32_t hv = (uint32_t)(wx * 73856093) ^ (uint32_t)(wy * 19349663);
            if (t == TILE_FLOOR) {                      // пол — чёрный (фон уже залит)
            } else if (t == TILE_WALL) {                // стена — спрайт
                spr->pushImage(vx * TILE, vy * TILE, WALL_TILES_PX, WALL_TILES_PX,
                               (uint16_t *)WALL_TILES[hv % WALL_TILES_N]);
            } else if (t == TILE_CHEST) {               // сундук (если не открыт)
                if (!chestOpenedAt(wx, wy))
                    blitKeyed(spr, vx * TILE, vy * TILE, CHEST_TILES_PX, CHEST_TILES[0]);
            } else if (t == TILE_MERCHANT) {            // торговец — спрайт
                blitKeyed(spr, vx * TILE + (TILE - TRADER_TILES_PX) / 2,
                          vy * TILE + (TILE - TRADER_TILES_PX) / 2, TRADER_TILES_PX, TRADER_TILES[0]);
            } else if (t == TILE_ALTAR) {               // алтарь (если не использован)
                if (!altarUsedAt(wx, wy)) drawTile(spr, vx * TILE, vy * TILE, t);
            } else if (t == TILE_EVENT) {               // руна (если не сработала)
                if (!eventTriggeredAt(wx, wy)) drawTile(spr, vx * TILE, vy * TILE, t);
            } else {
                drawTile(spr, vx * TILE, vy * TILE, t);  // лестница, торговец и пр.
            }
            if (monsterActiveAt(wx, wy, m)) {           // монстр — спрайт поверх пола
                blitKeyed(spr, vx * TILE + (TILE - MON_TILES_PX) / 2,
                          vy * TILE + (TILE - MON_TILES_PX) / 2, MON_TILES_PX, MON_TILES[m.spriteId]);
            }
        }
    drawPlayer(spr, (VW / 2) * TILE, (VH / 2) * TILE);

    // Стрелки управления по центрам граней.
    drawArrow(spr, VW * TILE / 2,        ARROW_OFF,             0);
    drawArrow(spr, VW * TILE / 2,        VH * TILE - ARROW_OFF, 1);
    drawArrow(spr, ARROW_OFF,            VH * TILE / 2,         2);
    drawArrow(spr, VW * TILE - ARROW_OFF, VH * TILE / 2,        3);

    spr->pushSprite(MAP_X, MAP_Y);
}
