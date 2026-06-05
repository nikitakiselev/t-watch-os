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
#include "../../../listnav.h"
#include "../../../modal.h"

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

// Сохранить прогресс прямо сейчас (после покупок, прокачки, боёв, лута — чтобы не терять).
static void gameSaveNow() { if (started) gameSave(px, py, campX, campY, hasCamp); }

// Компактный формат золота/цен: 9999 / 12.3k / 1.2M (чтобы не вылезало за экран).
static void goldStr(long g, char *o, int n)
{
    if (g < 10000)         snprintf(o, n, "%ld", g);
    else if (g < 1000000)  snprintf(o, n, "%ld.%ldk", g / 1000, (g % 1000) / 100);
    else                   snprintf(o, n, "%ld.%ldM", g / 1000000, (g % 1000000) / 100000);
}

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
    char gs[12]; goldStr(gp.gold, gs, sizeof(gs));
    snprintf(buf, sizeof(buf), "HP%d MP%d L%d F%d G%s", gp.hp, gp.mp, gp.level, gFloor, gs);
    tft->setTextDatum(ML_DATUM);
    tft->setTextColor(COL_GREEN, COL_BG);
    tft->drawString(buf, 6, 12, 2);

    // Кнопка-меню (гамбургер) сверху справа — в рамке, крупнее (легче попасть).
    const int mw = 34, mx = SCR_W - mw - 2, my = 1, mh = 22;
    tft->drawRoundRect(mx, my, mw, mh, 4, COL_AMBER);
    for (int i = 0; i < 3; i++) tft->drawFastHLine(mx + 8, my + 6 + i * 5, mw - 16, COL_AMBER);
}


// Окно награды после победы — поверх экрана боя (скруглённое).
static void rewardDialog(const Monster &m, int prevLevel)
{
    bool lvUp = gp.level > prevLevel;
    const int w = 188, h = lvUp ? 132 : 100, x0 = (SCR_W - w) / 2, y0 = (SCR_H - h) / 2;
    modalPanel(x0, y0, w, h, 12, COL_AMBER);
    tft->drawRoundRect(x0 + 2, y0 + 2, w - 4, h - 4, 10, COL_GREEN_DIM);

    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_AMBER, COL_BG);
    tft->drawString("VICTORY", SCR_W / 2, y0 + 22, 4);

    char b[28];
    tft->setTextColor(COL_GREEN, COL_BG);
    { char xs[12], gs2[12]; goldStr(m.xp, xs, sizeof(xs)); goldStr(m.gold, gs2, sizeof(gs2));
      snprintf(b, sizeof(b), "+%s XP   +%s gold", xs, gs2); }
    tft->drawString(b, SCR_W / 2, y0 + 48, 2);

    if (lvUp) {
        tft->setTextColor(COL_AMBER, COL_BG);
        snprintf(b, sizeof(b), "LEVEL UP!  Lv%d", gp.level);
        tft->drawString(b, SCR_W / 2, y0 + 72, 2);
        int lv = gp.level - prevLevel;
        tft->setTextColor(COL_GREEN, COL_BG);
        snprintf(b, sizeof(b), "+%d stat  +%d skill pts", lv * STAT_POINTS_PER_LEVEL, lv * SKILL_POINTS_PER_LEVEL);
        tft->drawString(b, SCR_W / 2, y0 + 92, 2);
        tft->setTextColor(COL_GREEN_DIM, COL_BG);
        tft->drawString("spend in Character", SCR_W / 2, y0 + 112, 1);
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
    modalPanel(x0, y0, w, h, 12, COL_AMBER);
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
    modalPanel(x0, y0, w, h, 12, COL_AMBER);
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

// ───── Торговец: экран с вкладками (Weapon/Armor/Potions/Sell), свайп ←/→ ─────
static void itemBonusStr(const Item &it, char *out, int n)
{
    if (it.kind == IT_WEAPON)         snprintf(out, n, "+%d ATK", it.power);
    else if (it.kind == IT_ARMOR)     snprintf(out, n, "+%d DEF", it.power);
    else if (it.kind == IT_HP_POTION) snprintf(out, n, "+%d HP",  it.power);
    else                              snprintf(out, n, "+%d MP",  it.power);
}

// Описание эффекта с величиной для диалога ("fire +5", "leech 10%", ...).
static void effectDescStr(const Item &it, char *out, int n)
{
    const char *ef = effectName(it.effect);
    if (!ef[0]) { out[0] = 0; return; }
    bool pct = (it.effect == EF_PIERCE || it.effect == EF_CRIT ||
                it.effect == EF_LIFESTEAL || it.effect == EF_EVASION);
    snprintf(out, n, "%s +%d%s", ef, it.effMag, pct ? "%" : "");
}

// Один столбец карточки предмета: заголовок, имя (цветом редкости), редкость, ATK/DEF, эффект,
// [цена]. powCol/effCol — цвета статов (для выбранного предмета подсвечивают «лучше/хуже»).
static void drawItemCol(int cx, int y0, const char *hdr, const Item &it,
                        uint16_t powCol, uint16_t effCol, int price)
{
    char b[24];
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_AMBER, COL_BG);                                     tft->drawString(hdr, cx, y0 + 12, 1);
    tft->setTextColor(rarityColor(it.rarity), COL_BG);                       tft->drawString(it.name, cx, y0 + 28, 1);
    tft->setTextColor(COL_GREEN_DIM, COL_BG);                                tft->drawString(rarityName(it.rarity), cx, y0 + 42, 1);
    snprintf(b, sizeof(b), "%s %d", it.kind == IT_WEAPON ? "ATK" : "DEF", it.power);
    tft->setTextColor(powCol, COL_BG);                                       tft->drawString(b, cx, y0 + 64, 2);
    char ee[24]; effectDescStr(it, ee, sizeof(ee));
    tft->setTextColor(effCol, COL_BG);                                       tft->drawString(ee[0] ? ee : "-", cx, y0 + 86, 1);
    if (price >= 0) {
        char ps[12]; goldStr(price, ps, sizeof(ps));
        snprintf(b, sizeof(b), "%sg", ps);
        tft->setTextColor(COL_AMBER, COL_BG);                                tft->drawString(b, cx, y0 + 104, 2);
    }
}

// Диалог с характеристиками предмета + 2 кнопки (verb / Cancel). Для оружия/брони при наличии
// надетого предмета того же слота показывает ДВЕ колонки: слева Equipped, справа Selected (полное
// сравнение, статы выбранного подсвечены лучше/хуже). Зелья и сам надетый предмет — одиночная карточка.
// enabled=false → кнопка действия неактивна (серая, тап игнорируется). Возврат: true = подтверждено.
static bool itemDetailDialog(const Item &it, int price, const char *verb, bool enabled)
{
    const Item *eq = nullptr;
    if      (it.kind == IT_WEAPON && hasWeapon) eq = &equWeapon;
    else if (it.kind == IT_ARMOR  && hasArmor)  eq = &equArmor;
    bool cmp = eq && !(eq->power == it.power && eq->effect == it.effect &&
                       eq->effMag == it.effMag && strcmp(eq->name, it.name) == 0);
    const uint16_t COL_BETTER = COL_GREEN_HI;
    const uint16_t COL_WORSE  = RGB565(0xFF, 0x44, 0x44);

    const int w = cmp ? 228 : 212, h = cmp ? 184 : 156;
    const int x0 = (SCR_W - w) / 2, y0 = (SCR_H - h) / 2;
    modalPanel(x0, y0, w, h, 12, COL_AMBER);
    char b[44];

    if (cmp) {                                      // ── две колонки: Equipped | Selected ──
        int dPow = it.power - eq->power;
        uint16_t powCol = dPow > 0 ? COL_BETTER : dPow < 0 ? COL_WORSE : COL_GREEN;
        uint16_t effCol = COL_AMBER;
        if (it.effect != EF_NONE && it.effect == eq->effect) {   // подсветка эффекта — при том же типе
            int dEff = it.effMag - eq->effMag;
            effCol = dEff > 0 ? COL_BETTER : dEff < 0 ? COL_WORSE : COL_AMBER;
        }
        int mid = SCR_W / 2;
        tft->drawFastVLine(mid, y0 + 10, h - 56, COL_FRAME);
        drawItemCol((x0 + mid) / 2,     y0, "EQUIPPED", *eq, COL_GREEN, COL_AMBER, -1);
        drawItemCol((mid + x0 + w) / 2, y0, "SELECTED", it,  powCol,    effCol,    price);
    } else {                                        // ── одиночная карточка (зелье/надетое/пусто) ──
        tft->setTextDatum(MC_DATUM);
        tft->setTextColor(rarityColor(it.rarity), COL_BG);
        tft->drawString(it.name, SCR_W / 2, y0 + 22, 2);
        tft->setTextColor(COL_GREEN_DIM, COL_BG);
        tft->drawString(rarityName(it.rarity), SCR_W / 2, y0 + 42, 1);
        tft->setTextDatum(ML_DATUM);
        tft->setTextColor(COL_GREEN, COL_BG);
        itemBonusStr(it, b, sizeof(b));
        tft->drawString(b, x0 + 18, y0 + 64, 2);
        char eff[28]; effectDescStr(it, eff, sizeof(eff));
        if (eff[0]) { tft->setTextColor(COL_AMBER, COL_BG); tft->drawString(eff, x0 + 18, y0 + 86, 2); }
        if (price >= 0) {
            tft->setTextDatum(MR_DATUM);
            tft->setTextColor(COL_AMBER, COL_BG);
            char ps[12]; goldStr(price, ps, sizeof(ps));
            snprintf(b, sizeof(b), "%sg", ps);
            tft->drawString(b, x0 + w - 18, y0 + 64, 4);
        }
    }

    const int bw = 84, bh = 30, by = y0 + h - bh - 12;
    const int yx = x0 + 14, cx = x0 + w - 14 - bw;
    tft->setTextDatum(MC_DATUM);
    uint16_t vc = enabled ? COL_GREEN : COL_FRAME;
    tft->drawRoundRect(yx, by, bw, bh, 6, vc);  tft->setTextColor(vc, COL_BG); tft->drawString(verb, yx + bw / 2, by + bh / 2, 2);
    tft->drawRoundRect(cx, by, bw, bh, 6, COL_AMBER);  tft->setTextColor(COL_AMBER, COL_BG); tft->drawString("Cancel", cx + bw / 2, by + bh / 2, 2);

    modalBegin();
    for (;;) {
        int16_t x, y; InputEvent e = modalPoll(x, y);
        if (e == EVT_BACK) return false;
        if (e == EVT_TAP) {
            if (y >= by && y <= by + bh) {
                if (enabled && x >= yx && x <= yx + bw) return true;
                if (x >= cx && x <= cx + bw) return false;
            }
            if (x < x0 || x > x0 + w || y < y0 || y > y0 + h) return false;
        }
    }
}
static const char *MTAB[5] = { "WEAPONS", "ARMOR", "POTIONS", "SELL", "UPGRADE" };
#define MTABS 5

// Стоимость апгрейда экипированного предмета (растёт с силой и глубиной).
static long upgradeCost(const Item &it, int depth)
{
    if (depth < 0) depth = 0;
    return (long)it.power * 80 * (10 + depth) / 10 + 100;
}

// Резолв строки списка магазина в предмет + цену. tab: 0-2 покупка, 3 продажа, 4 апгрейд.
// Возвращает false, если строки нет (пустой слот апгрейда).
static bool merchResolve(int tab, int v, int depth, Item &it, long &price)
{
    if (tab == 4) {
        if (v == 0 && hasWeapon)      it = equWeapon;
        else if (v == 1 && hasArmor)  it = equArmor;
        else return false;
        price = upgradeCost(it, depth);
        return true;
    }
    if (tab == 3) { it = inv[v]; price = itemSellValue(it); return true; }
    it = itemScaled(ITEM_DB[v], depth); price = it.price; return true;
}
static const int M_TOP = 54, M_ROWH = 38, M_VIS = 4, M_NAVY = 210, M_NAVH = 26;

// Нижний нав-бар вкладок: [< Prev]  [CLOSE]  [Next >] — общий для торговца и инвентаря.
static void drawTabNavbar()
{
    tft->drawRoundRect(8, M_NAVY, 76, M_NAVH, 6, COL_AMBER);
    tft->drawRoundRect(SCR_W - 84, M_NAVY, 76, M_NAVH, 6, COL_AMBER);
    tft->drawRoundRect(94, M_NAVY, 52, M_NAVH, 6, COL_AMBER);
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_AMBER, COL_BG);
    tft->drawString("< Prev", 46, M_NAVY + M_NAVH / 2, 1);
    tft->drawString("Next >", SCR_W - 46, M_NAVY + M_NAVH / 2, 1);
    tft->drawString("CLOSE", SCR_W / 2, M_NAVY + M_NAVH / 2, 1);
}

// Список индексов для вкладки: оружие/броня — окно тиров по глубине; зелья — все;
// продажа — инвентарь; апгрейд — экипировка.
static int merchList(int tab, int *out)
{
    int n = 0;
    if (tab == 3) { for (int i = 0; i < invCount; i++) out[n++] = i; return n; }
    if (tab == 4) { if (hasWeapon) out[n++] = 0; if (hasArmor) out[n++] = 1; return n; }
    int depth = -gFloor, lo, hi; merchTierWindow(depth, lo, hi);
    for (int i = 0; i < ITEM_DB_N; i++) {
        const ItemDef &d = ITEM_DB[i];
        if (tab == 0 && d.kind == IT_WEAPON && d.minLvl >= lo && d.minLvl <= hi) out[n++] = i;
        else if (tab == 1 && d.kind == IT_ARMOR && d.minLvl >= lo && d.minLvl <= hi) out[n++] = i;
        else if (tab == 2 && (d.kind == IT_HP_POTION || d.kind == IT_MP_POTION)) out[n++] = i;
    }
    return n;
}

static void drawMerchant(int tab, int sel, int top, const char *fb)
{
    tft->fillScreen(COL_BG);
    char b[44];
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_AMBER, COL_BG);
    snprintf(b, sizeof(b), "SHOP - %s", MTAB[tab]);
    tft->drawString(b, SCR_W / 2, 12, 2);
    tft->setTextColor(COL_GREEN, COL_BG);
    char gs[12]; goldStr(gp.gold, gs, sizeof(gs));
    snprintf(b, sizeof(b), "Gold: %s", gs);
    tft->drawString(b, SCR_W / 2, 32, 1);

    int depth = -gFloor;
    int idx[64]; int n = merchList(tab, idx);
    if (n == 0) {
        tft->setTextColor(COL_GREEN_DIM, COL_BG);
        tft->drawString(tab == 3 ? "nothing to sell" : tab == 4 ? "nothing equipped" : "nothing here",
                        SCR_W / 2, 110, 2);
    }
    for (int r = 0; r < M_VIS; r++) {
        int li = top + r; if (li >= n) break;
        int yy = M_TOP + r * M_ROWH;
        bool s = (li == sel);
        if (s) tft->fillRoundRect(4, yy, SCR_W - 8, M_ROWH - 3, 4, COL_GREEN_DIM);
        Item it; long price = 0;
        merchResolve(tab, idx[li], depth, it, price);
        bool ok = (tab == 3) || gp.gold >= price;
        char bon[12]; itemBonusStr(it, bon, sizeof(bon));
        const char *ef = effectName(it.effect);
        uint16_t bg = s ? COL_GREEN_DIM : COL_BG;
        // тусклый цвет сливается с заливкой выделения → на выделенной строке берём контрастный
        uint16_t dim = s ? COL_GREEN_HI : COL_GREEN_DIM;
        int rh = M_ROWH - 3;
        // строка 1: название — крупным шрифтом (font 2)
        tft->setTextDatum(ML_DATUM);
        tft->setTextColor(ok ? rarityColor(it.rarity) : dim, bg);
        tft->drawString(it.name, 8, yy + 9, 2);
        // строка 2: бонус и эффект (для апгрейда — текущий бонус и «+2»)
        char sub[24];
        if (tab == 4)   snprintf(sub, sizeof(sub), "%s -> +2", bon);
        else if (ef[0]) snprintf(sub, sizeof(sub), "%s [%s]", bon, ef);
        else            snprintf(sub, sizeof(sub), "%s", bon);
        tft->setTextColor(dim, bg);
        tft->drawString(sub, 10, yy + 24, 1);
        // цена справа — крупным (компактно)
        char ps[12]; goldStr(price, ps, sizeof(ps));
        tft->setTextDatum(MR_DATUM);
        tft->setTextColor(ok ? COL_AMBER : dim, bg);
        snprintf(b, sizeof(b), "%sg", ps);
        tft->drawString(b, SCR_W - 8, yy + rh / 2, 2);
    }

    if (fb && fb[0]) {                              // фидбэк наверху, под gold/bag (строки крупнее → низ занят)
        tft->setTextDatum(MC_DATUM);
        tft->setTextColor(COL_GREEN_HI, COL_BG);
        tft->drawString(fb, SCR_W / 2, 44, 1);
    }

    drawTabNavbar();
}

static void merchantScreen()
{
    int tab = 0, sel = 0, top = 0; char fb[24] = "";
    modalBegin();
    drawMerchant(tab, sel, top, fb);
    for (;;) {
        int16_t x, y; InputEvent e = modalPoll(x, y);
        if (e == EVT_BACK) return;
        int idx[64]; int n = merchList(tab, idx);

        int depth = -gFloor;
        if (e == EVT_LEFT || e == EVT_RIGHT) {          // свайп — смена вкладки
            tab = (tab + (e == EVT_RIGHT ? 1 : MTABS - 1)) % MTABS; sel = top = 0; fb[0] = 0;
            drawMerchant(tab, sel, top, fb); continue;
        }
        if (e == EVT_UP || e == EVT_DOWN) {
            ListNav l{n, M_VIS, sel, top};
            if (listNavEvent(l, e)) { sel = l.sel; top = l.top; drawMerchant(tab, sel, top, fb); }
            continue;
        }
        if (e == EVT_TAP) {
            if (y >= M_NAVY) {                          // нижний нав-бар
                if (x < 88) { tab = (tab + MTABS - 1) % MTABS; sel = top = 0; fb[0] = 0; drawMerchant(tab, sel, top, fb); }
                else if (x > SCR_W - 88) { tab = (tab + 1) % MTABS; sel = top = 0; fb[0] = 0; drawMerchant(tab, sel, top, fb); }
                else return;                            // центр — CLOSE
                continue;
            }
            if (n == 0) continue;
            Item it; long price = 0;
            if (!merchResolve(tab, idx[sel], depth, it, price)) continue;
            if (tab == 3) {                             // продать выделенное
                if (itemDetailDialog(it, (int)price, "Sell", true)) {
                    invSell(idx[sel]); snprintf(fb, sizeof(fb), "sold +%ldg", price);
                    soundBeep(900, 40); soundBeep(700, 40);
                    if (sel >= invCount) sel = invCount - 1; if (sel < 0) sel = 0; if (sel < top) top = sel;
                }
            } else if (tab == 4) {                      // апгрейд экипировки
                bool can = gp.gold >= price;
                if (itemDetailDialog(it, (int)price, "Upgrade", can)) {
                    Item &eq = (idx[sel] == 0) ? equWeapon : equArmor;
                    eq.power += 2;
                    if (eq.effect == EF_FIRE || eq.effect == EF_SHOCK || eq.effect == EF_POISON ||
                        eq.effect == EF_VITALITY || eq.effect == EF_SPIRIT) eq.effMag += 1;
                    gp.gold -= price; strcpy(fb, "upgraded!");
                    playerRecalcMax();                  // на случай брони с +HP/+MP
                    soundBeep(820, 50); soundBeep(1100, 60);
                }
            } else {                                    // купить выделенное
                bool canBuy = gp.gold >= price;
                if (itemDetailDialog(it, (int)price, "Buy", canBuy)) {
                    if (invAdd(it)) { gp.gold -= price; strcpy(fb, "bought!"); soundBeep(820, 50); soundBeep(1040, 50); }
                    else { strcpy(fb, "inventory full"); soundBeep(180, 50); }
                }
            }
            drawMerchant(tab, sel, top, fb);
            continue;
        }
    }
}

// ───── Лестница: выбор этажа (0 верх … -128 низ; глубже — сильнее монстры и награда) ─────
static const int FLD_W = 220, FLD_H = 158;
static const int FLD_BW = 84, FLD_BH = 30;

static void drawFloorDialog(int sel, int x0, int y0)
{
    modalPanel(x0, y0, FLD_W, FLD_H, 12, COL_AMBER);
    char b[32];
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_AMBER, COL_BG);
    tft->drawString("STAIRS", SCR_W / 2, y0 + 16, 2);
    tft->setTextColor(COL_GREEN_DIM, COL_BG);
    snprintf(b, sizeof(b), "now: floor %d", gFloor);
    tft->drawString(b, SCR_W / 2, y0 + 34, 1);

    // стрелки + выбранный этаж
    tft->setTextColor(COL_GREEN, COL_BG);
    tft->drawString("<", x0 + 22, y0 + 66, 4);
    tft->drawString(">", x0 + FLD_W - 22, y0 + 66, 4);
    tft->setTextColor(COL_GREEN_HI, COL_BG);
    snprintf(b, sizeof(b), "%d", sel);
    tft->drawString(b, SCR_W / 2, y0 + 66, 4);

    int depth = -sel; if (depth < 0) depth = 0;
    tft->setTextColor(depth > -gFloor ? COL_AMBER : COL_GREEN_DIM, COL_BG);
    snprintf(b, sizeof(b), "monsters Lv ~%d", 1 + depth);
    tft->drawString(b, SCR_W / 2, y0 + 92, 1);

    const int by = y0 + FLD_H - FLD_BH - 12;
    const int gx = x0 + 14, cx = x0 + FLD_W - 14 - FLD_BW;
    tft->drawRoundRect(gx, by, FLD_BW, FLD_BH, 6, COL_GREEN);
    tft->setTextColor(COL_GREEN, COL_BG); tft->drawString("GO", gx + FLD_BW / 2, by + FLD_BH / 2, 2);
    tft->drawRoundRect(cx, by, FLD_BW, FLD_BH, 6, COL_AMBER);
    tft->setTextColor(COL_AMBER, COL_BG); tft->drawString("Cancel", cx + FLD_BW / 2, by + FLD_BH / 2, 2);
}

// Возвращает выбранный этаж (== gFloor, если отмена).
static int floorSelectDialog()
{
    int sel = gFloor;
    const int x0 = (SCR_W - FLD_W) / 2, y0 = (SCR_H - FLD_H) / 2;
    drawFloorDialog(sel, x0, y0);
    modalBegin();
    const int by = y0 + FLD_H - FLD_BH - 12;
    const int gx = x0 + 14, cx = x0 + FLD_W - 14 - FLD_BW;
    for (;;) {
        int16_t x, y; InputEvent e = inputPoll(x, y);
        if (e != EVT_NONE) powerNoteActivity();
        if (e == EVT_BACK) return gFloor;
        if (e == EVT_LEFT)  { if (sel < FLOOR_MAX) { sel++; drawFloorDialog(sel, x0, y0); } continue; }
        if (e == EVT_RIGHT) { if (sel > FLOOR_MIN) { sel--; drawFloorDialog(sel, x0, y0); } continue; }
        if (e == EVT_TAP) {
            if (y >= by && y <= by + FLD_BH) {                       // нижние кнопки
                if (x >= gx && x <= gx + FLD_BW) return sel;          // GO
                if (x >= cx && x <= cx + FLD_BW) return gFloor;       // Cancel
            }
            if (y >= y0 + 50 && y <= y0 + 86) {                     // ряд стрелок
                if (x < SCR_W / 2 - 30)      { if (sel < FLOOR_MAX) { sel++; drawFloorDialog(sel, x0, y0); } continue; }
                if (x > SCR_W / 2 + 30)      { if (sel > FLOOR_MIN) { sel--; drawFloorDialog(sel, x0, y0); } continue; }
            }
            if (x < x0 || x > x0 + FLD_W || y < y0 || y > y0 + FLD_H) return gFloor;   // тап вне — отмена
        }
        delay(20);
    }
}

static bool step(int dx, int dy)                // true — игрок реально сдвинулся
{
    int32_t tx = px + dx, ty = py + dy;
    if (!isWalkable(tx, ty)) return false;

    uint8_t tt0 = tileAt(tx, ty);
    if (tt0 == TILE_STAIRS) {                       // лестница — выбор этажа, не входим
        int nf = floorSelectDialog();
        if (nf != gFloor) {
            gFloor = nf;
            floorReset();                           // новый этаж — свежий (монстры/сундуки/алтари заново)
            gameSave(px, py, campX, campY, hasCamp);
            char b[24]; snprintf(b, sizeof(b), "floor %d", gFloor);
            infoDialog("DESCEND", b, COL_GREEN_HI);
        }
        kernelRedraw();
        return false;
    }

    uint8_t tt = tileAt(tx, ty);
    if (tt == TILE_MERCHANT) {                      // торговец — экран с вкладками, не входим
        merchantScreen();
        gameSaveNow();                              // зафиксировать покупки/продажи
        kernelRedraw();
        return false;
    }
    if (tt == TILE_ALTAR && !altarUsedAt(tx, ty)) { // алтарь — благословение, не входим
        altarDialog();
        markAltarUsed(tx, ty);
        gameSaveNow();
        kernelRedraw();
        return false;
    }
    if (tt == TILE_EVENT && !eventTriggeredAt(tx, ty)) {   // руна — событие, затем проходим
        eventTrigger();
        markEventTriggered(tx, ty);
        px = tx; py = ty;
        gameSaveNow();
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
        Item it = itemRandom(chestSeed(tx, ty), -gFloor);   // лут масштабируется глубиной этажа
        bool added = invAdd(it);
        soundPlayFile("/sfx/podbor-bombyi.wav");
        lootDialog(it, added);
        markChestOpened(tx, ty);
        gameSaveNow();                              // зафиксировать лут
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
            gameSaveNow();                       // зафиксировать опыт/золото/уровень
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
            campX = campY = 0; hasCamp = false; gFloor = 0;
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

// ───── Экран персонажа (модальный, 3 страницы: Info / Stats / Skills) ─────
static int *statPtr(int i) { return i == 0 ? &gp.str : i == 1 ? &gp.dex : &gp.intel; }

// Нижняя панель навигации по страницам + индикатор «N/3».
static const int CH_NAVY = 206, CH_NAVH = 30, CH_PAGES = 3;
static void drawCharNav(int page)
{
    tft->drawRoundRect(8, CH_NAVY, 80, CH_NAVH, 6, page > 0 ? COL_AMBER : COL_FRAME);
    tft->drawRoundRect(SCR_W - 88, CH_NAVY, 80, CH_NAVH, 6, page < CH_PAGES - 1 ? COL_AMBER : COL_FRAME);
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(page > 0 ? COL_AMBER : COL_GREEN_DIM, COL_BG);
    tft->drawString("< Prev", 48, CH_NAVY + CH_NAVH / 2, 2);
    tft->setTextColor(page < CH_PAGES - 1 ? COL_AMBER : COL_GREEN_DIM, COL_BG);
    tft->drawString("Next >", SCR_W - 48, CH_NAVY + CH_NAVH / 2, 2);
    tft->drawRoundRect(92, CH_NAVY, 56, CH_NAVH, 6, COL_AMBER);   // CLOSE по центру
    tft->setTextColor(COL_AMBER, COL_BG);
    tft->drawString("CLOSE", SCR_W / 2, CH_NAVY + CH_NAVH / 2, 2);
}

// ── Страница 0: информация о персонаже ──
// Краткое описание эффекта экипированного предмета (с учётом мастерства для стихий).
static void gearEffStr(const Item &it, bool weapon, char *out, int n)
{
    out[0] = 0;
    switch (it.effect) {
        case EF_FIRE:      snprintf(out, n, " Fire+%d",  it.effMag * (100 + playerMasteryPct(DMG_FIRE))  / 100); break;
        case EF_SHOCK:     snprintf(out, n, " Light+%d", it.effMag * (100 + playerMasteryPct(DMG_LIGHT)) / 100); break;
        case EF_POISON:    snprintf(out, n, " Psn%d/t",  it.effMag); break;
        case EF_PIERCE:    snprintf(out, n, " Pierce%d%%", it.effMag); break;
        case EF_CRIT:      snprintf(out, n, " Crit+%d%%",  it.effMag); break;
        case EF_LIFESTEAL: snprintf(out, n, " Leech%d%%",  it.effMag); break;
        case EF_VITALITY:  snprintf(out, n, " +%dHP",      it.effMag); break;
        case EF_SPIRIT:    snprintf(out, n, " +%dMP",      it.effMag); break;
        case EF_EVASION:   snprintf(out, n, " +%d%%dodge", it.effMag); break;
        default: break;
    }
    (void)weapon;
}

static void drawCharInfo()
{
    char b[48], eff[20];
    int baseAtk = gp.str + 5, wpn = playerAtkBonus();
    int baseDef = gp.dex / 2, arm = playerDefBonus();
    int critPct  = gp.dex + ((hasWeapon && equWeapon.effect == EF_CRIT)    ? equWeapon.effMag : 0);
    int dodgePct = gp.dex + ((hasArmor  && equArmor.effect  == EF_EVASION) ? equArmor.effMag  : 0);
    if (critPct  > 75) critPct  = 75;          // потолок (см. dmgCalc MAX_CRIT/MAX_DODGE)
    if (dodgePct > 75) dodgePct = 75;

    tft->setTextDatum(ML_DATUM);
    tft->setTextColor(COL_GREEN, COL_BG);
    snprintf(b, sizeof(b), "Lv %d   XP %ld/%ld", gp.level, gp.xp, xpForNext(gp.level));
    tft->drawString(b, 10, 36, 2);
    snprintf(b, sizeof(b), "HP %d/%d   MP %d/%d", gp.hp, gp.hpMax, gp.mp, gp.mpMax);
    tft->drawString(b, 10, 58, 2);

    snprintf(b, sizeof(b), "ATK %d", baseAtk + wpn); tft->drawString(b, 10, 80, 2);
    tft->setTextColor(COL_GREEN_DIM, COL_BG);
    snprintf(b, sizeof(b), "%d base +%d wpn", baseAtk, wpn); tft->drawString(b, 96, 82, 1);
    tft->setTextColor(COL_GREEN, COL_BG);
    snprintf(b, sizeof(b), "DEF %d", baseDef + arm); tft->drawString(b, 10, 100, 2);
    tft->setTextColor(COL_GREEN_DIM, COL_BG);
    snprintf(b, sizeof(b), "%d base +%d arm", baseDef, arm); tft->drawString(b, 96, 102, 1);

    tft->setTextColor(COL_GREEN, COL_BG);
    snprintf(b, sizeof(b), "Crit %d%%   Dodge %d%%", critPct, dodgePct);
    tft->drawString(b, 10, 122, 2);

    // ── Экипировка (с эффектами/стихийным уроном) ──
    tft->setTextColor(COL_AMBER, COL_BG);
    if (hasWeapon) { gearEffStr(equWeapon, true, eff, sizeof(eff));
                     snprintf(b, sizeof(b), "W: %s +%dA%s", equWeapon.name, equWeapon.power, eff); }
    else           snprintf(b, sizeof(b), "W: none");
    tft->drawString(b, 10, 146, 1);
    if (hasArmor)  { gearEffStr(equArmor, false, eff, sizeof(eff));
                     snprintf(b, sizeof(b), "A: %s +%dD%s", equArmor.name, equArmor.power, eff); }
    else           snprintf(b, sizeof(b), "A: none");
    tft->drawString(b, 10, 160, 1);

    // ── Пассивы (+% к урону стихией; вампиризм — % HP от физ. урона) ──
    tft->setTextColor(COL_GREEN_HI, COL_BG);
    snprintf(b, sizeof(b), "Mast F+%d%% S+%d%% V+%d%%  Vamp+%d%%",
             playerMasteryPct(DMG_FIRE), playerMasteryPct(DMG_LIGHT),
             playerMasteryPct(DMG_POISON), playerLifestealPct());
    tft->drawString(b, 10, 176, 1);

    tft->setTextColor(COL_GREEN_DIM, COL_BG);
    char gs[12]; goldStr(gp.gold, gs, sizeof(gs));
    snprintf(b, sizeof(b), "Gold %s   Floor %d   pts S%d K%d",
             gs, gFloor, gp.statPoints, gp.skillPoints);
    tft->drawString(b, 10, 190, 1);
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

// ── Страница 2: навыки (прокручиваемый список) ──
static const int SKP_TOP = 50, SKP_ROWH = 38, SKP_VIS = 4;
static int chSkTop = 0, chSkSel = 0;

// Геометрия инфо-окна навыка.
static const int SKI_W = 216, SKI_H = 160;
static const int SKI_BW = 96, SKI_BH = 30;

static void drawSkillInfo(int id)
{
    const SkillDef &s = SKILLS[id];
    int rank = skillRank(id);
    int dr   = rank > 0 ? rank : 1;            // если не изучен — превью с R1
    int pw   = skillPowerAt(id, dr);
    int dur  = skillDurationAt(id, dr);
    bool passive = skillPassive(id);

    char l1[40] = "", l2[40] = "", l3[40] = "";
    if (passive) {
        if (id == SK_M_VAMP) {
            snprintf(l1, sizeof(l1), "Passive - vampirism");
            snprintf(l2, sizeof(l2), "+%d%% HP from phys dmg", rank);
        } else {
            snprintf(l1, sizeof(l1), "Passive - %s mastery", dmgTypeName(s.dmgType));
            snprintf(l2, sizeof(l2), "+%d%% %s damage", rank, dmgTypeName(s.dmgType));
        }
    } else {
        switch (s.kind) {
            case SKK_DAMAGE:
                if (id == SK_POWERSTRIKE) {                  // усиленный удар оружием
                    int atkv = gp.str + 5 + playerAtkBonus();
                    int mult = 150 + (dr - 1) * 6;
                    snprintf(l1, sizeof(l1), "Active - weapon strike");
                    snprintf(l2, sizeof(l2), "~%d dmg (x%d.%02d ATK)", atkv * mult / 100, mult / 100, mult % 100);
                } else {
                    snprintf(l1, sizeof(l1), "Active - %s damage", dmgTypeName(s.dmgType));
                    snprintf(l2, sizeof(l2), "~%d dmg%s", pw, s.ignoreDef ? ", ignores armor" : "");
                }
                break;
            case SKK_DOT:
                snprintf(l1, sizeof(l1), "Active - %s over time", dmgTypeName(s.dmgType));
                snprintf(l2, sizeof(l2), "%d/turn x%d turns", pw, dur);
                break;
            case SKK_DRAIN:
                snprintf(l1, sizeof(l1), "Active - %s lifesteal", dmgTypeName(s.dmgType));
                snprintf(l2, sizeof(l2), "~%d dmg, heal 50%%", pw);
                break;
            case SKK_HEAL:
                snprintf(l1, sizeof(l1), "Active - heal");
                snprintf(l2, sizeof(l2), "+%d HP", pw);
                break;
            case SKK_HOT:
                snprintf(l1, sizeof(l1), "Active - regen");
                snprintf(l2, sizeof(l2), "+%d HP/turn x%d", pw, dur);
                break;
            case SKK_SHIELD:
                snprintf(l1, sizeof(l1), "Active - shield");
                snprintf(l2, sizeof(l2), "incoming dmg /2, %d turns", dur);
                break;
        }
        snprintf(l3, sizeof(l3), "Cost: %d MP", s.cost);
    }

    const int x0 = (SCR_W - SKI_W) / 2, y0 = (SCR_H - SKI_H) / 2;
    modalPanel(x0, y0, SKI_W, SKI_H, 12, passive ? COL_AMBER : COL_GREEN);
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(passive ? COL_AMBER : COL_GREEN_HI, COL_BG);
    tft->drawString(s.name, SCR_W / 2, y0 + 20, 4);
    tft->setTextColor(COL_GREEN_DIM, COL_BG);
    tft->drawString(l1, SCR_W / 2, y0 + 44, 2);
    tft->setTextColor(COL_GREEN, COL_BG);
    tft->drawString(l2, SCR_W / 2, y0 + 64, 2);
    if (l3[0]) { tft->setTextColor(COL_AMBER, COL_BG); tft->drawString(l3, SCR_W / 2, y0 + 84, 2); }
    tft->setTextColor(COL_GREEN_DIM, COL_BG);
    char rl[28]; snprintf(rl, sizeof(rl), "Rank %d / %d   (pts %d)", rank, SKILL_MAX_RANK, gp.skillPoints);
    tft->drawString(rl, SCR_W / 2, y0 + 104, 2);

    // Кнопки: [+1 Rank] (если есть очки и не максимум) и [Close].
    bool canUp = gp.skillPoints > 0 && rank < SKILL_MAX_RANK;
    const int by = y0 + SKI_H - SKI_BH - 12;
    const int ux = x0 + 14, cx = x0 + SKI_W - 14 - SKI_BW;
    uint16_t uc = canUp ? COL_GREEN : COL_FRAME;
    tft->drawRoundRect(ux, by, SKI_BW, SKI_BH, 6, uc);
    tft->setTextColor(canUp ? COL_GREEN : COL_GREEN_DIM, COL_BG);
    tft->drawString(canUp ? "+1 Rank" : "max", ux + SKI_BW / 2, by + SKI_BH / 2, 2);
    tft->drawRoundRect(cx, by, SKI_BW, SKI_BH, 6, COL_AMBER);
    tft->setTextColor(COL_AMBER, COL_BG);
    tft->drawString("Close", cx + SKI_BW / 2, by + SKI_BH / 2, 2);
}

// Инфо-окно навыка с прокачкой. Возвращает true, если ранг был изменён (нужна перерисовка списка).
static bool skillInfoDialog(int id)
{
    bool changed = false;
    const int x0 = (SCR_W - SKI_W) / 2, y0 = (SCR_H - SKI_H) / 2;
    const int by = y0 + SKI_H - SKI_BH - 12;
    const int ux = x0 + 14, cx = x0 + SKI_W - 14 - SKI_BW;
    drawSkillInfo(id);
    modalBegin();
    for (;;) {
        int16_t x, y; InputEvent e = inputPoll(x, y);
        if (e != EVT_NONE) powerNoteActivity();
        if (e == EVT_BACK) return changed;
        if (e == EVT_TAP) {
            if (y >= by && y <= by + SKI_BH) {
                if (x >= cx && x <= cx + SKI_BW) return changed;       // Close
                if (x >= ux && x <= ux + SKI_BW &&                     // +1 Rank
                    gp.skillPoints > 0 && skillRank(id) < SKILL_MAX_RANK) {
                    gp.skillRank[id]++; gp.skillPoints--; changed = true;
                    soundBeep(900, 30);
                    drawSkillInfo(id);
                    continue;
                }
            }
            if (x < x0 || x > x0 + SKI_W || y < y0 || y > y0 + SKI_H) return changed;  // тап вне — закрыть
        }
        delay(20);
    }
}

static void drawCharSkills()
{
    char b[40];
    tft->setTextDatum(MC_DATUM);
    if (gp.skillPoints > 0) {
        tft->setTextColor(COL_AMBER, COL_BG);
        snprintf(b, sizeof(b), "Skill pts: %d", gp.skillPoints);
    } else {
        tft->setTextColor(COL_GREEN_DIM, COL_BG);
        snprintf(b, sizeof(b), "no skill points");
    }
    tft->drawString(b, SCR_W / 2, 32, 2);

    for (int r = 0; r < SKP_VIS; r++) {
        int li = chSkTop + r; if (li >= SK_COUNT) break;
        const SkillDef &s = SKILLS[li];
        int rank = skillRank(li);
        int yy = SKP_TOP + r * SKP_ROWH;
        bool sel = (li == chSkSel);
        if (sel) tft->fillRoundRect(4, yy, SCR_W - 8, SKP_ROWH - 3, 4, COL_GREEN_DIM);
        uint16_t bg = sel ? COL_GREEN_DIM : COL_BG;
        bool passive = (s.kind == SKK_PASSIVE);

        tft->setTextDatum(ML_DATUM);
        uint16_t nc = rank > 0 ? (passive ? COL_AMBER : COL_GREEN) : (sel ? COL_GREEN_HI : COL_GREEN_DIM);
        tft->setTextColor(nc, bg);
        tft->drawString(s.name, 10, yy + (SKP_ROWH - 3) / 2, 2);

        tft->setTextDatum(MR_DATUM);
        tft->setTextColor(sel ? COL_GREEN_HI : COL_GREEN_DIM, bg);
        if (passive) snprintf(b, sizeof(b), "+%d%%", rank);    // мастерство: +1%/ранг
        else         snprintf(b, sizeof(b), "R%d", rank);
        tft->drawString(b, SCR_W - 12, yy + (SKP_ROWH - 3) / 2, 2);
    }
}

static void drawCharacterPage(int page)
{
    tft->fillScreen(COL_BG);
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_AMBER, COL_BG);
    char t[20]; snprintf(t, sizeof(t), "CHARACTER  %d/%d", page + 1, CH_PAGES);
    tft->drawString(t, SCR_W / 2, 12, 2);
    if (page == 0) drawCharInfo(); else if (page == 1) drawCharStats(); else drawCharSkills();
    drawCharNav(page);
}

static void characterScreen()
{
    int page = 0;
    chSkTop = chSkSel = 0;
    drawCharacterPage(page);
    for (;;) {
        int16_t x, y; InputEvent e = inputPoll(x, y);
        if (e != EVT_NONE) powerNoteActivity();
        if (e == EVT_BACK) return;

        if (page == 2 && (e == EVT_UP || e == EVT_DOWN)) {      // прокрутка списка навыков
            ListNav l{SK_COUNT, SKP_VIS, chSkSel, chSkTop};
            if (listNavEvent(l, e)) { chSkSel = l.sel; chSkTop = l.top; drawCharacterPage(page); }
            continue;
        }
        if (e != EVT_TAP) { delay(20); continue; }

        if (y >= CH_NAVY) {                                      // нижний нав-бар
            if (x < 90) { if (page > 0) { page--; drawCharacterPage(page); } }
            else if (x > SCR_W - 90) { if (page < CH_PAGES - 1) { page++; drawCharacterPage(page); } }
            else return;                                         // центр = CLOSE
            continue;
        }

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
        else if (page == 2) {       // тап активирует ВЫДЕЛЕННЫЙ навык (свайп — выбор)
            skillInfoDialog(chSkSel);
            drawCharacterPage(page);
        }
        delay(20);
    }
}

// ───── Инвентарь: тот же интерфейс с вкладками, что у торговца ─────
// Вкладки Weapons/Armor/Potions (имена берём из MTAB[0..2]). Свайп ←/→ + навбар.
#define INV_TABS 3

// Список строк вкладки. Кодирование строки: -1 = надетое оружие, -2 = надетая броня,
// >= 0 — индекс inv[]. Надетый предмет идёт первой (закреплённой) строкой своей вкладки.
static int invTabList(int tab, int *out)
{
    int n = 0;
    if (tab == 0) {                                 // Weapons
        if (hasWeapon) out[n++] = -1;
        for (int i = 0; i < invCount; i++) if (inv[i].kind == IT_WEAPON) out[n++] = i;
    } else if (tab == 1) {                          // Armor
        if (hasArmor) out[n++] = -2;
        for (int i = 0; i < invCount; i++) if (inv[i].kind == IT_ARMOR) out[n++] = i;
    } else {                                        // Potions
        for (int i = 0; i < invCount; i++)
            if (inv[i].kind == IT_HP_POTION || inv[i].kind == IT_MP_POTION) out[n++] = i;
    }
    return n;
}

// Резолв кода строки в предмет; equipped=true для надетого (-1/-2).
static const Item &invRowItem(int code, bool &equipped)
{
    equipped = (code < 0);
    if (code == -1) return equWeapon;
    if (code == -2) return equArmor;
    return inv[code];
}

// Зелье полезно прямо сейчас? (HP/MP полные → бесполезно; снаряжение всегда «да»).
static bool invRowUsable(const Item &it)
{
    if (it.kind == IT_HP_POTION) return gp.hp < gp.hpMax;
    if (it.kind == IT_MP_POTION) return gp.mp < gp.mpMax;
    return true;
}

static void drawInvTab(int tab, int sel, int top, const char *fb)
{
    tft->fillScreen(COL_BG);
    char b[44];
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_AMBER, COL_BG);
    snprintf(b, sizeof(b), "INV - %s", MTAB[tab]);
    tft->drawString(b, SCR_W / 2, 12, 2);
    tft->setTextColor(COL_GREEN, COL_BG);
    snprintf(b, sizeof(b), "Bag %d/%d", invCount, INV_MAX);
    tft->drawString(b, SCR_W / 2, 32, 1);

    int idx[INV_MAX + 2]; int n = invTabList(tab, idx);
    if (n == 0) {
        tft->setTextColor(COL_GREEN_DIM, COL_BG);
        tft->drawString("empty", SCR_W / 2, 110, 2);
    }
    for (int r = 0; r < M_VIS; r++) {
        int li = top + r; if (li >= n) break;
        int yy = M_TOP + r * M_ROWH;
        bool s = (li == sel);
        if (s) tft->fillRoundRect(4, yy, SCR_W - 8, M_ROWH - 3, 4, COL_GREEN_DIM);
        bool eq; const Item &it = invRowItem(idx[li], eq);
        bool usable = invRowUsable(it);
        uint16_t bg = s ? COL_GREEN_DIM : COL_BG;
        uint16_t dim = s ? COL_GREEN_HI : COL_GREEN_DIM;
        int rh = M_ROWH - 3;
        // строка 1: название цветом редкости (тускло, если зелье бесполезно сейчас)
        tft->setTextDatum(ML_DATUM);
        tft->setTextColor(usable ? rarityColor(it.rarity) : dim, bg);
        tft->drawString(it.name, 8, yy + 9, 2);
        // строка 2: бонус и эффект
        char bon[12]; itemBonusStr(it, bon, sizeof(bon));
        const char *ef = effectName(it.effect);
        char sub[24];
        if (ef[0]) snprintf(sub, sizeof(sub), "%s [%s]", bon, ef);
        else       snprintf(sub, sizeof(sub), "%s", bon);
        tft->setTextColor(dim, bg);
        tft->drawString(sub, 10, yy + 24, 1);
        // правый столбец: [E] для надетого, xN для стопки зелий
        tft->setTextDatum(MR_DATUM);
        if (eq) {
            tft->setTextColor(COL_AMBER, bg);
            tft->drawString("[E]", SCR_W - 8, yy + rh / 2, 2);
        } else if (it.count > 1) {
            tft->setTextColor(dim, bg);
            snprintf(b, sizeof(b), "x%d", it.count);
            tft->drawString(b, SCR_W - 8, yy + rh / 2, 2);
        }
    }

    if (fb && fb[0]) {                              // фидбэк наверху, под gold/bag (строки крупнее → низ занят)
        tft->setTextDatum(MC_DATUM);
        tft->setTextColor(COL_GREEN_HI, COL_BG);
        tft->drawString(fb, SCR_W / 2, 44, 1);
    }

    drawTabNavbar();
}

static void inventoryScreen()
{
    int tab = 0, sel = 0, top = 0; char fb[24] = "";
    modalBegin();
    drawInvTab(tab, sel, top, fb);
    for (;;) {
        int16_t x, y; InputEvent e = modalPoll(x, y);
        if (e == EVT_BACK) return;
        int idx[INV_MAX + 2]; int n = invTabList(tab, idx);

        if (e == EVT_LEFT || e == EVT_RIGHT) {          // свайп — смена вкладки
            tab = (tab + (e == EVT_RIGHT ? 1 : INV_TABS - 1)) % INV_TABS; sel = top = 0; fb[0] = 0;
            drawInvTab(tab, sel, top, fb); continue;
        }
        if (e == EVT_UP || e == EVT_DOWN) {
            ListNav l{n, M_VIS, sel, top};
            if (listNavEvent(l, e)) { sel = l.sel; top = l.top; drawInvTab(tab, sel, top, fb); }
            continue;
        }
        if (e == EVT_TAP) {
            if (y >= M_NAVY) {                          // нижний нав-бар
                if (x < 88) { tab = (tab + INV_TABS - 1) % INV_TABS; sel = top = 0; fb[0] = 0; drawInvTab(tab, sel, top, fb); }
                else if (x > SCR_W - 88) { tab = (tab + 1) % INV_TABS; sel = top = 0; fb[0] = 0; drawInvTab(tab, sel, top, fb); }
                else return;                            // центр — CLOSE
                continue;
            }
            if (n == 0) continue;
            bool eq; const Item &it = invRowItem(idx[sel], eq);
            if (eq) {                                   // надетое — только просмотр статов
                itemDetailDialog(it, -1, "Equipped", false);
            } else if (itemIsGear(it)) {                // снаряжение — надеть
                if (itemDetailDialog(it, -1, "Equip", true)) {
                    invEquip(idx[sel]); strcpy(fb, "equipped"); soundBeep(820, 50);
                    sel = top = 0;                      // список перестроился (предмет ушёл в [E])
                }
            } else {                                    // зелье — выпить
                bool usable = invRowUsable(it);
                if (itemDetailDialog(it, -1, "Use", usable)) {
                    int h0 = gp.hp, m0 = gp.mp;
                    if (invUse(idx[sel])) {
                        int dh = gp.hp - h0, dm = gp.mp - m0;
                        snprintf(fb, sizeof(fb), dh ? "+%d HP" : "+%d MP", dh ? dh : dm);
                        soundBeep(820, 50);
                        n = invTabList(tab, idx);       // стопка уменьшилась/строка ушла — поправим sel
                        if (sel >= n) sel = n - 1; if (sel < 0) sel = 0; if (sel < top) top = sel;
                    }
                }
            }
            drawInvTab(tab, sel, top, fb);
            continue;
        }
    }
}

// ───── Меню игры (по кнопке ≡ сверху справа) ─────
static const int GM_W = 162, GM_H = 150, GM_X0 = (SCR_W - 162) / 2, GM_Y0 = 44;
static void drawGameMenu(int sel)
{
    modalPanel(GM_X0, GM_Y0, GM_W, GM_H, 12, COL_GREEN);
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_AMBER, COL_BG);
    tft->drawString("MENU", SCR_W / 2, GM_Y0 + 16, 2);

    const char *items[3] = { "Inventory", "Character", "Exit" };
    const int bx = GM_X0 + 14, bw = GM_W - 28, bh = 30, gap = 6, by0 = GM_Y0 + 34;
    for (int i = 0; i < 3; i++) {
        int by = by0 + i * (bh + gap);
        if (i == sel) tft->fillRoundRect(bx, by, bw, bh, 6, COL_GREEN_DIM);
        tft->drawRoundRect(bx, by, bw, bh, 6, i == sel ? COL_AMBER : COL_FRAME);
        tft->setTextColor(i == 2 ? COL_AMBER : COL_GREEN, i == sel ? COL_GREEN_DIM : COL_BG);
        tft->drawString(items[i], SCR_W / 2, by + bh / 2, 2);
    }
}

static void gameMenu()
{
    ListNav l{3, 0, 0, 0};                       // свайп ↑↓ — выбор, тап — выбрать активный
    drawGameMenu(l.sel);
    for (;;) {
        int16_t x, y; InputEvent e = modalPoll(x, y);
        if (e == EVT_BACK) { kernelRedraw(); return; }
        if (e == EVT_UP || e == EVT_DOWN) { if (listNavEvent(l, e)) drawGameMenu(l.sel); continue; }
        if (e == EVT_TAP) {
            if (x < GM_X0 || x > GM_X0 + GM_W || y < GM_Y0 || y > GM_Y0 + GM_H) { kernelRedraw(); return; }  // тап мимо — закрыть
            if (l.sel == 0) { inventoryScreen(); gameSaveNow(); kernelRedraw(); return; }   // экипировка/зелья
            if (l.sel == 1) { characterScreen(); gameSaveNow(); kernelRedraw(); return; }   // очки статов/навыков
            kernelBack(); return;                // Exit
        }
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
