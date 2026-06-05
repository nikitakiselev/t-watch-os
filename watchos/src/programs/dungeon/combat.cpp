#include "combat.h"
#include "player.h"
#include "inventory.h"
#include "skills.h"
#include "sprites_gen.h"
#include "../../../hw.h"
#include "../../../theme.h"
#include "../../../input.h"
#include "../../../power.h"
#include "../../../sound.h"
#include "../../../listnav.h"
#include "../../../modal.h"
#include <Arduino.h>
#include <string.h>
#include <stdarg.h>

static char logP[28] = "";               // строка действия игрока
static char logM[28] = "";               // строка действия монстра

// Состояние эффектов на время одного боя.
static int shieldTurns   = 0;            // ходов щита (входящий урон ×½)
static int monPoisonTurns = 0;           // ходов DoT на монстре
static int monPoisonDmg   = 0;           // урон DoT за ход
static int monPoisonLast  = 0;           // фактический урон последнего тика (для показа)
static int monDotType     = DMG_POISON;  // тип DoT (для слаб./сопр./мастерства)
static int regenTurns     = 0;           // ходов регена игрока
static int regenAmt       = 0;           // лечение регена за ход

// Подробный разбор последнего удара (раскрывается тапом по логу боя; полноэкранный, прокручиваемый).
#define HIT_MAXLINES 28
struct HitInfo { char title[24]; char lines[HIT_MAXLINES][34]; int n; };
static HitInfo hitP, hitM;
static bool hasHitP = false, hasHitM = false;
static void hitBegin(HitInfo &h, const char *t)
{
    strncpy(h.title, t, sizeof(h.title) - 1); h.title[sizeof(h.title) - 1] = 0; h.n = 0;
}
static void hitLine(HitInfo &h, const char *fmt, ...)
{
    if (h.n >= HIT_MAXLINES) return;
    va_list a; va_start(a, fmt);
    vsnprintf(h.lines[h.n], sizeof(h.lines[h.n]), fmt, a);
    va_end(a); h.n++;
}

// Потолки крита/уклона — иначе при высоком DEX наступает 100% неуязвимость/вечный крит.
#define MAX_CRIT  75
#define MAX_DODGE 75

// Урон с учётом защиты, крита и уклона. tag: "crit"/"miss"/"".
static int dmgCalc(int atk, int def, int critPct, int dodgePct, const char **tag)
{
    *tag = "";
    if (critPct  > MAX_CRIT)  critPct  = MAX_CRIT;
    if (dodgePct > MAX_DODGE) dodgePct = MAX_DODGE;
    if (critPct < 0) critPct = 0;  if (dodgePct < 0) dodgePct = 0;
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

// Спрайт 16×16 с увеличением ×2 (32×32), ключ прозрачности 0xF81F.
static void blitSpriteX2(const uint16_t *d, int x, int y)
{
    for (int yy = 0; yy < SPR_PX; yy++)
        for (int xx = 0; xx < SPR_PX; xx++) {
            uint16_t c = d[yy * SPR_PX + xx];
            // 0xF81F — прозрачность, 0xF81E — маркер тени (на чёрном фоне боя невидим) → оба пропускаем.
            if (c != 0xF81F && c != 0xF81E) tft->fillRect(x + xx * 2, y + yy * 2, 2, 2, c);
        }
}

// Округление вверх деления на 100 (для x>=0). Любой положительный процент даёт >=1:
// пассивки (+1%/ранг) вносят вклад уже с 1-го ранга, а не теряются при floor.
static int ceilDiv100(int x) { return x > 0 ? (x + 99) / 100 : 0; }

// Модификатор урона по типу: слабость ×2, сопротивление ÷2 (минимум 1, если урон был).
static const char *gTypeTag = "";   // метка последнего удара: "WEAK!"/"RES"/"" (для лога)

static int typeMod(int d, int type, const Monster &mon)
{
    gTypeTag = "";
    if (d <= 0) return d;
    d += ceilDiv100(d * playerMasteryPct(type));      // пассивное мастерство игрока (+% урона типа, ceil)
    if      (type == mon.weak)   { d *= 2; gTypeTag = "WEAK!"; }
    else if (type == mon.resist) { d /= 2; if (d < 1) d = 1; gTypeTag = "RES"; }
    return d;
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

    // Спрайт монстра ×2 (32×32), слева — тап по нему открывает инфо.
    blitSpriteX2(SPRITES[SPR_MON_BASE + mon.spriteId], 6, 20);

    drawBar(60, 28, SCR_W - 70, mon.hp, mon.hpMax, COL_AMBER);
    tft->setTextDatum(ML_DATUM);
    tft->setTextColor(COL_GREEN, COL_BG);
    snprintf(b, sizeof(b), "HP %d/%d", mon.hp, mon.hpMax);
    tft->drawString(b, 60, 46, 1);
    if (monPoisonTurns > 0) {                            // живой DoT: ходов осталось + урон/тик
        tft->setTextColor(COL_GREEN_HI, COL_BG);
        snprintf(b, sizeof(b), "DoT x%d  -%d/t", monPoisonTurns, monPoisonLast);
        tft->drawString(b, 120, 46, 1);
    }
    tft->setTextDatum(MR_DATUM);
    tft->setTextColor(COL_GREEN_DIM, COL_BG);
    tft->drawString("[i]", SCR_W - 4, 8, 1);   // подсказка: тап по иконке = инфо

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

    // ───── НИЗ: игрок (симметрично монстру — спрайт ×2 слева, тап = инфо) ─────
    blitSpriteX2(SPRITES[SPR_PLAYER], 6, 114);
    tft->setTextDatum(ML_DATUM);
    tft->setTextColor(COL_GREEN, COL_BG);
    snprintf(b, sizeof(b), "YOU  L%d", gp.level);
    tft->drawString(b, 58, 116, 2);
    if (shieldTurns > 0) {                               // индикатор щита
        tft->setTextColor(COL_AMBER, COL_BG);
        snprintf(b, sizeof(b), "[SH%d]", shieldTurns);
        tft->drawString(b, 120, 116, 1);
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

// Величина эффекта экипированного оружия/брони (0, если нет/не тот эффект).
static int wpnEff(uint8_t ef) { return (hasWeapon && equWeapon.effect == ef) ? equWeapon.effMag : 0; }
static int armEff(uint8_t ef) { return (hasArmor  && equArmor.effect  == ef) ? equArmor.effMag  : 0; }

// Модальное окно: ждёт тапа/кнопки и закрывается (чисто информационное).
static void waitDismiss()
{
    modalBegin();
    for (;;) {
        int16_t x, y; InputEvent e = modalPoll(x, y);
        if (e == EVT_TAP || e == EVT_BACK) return;
    }
}

// Инфо о монстре: статы + слабость/сопротивление — чтобы решить, вступать ли в бой.
static void monsterInfoDialog(const Monster &mon)
{
    const int w = 216, h = 184, x0 = (SCR_W - w) / 2, y0 = (SCR_H - h) / 2;
    modalPanel(x0, y0, w, h, 12, COL_AMBER);
    blitSpriteX2(SPRITES[SPR_MON_BASE + mon.spriteId], x0 + 14, y0 + 14);

    char b[32];
    tft->setTextDatum(ML_DATUM);
    tft->setTextColor(COL_AMBER, COL_BG);
    tft->drawString(mon.name, x0 + 54, y0 + 20, 2);
    tft->setTextColor(COL_GREEN_DIM, COL_BG);
    snprintf(b, sizeof(b), "Level %d", mon.level);
    tft->drawString(b, x0 + 54, y0 + 40, 1);

    int yy = y0 + 60;
    tft->setTextColor(COL_GREEN, COL_BG);
    snprintf(b, sizeof(b), "HP %d", mon.hpMax);             tft->drawString(b, x0 + 14, yy, 2);
    snprintf(b, sizeof(b), "ATK %d", mon.atk);              tft->drawString(b, x0 + 116, yy, 2); yy += 22;
    snprintf(b, sizeof(b), "DEF %d", mon.def);              tft->drawString(b, x0 + 14, yy, 2);
    snprintf(b, sizeof(b), "Dodge %d%%", mon.dodge);        tft->drawString(b, x0 + 116, yy, 2); yy += 24;

    tft->setTextColor(COL_GREEN_DIM, COL_BG);
    tft->drawString("Weak", x0 + 14, yy, 1);
    tft->drawString("Resist", x0 + 116, yy, 1); yy += 14;
    tft->setTextColor(COL_GREEN_HI, COL_BG);
    tft->drawString(mon.weak   >= 0 ? dmgTypeName(mon.weak)   : "-", x0 + 14, yy, 2);
    tft->setTextColor(COL_AMBER, COL_BG);
    tft->drawString(mon.resist >= 0 ? dmgTypeName(mon.resist) : "-", x0 + 116, yy, 2);

    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_GREEN_DIM, COL_BG);
    tft->drawString("tap to close", SCR_W / 2, y0 + h - 12, 1);
    waitDismiss();
}

// Инфо об игроке: базовые статы для сравнения с монстром.
static void playerInfoDialog()
{
    const int w = 216, h = 184, x0 = (SCR_W - w) / 2, y0 = (SCR_H - h) / 2;
    modalPanel(x0, y0, w, h, 12, COL_GREEN);
    blitSpriteX2(SPRITES[SPR_PLAYER], x0 + 14, y0 + 14);

    char b[32];
    tft->setTextDatum(ML_DATUM);
    tft->setTextColor(COL_GREEN, COL_BG);
    tft->drawString("YOU", x0 + 54, y0 + 20, 2);
    tft->setTextColor(COL_GREEN_DIM, COL_BG);
    snprintf(b, sizeof(b), "Level %d", gp.level);
    tft->drawString(b, x0 + 54, y0 + 40, 1);

    int atk  = gp.str + 5 + playerAtkBonus();
    int def  = gp.dex / 2 + playerDefBonus();
    int crit = gp.dex + wpnEff(EF_CRIT);   if (crit  > MAX_CRIT)  crit  = MAX_CRIT;
    int dodge= gp.dex + armEff(EF_EVASION); if (dodge > MAX_DODGE) dodge = MAX_DODGE;

    int yy = y0 + 60;
    tft->setTextColor(COL_GREEN, COL_BG);
    snprintf(b, sizeof(b), "HP %d/%d", gp.hp, gp.hpMax);    tft->drawString(b, x0 + 14, yy, 2);
    snprintf(b, sizeof(b), "MP %d/%d", gp.mp, gp.mpMax);    tft->drawString(b, x0 + 116, yy, 2); yy += 22;
    snprintf(b, sizeof(b), "ATK %d", atk);                 tft->drawString(b, x0 + 14, yy, 2);
    snprintf(b, sizeof(b), "DEF %d", def);                 tft->drawString(b, x0 + 116, yy, 2); yy += 22;
    snprintf(b, sizeof(b), "Crit %d%%", crit);             tft->drawString(b, x0 + 14, yy, 2);
    snprintf(b, sizeof(b), "Dodge %d%%", dodge);           tft->drawString(b, x0 + 116, yy, 2); yy += 22;
    tft->setTextColor(COL_GREEN_DIM, COL_BG);
    snprintf(b, sizeof(b), "STR %d  DEX %d  INT %d", gp.str, gp.dex, gp.intel);
    tft->drawString(b, x0 + 14, yy, 1); yy += 16;

    int mf = playerMasteryPct(DMG_FIRE), msg = playerMasteryPct(DMG_LIGHT), mv = playerMasteryPct(DMG_POISON);
    tft->setTextColor(COL_AMBER, COL_BG);
    snprintf(b, sizeof(b), "Mast F+%d S+%d V+%d Vamp+%d%%", mf, msg, mv, playerLifestealPct());
    tft->drawString(b, x0 + 14, yy, 1);

    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_GREEN_DIM, COL_BG);
    tft->drawString("tap to close", SCR_W / 2, y0 + h - 12, 1);
    waitDismiss();
}

// Полноэкранный разбор удара: свайп ↑/↓ — прокрутка текста, ←/→ — смена стороны, тап — закрыть.
static const int HV_TOP = 50, HV_ROWH = 20, HV_VIS = 8;
static void drawHitFull(const HitInfo &h, int top, int ns)
{
    tft->fillScreen(COL_BG);
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_AMBER, COL_BG);
    tft->drawString(h.title, SCR_W / 2, 14, 4);
    tft->setTextColor(COL_GREEN_DIM, COL_BG);
    tft->drawString(ns > 1 ? "swipe Up/Dn . Lf/Rt side" : "swipe Up/Dn to scroll", SCR_W / 2, 34, 1);
    tft->drawFastHLine(8, 44, SCR_W - 16, COL_FRAME);

    tft->setTextDatum(ML_DATUM);
    for (int r = 0; r < HV_VIS; r++) {
        int li = top + r; if (li >= h.n) break;
        const char *s = h.lines[li];
        // строки-итоги и заголовки секций выделяем ярче
        bool emph = (s[0] == '=' || s[0] == '-');
        tft->setTextColor(emph ? COL_GREEN_HI : COL_GREEN, COL_BG);
        tft->drawString(s, 12, HV_TOP + r * HV_ROWH, 2);
    }

    // индикаторы прокрутки + футер
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_AMBER, COL_BG);
    if (top > 0)                tft->drawString("^", SCR_W - 14, HV_TOP, 2);
    if (top + HV_VIS < h.n)     tft->drawString("v", SCR_W - 14, HV_TOP + (HV_VIS - 1) * HV_ROWH, 2);
    tft->setTextColor(COL_GREEN_DIM, COL_BG);
    tft->drawString("tap to close", SCR_W / 2, 230, 1);
}

// start: 0 — начать со своего удара, 1 — с удара монстра.
static void showHitDialog(int start)
{
    int sides[2], ns = 0;
    if (hasHitP) sides[ns++] = 0;
    if (hasHitM) sides[ns++] = 1;
    if (ns == 0) return;
    int cur = 0;
    for (int i = 0; i < ns; i++) if (sides[i] == start) cur = i;

    int top = 0;
    const HitInfo *h = (sides[cur] == 0) ? &hitP : &hitM;
    drawHitFull(*h, top, ns);
    modalBegin();
    for (;;) {
        int16_t x, y; InputEvent e = modalPoll(x, y);
        if (e == EVT_BACK) return;
        if (e == EVT_DOWN) { if (top + HV_VIS < h->n) { top++; drawHitFull(*h, top, ns); } continue; }
        if (e == EVT_UP)   { if (top > 0)             { top--; drawHitFull(*h, top, ns); } continue; }
        if (ns > 1 && (e == EVT_LEFT || e == EVT_RIGHT)) {
            cur = (cur + (e == EVT_RIGHT ? 1 : ns - 1)) % ns;
            h = (sides[cur] == 0) ? &hitP : &hitM; top = 0;
            drawHitFull(*h, top, ns);
            continue;
        }
        if (e == EVT_TAP) return;                          // тап — закрыть
    }
}

static void playerAttack(Monster &mon)
{
    const char *tag;
    int edef = mon.def * (100 - wpnEff(EF_PIERCE)) / 100;     // пробитие брони
    int crit = gp.dex + wpnEff(EF_CRIT);                      // +крит от оружия
    int roll = dmgCalc(gp.str + 5 + playerAtkBonus(), edef, crit, mon.dodge, &tag);
    bool crt = (tag[0] == 'c');

    hitBegin(hitP, "Your attack");
    if (roll == 0) {
        hitLine(hitP, "missed (foe dodge %d%%)", mon.dodge);
        strcpy(logP, "YOU: miss"); if (!soundPlayFile("/sfx/miss.wav")) soundBeep(300, 30);
        hasHitP = true; return;
    }

    // Физическая часть удара.
    int dphys = typeMod(roll, DMG_PHYS, mon);
    const char *physTag = gTypeTag;

    // Стихия оружия (огонь/свет) — отдельный кусок своим типом: ловит слабость + мастерство.
    int wef = hasWeapon ? equWeapon.effect : EF_NONE;
    int elemBase = 0, elemD = 0, et = -1, emast = 0;
    const char *elemTag = "";
    if (wef == EF_FIRE || wef == EF_SHOCK) {
        et = effectDmgType(wef);
        elemBase = equWeapon.effMag;
        emast = playerMasteryPct(et);
        elemD = typeMod(elemBase, et, mon);
        elemTag = gTypeTag;
    }

    int d = dphys + elemD;
    mon.hp -= d;

    int atkVal = gp.str + 5 + playerAtkBonus();
    int pbase  = atkVal - edef / 2; if (pbase < 1) pbase = 1;
    int rollPre = crt ? roll / 2 : roll;

    hitLine(hitP, "-- PHYSICAL --");
    hitLine(hitP, "STR+5 base    %d", gp.str + 5);
    if (playerAtkBonus()) hitLine(hitP, "weapon ATK   +%d", playerAtkBonus());
    hitLine(hitP, "your ATK      %d", atkVal);
    hitLine(hitP, "foe DEF       %d%s", edef, wpnEff(EF_PIERCE) ? " (pierced)" : "");
    hitLine(hitP, "minus DEF/2  -%d", edef / 2);
    hitLine(hitP, "base hit      %d", pbase);
    hitLine(hitP, "with rng     ~%d", rollPre);
    if (crt) hitLine(hitP, "CRIT x2       %d", roll);
    if (physTag[0]) hitLine(hitP, "foe %s %s   %d", physTag, physTag[0] == 'W' ? "x2" : "/2", dphys);
    hitLine(hitP, "= phys dmg    %d", dphys);

    if (et >= 0) {                                           // стихийный кусок оружия
        int elemMast = elemBase + ceilDiv100(elemBase * emast);
        hitLine(hitP, "-- WEAPON %s --", dmgTypeName(et));
        hitLine(hitP, "elem base     %d", elemBase);
        if (emast)      hitLine(hitP, "mastery +%d%%  %d", emast, elemMast);
        if (elemTag[0]) hitLine(hitP, "foe %s %s   %d", elemTag, elemTag[0] == 'W' ? "x2" : "/2", elemD);
        hitLine(hitP, "= %s dmg  %d", dmgTypeName(et), elemD);
    }

    hitLine(hitP, "-- TOTAL --");
    if (et >= 0) hitLine(hitP, "phys %d + elem %d", dphys, elemD);
    hitLine(hitP, "= %d damage", d);

    // Вампиризм: пассивный навык (всегда на физ. урон) + бонус оружия.
    int lsPct = playerLifestealPct() + wpnEff(EF_LIFESTEAL);
    int lsHp = 0;
    if (lsPct > 0) {
        lsHp = ceilDiv100(d * lsPct);                        // ceil: «чуть-чуть» = минимум 1
        gp.hp += lsHp; if (gp.hp > gp.hpMax) gp.hp = gp.hpMax;
        hitLine(hitP, "lifesteal %d%%  +%d hp", lsPct, lsHp);
    }

    // В лог выносим самый показательный тег (стихия важнее физики) + вампиризм.
    const char *logTag = (et >= 0 && elemTag[0]) ? elemTag : physTag;
    char lsbuf[12] = "";
    if (lsHp > 0) snprintf(lsbuf, sizeof(lsbuf), " +%dhp", lsHp);
    snprintf(logP, sizeof(logP), "YOU: -%d%s%s%s%s", d, crt ? " CRIT" : "",
             logTag[0] ? " " : "", logTag, lsbuf);
    if (crt) { if (!soundPlayFile("/sfx/crit.wav")) soundBeep(950, 60); }
    else     { if (!soundPlayFile("/sfx/hit.wav"))  soundBeep(680, 40); }

    int pz = wpnEff(EF_POISON);                              // яд от оружия (DoT)
    if (pz) { monPoisonTurns = 4; monPoisonDmg = pz; monDotType = DMG_POISON;
              hitLine(hitP, "applies poison %d/t x4", pz); }
    hasHitP = true;
}

// Применить навык. false → ход не потрачен (не хватило маны).
static bool castSkill(int id, Monster &mon)
{
    const SkillDef &s = SKILLS[id];
    int rank = skillRank(id);
    int pw   = skillPowerAt(id, rank);
    int dur  = skillDurationAt(id, rank);
    if (gp.mp < s.cost) { strcpy(logP, "no mana"); soundBeep(180, 50); return false; }
    gp.mp -= s.cost;

    int mast = playerMasteryPct(s.dmgType);
    hitBegin(hitP, s.name);
    switch (s.kind) {
        case SKK_DAMAGE: {
            bool wstrike = (id == SK_POWERSTRIKE);            // усиленный удар оружием
            int atkv = gp.str + 5 + playerAtkBonus();
            int mult = 150 + (rank - 1) * 6;                  // ×1.5 на r1, +6%/ранг
            int base = wstrike ? (atkv * mult / 100) : pw;
            int afterDef = base - (s.ignoreDef ? 0 : mon.def / 4); if (afterDef < 1) afterDef = 1;
            int afterMast = afterDef + ceilDiv100(afterDef * mast);
            int d = typeMod(afterDef, s.dmgType, mon);
            mon.hp -= d;
            const char *stat = (s.dmgType == DMG_PHYS) ? "STR" : "INT";
            hitLine(hitP, "-- %s (%s) --", s.name, dmgTypeName(s.dmgType));
            if (wstrike) {
                hitLine(hitP, "your ATK      %d", atkv);
                hitLine(hitP, " STR+5 %d + wpn %d", gp.str + 5, playerAtkBonus());
                hitLine(hitP, "x%d.%02d -> %d", mult / 100, mult % 100, base);
            } else {
                hitLine(hitP, "power         %d", base);
                hitLine(hitP, " base%d +%s scale", s.basePow + s.perRank * (rank - 1), stat);
            }
            if (!s.ignoreDef) hitLine(hitP, "foe DEF/4    -%d", mon.def / 4);
            else              hitLine(hitP, "ignores armor");
            hitLine(hitP, "after def     %d", afterDef);
            if (mast)        hitLine(hitP, "mastery +%d%%  %d", mast, afterMast);
            if (gTypeTag[0]) hitLine(hitP, "foe %s %s   %d", gTypeTag, gTypeTag[0] == 'W' ? "x2" : "/2", d);
            hitLine(hitP, "= %d damage", d);
            // Вампиризм действует на ЛЮБОЙ физ. урон (вкл. Power Strike).
            int lsHp = 0;
            if (s.dmgType == DMG_PHYS) {
                int lsPct = playerLifestealPct();
                if (lsPct > 0) { lsHp = ceilDiv100(d * lsPct);
                                 gp.hp += lsHp; if (gp.hp > gp.hpMax) gp.hp = gp.hpMax;
                                 hitLine(hitP, "lifesteal %d%%  +%d hp", lsPct, lsHp); }
            }
            hitLine(hitP, "cost          %d MP", s.cost);
            char lsbuf[12] = ""; if (lsHp > 0) snprintf(lsbuf, sizeof(lsbuf), " +%dhp", lsHp);
            snprintf(logP, sizeof(logP), "YOU: -%d %s%s%s%s", d, dmgTypeName(s.dmgType),
                     gTypeTag[0] ? " " : "", gTypeTag, lsbuf);
            if (!soundPlayFile("/sfx/fire.wav")) { soundBeep(1100, 45); soundBeep(1400, 45); }
            break;
        }
        case SKK_DOT: {
            monPoisonTurns = dur;
            monPoisonDmg   = pw;
            monDotType     = s.dmgType;
            const char *t = (s.dmgType == mon.weak) ? " WEAK!" : (s.dmgType == mon.resist) ? " RES" : "";
            hitLine(hitP, "-- %s (%s DoT) --", s.name, dmgTypeName(s.dmgType));
            hitLine(hitP, "damage/turn   %d", pw);
            if (mast) hitLine(hitP, "mastery +%d%%", mast);
            if (t[0]) hitLine(hitP, "foe %s (per tick)", t + 1);
            hitLine(hitP, "lasts         %d turns", dur);
            hitLine(hitP, "cost          %d MP", s.cost);
            snprintf(logP, sizeof(logP), "YOU: %s x%d%s", s.name, monPoisonTurns, t);
            soundBeep(440, 50); soundBeep(360, 70);
            break;
        }
        case SKK_DRAIN: {
            int afterDef = pw - mon.def / 4; if (afterDef < 1) afterDef = 1;
            int d = typeMod(afterDef, s.dmgType, mon);
            mon.hp -= d;
            int heal = d / 2;                               // вампиризм: лечит на половину урона
            int h0 = gp.hp; gp.hp += heal; if (gp.hp > gp.hpMax) gp.hp = gp.hpMax;
            hitLine(hitP, "-- %s (%s) --", s.name, dmgTypeName(s.dmgType));
            hitLine(hitP, "power         %d", pw);
            hitLine(hitP, "foe DEF/4    -%d", mon.def / 4);
            if (mast)        hitLine(hitP, "mastery +%d%%", mast);
            if (gTypeTag[0]) hitLine(hitP, "foe %s %s   %d", gTypeTag, gTypeTag[0] == 'W' ? "x2" : "/2", d);
            hitLine(hitP, "= %d damage", d);
            hitLine(hitP, "lifesteal     +%d hp", gp.hp - h0);
            hitLine(hitP, "cost          %d MP", s.cost);
            snprintf(logP, sizeof(logP), "YOU: -%d +%dhp%s%s", d, gp.hp - h0,
                     gTypeTag[0] ? " " : "", gTypeTag);
            soundBeep(700, 50); soundBeep(500, 60);
            break;
        }
        case SKK_HEAL: {
            int h0 = gp.hp; gp.hp += pw;
            if (gp.hp > gp.hpMax) gp.hp = gp.hpMax;
            hitLine(hitP, "-- %s --", s.name);
            hitLine(hitP, "heal power    %d", pw);
            hitLine(hitP, "= +%d hp", gp.hp - h0);
            hitLine(hitP, "cost          %d MP", s.cost);
            snprintf(logP, sizeof(logP), "YOU: +%d hp", gp.hp - h0);
            soundBeep(820, 50); soundBeep(1040, 60);
            break;
        }
        case SKK_HOT: {
            regenTurns = dur;
            regenAmt   = pw;
            hitLine(hitP, "-- %s --", s.name);
            hitLine(hitP, "regen/turn    +%d hp", pw);
            hitLine(hitP, "lasts         %d turns", dur);
            hitLine(hitP, "cost          %d MP", s.cost);
            snprintf(logP, sizeof(logP), "YOU: regen x%d", regenTurns);
            soundBeep(700, 50); soundBeep(900, 50);
            break;
        }
        case SKK_SHIELD: {
            shieldTurns = dur;
            hitLine(hitP, "-- %s --", s.name);
            hitLine(hitP, "incoming dmg  /2");
            hitLine(hitP, "lasts         %d turns", dur);
            hitLine(hitP, "cost          %d MP", s.cost);
            snprintf(logP, sizeof(logP), "YOU: shield %d", shieldTurns);
            soundBeep(500, 60); soundBeep(620, 60);
            break;
        }
        default: break;
    }
    hasHitP = true;
    return true;
}

// Меню навыков: прокручиваемый список (свайп) с выделением выбранного.
static const int SKM_W = 200, SKM_ROWH = 28, SKM_VIS = 5;
static const int SKM_H = 34 + SKM_VIS * SKM_ROWH;
static void drawSkillMenu(const int *ids, int n, int sel, int top, int x0, int y0)
{
    modalPanel(x0, y0, SKM_W, SKM_H, 10, COL_GREEN);
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_AMBER, COL_BG);
    char b[32];
    snprintf(b, sizeof(b), "SKILL  MP %d", gp.mp);
    tft->drawString(b, x0 + SKM_W / 2, y0 + 14, 2);
    for (int r = 0; r < SKM_VIS; r++) {
        int li = top + r; if (li >= n) break;
        int id = ids[li];
        int yy = y0 + 28 + r * SKM_ROWH;
        bool s = (li == sel);
        if (s) tft->fillRoundRect(x0 + 4, yy, SKM_W - 8, SKM_ROWH - 3, 4, COL_GREEN_DIM);
        bool aff = gp.mp >= SKILLS[id].cost;
        uint16_t bg = s ? COL_GREEN_DIM : COL_BG;
        tft->setTextDatum(ML_DATUM);
        tft->setTextColor(aff ? COL_GREEN : (s ? COL_GREEN_HI : COL_GREEN_DIM), bg);
        snprintf(b, sizeof(b), "%s R%d", SKILLS[id].name, skillRank(id));
        tft->drawString(b, x0 + 12, yy + (SKM_ROWH - 3) / 2, 2);
        tft->setTextDatum(MR_DATUM);
        tft->setTextColor(aff ? COL_AMBER : (s ? COL_GREEN_HI : COL_GREEN_DIM), bg);
        snprintf(b, sizeof(b), "%dmp", SKILLS[id].cost);
        tft->drawString(b, x0 + SKM_W - 10, yy + (SKM_ROWH - 3) / 2, 1);
    }
    if (n > SKM_VIS) {                                  // индикатор прокрутки
        tft->setTextDatum(MC_DATUM);
        tft->setTextColor(COL_GREEN_DIM, COL_BG);
        tft->drawString("swipe", x0 + SKM_W / 2, y0 + SKM_H - 8, 1);
    }
}

// Модальное меню навыков (свайп — выбор, тап — применить выделенный). -1 = отмена.
static int skillMenu()
{
    int ids[SK_COUNT], n = 0;
    for (int i = 0; i < SK_COUNT; i++) if (skillUsable(i)) ids[n++] = i;
    if (n == 0) { strcpy(logP, "no skills"); return -1; }

    int x0 = (SCR_W - SKM_W) / 2, y0 = (SCR_H - SKM_H) / 2, sel = 0, top = 0;

    // Дождаться отпускания пальца (которым нажали Skill), затем слушать.
    modalBegin();
    drawSkillMenu(ids, n, sel, top, x0, y0);
    for (;;) {
        int16_t x, y;
        InputEvent e = modalPoll(x, y);
        if (e == EVT_BACK) return -1;
        if (e == EVT_UP || e == EVT_DOWN) {
            ListNav l{n, SKM_VIS, sel, top};
            if (listNavEvent(l, e)) { sel = l.sel; top = l.top; drawSkillMenu(ids, n, sel, top, x0, y0); }
        }
        else if (e == EVT_TAP) {
            if (x < x0 || x > x0 + SKM_W || y < y0 || y > y0 + SKM_H) return -1;   // тап мимо — отмена
            return ids[sel];                                                      // применить выделенный
        }
    }
}

// Зелье полезно прямо сейчас? (HP полное / MP полное — бесполезно, рисуем тускло).
static bool potionUsable(const Item &it)
{
    if (it.kind == IT_HP_POTION) return gp.hp < gp.hpMax;
    if (it.kind == IT_MP_POTION) return gp.mp < gp.mpMax;
    return false;
}

// Меню зелий (HP/MP) — зеркало меню навыков: список со свайпом, выделение выбранного.
static void drawItemMenu(const int *ids, int n, int sel, int top, int x0, int y0)
{
    modalPanel(x0, y0, SKM_W, SKM_H, 10, COL_GREEN);
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_AMBER, COL_BG);
    tft->drawString("ITEM", x0 + SKM_W / 2, y0 + 14, 2);
    char b[32];
    for (int r = 0; r < SKM_VIS; r++) {
        int li = top + r; if (li >= n) break;
        const Item &it = inv[ids[li]];
        int yy = y0 + 28 + r * SKM_ROWH;
        bool s = (li == sel);
        if (s) tft->fillRoundRect(x0 + 4, yy, SKM_W - 8, SKM_ROWH - 3, 4, COL_GREEN_DIM);
        bool aff = potionUsable(it);
        uint16_t bg = s ? COL_GREEN_DIM : COL_BG;
        tft->setTextDatum(ML_DATUM);
        tft->setTextColor(aff ? COL_GREEN : (s ? COL_GREEN_HI : COL_GREEN_DIM), bg);
        snprintf(b, sizeof(b), "%s +%d", it.name, it.power);
        tft->drawString(b, x0 + 12, yy + (SKM_ROWH - 3) / 2, 2);
        tft->setTextDatum(MR_DATUM);
        tft->setTextColor(aff ? COL_AMBER : (s ? COL_GREEN_HI : COL_GREEN_DIM), bg);
        snprintf(b, sizeof(b), "x%d", it.count);
        tft->drawString(b, x0 + SKM_W - 10, yy + (SKM_ROWH - 3) / 2, 1);
    }
    if (n > SKM_VIS) {                                  // индикатор прокрутки
        tft->setTextDatum(MC_DATUM);
        tft->setTextColor(COL_GREEN_DIM, COL_BG);
        tft->drawString("swipe", x0 + SKM_W / 2, y0 + SKM_H - 8, 1);
    }
}

// Модальное меню зелий (свайп — выбор, тап — применить). -1 = отмена/нет зелий.
// Возвращает индекс в inv[]; бесполезные сейчас зелья выбрать нельзя.
static int itemMenu()
{
    int ids[INV_MAX], n = 0;
    for (int i = 0; i < invCount; i++)
        if (inv[i].kind == IT_HP_POTION || inv[i].kind == IT_MP_POTION) ids[n++] = i;
    if (n == 0) { strcpy(logP, "no potions"); return -1; }

    int x0 = (SCR_W - SKM_W) / 2, y0 = (SCR_H - SKM_H) / 2, sel = 0, top = 0;

    // Дождаться отпускания пальца (которым нажали Item), затем слушать.
    modalBegin();
    drawItemMenu(ids, n, sel, top, x0, y0);
    for (;;) {
        int16_t x, y;
        InputEvent e = modalPoll(x, y);
        if (e == EVT_BACK) return -1;
        if (e == EVT_UP || e == EVT_DOWN) {
            ListNav l{n, SKM_VIS, sel, top};
            if (listNavEvent(l, e)) { sel = l.sel; top = l.top; drawItemMenu(ids, n, sel, top, x0, y0); }
        }
        else if (e == EVT_TAP) {
            if (x < x0 || x > x0 + SKM_W || y < y0 || y > y0 + SKM_H) return -1;   // тап мимо — отмена
            if (!potionUsable(inv[ids[sel]])) continue;                           // бесполезное — игнор
            return ids[sel];                                                      // применить выделенное
        }
    }
}

static void monsterTurn(Monster &mon)
{
    const char *tag;
    int mydef = gp.dex / 2 + playerDefBonus();
    int mydodge = gp.dex + armEff(EF_EVASION);
    int roll = dmgCalc(mon.atk, mydef, 5, mydodge, &tag);
    bool crt = (tag[0] == 'c');
    bool sh = (roll > 0 && shieldTurns > 0);
    int d = roll;
    if (sh) { d = (d + 1) / 2; shieldTurns--; }                // щит: входящий урон ×½
    gp.hp -= d;

    hitBegin(hitM, mon.name);
    if (d == 0) {
        hitLine(hitM, "missed (your dodge %d%%)", mydodge);
        strcpy(logM, "FOE: miss"); if (!soundPlayFile("/sfx/miss.wav")) soundBeep(220, 40);
    } else {
        int base = mon.atk - mydef / 2; if (base < 1) base = 1;
        int rollPre = crt ? roll / 2 : roll;
        hitLine(hitM, "-- FOE ATTACK --");
        hitLine(hitM, "foe ATK       %d", mon.atk);
        hitLine(hitM, "your DEF      %d", mydef);
        hitLine(hitM, " dex/2 %d + arm %d", gp.dex / 2, playerDefBonus());
        hitLine(hitM, "minus DEF/2  -%d", mydef / 2);
        hitLine(hitM, "base hit      %d", base);
        hitLine(hitM, "with rng     ~%d", rollPre);
        if (crt) hitLine(hitM, "CRIT x2       %d", roll);
        if (sh)  hitLine(hitM, "your shield /2 %d", d);
        hitLine(hitM, "= %d damage", d);
        snprintf(logM, sizeof(logM), "FOE: -%d%s", d, crt ? " CRIT" : "");
        if (!soundPlayFile("/sfx/hurt.wav")) soundBeep(260, 60);
    }
    hasHitM = true;
}

CombatResult combatRun(Monster &mon)
{
    logP[0] = 0; logM[0] = 0;
    shieldTurns = 0; monPoisonTurns = 0; monPoisonDmg = 0; monPoisonLast = 0; monDotType = DMG_POISON;
    regenTurns = 0; regenAmt = 0;
    hasHitP = hasHitM = false;
    drawCombat(mon);

    // Дождаться отпускания пальца (которым вошли в бой), иначе его release
    // сразу нажмёт кнопку (стрелка вниз совпадает по месту с кнопками).
    modalBegin();

    for (;;) {
        int16_t x, y;
        InputEvent e = inputPoll(x, y);
        if (e != EVT_NONE) powerNoteActivity();     // бой модальный — сами держим таймер сна
        if (e == EVT_BACK) return CR_FLEE;
        if (e != EVT_TAP) { delay(20); continue; }
        // Тап по иконке монстра/игрока — окно с характеристиками (ход не тратится).
        if (x <= 56 && y >= 14 && y <= 60)   { monsterInfoDialog(mon); drawCombat(mon); continue; }
        if (x <= 56 && y >= 108 && y <= 154) { playerInfoDialog();     drawCombat(mon); continue; }
        // Тап по строке лога — одно окно раскладки (переключение сторон стрелками/свайпом).
        if ((hasHitP || hasHitM) && y >= 72 && y < 112) {
            showHitDialog(y < 92 ? 0 : 1); drawCombat(mon); continue;
        }
        if (y < 202) { delay(20); continue; }          // не по кнопкам

        int act = x / 60; if (act > 3) act = 3;
        bool tookTurn = true;
        if (act == 0) { playerAttack(mon); }
        else if (act == 1) {                            // Skill — меню навыков
            int sk = skillMenu();
            if (sk < 0 || !castSkill(sk, mon)) tookTurn = false;
        }
        else if (act == 2) {                            // Item — меню зелий (HP/MP)
            int ii = itemMenu();
            if (ii < 0) tookTurn = false;               // нет зелий / отмена — ход не тратится
            else {
                int h0 = gp.hp, m0 = gp.mp;
                if (invUse(ii)) {
                    int dh = gp.hp - h0, dm = gp.mp - m0;
                    if (dh) snprintf(logP, sizeof(logP), "YOU: +%d hp", dh);
                    else    snprintf(logP, sizeof(logP), "YOU: +%d mp", dm);
                    soundBeep(820, 50);
                } else { strcpy(logP, "no effect"); tookTurn = false; }
            }
        }
        else {                                          // Run (побег)
            if ((int)random(100) < fleeChance(mon)) return CR_FLEE;
            strcpy(logP, "run failed");
        }

        if (tookTurn && monPoisonTurns > 0) {           // тик DoT на монстре (с учётом типа/слаб./сопр.)
            monPoisonLast = typeMod(monPoisonDmg, monDotType, mon);
            mon.hp -= monPoisonLast; monPoisonTurns--;
        }
        if (tookTurn && regenTurns > 0) {               // тик регена игрока
            gp.hp += regenAmt; if (gp.hp > gp.hpMax) gp.hp = gp.hpMax; regenTurns--;
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
