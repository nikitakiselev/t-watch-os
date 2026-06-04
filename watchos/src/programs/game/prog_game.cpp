#include "prog_game.h"
#include "gamerender.h"
#include "worldgen.h"
#include "entities.h"
#include "combat.h"
#include "player.h"
#include "items.h"
#include "inventory.h"
#include "skills.h"
#include "savegame.h"
#include "../../../hw.h"
#include "../../../theme.h"
#include "../../../power.h"
#include "../../../input.h"
#include "../../../sound.h"

// Управление: тап по стрелке-зоне → шаг; удержание → автоповтор.
static const int      HIT_R      = 24;    // полуразмер хит-зоны вокруг стрелки, px
static const uint32_t HOLD_DELAY = 500;   // мс до начала автоповтора
static const uint32_t REPEAT_MS  = 180;   // период автоповтора

static int      heldDx = 0, heldDy = 0;
static uint32_t holdStart = 0, lastRep = 0;

// Отслеживание текущего касания, чтобы отличить свайп от удержания стрелки.
static int16_t  gsX = 0, gsY = 0;
static bool     gsActive = false, gsSwipe = false;

// Состояние игрока (статика → переживает light sleep).
static int32_t px = 0, py = 0;
static bool    started = false;
static int32_t campX = 0, campY = 0;       // последний лагерь-чекпоинт
static bool    hasCamp = false;            // посещён ли хоть один лагерь

static void findStart()
{
    for (int r = 0; r < 64; r++)
        for (int dy = -r; dy <= r; dy++)
            for (int dx = -r; dx <= r; dx++)
                if (isWalkable(dx, dy)) { px = dx; py = dy; return; }
}

static void drawHud()
{
    tft->fillRect(0, 0, SCR_W, 24, COL_BG);
    tft->drawFastHLine(0, 23, SCR_W, COL_FRAME);
    char buf[40];
    snprintf(buf, sizeof(buf), "HP%d MP%d L%d G%d", gp.hp, gp.mp, gp.level, gp.gold);
    tft->setTextDatum(ML_DATUM);
    tft->setTextColor(COL_GREEN, COL_BG);
    tft->drawString(buf, 6, 12, 2);

    // Кнопка-меню (гамбургер) сверху справа — в рамке, крупнее (легче попасть).
    const int mw = 34, mx = SCR_W - mw - 2, my = 1, mh = 22;
    tft->drawRoundRect(mx, my, mw, mh, 4, COL_AMBER);
    for (int i = 0; i < 3; i++) tft->drawFastHLine(mx + 8, my + 6 + i * 5, mw - 16, COL_AMBER);
}

// Кнопка закрытия [X] в правом-верхнем углу области (rightX — правый край, topY — верх).
static void drawCloseX(int rightX, int topY)
{
    tft->drawRoundRect(rightX - 28, topY, 28, 22, 4, COL_AMBER);
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_AMBER, COL_BG);
    tft->drawString("X", rightX - 14, topY + 11, 2);
}
static bool hitCloseX(int rightX, int topY, int16_t x, int16_t y)
{
    return x >= rightX - 30 && x <= rightX + 2 && y >= topY - 2 && y <= topY + 24;
}

// Окно награды после победы — поверх экрана боя (скруглённое).
static void rewardDialog(const Monster &m, int prevLevel)
{
    bool lvUp = gp.level > prevLevel;
    const int w = 188, h = lvUp ? 132 : 100, x0 = (SCR_W - w) / 2, y0 = (SCR_H - h) / 2;
    tft->fillRoundRect(x0, y0, w, h, 12, COL_BG);
    tft->drawRoundRect(x0, y0, w, h, 12, COL_AMBER);
    tft->drawRoundRect(x0 + 2, y0 + 2, w - 4, h - 4, 10, COL_GREEN_DIM);

    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_AMBER, COL_BG);
    tft->drawString("VICTORY", SCR_W / 2, y0 + 22, 4);

    char b[28];
    tft->setTextColor(COL_GREEN, COL_BG);
    snprintf(b, sizeof(b), "+%d XP   +%d gold", m.xp, m.gold);
    tft->drawString(b, SCR_W / 2, y0 + 48, 2);

    if (lvUp) {
        tft->setTextColor(COL_AMBER, COL_BG);
        snprintf(b, sizeof(b), "LEVEL UP!  Lv%d", gp.level);
        tft->drawString(b, SCR_W / 2, y0 + 72, 2);
        tft->setTextColor(COL_GREEN, COL_BG);
        snprintf(b, sizeof(b), "+%d points", (gp.level - prevLevel) * STAT_POINTS_PER_LEVEL);
        tft->drawString(b, SCR_W / 2, y0 + 92, 2);
        if (gLearnedSkill >= 0) {
            tft->setTextColor(COL_GREEN_HI, COL_BG);
            snprintf(b, sizeof(b), "Learned: %s", SKILLS[gLearnedSkill].name);
            tft->drawString(b, SCR_W / 2, y0 + 112, 2);
        } else {
            tft->setTextColor(COL_GREEN_DIM, COL_BG);
            tft->drawString("spend in Character", SCR_W / 2, y0 + 112, 1);
        }
        if (!soundPlayFile("/sfx/levelup.wav")) { soundBeep(700, 60); soundBeep(950, 60); soundBeep(1300, 130); }
    } else {
        tft->setTextColor(COL_GREEN_DIM, COL_BG);
        tft->drawString("tap to continue", SCR_W / 2, y0 + 78, 1);
    }

    uint32_t t0 = millis();
    for (;;) {
        int16_t x, y;
        InputEvent e = inputPoll(x, y);
        if (e != EVT_NONE) powerNoteActivity();
        uint32_t el = millis() - t0;
        if (el > 700 && (e == EVT_TAP || e == EVT_BACK)) break;   // не закрывать тем же тапом
        if (el > 4000) break;
        delay(20);
    }
}

static void deathScreen()
{
    tft->fillScreen(COL_BG);
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_AMBER, COL_BG);
    tft->drawString("YOU DIED", SCR_W / 2, SCR_H / 2 - 12, 4);
    tft->setTextColor(COL_GREEN_DIM, COL_BG);
    tft->drawString(hasCamp ? "respawn at camp" : "respawn at start", SCR_W / 2, SCR_H / 2 + 16, 2);
    delay(1600);
}

static uint32_t chestSeed(int32_t x, int32_t y)
{
    return (uint32_t)(x * 374761393) ^ (uint32_t)(y * 668265263);
}

// Окно лута (скруглённое, поверх карты).
static void lootDialog(const Item &it, bool added)
{
    const int w = 188, h = 96, x0 = (SCR_W - w) / 2, y0 = (SCR_H - h) / 2;
    tft->fillRoundRect(x0, y0, w, h, 12, COL_BG);
    tft->drawRoundRect(x0, y0, w, h, 12, COL_AMBER);
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_AMBER, COL_BG);
    tft->drawString(added ? "TREASURE" : "FULL!", SCR_W / 2, y0 + 22, 4);

    char b[28];
    if (added) {
        tft->setTextColor(rarityColor(it.rarity), COL_BG);
        tft->drawString(it.name, SCR_W / 2, y0 + 50, 2);
        tft->setTextColor(COL_GREEN, COL_BG);
        if (it.kind == IT_WEAPON)      snprintf(b, sizeof(b), "+%d ATK", it.power);
        else if (it.kind == IT_ARMOR)  snprintf(b, sizeof(b), "+%d DEF", it.power);
        else                           snprintf(b, sizeof(b), "+%d", it.power);
        tft->drawString(b, SCR_W / 2, y0 + 70, 2);
    } else {
        tft->setTextColor(COL_GREEN_DIM, COL_BG);
        tft->drawString("inventory full", SCR_W / 2, y0 + 56, 2);
    }

    uint32_t t0 = millis();
    for (;;) {
        int16_t x, y;
        InputEvent e = inputPoll(x, y);
        if (e != EVT_NONE) powerNoteActivity();
        uint32_t el = millis() - t0;
        if (el > 700 && (e == EVT_TAP || e == EVT_BACK)) break;   // не закрывать тем же тапом
        if (el > 4000) break;
        delay(20);
    }
}

// Универсальное информ-окно (скруглённое) с защитой от закрытия тем же тапом.
static void infoDialog(const char *title, const char *msg, uint16_t titleCol)
{
    const int w = 184, h = 92, x0 = (SCR_W - w) / 2, y0 = (SCR_H - h) / 2;
    tft->fillRoundRect(x0, y0, w, h, 12, COL_BG);
    tft->drawRoundRect(x0, y0, w, h, 12, COL_AMBER);
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(titleCol, COL_BG);
    tft->drawString(title, SCR_W / 2, y0 + 26, 4);
    tft->setTextColor(COL_GREEN, COL_BG);
    tft->drawString(msg, SCR_W / 2, y0 + 60, 2);

    uint32_t t0 = millis();
    for (;;) {
        int16_t x, y; InputEvent e = inputPoll(x, y);
        if (e != EVT_NONE) powerNoteActivity();
        uint32_t el = millis() - t0;
        if (el > 700 && (e == EVT_TAP || e == EVT_BACK)) break;
        if (el > 4000) break;
        delay(20);
    }
}

// ───── Алтарь: одноразовое случайное благословение ─────
static void altarDialog()
{
    char msg[28];
    switch (random(4)) {
        case 0:  gp.str++;   playerRecalcMax();  strcpy(msg, "+1 STR (+HP)"); break;
        case 1:  gp.dex++;                       strcpy(msg, "+1 DEX");       break;
        case 2:  gp.intel++; playerRecalcMax();  strcpy(msg, "+1 INT (+MP)"); break;
        default: gp.hp = gp.hpMax; gp.mp = gp.mpMax; strcpy(msg, "Fully restored"); break;
    }
    soundBeep(600, 60); soundBeep(900, 90);
    infoDialog("ALTAR", msg, COL_GREEN_HI);
}

// ───── Руна события: одноразовый случайный эффект ─────
static void eventTrigger()
{
    char title[12], msg[24]; uint16_t col = COL_AMBER;
    switch (random(4)) {
        case 0: { int g = 10 + random(31); gp.gold += g; strcpy(title, "FORTUNE");
                  snprintf(msg, sizeof(msg), "+%d gold", g);
                  if (!soundPlayFile("/sfx/podbor-bombyi.wav")) soundBeep(900, 60); break; }
        case 1: { int d = 8 + random(13); gp.hp -= d; if (gp.hp < 1) gp.hp = 1;
                  strcpy(title, "TRAP!"); snprintf(msg, sizeof(msg), "-%d HP", d);
                  if (!soundPlayFile("/sfx/hurt.wav")) soundBeep(200, 90); break; }
        case 2: { int h0 = gp.hp; gp.hp += 20 + random(21); if (gp.hp > gp.hpMax) gp.hp = gp.hpMax;
                  strcpy(title, "FOUNTAIN"); snprintf(msg, sizeof(msg), "+%d HP", gp.hp - h0);
                  soundBeep(700, 60); break; }
        default:{ int m0 = gp.mp; gp.mp += 15 + random(16); if (gp.mp > gp.mpMax) gp.mp = gp.mpMax;
                  strcpy(title, "MANA WELL"); snprintf(msg, sizeof(msg), "+%d MP", gp.mp - m0);
                  soundBeep(900, 60); break; }
    }
    infoDialog(title, msg, col);
}

// ───── Магазин (торговец) — прокручиваемый список, покупка тапом выделенного ─────
struct Offer { Item item; int price; };
static const int SHOP_TOP = 64, SHOP_ROWH = 26, SHOP_VIS = 5;

// Короткое описание бонуса предмета в строку (для магазина/инвентаря).
static void itemBonusStr(const Item &it, char *out, int n)
{
    if (it.kind == IT_WEAPON)      snprintf(out, n, "+%d ATK", it.power);
    else if (it.kind == IT_ARMOR)  snprintf(out, n, "+%d DEF", it.power);
    else if (it.kind == IT_HP_POTION) snprintf(out, n, "+%d HP", it.power);
    else                           snprintf(out, n, "+%d MP", it.power);
}

static int shopBuild(int32_t mx, int32_t my, Offer *of)
{
    long ax = mx < 0 ? -mx : mx, ay = my < 0 ? -my : my, dist = ax > ay ? ax : ay;
    int n = 0;
    // Зелья (фиксированные).
    Item hp; hp.kind = IT_HP_POTION; hp.rarity = R_COMMON; hp.count = 1; hp.power = 40; strcpy(hp.name, "HP Potion");
    of[n].item = hp; of[n].price = 25; n++;
    Item mp; mp.kind = IT_MP_POTION; mp.rarity = R_COMMON; mp.count = 1; mp.power = 30; strcpy(mp.name, "MP Potion");
    of[n].item = mp; of[n].price = 20; n++;
    // Три случайных предмета снаряжения (детерминированы по клетке торговца).
    for (int g = 0; g < 3; g++) {
        Item it = itemRandom(chestSeed(mx, my) ^ (0x9999u + g * 0x2D2Bu), dist);
        of[n].item = it;
        of[n].price = it.power * 6 + it.rarity * 25 + 20;
        n++;
    }
    return n;
}

static void drawShopList(Offer *of, int n, int sel, int top, const char *fb)
{
    tft->fillScreen(COL_BG);
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_AMBER, COL_BG);
    tft->drawString("MERCHANT", SCR_W / 2, 12, 2);
    drawCloseX(SCR_W - 2, 2);
    char b[44];
    tft->setTextColor(COL_GREEN, COL_BG);
    snprintf(b, sizeof(b), "Gold: %d", gp.gold);
    tft->drawString(b, SCR_W / 2, 36, 2);

    for (int r = 0; r < SHOP_VIS; r++) {
        int i = top + r; if (i >= n) break;
        int yy = SHOP_TOP + r * SHOP_ROWH;
        bool s = (i == sel);
        if (s) tft->fillRoundRect(4, yy, SCR_W - 8, SHOP_ROWH - 3, 4, COL_GREEN_DIM);
        bool aff = gp.gold >= of[i].price;
        char bon[12]; itemBonusStr(of[i].item, bon, sizeof(bon));
        snprintf(b, sizeof(b), "%s %s", of[i].item.name, bon);
        tft->setTextDatum(ML_DATUM);
        tft->setTextColor(aff ? rarityColor(of[i].item.rarity) : COL_GREEN_DIM, s ? COL_GREEN_DIM : COL_BG);
        tft->drawString(b, 8, yy + (SHOP_ROWH - 3) / 2, 2);
        tft->setTextDatum(MR_DATUM);
        tft->setTextColor(aff ? COL_AMBER : COL_GREEN_DIM, s ? COL_GREEN_DIM : COL_BG);
        snprintf(b, sizeof(b), "%dg", of[i].price);
        tft->drawString(b, SCR_W - 10, yy + (SHOP_ROWH - 3) / 2, 2);
    }

    tft->setTextDatum(MC_DATUM);
    if (fb && fb[0]) {
        tft->setTextColor(COL_GREEN_HI, COL_BG);
        tft->drawString(fb, SCR_W / 2, SCR_H - 10, 1);
    } else {
        tft->setTextColor(COL_GREEN_DIM, COL_BG);
        tft->drawString("swipe: select    tap: buy    X: close", SCR_W / 2, SCR_H - 10, 1);
    }
}

static void shopDialog(int32_t mx, int32_t my)
{
    Offer of[8];
    int n = shopBuild(mx, my, of);
    int sel = 0, top = 0;
    char fb[24] = "";

    { int16_t wx, wy; uint32_t t = millis(); while (watch->getTouch(wx, wy) && millis() - t < 1500) delay(10); }
    inputBegin();
    drawShopList(of, n, sel, top, fb);

    for (;;) {
        int16_t x, y; InputEvent e = inputPoll(x, y);
        if (e != EVT_NONE) powerNoteActivity();
        if (e == EVT_BACK) return;
        if (e == EVT_TAP && hitCloseX(SCR_W - 2, 2, x, y)) return;        // X — выйти
        if (e == EVT_UP)   { if (sel > 0) sel--; if (sel < top) top = sel; drawShopList(of, n, sel, top, fb); }
        else if (e == EVT_DOWN) { if (sel < n - 1) sel++; if (sel >= top + SHOP_VIS) top = sel - SHOP_VIS + 1; drawShopList(of, n, sel, top, fb); }
        else if (e == EVT_TAP) {                       // купить ВЫДЕЛЕННЫЙ пункт
            if (gp.gold < of[sel].price) { strcpy(fb, "not enough gold"); soundBeep(180, 50); }
            else if (invAdd(of[sel].item)) { gp.gold -= of[sel].price; strcpy(fb, "bought!"); soundBeep(820, 50); soundBeep(1040, 50); }
            else { strcpy(fb, "inventory full"); soundBeep(180, 50); }
            drawShopList(of, n, sel, top, fb);
        }
        delay(20);
    }
}

static bool step(int dx, int dy)                // true — игрок реально сдвинулся
{
    int32_t tx = px + dx, ty = py + dy;
    if (!isWalkable(tx, ty)) return false;

    uint8_t tt = tileAt(tx, ty);
    if (tt == TILE_MERCHANT) {                      // торговец — магазин, не входим в клетку
        shopDialog(tx, ty);
        kernelRedraw();
        return false;
    }
    if (tt == TILE_ALTAR && !altarUsedAt(tx, ty)) { // алтарь — благословение, не входим
        altarDialog();
        markAltarUsed(tx, ty);
        kernelRedraw();
        return false;
    }
    if (tt == TILE_EVENT && !eventTriggeredAt(tx, ty)) {   // руна — событие, затем проходим
        eventTrigger();
        markEventTriggered(tx, ty);
        px = tx; py = ty;
        kernelRedraw();
        return true;
    }
    if (tt == TILE_CAMP) {                          // лагерь — чекпоинт: лечит + сохраняет
        campX = tx; campY = ty; hasCamp = true;
        gp.hp = gp.hpMax; gp.mp = gp.mpMax;
        px = tx; py = ty;
        gameSave(px, py, campX, campY, hasCamp);
        infoDialog("CAMP", "saved & healed", COL_GREEN_HI);
        kernelRedraw();
        return true;
    }

    if (tt == TILE_CHEST && !chestOpenedAt(tx, ty)) {   // открыть сундук
        long ax = tx < 0 ? -tx : tx, ay = ty < 0 ? -ty : ty, d = ax > ay ? ax : ay;
        Item it = itemRandom(chestSeed(tx, ty), d);
        bool added = invAdd(it);
        soundPlayFile("/sfx/podbor-bombyi.wav");
        lootDialog(it, added);
        markChestOpened(tx, ty);
        kernelRedraw();
        return false;
    }

    Monster m;
    if (monsterActiveAt(tx, ty, m)) {           // вход в клетку монстра → бой
        CombatResult r = combatRun(m);
        if (r == CR_WIN) {
            int prevLevel = gp.level;
            playerGainXp(m.xp);
            gp.gold += m.gold;
            monsterMarkCleared(tx, ty);
            rewardDialog(m, prevLevel);          // окно награды поверх боя
        } else if (r == CR_LOSE) {
            deathScreen();
            gp.hp = gp.hpMax;
            gp.gold /= 2;                        // штраф смерти
            if (hasCamp) { px = campX; py = campY; }   // респавн у лагеря
            else findStart();
            gameSave(px, py, campX, campY, hasCamp);    // зафиксировать штраф/позицию
        }
        kernelRedraw();                          // вернуть игру + навбар
        return false;                            // в клетку монстра не входим
    }

    px = tx; py = ty;
    gameRenderMap(px, py);
    return true;
}

static void gameEnter()
{
    if (!started) {
        randomSeed(micros());
        if (!gameLoad(px, py, campX, campY, hasCamp)) {   // нет сейва → новая игра
            playerInit(); inventoryInit(); findStart();
            campX = campY = 0; hasCamp = false;
        }
        started = true;
    }
    gameRenderInit();
    tft->fillScreen(COL_BG);
    drawHud();
    gameRenderMap(px, py);
}

static void gameExit()
{
    if (started) gameSave(px, py, campX, campY, hasCamp);   // сохранить при выходе
    gameRenderFree();
}

// Непрерывный опрос тача: краевые зоны карты + автоповтор при удержании.
static void gameTick()
{
    int16_t x, y;
    bool down = watch->getTouch(x, y);

    // Распознать, стал ли текущий жест свайпом (палец ушёл далеко от точки касания).
    if (down) {
        powerNoteActivity();                // пока палец на экране — не спим
        if (!gsActive) { gsActive = true; gsX = x; gsY = y; gsSwipe = false; }
        else if (abs(x - gsX) >= SWIPE_MIN_DELTA || abs(y - gsY) >= SWIPE_MIN_DELTA) gsSwipe = true;
    } else {
        gsActive = false; gsSwipe = false;  // отпустили — свайп обработает inputPoll→onEvent
    }

    int dx = 0, dy = 0;
    bool inEdge = false;

    // Центры стрелок на экране (совпадают с отрисованными в gamerender).
    const int cxMap = MAP_X + VW * TILE / 2, cyMap = MAP_Y + VH * TILE / 2;
    const struct { int ax, ay, dx, dy; } arrows[4] = {
        { cxMap,                       MAP_Y + ARROW_OFF,             0, -1 },
        { cxMap,                       MAP_Y + VH * TILE - ARROW_OFF, 0,  1 },
        { MAP_X + ARROW_OFF,           cyMap,                        -1,  0 },
        { MAP_X + VW * TILE - ARROW_OFF, cyMap,                       1,  0 },
    };
    if (down && !gsSwipe) {                 // во время свайпа стрелками не двигаем
        for (int i = 0; i < 4; i++) {
            int ddx = x - arrows[i].ax, ddy = y - arrows[i].ay;
            if (ddx < 0) ddx = -ddx;
            if (ddy < 0) ddy = -ddy;
            if (ddx <= HIT_R && ddy <= HIT_R) {
                dx = arrows[i].dx; dy = arrows[i].dy; inEdge = true;
                break;
            }
        }
    }

    uint32_t now = millis();
    if (inEdge) {
        if (dx != heldDx || dy != heldDy) {            // новое нажатие в эту сторону
            heldDx = dx; heldDy = dy;
            holdStart = now; lastRep = now;
            step(dx, dy);
        } else if (now - holdStart >= HOLD_DELAY && now - lastRep >= REPEAT_MS) {
            lastRep = now;
            step(dx, dy);                              // автоповтор
        }
    } else {
        heldDx = heldDy = 0;                            // отпустили / центр
    }
}

// ───── Экран персонажа (модальный, 2 страницы: Info / Stats) ─────
static int *statPtr(int i) { return i == 0 ? &gp.str : i == 1 ? &gp.dex : &gp.intel; }

// Нижняя панель навигации по страницам + индикатор «N/2».
static const int CH_NAVY = 206, CH_NAVH = 30, CH_PAGES = 2;
static void drawCharNav(int page)
{
    tft->drawRoundRect(8, CH_NAVY, 80, CH_NAVH, 6, page > 0 ? COL_AMBER : COL_FRAME);
    tft->drawRoundRect(SCR_W - 88, CH_NAVY, 80, CH_NAVH, 6, page < CH_PAGES - 1 ? COL_AMBER : COL_FRAME);
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(page > 0 ? COL_AMBER : COL_GREEN_DIM, COL_BG);
    tft->drawString("< Prev", 48, CH_NAVY + CH_NAVH / 2, 2);
    tft->setTextColor(page < CH_PAGES - 1 ? COL_AMBER : COL_GREEN_DIM, COL_BG);
    tft->drawString("Next >", SCR_W - 48, CH_NAVY + CH_NAVH / 2, 2);
    char b[8]; snprintf(b, sizeof(b), "%d/%d", page + 1, CH_PAGES);
    tft->setTextColor(COL_GREEN, COL_BG);
    tft->drawString(b, SCR_W / 2, CH_NAVY + CH_NAVH / 2, 2);
}

// ── Страница 0: информация о персонаже ──
static void drawCharInfo()
{
    char b[44];
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_GREEN_DIM, COL_BG);
    tft->drawString("- INFO -", SCR_W / 2, 32, 2);

    tft->setTextDatum(ML_DATUM);
    tft->setTextColor(COL_GREEN, COL_BG);
    snprintf(b, sizeof(b), "Lv %d   XP %ld/%ld", gp.level, gp.xp, xpForNext(gp.level));
    tft->drawString(b, 12, 54, 2);
    snprintf(b, sizeof(b), "HP %d/%d", gp.hp, gp.hpMax);
    tft->drawString(b, 12, 76, 2);
    snprintf(b, sizeof(b), "MP %d/%d", gp.mp, gp.mpMax);
    tft->drawString(b, 12, 96, 2);

    int baseAtk = gp.str + 5,  wpn = playerAtkBonus();
    int baseDef = gp.dex / 2,  arm = playerDefBonus();
    tft->setTextColor(COL_GREEN, COL_BG);
    snprintf(b, sizeof(b), "ATK %d", baseAtk + wpn); tft->drawString(b, 12, 122, 2);
    tft->setTextColor(COL_GREEN_DIM, COL_BG);
    snprintf(b, sizeof(b), "= %d base + %d weapon", baseAtk, wpn); tft->drawString(b, 80, 124, 1);

    tft->setTextColor(COL_GREEN, COL_BG);
    snprintf(b, sizeof(b), "DEF %d", baseDef + arm); tft->drawString(b, 12, 144, 2);
    tft->setTextColor(COL_GREEN_DIM, COL_BG);
    snprintf(b, sizeof(b), "= %d base + %d armor", baseDef, arm); tft->drawString(b, 80, 146, 1);

    tft->setTextColor(COL_GREEN, COL_BG);
    snprintf(b, sizeof(b), "Crit %d%%   Dodge %d%%", gp.dex, gp.dex); tft->drawString(b, 12, 168, 2);
    snprintf(b, sizeof(b), "Gold %d", gp.gold); tft->drawString(b, 12, 188, 2);
}

// ── Страница 1: прокачка статов (крупные кнопки [+]) ──
static const int ST_ROW0 = 78, ST_RH = 44;            // центры строк STR/DEX/INT
static const int ST_PLUSX = 162, ST_PLUSW = 66, ST_PLUSH = 38;

static void drawCharStats()
{
    char b[40];
    tft->setTextDatum(MC_DATUM);
    if (gp.statPoints > 0) {
        tft->setTextColor(COL_AMBER, COL_BG);
        snprintf(b, sizeof(b), "Points: %d", gp.statPoints);
    } else {
        tft->setTextColor(COL_GREEN_DIM, COL_BG);
        snprintf(b, sizeof(b), "no points to spend");
    }
    tft->drawString(b, SCR_W / 2, 34, 2);

    const char *names[3] = { "STR", "DEX", "INT" };
    const char *eff[3]   = { "atk +10HP", "dodge/crit/def", "magic +5MP" };
    int vals[3] = { gp.str, gp.dex, gp.intel };
    for (int i = 0; i < 3; i++) {
        int cy = ST_ROW0 + i * ST_RH;
        tft->setTextDatum(ML_DATUM);
        tft->setTextColor(COL_GREEN, COL_BG);
        snprintf(b, sizeof(b), "%s %d", names[i], vals[i]);
        tft->drawString(b, 12, cy - 6, 4);             // крупно
        tft->setTextColor(COL_GREEN_DIM, COL_BG);
        tft->drawString(eff[i], 14, cy + 12, 1);
        if (gp.statPoints > 0) {                       // крупная кнопка [+]
            int by = cy - ST_PLUSH / 2;
            tft->fillRoundRect(ST_PLUSX, by, ST_PLUSW, ST_PLUSH, 6, COL_BG);
            tft->drawRoundRect(ST_PLUSX, by, ST_PLUSW, ST_PLUSH, 6, COL_AMBER);
            // «+» рисуем вручную (в font 6 нет этого символа).
            int pcx = ST_PLUSX + ST_PLUSW / 2, pcy = by + ST_PLUSH / 2, len = 20, th = 4;
            tft->fillRect(pcx - len / 2, pcy - th / 2, len, th, COL_AMBER);
            tft->fillRect(pcx - th / 2, pcy - len / 2, th, len, COL_AMBER);
        }
    }
}

static void drawCharacterPage(int page)
{
    tft->fillScreen(COL_BG);
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_AMBER, COL_BG);
    tft->drawString("CHARACTER", SCR_W / 2, 12, 2);
    drawCloseX(SCR_W - 2, 2);
    if (page == 0) drawCharInfo(); else drawCharStats();
    drawCharNav(page);
}

static void characterScreen()
{
    int page = 0;
    drawCharacterPage(page);
    for (;;) {
        int16_t x, y; InputEvent e = inputPoll(x, y);
        if (e != EVT_NONE) powerNoteActivity();
        if (e == EVT_BACK) return;
        if (e != EVT_TAP) { delay(20); continue; }

        if (hitCloseX(SCR_W - 2, 2, x, y)) return;               // X — закрыть
        // Свайпы тоже листают страницы.
        if (y >= CH_NAVY && x < 90) { if (page > 0) { page--; drawCharacterPage(page); } continue; }
        if (y >= CH_NAVY && x > SCR_W - 90) { if (page < CH_PAGES - 1) { page++; drawCharacterPage(page); } continue; }

        if (page == 1 && gp.statPoints > 0 && x >= ST_PLUSX && x <= ST_PLUSX + ST_PLUSW) {
            for (int i = 0; i < 3; i++) {
                int by = ST_ROW0 + i * ST_RH - ST_PLUSH / 2;
                if (y >= by && y <= by + ST_PLUSH) {
                    (*statPtr(i))++; gp.statPoints--;
                    playerRecalcMax();                  // STR→HP, INT→MP
                    drawCharacterPage(page);
                    break;
                }
            }
        }
        delay(20);
    }
}

// ───── Инвентарь (модальный) ─────
static const int INV_TOP = 58, INV_ROWH = 22, INV_VIS = 5;
static const int INV_SELL_X = 60, INV_SELL_Y = 176, INV_SELL_W = 120, INV_SELL_H = 26;

static void drawInv(int sel, int top, const char *fb = nullptr)
{
    tft->fillScreen(COL_BG);
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_AMBER, COL_BG);
    tft->drawString("INVENTORY", SCR_W / 2, 12, 2);
    drawCloseX(SCR_W - 2, 2);

    char b[44];
    tft->setTextDatum(ML_DATUM);
    tft->setTextColor(COL_GREEN, COL_BG);
    snprintf(b, sizeof(b), "Wpn:%s +%d", hasWeapon ? equWeapon.name : "-", playerAtkBonus());
    tft->drawString(b, 6, 32, 1);
    snprintf(b, sizeof(b), "Arm:%s +%d", hasArmor ? equArmor.name : "-", playerDefBonus());
    tft->drawString(b, 6, 44, 1);

    if (invCount == 0) {
        tft->setTextDatum(MC_DATUM);
        tft->setTextColor(COL_GREEN_DIM, COL_BG);
        tft->drawString("empty", SCR_W / 2, 120, 4);
        return;
    }
    for (int r = 0; r < INV_VIS; r++) {
        int i = top + r; if (i >= invCount) break;
        int yy = INV_TOP + r * INV_ROWH;
        bool s = (i == sel);
        if (s) tft->fillRoundRect(4, yy, SCR_W - 8, INV_ROWH - 3, 4, COL_GREEN_DIM);
        char info[14];
        if (inv[i].kind == IT_WEAPON)      snprintf(info, sizeof(info), "A%d", inv[i].power);
        else if (inv[i].kind == IT_ARMOR)  snprintf(info, sizeof(info), "D%d", inv[i].power);
        else if (inv[i].count > 1)         snprintf(info, sizeof(info), "+%d x%d", inv[i].power, inv[i].count);
        else                               snprintf(info, sizeof(info), "+%d", inv[i].power);
        snprintf(b, sizeof(b), "%s %s", inv[i].name, info);
        tft->setTextDatum(ML_DATUM);
        tft->setTextColor(rarityColor(inv[i].rarity), s ? COL_GREEN_DIM : COL_BG);
        tft->drawString(b, 8, yy + (INV_ROWH - 3) / 2, 2);
    }
    // Кнопка продажи выделенного предмета.
    int sp = itemSellValue(inv[sel]);
    tft->drawRoundRect(INV_SELL_X, INV_SELL_Y, INV_SELL_W, INV_SELL_H, 6, COL_AMBER);
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_AMBER, COL_BG);
    snprintf(b, sizeof(b), "SELL  +%dg", sp);
    tft->drawString(b, SCR_W / 2, INV_SELL_Y + INV_SELL_H / 2, 2);

    tft->setTextDatum(MC_DATUM);
    if (fb) {
        tft->setTextColor(COL_GREEN_HI, COL_BG);
        tft->drawString(fb, SCR_W / 2, SCR_H - 10, 1);
    } else {
        tft->setTextColor(COL_GREEN_DIM, COL_BG);
        tft->drawString("swipe:select  tap:use  SELL  X:close", SCR_W / 2, SCR_H - 10, 1);
    }
}

static void inventoryScreen()
{
    int sel = 0, top = 0;
    char fb[24] = "";
    drawInv(sel, top);
    for (;;) {
        int16_t x, y; InputEvent e = inputPoll(x, y);
        if (e != EVT_NONE) powerNoteActivity();
        if (e == EVT_BACK) return;
        if (e == EVT_TAP && hitCloseX(SCR_W - 2, 2, x, y)) return;   // кнопка X
        if (invCount == 0) { if (e == EVT_TAP) return; delay(20); continue; }

        if (e == EVT_UP)   { if (sel > 0) sel--; if (sel < top) top = sel; fb[0] = 0; drawInv(sel, top); }
        else if (e == EVT_DOWN) { if (sel < invCount - 1) sel++; if (sel >= top + INV_VIS) top = sel - INV_VIS + 1; fb[0] = 0; drawInv(sel, top); }
        else if (e == EVT_TAP) {
            bool sellTap = (x >= INV_SELL_X && x <= INV_SELL_X + INV_SELL_W &&
                            y >= INV_SELL_Y && y <= INV_SELL_Y + INV_SELL_H);
            if (sellTap) {                          // продать выделенный
                int g = invSell(sel);
                snprintf(fb, sizeof(fb), "sold +%dg", g);
                soundBeep(900, 40); soundBeep(700, 40);
            } else {                                // применить ВЫДЕЛЕННЫЙ пункт
                Item &it = inv[sel];
                if (itemIsGear(it)) { invEquip(sel); fb[0] = 0; }
                else if (!invUse(sel)) strcpy(fb, it.kind == IT_HP_POTION ? "HP already full" : "MP already full");
                else fb[0] = 0;
            }
            if (sel >= invCount) sel = invCount - 1;
            if (sel < 0) sel = 0;
            if (sel < top) top = sel;
            if (invCount == 0) return;              // всё продано/использовано — выходим
            drawInv(sel, top, fb[0] ? fb : nullptr);
        }
        delay(20);
    }
}

// ───── Меню игры (по кнопке ≡ сверху справа) ─────
static void gameMenu()
{
    const int w = 162, h = 150, x0 = (SCR_W - w) / 2, y0 = 44;
    tft->fillRoundRect(x0, y0, w, h, 12, COL_BG);
    tft->drawRoundRect(x0, y0, w, h, 12, COL_GREEN);
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_AMBER, COL_BG);
    tft->drawString("MENU", SCR_W / 2, y0 + 16, 2);
    drawCloseX(x0 + w - 4, y0 + 4);

    const char *items[3] = { "Inventory", "Character", "Exit" };
    const int bx = x0 + 14, bw = w - 28, bh = 30, gap = 6, by0 = y0 + 34;
    for (int i = 0; i < 3; i++) {
        int by = by0 + i * (bh + gap);
        tft->drawRoundRect(bx, by, bw, bh, 6, COL_FRAME);
        tft->setTextColor(i == 2 ? COL_AMBER : COL_GREEN, COL_BG);
        tft->drawString(items[i], SCR_W / 2, by + bh / 2, 2);
    }

    for (;;) {
        int16_t x, y; InputEvent e = inputPoll(x, y);
        if (e != EVT_NONE) powerNoteActivity();
        if (e == EVT_BACK) { kernelRedraw(); return; }
        if (e == EVT_TAP) {
            if (x >= bx && x <= bx + bw && y >= by0 && y < by0 + 3 * (bh + gap)) {
                int i = (y - by0) / (bh + gap);
                if (i == 0) { inventoryScreen(); kernelRedraw(); return; }
                if (i == 1) { characterScreen(); kernelRedraw(); return; }
                if (i == 2) { kernelBack(); return; }
            }
            if (x < x0 || x > x0 + w || y < y0 || y > y0 + h) { kernelRedraw(); return; }
        }
        delay(20);
    }
}

// Тап по кнопке-меню (сверху справа) + свайпы для движения.
static void gameEvent(InputEvent e, int16_t x, int16_t y)
{
    if (e == EVT_TAP && x >= SCR_W - 48 && y <= 30) { gameMenu(); return; }   // увеличенная зона
    switch (e) {                                    // свайп = шаг в сторону
        case EVT_UP:    step(0, -1); break;
        case EVT_DOWN:  step(0,  1); break;
        case EVT_LEFT:  step(-1, 0); break;
        case EVT_RIGHT: step( 1, 0); break;
        default: break;
    }
}

// Иконка — меч.
static void gameIcon(TFT_eSPI &g, int cx, int cy, int r)
{
    g.drawLine(cx - r / 2, cy + r / 2, cx + r / 2, cy - r / 2, COL_GREEN);
    g.drawLine(cx - r / 2 + 1, cy + r / 2, cx + r / 2 + 1, cy - r / 2, COL_GREEN);
    g.drawLine(cx - r / 2 - 3, cy + r / 2 - 1, cx - r / 2 + 3, cy + r / 2 + 3, COL_AMBER); // гарда
}

const Program gameProgram = {
    "Dungeon", gameEnter, gameTick, gameEvent, gameIcon, nullptr, -1, nullptr, gameExit
};
