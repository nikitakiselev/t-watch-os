#include "combat.h"
#include "player.h"
#include "inventory.h"
#include "skills.h"
#include "mon_sprites.h"
#include "player_sprites.h"
#include "../../../hw.h"
#include "../../../theme.h"
#include "../../../input.h"
#include "../../../power.h"
#include "../../../sound.h"
#include <Arduino.h>
#include <string.h>

static char logP[28] = "";               // строка действия игрока
static char logM[28] = "";               // строка действия монстра

// Состояние эффектов на время одного боя.
static int shieldTurns   = 0;            // ходов щита (входящий урон ×½)
static int monPoisonTurns = 0;           // ходов яда на монстре
static int monPoisonDmg   = 0;           // урон яда за ход

// Урон с учётом защиты, крита и уклона. tag: "crit"/"miss"/"".
static int dmgCalc(int atk, int def, int critPct, int dodgePct, const char **tag)
{
    *tag = "";
    if ((int)random(100) < dodgePct) { *tag = "miss"; return 0; }
    int base = atk - def / 2; if (base < 1) base = 1;
    int v = base / 4 + 1;
    int d = base + (int)random(-v, v + 1); if (d < 1) d = 1;
    if ((int)random(100) < critPct) { d *= 2; *tag = "crit"; }
    return d;
}

static void drawBar(int x, int y, int w, int cur, int mx, uint16_t col)
{
    if (mx <= 0) mx = 1;
    if (cur < 0) cur = 0;
    tft->drawRect(x, y, w, 10, COL_FRAME);
    int fw = (w - 2) * cur / mx;
    tft->fillRect(x + 1, y + 1, w - 2, 8, COL_BG);
    if (fw > 0) tft->fillRect(x + 1, y + 1, fw, 8, col);
}

// Шанс побега, % (используется и для отображения, и для броска).
static int fleeChance(const Monster &mon)
{
    int c = 50 + (gp.dex - mon.level * 2);
    if (c < 5)  c = 5;
    if (c > 95) c = 95;
    return c;
}

static void drawButton(int x, int w, const char *label, uint16_t col)
{
    tft->drawRoundRect(x + 3, 202, w - 6, 34, 4, COL_FRAME);
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(col, COL_BG);
    tft->drawString(label, x + w / 2, 219, 2);
}

static void drawCombat(const Monster &mon)
{
    tft->fillScreen(COL_BG);
    char b[32];

    // ───── ВЕРХ: монстр (враг) ─────
    snprintf(b, sizeof(b), "%s  L%d", mon.name, mon.level);
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_AMBER, COL_BG);
    tft->drawString(b, SCR_W / 2, 10, 2);

    // Спрайт монстра ×2 (40×40), слева.
    {
        const uint16_t *md = MON_TILES[mon.spriteId];
        for (int yy = 0; yy < MON_TILES_PX; yy++)
            for (int xx = 0; xx < MON_TILES_PX; xx++) {
                uint16_t c = md[yy * MON_TILES_PX + xx];
                if (c != 0xF81F) tft->fillRect(6 + xx * 2, 20 + yy * 2, 2, 2, c);
            }
    }

    drawBar(60, 28, SCR_W - 70, mon.hp, mon.hpMax, COL_AMBER);
    tft->setTextDatum(ML_DATUM);
    tft->setTextColor(COL_GREEN, COL_BG);
    snprintf(b, sizeof(b), "HP %d/%d  ATK %d  DEF %d", mon.hp, mon.hpMax, mon.atk, mon.def);
    tft->drawString(b, 60, 46, 1);

    // ───── Разделитель «VS» ─────
    tft->drawFastHLine(0, 64, SCR_W, COL_FRAME);
    tft->fillRect(SCR_W / 2 - 14, 56, 28, 16, COL_BG);
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_AMBER, COL_BG);
    tft->drawString("VS", SCR_W / 2, 64, 2);

    // ───── Обмен ударами (обе стороны рядом) ─────
    tft->setTextColor(COL_GREEN_HI, COL_BG);
    tft->drawString(logP, SCR_W / 2, 82, 2);             // твой удар
    tft->setTextColor(COL_AMBER, COL_BG);
    tft->drawString(logM, SCR_W / 2, 100, 2);            // удар монстра

    // ───── НИЗ: игрок (симметрично монстру — спрайт ×2 слева) ─────
    {
        const uint16_t *pd = PLAYER_TILES[0];
        for (int yy = 0; yy < PLAYER_TILES_PX; yy++)
            for (int xx = 0; xx < PLAYER_TILES_PX; xx++) {
                uint16_t c = pd[yy * PLAYER_TILES_PX + xx];
                if (c != 0xF81F) tft->fillRect(6 + xx * 2, 114 + yy * 2, 2, 2, c);
            }
    }
    tft->setTextDatum(ML_DATUM);
    tft->setTextColor(COL_GREEN, COL_BG);
    snprintf(b, sizeof(b), "YOU  L%d", gp.level);
    tft->drawString(b, 58, 116, 2);
    if (shieldTurns > 0) {                               // индикатор щита
        tft->setTextColor(COL_AMBER, COL_BG);
        snprintf(b, sizeof(b), "[SH%d]", shieldTurns);
        tft->drawString(b, 120, 116, 1);
    }
    if (monPoisonTurns > 0) {                            // индикатор яда на враге
        tft->setTextColor(COL_GREEN_HI, COL_BG);
        snprintf(b, sizeof(b), "[PSN%d]", monPoisonTurns);
        tft->drawString(b, 60, 8, 1);
    }

    drawBar(58, 130, 120, gp.hp, gp.hpMax, COL_GREEN);
    snprintf(b, sizeof(b), "%d/%d", gp.hp, gp.hpMax);
    tft->drawString(b, 182, 135, 1);
    drawBar(58, 148, 120, gp.mp, gp.mpMax, COL_AMBER);
    tft->setTextColor(COL_AMBER, COL_BG);
    snprintf(b, sizeof(b), "%d/%d", gp.mp, gp.mpMax);
    tft->drawString(b, 182, 153, 1);

    // ───── Кнопки (4 шт.) ─────
    char run[10];
    snprintf(run, sizeof(run), "Run%d", fleeChance(mon));
    drawButton(0,   60, "Atk",   COL_GREEN);
    drawButton(60,  60, "Skill", COL_AMBER);
    drawButton(120, 60, "Item",  COL_GREEN);
    drawButton(180, 60, run,     COL_GREEN);
}

static void playerAttack(Monster &mon)
{
    const char *tag;
    int d = dmgCalc(gp.str + 5 + playerAtkBonus(), mon.def, gp.dex, mon.dodge, &tag);
    mon.hp -= d;
    if (d == 0) { strcpy(logP, "YOU: miss"); if (!soundPlayFile("/sfx/miss.wav")) soundBeep(300, 30); }
    else {
        bool crit = (tag[0] == 'c');
        snprintf(logP, sizeof(logP), "YOU: -%d%s", d, crit ? " CRIT" : "");
        if (crit) { if (!soundPlayFile("/sfx/crit.wav")) soundBeep(950, 60); }
        else      { if (!soundPlayFile("/sfx/hit.wav"))  soundBeep(680, 40); }
    }
}

// Применить навык. false → ход не потрачен (не хватило маны).
static bool castSkill(int id, Monster &mon)
{
    const SkillDef &s = SKILLS[id];
    if (gp.mp < s.cost) { strcpy(logP, "no mana"); soundBeep(180, 50); return false; }
    gp.mp -= s.cost;
    switch (id) {
        case SK_FIREBALL: {
            int d = gp.intel * 2 + 6 + gp.level - mon.def / 4; if (d < 1) d = 1;
            mon.hp -= d;
            snprintf(logP, sizeof(logP), "YOU: -%d fire", d);
            if (!soundPlayFile("/sfx/fire.wav")) { soundBeep(1000, 45); soundBeep(1300, 45); }
            break;
        }
        case SK_HEAL: {
            int h0 = gp.hp; gp.hp += gp.intel * 2 + 12;
            if (gp.hp > gp.hpMax) gp.hp = gp.hpMax;
            snprintf(logP, sizeof(logP), "YOU: +%d hp", gp.hp - h0);
            soundBeep(820, 50); soundBeep(1040, 60);
            break;
        }
        case SK_LIGHTNING: {
            int d = gp.intel * 3 + 8; if (d < 1) d = 1;     // почти игнорирует броню
            mon.hp -= d;
            snprintf(logP, sizeof(logP), "YOU: -%d bolt", d);
            if (!soundPlayFile("/sfx/fire.wav")) { soundBeep(1400, 40); soundBeep(900, 50); }
            break;
        }
        case SK_SHIELD: {
            shieldTurns = 3;
            strcpy(logP, "YOU: shield up");
            soundBeep(500, 60); soundBeep(620, 60);
            break;
        }
        case SK_POISON: {
            monPoisonTurns = 4; monPoisonDmg = 4 + gp.intel / 2;
            strcpy(logP, "YOU: poison!");
            soundBeep(440, 50); soundBeep(360, 70);
            break;
        }
    }
    return true;
}

// Отрисовка меню навыков: список с выделением выбранного пункта.
static const int SKM_W = 190, SKM_ROWH = 30;
static void drawSkillMenu(const int *ids, int n, int sel, int x0, int y0, int h)
{
    tft->fillRoundRect(x0, y0, SKM_W, h, 10, COL_BG);
    tft->drawRoundRect(x0, y0, SKM_W, h, 10, COL_GREEN);
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_AMBER, COL_BG);
    char b[28];
    snprintf(b, sizeof(b), "SKILL  MP %d", gp.mp);
    tft->drawString(b, x0 + SKM_W / 2, y0 + 14, 2);
    tft->drawRoundRect(x0 + SKM_W - 26, y0 + 2, 24, 20, 4, COL_AMBER);
    tft->drawString("X", x0 + SKM_W - 14, y0 + 12, 2);
    for (int r = 0; r < n; r++) {
        int yy = y0 + 28 + r * SKM_ROWH;
        bool s = (r == sel);
        if (s) tft->fillRoundRect(x0 + 4, yy, SKM_W - 8, SKM_ROWH - 3, 4, COL_GREEN_DIM);
        bool aff = gp.mp >= SKILLS[ids[r]].cost;
        tft->setTextDatum(ML_DATUM);
        tft->setTextColor(aff ? COL_GREEN : COL_GREEN_DIM, s ? COL_GREEN_DIM : COL_BG);
        snprintf(b, sizeof(b), "%-9s %dmp", SKILLS[ids[r]].name, SKILLS[ids[r]].cost);
        tft->drawString(b, x0 + 12, yy + (SKM_ROWH - 3) / 2, 2);
    }
}

// Модальное меню навыков (свайп — выбор, тап — применить выделенный). -1 = отмена.
static int skillMenu()
{
    int ids[SK_COUNT], n = 0;
    for (int i = 0; i < SK_COUNT; i++) if (skillKnown(i)) ids[n++] = i;
    if (n == 0) { strcpy(logP, "no skills"); return -1; }

    int h = 34 + n * SKM_ROWH, x0 = (SCR_W - SKM_W) / 2, y0 = (SCR_H - h) / 2, sel = 0;

    // Дождаться отпускания пальца (которым нажали Skill), затем слушать.
    { int16_t wx, wy; uint32_t t = millis(); while (watch->getTouch(wx, wy) && millis() - t < 1500) delay(10); }
    inputBegin();
    drawSkillMenu(ids, n, sel, x0, y0, h);
    for (;;) {
        int16_t x, y;
        InputEvent e = inputPoll(x, y);
        if (e != EVT_NONE) powerNoteActivity();
        if (e == EVT_BACK) return -1;
        if (e == EVT_UP)   { if (sel > 0) { sel--; drawSkillMenu(ids, n, sel, x0, y0, h); } }
        else if (e == EVT_DOWN) { if (sel < n - 1) { sel++; drawSkillMenu(ids, n, sel, x0, y0, h); } }
        else if (e == EVT_TAP) {
            if (x >= x0 + SKM_W - 28 && y >= y0 && y <= y0 + 24) return -1;     // кнопка X
            if (x < x0 || x > x0 + SKM_W || y < y0 || y > y0 + h) return -1;    // тап мимо — отмена
            return ids[sel];                                                   // применить выделенный
        }
        delay(20);
    }
}

static void monsterTurn(Monster &mon)
{
    const char *tag;
    int d = dmgCalc(mon.atk, gp.dex / 2 + playerDefBonus(), 5, gp.dex, &tag);
    if (shieldTurns > 0) { d = (d + 1) / 2; shieldTurns--; }   // щит: входящий урон ×½
    gp.hp -= d;
    if (d == 0) { strcpy(logM, "FOE: miss"); if (!soundPlayFile("/sfx/miss.wav")) soundBeep(220, 40); }
    else {
        snprintf(logM, sizeof(logM), "FOE: -%d%s", d, tag[0] == 'c' ? " CRIT" : "");
        if (!soundPlayFile("/sfx/hurt.wav")) soundBeep(260, 60);
    }
}

CombatResult combatRun(Monster &mon)
{
    logP[0] = 0; logM[0] = 0;
    shieldTurns = 0; monPoisonTurns = 0; monPoisonDmg = 0;
    drawCombat(mon);

    // Дождаться отпускания пальца (которым вошли в бой), иначе его release
    // сразу нажмёт кнопку (стрелка вниз совпадает по месту с кнопками).
    { int16_t wx, wy; uint32_t t = millis(); while (watch->getTouch(wx, wy) && millis() - t < 1500) delay(10); }
    inputBegin();

    for (;;) {
        int16_t x, y;
        InputEvent e = inputPoll(x, y);
        if (e != EVT_NONE) powerNoteActivity();     // бой модальный — сами держим таймер сна
        if (e == EVT_BACK) return CR_FLEE;
        if (e != EVT_TAP) { delay(20); continue; }
        if (y < 202) { delay(20); continue; }          // не по кнопкам

        int act = x / 60; if (act > 3) act = 3;
        bool tookTurn = true;
        if (act == 0) { playerAttack(mon); }
        else if (act == 1) {                            // Skill — меню навыков
            int sk = skillMenu();
            if (sk < 0 || !castSkill(sk, mon)) tookTurn = false;
        }
        else if (act == 2) {                            // Item — зелье HP
            int pi = invFindHpPotion();
            int h0 = gp.hp;
            if (pi >= 0 && invUse(pi)) { snprintf(logP, sizeof(logP), "YOU: +%d hp", gp.hp - h0); soundBeep(820, 50); }
            else { strcpy(logP, pi < 0 ? "no potion" : "hp full"); tookTurn = false; }
        }
        else {                                          // Run (побег)
            if ((int)random(100) < fleeChance(mon)) return CR_FLEE;
            strcpy(logP, "run failed");
        }

        if (tookTurn && monPoisonTurns > 0) {           // тик яда на монстре
            mon.hp -= monPoisonDmg; monPoisonTurns--;
        }
        if (mon.hp <= 0) {
            drawCombat(mon);
            if (!soundPlayFile("/sfx/win.wav")) { soundBeep(700, 50); soundBeep(950, 50); soundBeep(1250, 110); }
            delay(250);
            return CR_WIN;
        }
        if (tookTurn) monsterTurn(mon);
        if (gp.hp <= 0) {
            gp.hp = 0; drawCombat(mon);
            if (!soundPlayFile("/sfx/lose.wav")) { soundBeep(400, 80); soundBeep(300, 80); soundBeep(160, 180); }
            return CR_LOSE;
        }
        drawCombat(mon);
        delay(20);
    }
}
