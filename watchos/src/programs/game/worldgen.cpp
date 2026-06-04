#include "worldgen.h"

int gFloor = 0;                            // текущий этаж (геометрия мира от него не зависит)

static const uint32_t SEED = 0x1337C0DEu;
static const int      C    = 12;          // размер чанка в клетках
static const int      MID  = C / 2;       // позиция «креста» коридоров

static uint32_t hash2(int32_t x, int32_t y, uint32_t seed)
{
    uint32_t h = seed;
    h ^= (uint32_t)x * 0x9E3779B1u; h = (h ^ (h >> 16)) * 0x85EBCA6Bu;
    h ^= (uint32_t)y * 0xC2B2AE35u; h = (h ^ (h >> 13)) * 0x27D4EB2Fu;
    h ^= h >> 16;
    return h;
}

static int32_t fdiv(int32_t a, int32_t b) { int32_t q = a / b; if ((a % b) && ((a < 0) != (b < 0))) q--; return q; }

// ── Камп: специальная безопасная комната-хаб, захардкоженный фикс-лейаут ──
static bool isCampChunk(int32_t cx, int32_t cy)
{
    if (cx == 0 && cy == 0) return true;                  // стартовый камп
    return hash2(cx, cy, SEED ^ 0xCA77Fu) % 13u == 0u;    // ~раз в ~13 чанков
}

// Раскладка кампа в локальных координатах чанка (lx,ly = 0..C-1).
// Прямоугольная комната с дверями по серединам граней (под крест-коридоры),
// внутри — костёр (save+heal), торговец, алтарь. Монстров тут нет (см. entities).
static uint8_t campTile(int lx, int ly)
{
    bool border = (lx == 0 || lx == C - 1 || ly == 0 || ly == C - 1);
    if (border) {
        bool door = (lx == MID && (ly == 0 || ly == C - 1)) ||
                    (ly == MID && (lx == 0 || lx == C - 1));
        return door ? TILE_FLOOR : TILE_WALL;
    }
    if (lx == 3 && ly == 3) return TILE_CAMP;       // костёр: сохранение + полный отдых
    if (lx == 8 && ly == 3) return TILE_MERCHANT;   // торговец
    if (lx == 3 && ly == 8) return TILE_ALTAR;      // алтарь
    if (lx == C - 2 && ly == 1) return TILE_STAIRS; // лестница (правый верхний угол): смена этажа
    return TILE_FLOOR;
}

uint8_t tileAt(int32_t x, int32_t y)
{
    int32_t cx = fdiv(x, C), cy = fdiv(y, C);
    int lx = (int)(x - cx * C);
    int ly = (int)(y - cy * C);

    if (isCampChunk(cx, cy)) return campTile(lx, ly);

    uint32_t h = hash2(cx, cy, SEED);

    int rw = 3 + (h % 4);
    int rh = 3 + ((h >> 3) % 4);
    int rx = 1 + ((h >> 6)  % (C - rw - 1));
    int ry = 1 + ((h >> 10) % (C - rh - 1));
    int rcx = rx + rw / 2, rcy = ry + rh / 2;

    bool inRoom = (lx >= rx && lx < rx + rw && ly >= ry && ly < ry + rh);

    int lo = (rcx < MID) ? rcx : MID, hi = (rcx < MID) ? MID : rcx;
    bool corridor = (lx == MID) || (ly == MID) ||
                    (ly == rcy && lx >= lo && lx <= hi);

    if (!(inRoom || corridor)) return TILE_WALL;

    // Объект в центре обычной комнаты (кампов тут больше нет — они отдельные чанки).
    if (lx == rcx && ly == rcy) {
        uint32_t f = (h >> 22) % 12u;
        if (f <= 1u) return TILE_CHEST;
        if (f == 2u) return TILE_MERCHANT;
        if (f == 3u) return TILE_ALTAR;
    }
    if ((hash2(x, y, SEED ^ 0x5A17E5u) % 48u) == 0u) return TILE_EVENT;

    return TILE_FLOOR;
}

bool inCamp(int32_t x, int32_t y) { return isCampChunk(fdiv(x, C), fdiv(y, C)); }
bool isWalkable(int32_t x, int32_t y) { return tileAt(x, y) != TILE_WALL; }
