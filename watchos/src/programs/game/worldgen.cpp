#include "worldgen.h"

static const uint32_t SEED = 0x1337C0DEu;
static const int      C    = 12;          // размер чанка в клетках
static const int      MID  = C / 2;       // позиция «креста» коридоров

// Целочисленный хеш координат чанка.
static uint32_t hash2(int32_t x, int32_t y, uint32_t seed)
{
    uint32_t h = seed;
    h ^= (uint32_t)x * 0x9E3779B1u; h = (h ^ (h >> 16)) * 0x85EBCA6Bu;
    h ^= (uint32_t)y * 0xC2B2AE35u; h = (h ^ (h >> 13)) * 0x27D4EB2Fu;
    h ^= h >> 16;
    return h;
}

// Деление с округлением вниз (для отрицательных координат).
static int32_t fdiv(int32_t a, int32_t b) { int32_t q = a / b; if ((a % b) && ((a < 0) != (b < 0))) q--; return q; }

uint8_t tileAt(int32_t x, int32_t y)
{
    int32_t cx = fdiv(x, C), cy = fdiv(y, C);
    int lx = (int)(x - cx * C);            // 0..C-1
    int ly = (int)(y - cy * C);

    uint32_t h = hash2(cx, cy, SEED);

    // Комната внутри чанка.
    int rw = 3 + (h % 4);                  // 3..6
    int rh = 3 + ((h >> 3) % 4);
    int rx = 1 + ((h >> 6)  % (C - rw - 1));
    int ry = 1 + ((h >> 10) % (C - rh - 1));
    int rcx = rx + rw / 2, rcy = ry + rh / 2;

    bool inRoom = (lx >= rx && lx < rx + rw && ly >= ry && ly < ry + rh);

    // Коридоры: «крест» по col=MID и row=MID (соседние чанки стыкуются на серединах
    // рёбер → мир гарантированно связный) + перемычка от центра комнаты к спайну.
    int lo = (rcx < MID) ? rcx : MID, hi = (rcx < MID) ? MID : rcx;
    bool corridor = (lx == MID) || (ly == MID) ||
                    (ly == rcy && lx >= lo && lx <= hi);

    if (!(inRoom || corridor)) return TILE_WALL;

    // Объект в центре комнаты: лагерь (каждый 3-й чанк по обеим осям) либо
    // сундук / торговец / алтарь (по корзинам хеша).
    if (lx == rcx && ly == rcy) {
        if ((cx % 3) == 0 && (cy % 3) == 0) return TILE_CAMP;
        uint32_t f = (h >> 22) % 12u;
        if (f <= 1u) return TILE_CHEST;        // ~2/12 комнат
        if (f == 2u) return TILE_MERCHANT;     // ~1/12
        if (f == 3u) return TILE_ALTAR;        // ~1/12
    }
    // Лестница: в части чанков, в углу комнаты.
    if (lx == rx && ly == ry && ((h >> 20) % 6u) == 0u) return TILE_STAIRS;

    // Руна случайного события: ~1/48 клеток пола (по координате клетки).
    if ((hash2(x, y, SEED ^ 0x5A17E5u) % 48u) == 0u) return TILE_EVENT;

    return TILE_FLOOR;
}

bool isWalkable(int32_t x, int32_t y) { return tileAt(x, y) != TILE_WALL; }
