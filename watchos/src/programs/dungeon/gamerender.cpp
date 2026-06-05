#include "gamerender.h"
#include "worldgen.h"
#include "entities.h"
#include "sprites_gen.h"
#include "../../../hw.h"
#include "../../../theme.h"

// ── Индексы тайлов в DUN_TILES (idx = row*10 + col) ──
#define T_TL 0
#define T_T  1
#define T_TR 5
#define T_L  10
#define T_R  15
#define T_BL 40
#define T_B  41
#define T_BR 45
#define T_VOID 255       // не рисуем — фон пустоты
// Фон пустоты = «внешний» цвет тайлсета (37,19,26), вшитый в края тайлов стен,
// иначе по бокам вертикальных стен видна тёмная полоса, не совпадающая с чёрным.
#define COL_VOID 0x2083

// Полупрозрачная тень спрайта: маркер 0xF81E (ставит gen_sprites) → блендим чёрный
// с пикселем под спрайтом. SHADOW_ALPHA — доля чёрного (90/255 ≈ 35%, как в исходном PNG).
#define COL_SHADOW_KEY 0xF81E
#define SHADOW_ALPHA   90
static uint16_t blend565(uint16_t fg, uint16_t bg, uint8_t a);   // определена ниже

static TFT_eSprite *spr = nullptr;

// ── Анимация игрока (рыцарь) ──
// idle: 2 кадра (SPR_KNIGHT_IDLE..+1), run: 6 кадров (SPR_KNIGHT_RUN..+5).
// Бег нарисован «вправо»; влево — зеркалим по горизонтали при отрисовке (без доп. спрайтов).
static bool pMoving = false;     // бег vs простой
static bool pFlip   = false;     // true → смотрит влево (горизонтальное зеркало)
static int  pIdleF  = 0;         // кадр idle 0..1
static int  pRunF   = 0;         // кадр run 0..5
void gamePlayerSetMoving(bool moving, int dx)
{
    pMoving = moving;
    if (dx < 0) pFlip = true; else if (dx > 0) pFlip = false;   // dx==0 — оставить как было
}
void gamePlayerAnimAdvance()
{
    if (pMoving) pRunF = (pRunF + 1) % 6;
    else         pIdleF ^= 1;
}

// Пол — 12 вариантов с бледными трещинами, рандом по координате.
static const uint8_t FLOORS[12] = { 11,12,13,14, 21,22,23,24, 31,32,33,34 };
static uint8_t floorAt(int32_t wx, int32_t wy)
{
    uint32_t h = (uint32_t)wx * 0x9E3779B1u ^ (uint32_t)wy * 0x85EBCA6Bu;
    h ^= h >> 13;
    return FLOORS[h % 12u];
}

// Есть ли факел на этой стене (детерминированно по координате, чтобы не «прыгал»).
// Ставим только на ВЕРХНИЕ стены (где south=пол → тайл T_T), там видна лицевая грань.
// ~16% таких тайлов несут факел — редко и нарядно.
static bool torchAt(int32_t wx, int32_t wy)
{
    uint32_t h = (uint32_t)wx * 0x27D4EB2Fu ^ (uint32_t)wy * 0x165667B1u;
    h ^= h >> 15;
    return (h % 100u) < 16u;
}

// Автотайл стены (клетка-не-пол, граничит с полом). Выбор тайла по соседям-полу.
static uint8_t wallTileAt(int32_t wx, int32_t wy)
{
    bool s = isWalkable(wx, wy + 1), n = isWalkable(wx, wy - 1);
    bool e = isWalkable(wx + 1, wy), w = isWalkable(wx - 1, wy);
    // Вогнутые (внутренние) углы — отдельные тайлы (подобраны в редакторе),
    // НЕ те же, что внешние. INNER_TL=3, INNER_TR=4, INNER_BL=55, INNER_BR=50.
    if (s && e) return 3;  if (s && w) return 4;
    if (n && e) return 55; if (n && w) return 50;
    // один ортогональный пол — прямая кромка
    if (s) return T_T; if (n) return T_B;
    if (e) return T_L; if (w) return T_R;
    // только по диагонали — выпуклый (внешний) угол
    if (isWalkable(wx + 1, wy + 1)) return T_TL;
    if (isWalkable(wx - 1, wy + 1)) return T_TR;
    if (isWalkable(wx + 1, wy - 1)) return T_BL;
    if (isWalkable(wx - 1, wy - 1)) return T_BR;
    return T_VOID;
}

// Непрозрачный блит тайла SRC_PX→TILE (nearest-neighbor) через drawPixel.
static void blitTile(int x, int y, const uint16_t *d)
{
    for (int dy = 0; dy < TILE; dy++) {
        int sy = dy * SRC_PX / TILE;
        for (int dx = 0; dx < TILE; dx++) {
            int sx = dx * SRC_PX / TILE;
            spr->drawPixel(x + dx, y + dy, d[sy * SRC_PX + sx]);
        }
    }
}

// Прозрачный блит спрайта (ключ 0xF81F) с масштабом SRC_PX→TILE (×2), на всю клетку —
// чтобы сущности были того же размера, что пол/стены (а не вдвое мельче).
static void blitKeyedCentered(int cellX, int cellY, int px, const uint16_t *d, bool flip = false)
{
    (void)px;
    for (int dy = 0; dy < TILE; dy++) {
        int sy = dy * SRC_PX / TILE;
        for (int dx = 0; dx < TILE; dx++) {
            int sx = dx * SRC_PX / TILE;
            if (flip) sx = SRC_PX - 1 - sx;                     // зеркало по горизонтали
            uint16_t c = d[sy * SRC_PX + sx];
            if (c == 0xF81F) continue;                          // прозрачно
            int X = cellX + dx, Y = cellY + dy;
            if (c == COL_SHADOW_KEY)                            // тень: чёрный поверх фона
                spr->drawPixel(X, Y, blend565(0x0000, spr->readPixel(X, Y), SHADOW_ALPHA));
            else
                spr->drawPixel(X, Y, c);
        }
    }
}

// Простой амбер-маркер для объектов без спрайта (лестница/алтарь/руна/лагерь).
static void drawMarker(int x, int y, uint8_t t)
{
    int cx = x + TILE / 2, cy = y + TILE / 2;
    if (t == TILE_STAIRS) {
        for (int i = 0; i < 3; i++) spr->fillRect(x + 2 + i * 4, y + 4 + i * 4, TILE - 4 - i * 4, 2, COL_GREEN);
    } else if (t == TILE_CAMP) {
        spr->fillTriangle(cx, cy - 6, cx - 4, cy + 3, cx + 4, cy + 3, COL_AMBER);
    } else if (t == TILE_ALTAR) {
        spr->fillRect(x + 4, y + TILE - 5, TILE - 8, 4, COL_GREEN_DIM);
        spr->fillTriangle(cx, cy - 5, cx - 3, cy + 2, cx + 3, cy + 2, COL_AMBER);
    } else if (t == TILE_EVENT) {
        spr->drawLine(cx, cy - 5, cx + 5, cy, COL_AMBER);
        spr->drawLine(cx + 5, cy, cx, cy + 5, COL_AMBER);
        spr->drawLine(cx, cy + 5, cx - 5, cy, COL_AMBER);
        spr->drawLine(cx - 5, cy, cx, cy - 5, COL_AMBER);
    }
}

void gameRenderInit()
{
    if (spr) return;
    spr = new TFT_eSprite(tft);
    spr->setColorDepth(16);
    spr->createSprite(VW * TILE, VH * TILE);
    spr->setSwapBytes(false);    // ТЕСТ порядка байт: tiles рисуются drawPixel'ом сырым 565
}

void gameRenderFree()
{
    if (spr) { spr->deleteSprite(); delete spr; spr = nullptr; }
}

// ── Полупрозрачные стрелки управления ──
#define ARROW_ALPHA 115
static uint16_t blend565(uint16_t fg, uint16_t bg, uint8_t a)
{
    uint8_t fr=(fg>>11)&0x1F,fgr=(fg>>5)&0x3F,fb=fg&0x1F;
    uint8_t br=(bg>>11)&0x1F,bgr=(bg>>5)&0x3F,bb=bg&0x1F;
    return (uint16_t)((((fr*a+br*(255-a))/255)<<11)|(((fgr*a+bgr*(255-a))/255)<<5)|((fb*a+bb*(255-a))/255));
}
static int edgeFn(int ax,int ay,int bx,int by,int px,int py){return (bx-ax)*(py-ay)-(by-ay)*(px-ax);}
static void fillTriAlpha(int x0,int y0,int x1,int y1,int x2,int y2,uint16_t c)
{
    int minx=min(x0,min(x1,x2)),maxx=max(x0,max(x1,x2)),miny=min(y0,min(y1,y2)),maxy=max(y0,max(y1,y2));
    for(int py=miny;py<=maxy;py++)for(int px=minx;px<=maxx;px++){
        int w0=edgeFn(x1,y1,x2,y2,px,py),w1=edgeFn(x2,y2,x0,y0,px,py),w2=edgeFn(x0,y0,x1,y1,px,py);
        if((w0>=0&&w1>=0&&w2>=0)||(w0<=0&&w1<=0&&w2<=0))
            spr->drawPixel(px,py,blend565(c,spr->readPixel(px,py),ARROW_ALPHA));
    }
}
static void drawArrow(int cx,int cy,int dir)
{
    const int r=7; const uint16_t c=COL_AMBER;
    if(dir==0)fillTriAlpha(cx,cy-r,cx-r,cy+r,cx+r,cy+r,c);
    else if(dir==1)fillTriAlpha(cx,cy+r,cx-r,cy-r,cx+r,cy-r,c);
    else if(dir==2)fillTriAlpha(cx-r,cy,cx+r,cy-r,cx+r,cy+r,c);
    else fillTriAlpha(cx+r,cy,cx-r,cy-r,cx-r,cy+r,c);
}

void gameRenderMap(int32_t px, int32_t py, int ox, int oy)
{
    if (!spr) return;
    spr->fillSprite(COL_VOID);                     // пустота = тёмный фон тайлсета

    Monster m;
    // При сдвиге (ox/oy != 0) рисуем на один ряд/столбец больше с каждой стороны —
    // тайлы, заезжающие из-за края. drawPixel клипует выход за границы спрайта.
    for (int vy = -1; vy <= VH; vy++)
        for (int vx = -1; vx <= VW; vx++) {
            int32_t wx = px + vx - VW/2, wy = py + vy - VH/2;
            int x = vx * TILE + ox, y = vy * TILE + oy;
            uint8_t t = tileAt(wx, wy);

            if (t == TILE_WALL) {                  // стена / пустота
                uint8_t wt = wallTileAt(wx, wy);
                if (wt != T_VOID) blitTile(x, y, SPRITES[wt]);     // стена (непрозрачна)
                if (wt == T_T && torchAt(wx, wy)) {                // факел на верхней стене
                    int tf = (int)(millis() / 110) % 8;           // анимация пламени
                    blitKeyedCentered(x, y, SPR_PX, SPRITES[SPR_TORCH + tf]);
                }
                continue;                          // иначе оставляем фон пустоты
            }

            blitTile(x, y, SPRITES[floorAt(wx, wy)]);   // пол

            if (t == TILE_CHEST) { if (!chestOpenedAt(wx, wy)) blitKeyedCentered(x, y, SPR_PX, SPRITES[SPR_CHEST]); }
            else if (t == TILE_MERCHANT) blitKeyedCentered(x, y, SPR_PX, SPRITES[SPR_TRADER]);
            else if (t == TILE_STAIRS) blitKeyedCentered(x, y, SPR_PX, SPRITES[SPR_STAIRS]);
            else if (t == TILE_ALTAR) { if (!altarUsedAt(wx, wy)) drawMarker(x, y, t); }
            else if (t == TILE_EVENT) { if (!eventTriggeredAt(wx, wy)) drawMarker(x, y, t); }
            else if (t == TILE_CAMP) drawMarker(x, y, t);

            if (monsterActiveAt(wx, wy, m)) {       // монстр поверх пола
                int fr = (m.anim > 1) ? ((int)(millis() / 400) % m.anim) : 0;   // idle-анимация
                blitKeyedCentered(x, y, SPR_PX, SPRITES[SPR_MON_BASE + m.spriteId + fr]);
            }
        }

    // Игрок по центру вьюпорта (idle/run кадр, при взгляде влево — зеркало).
    int pIdx = pMoving ? (SPR_KNIGHT_RUN + pRunF) : (SPR_KNIGHT_IDLE + pIdleF);
    blitKeyedCentered((VW/2)*TILE, (VH/2)*TILE, SPR_PX, SPRITES[pIdx], pFlip);

    // Стрелки управления.
    drawArrow(VW*TILE/2,           ARROW_OFF,           0);
    drawArrow(VW*TILE/2,           VH*TILE - ARROW_OFF, 1);
    drawArrow(ARROW_OFF,           VH*TILE/2,           2);
    drawArrow(VW*TILE - ARROW_OFF, VH*TILE/2,           3);

    spr->pushSprite(MAP_X, MAP_Y);
}
