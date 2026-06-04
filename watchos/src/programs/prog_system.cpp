#include "prog_system.h"
#include "../../hw.h"
#include "../../statusbar.h"
#include "../../theme.h"
#include <esp_system.h>

// ─────────────────────────────── Вкладки ───────────────────────────────
enum { TAB_CHIP = 0, TAB_MEM, TAB_RUNTIME, TAB_BATT, NUM_TABS };
static const char *const TAB_NAME[NUM_TABS] = { "CHIP", "MEMORY", "RUNTIME", "BATTERY" };

static int          curTab    = 0;
static unsigned long lastBody = 0;   // троттлинг перерисовки динамики

// ─────────────────────────────── Геометрия ─────────────────────────────
static const int TITLE_Y  = STATUSBAR_H + 12;            // 36
static const int DOTS_Y   = STATUSBAR_H + 26;            // 50
static const int BODY_TOP = STATUSBAR_H + 34;            // 58
static const int ROW_H    = 20;
static const int ROW_Y0   = BODY_TOP + 12;               // центр первой строки (70)
static const int LBL_X    = 22;                          // метка (ML)
static const int VAL_X    = SCR_W - 22;                  // значение (MR)
static const int ARROW_CY = (BODY_TOP + CONTENT_BOTTOM) / 2;
static const int TAP_EDGE = 36;                          // ширина зоны тапа по стрелке

// ─────────────────────────── Хелперы форматирования ────────────────────
static void fmtBytes(uint32_t b, char *out, size_t n)
{
    if (b >= 1024UL * 1024UL) snprintf(out, n, "%.1fM", b / 1048576.0);
    else if (b >= 1024UL)     snprintf(out, n, "%.0fK", b / 1024.0);
    else                      snprintf(out, n, "%uB", (unsigned)b);
}

static const char *resetReason()
{
    switch (esp_reset_reason()) {
    case ESP_RST_POWERON:   return "Power-on";
    case ESP_RST_SW:        return "Software";
    case ESP_RST_PANIC:     return "Panic";
    case ESP_RST_INT_WDT:   return "Int WDT";
    case ESP_RST_TASK_WDT:  return "Task WDT";
    case ESP_RST_WDT:       return "WDT";
    case ESP_RST_DEEPSLEEP: return "Deepsleep";
    case ESP_RST_BROWNOUT:  return "Brownout";
    case ESP_RST_SDIO:      return "SDIO";
    case ESP_RST_EXT:       return "External";
    default:                return "Unknown";
    }
}

static void fmtUptime(char *out, size_t n)
{
    unsigned long s = millis() / 1000UL;
    int d = s / 86400UL; s %= 86400UL;
    int h = s / 3600UL;  s %= 3600UL;
    int m = s / 60UL;
    int sec = s % 60UL;
    if (d) snprintf(out, n, "%dd %02d:%02d:%02d", d, h, m, sec);
    else   snprintf(out, n, "%02d:%02d:%02d", h, m, sec);
}

// ─────────────────────────────── Отрисовка строки ──────────────────────
static void drawRow(int idx, const char *label, const char *val)
{
    int cy = ROW_Y0 + idx * ROW_H;
    tft->setTextDatum(ML_DATUM);
    tft->setTextColor(COL_GREEN_DIM, COL_BG);
    tft->drawString(label, LBL_X, cy, 2);
    tft->setTextDatum(MR_DATUM);
    tft->setTextColor(COL_GREEN, COL_BG);
    tft->drawString(val, VAL_X, cy, 2);
}

// ─────────────────────────────── Тела вкладок ──────────────────────────
static void bodyChip()
{
    char buf[24];
    drawRow(0, "Model",  ESP.getChipModel());
    snprintf(buf, sizeof(buf), "%d", ESP.getChipRevision());
    drawRow(1, "Rev",    buf);
    snprintf(buf, sizeof(buf), "%d", ESP.getChipCores());
    drawRow(2, "Cores",  buf);
    snprintf(buf, sizeof(buf), "%u MHz", (unsigned)ESP.getCpuFreqMHz());
    drawRow(3, "CPU",    buf);
    drawRow(4, "SDK",    ESP.getSdkVersion());
    uint64_t mac = ESP.getEfuseMac();
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             (uint8_t)(mac), (uint8_t)(mac >> 8), (uint8_t)(mac >> 16),
             (uint8_t)(mac >> 24), (uint8_t)(mac >> 32), (uint8_t)(mac >> 40));
    drawRow(5, "MAC",    buf);
}

static void bodyMem()
{
    char buf[24], a[12], b[12];
    fmtBytes(ESP.getFreeHeap(), a, sizeof(a));
    fmtBytes(ESP.getHeapSize(), b, sizeof(b));
    snprintf(buf, sizeof(buf), "%s/%s", a, b);
    drawRow(0, "Heap",      buf);
    fmtBytes(ESP.getMinFreeHeap(), a, sizeof(a));
    drawRow(1, "Heap min",  a);
    fmtBytes(ESP.getMaxAllocHeap(), a, sizeof(a));
    drawRow(2, "Max block", a);
    fmtBytes(ESP.getFreePsram(), a, sizeof(a));
    fmtBytes(ESP.getPsramSize(), b, sizeof(b));
    snprintf(buf, sizeof(buf), "%s/%s", a, b);
    drawRow(3, "PSRAM",     buf);
    fmtBytes(ESP.getFlashChipSize(), a, sizeof(a));
    snprintf(buf, sizeof(buf), "%s @ %uM", a, (unsigned)(ESP.getFlashChipSpeed() / 1000000UL));
    drawRow(4, "Flash",     buf);
    fmtBytes(ESP.getSketchSize(), a, sizeof(a));
    fmtBytes(ESP.getFreeSketchSpace(), b, sizeof(b));
    snprintf(buf, sizeof(buf), "%s free %s", a, b);
    drawRow(5, "Sketch",    buf);
}

static void bodyRuntime()
{
    char buf[24];
    fmtUptime(buf, sizeof(buf));
    drawRow(0, "Uptime", buf);
    drawRow(1, "Reset",  resetReason());
    snprintf(buf, sizeof(buf), "%.1f C", watch->power->getTemp());
    drawRow(2, "PMU temp", buf);
    fmtBytes(ESP.getFreeHeap(), buf, sizeof(buf));
    drawRow(3, "Free heap", buf);
    RTC_Date d = hwNow();
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d", d.year, d.month, d.day);
    drawRow(4, "Date",   buf);
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", d.hour, d.minute, d.second);
    drawRow(5, "Time MSK", buf);
}

static void bodyBatt()
{
    AXP20X_Class *p = watch->power;
    bool batt = p->isBatteryConnect();
    bool chg  = p->isChargeing();
    bool usb  = p->isVBUSPlug();
    char buf[24];

    const char *state = chg ? "Charging" : (usb ? "USB" : (batt ? "Discharging" : "No battery"));
    drawRow(0, "State", state);

    snprintf(buf, sizeof(buf), "%d%%", hwBattPercent());
    drawRow(1, "Level", buf);

    snprintf(buf, sizeof(buf), "%.3f V", p->getBattVoltage() / 1000.0);
    drawRow(2, "Voltage", buf);

    if (chg) snprintf(buf, sizeof(buf), "+%.0f mA", p->getBattChargeCurrent());
    else     snprintf(buf, sizeof(buf), "-%.0f mA", p->getBattDischargeCurrent());
    drawRow(3, "Current", buf);

    snprintf(buf, sizeof(buf), "%.0f mW", p->getBattInpower());
    drawRow(4, "Power", buf);

    if (usb) snprintf(buf, sizeof(buf), "%.2fV %.0fmA", p->getVbusVoltage() / 1000.0, p->getVbusCurrent());
    else     snprintf(buf, sizeof(buf), "unplugged");
    drawRow(5, "USB", buf);

    snprintf(buf, sizeof(buf), "%.1f C", p->getTemp());
    drawRow(6, "PMU temp", buf);
}

// Перерисовать только область строк (динамика без мерцания заголовка/стрелок).
static void drawBody()
{
    tft->fillRect(TAP_EDGE, BODY_TOP, SCR_W - 2 * TAP_EDGE, CONTENT_BOTTOM - BODY_TOP, COL_BG);
    switch (curTab) {
    case TAB_CHIP:    bodyChip();    break;
    case TAB_MEM:     bodyMem();     break;
    case TAB_RUNTIME: bodyRuntime(); break;
    case TAB_BATT:    bodyBatt();    break;
    }
    lastBody = millis();
}

// Заголовок, точки-индикатор вкладок и боковые стрелки.
static void drawChrome()
{
    tft->fillRect(0, STATUSBAR_H, SCR_W, CONTENT_BOTTOM - STATUSBAR_H, COL_BG);
    statusbarDraw();

    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_AMBER, COL_BG);
    tft->drawString(TAB_NAME[curTab], SCR_W / 2, TITLE_Y, 2);

    // Точки-индикатор: активная — янтарная, остальные тусклые.
    int spacing = 12;
    int x0 = SCR_W / 2 - (NUM_TABS - 1) * spacing / 2;
    for (int i = 0; i < NUM_TABS; i++) {
        if (i == curTab) tft->fillCircle(x0 + i * spacing, DOTS_Y, 3, COL_AMBER);
        else             tft->fillCircle(x0 + i * spacing, DOTS_Y, 2, COL_GREEN_DIM);
    }

    // Боковые стрелки (тусклые на крайних вкладках, где идти некуда).
    uint16_t lc = (curTab > 0)            ? COL_GREEN : COL_GREEN_DIM;
    uint16_t rc = (curTab < NUM_TABS - 1) ? COL_GREEN : COL_GREEN_DIM;
    tft->fillTriangle(6, ARROW_CY, 16, ARROW_CY - 9, 16, ARROW_CY + 9, lc);
    tft->fillTriangle(SCR_W - 6, ARROW_CY, SCR_W - 16, ARROW_CY - 9, SCR_W - 16, ARROW_CY + 9, rc);
}

static void switchTab(int dir)
{
    int next = curTab + dir;
    if (next < 0 || next >= NUM_TABS) return;
    curTab = next;
    drawChrome();
    drawBody();
}

// ─────────────────────────────── Программа ─────────────────────────────
static void sysEnter()
{
    drawChrome();
    drawBody();
}

static void sysTick()
{
    if (millis() - lastBody >= 800) drawBody();   // обновляем динамику ~раз в 0.8 c
}

static void sysEvent(InputEvent e, int16_t x, int16_t y)
{
    switch (e) {
    case EVT_LEFT:  switchTab(+1); break;          // свайп влево → следующая
    case EVT_RIGHT: switchTab(-1); break;          // свайп вправо → предыдущая
    case EVT_TAP:
        if (x < TAP_EDGE)            switchTab(-1); // тап по левой стрелке
        else if (x > SCR_W - TAP_EDGE) switchTab(+1);
        break;
    default: break;
    }
}

// ASCII-иконка — микросхема с ножками.
static const char *const SYS_ART[] = {
    "_||_",
    "|##|",
    "|##|",
    " ||",
};
static void sysIcon(TFT_eSPI &g, int cx, int cy, int r)
{
    drawAsciiArt(g, SYS_ART, 4, cx, cy, COL_GREEN, r / 8);
}

const Program systemProgram = {
    "System", sysEnter, sysTick, sysEvent, sysIcon, nullptr, 0, nullptr, nullptr
};
