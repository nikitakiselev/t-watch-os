#include "prog_wifi.h"
#include "../../hw.h"
#include "../../statusbar.h"
#include "../../theme.h"
#include "../../wifi.h"
#include "../../keyboard.h"
#include "../../listnav.h"
#include "../../modal.h"
#include "../../speedtest.h"

static const int TITLE_Y  = STATUSBAR_H + 12;
static const int STATUS_Y = STATUSBAR_H + 30;
static const int LIST_TOP = STATUSBAR_H + 44;   // ~68
static const int ROW_H    = 26;
static const int VISIBLE  = 5;

static WifiNet nets[24];
static int     netCount  = 0;
static int     selected  = 0;
static int     scrollTop = 0;
static bool    acquired  = false;

static void drawStatusLine()
{
    tft->fillRect(0, STATUS_Y - 8, SCR_W, 16, COL_BG);
    tft->setTextDatum(MC_DATUM);
    char buf[40];
    if (wifiConnected()) {
        snprintf(buf, sizeof(buf), "OK: %s", wifiCurrentSsid());
        tft->setTextColor(COL_GREEN, COL_BG);
    } else {
        snprintf(buf, sizeof(buf), "networks: %d", netCount);
        tft->setTextColor(COL_GREEN_DIM, COL_BG);
    }
    tft->drawString(buf, SCR_W / 2, STATUS_Y, 2);
}

static void drawRow(int idx, int y)
{
    bool sel = (idx == selected);
    if (sel) {
        tft->fillRoundRect(6, y, SCR_W - 12, ROW_H - 4, 4, COL_GREEN_DIM);
    }
    uint16_t fg = sel ? COL_GREEN_HI : COL_GREEN;

    // Шкала сигнала (4 столбика) — общий хелпер темы.
    drawSignalBars(*tft, 14, y + (ROW_H - 4) - 2, nets[idx].rssi, fg, COL_GREEN_DIM);

    // SSID + метки: ! защищена, * сохранена/подключена.
    char line[40];
    bool conn = wifiConnected() && strncmp(nets[idx].ssid, wifiCurrentSsid(), WIFI_SSID_LEN) == 0;
    snprintf(line, sizeof(line), "%s%s%s",
             nets[idx].ssid,
             nets[idx].secured ? " !" : "",
             conn ? " <" : (nets[idx].saved ? " *" : ""));
    tft->setTextDatum(ML_DATUM);
    tft->setTextColor(fg, sel ? COL_GREEN_DIM : COL_BG);
    tft->drawString(line, 36, y + (ROW_H - 4) / 2, 2);
}

static void drawList()
{
    tft->fillRect(0, LIST_TOP, SCR_W, CONTENT_BOTTOM - LIST_TOP, COL_BG);
    if (netCount == 0) {
        tft->setTextDatum(MC_DATUM);
        tft->setTextColor(COL_GREEN_DIM, COL_BG);
        tft->drawString("no networks", SCR_W / 2, LIST_TOP + 40, 4);
        return;
    }
    for (int i = scrollTop; i < scrollTop + VISIBLE && i < netCount; i++) {
        drawRow(i, LIST_TOP + (i - scrollTop) * ROW_H);
    }
}

static void drawAll()
{
    // Чистим только статусбар + контент (до навбара) — нижние кнопки не трогаем,
    // их рисует ядро. Поэтому НЕ используем themeBackdrop() (он fillScreen).
    tft->fillRect(0, 0, SCR_W, CONTENT_BOTTOM, COL_BG);
    statusbarDraw();
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_AMBER, COL_BG);
    tft->drawString("[ WI-FI ]", SCR_W / 2, TITLE_Y, 2);
    drawStatusLine();
    drawList();
}

// Центрированное сообщение поверх списка (scanning/connecting/инфо).
static void overlay(const char *msg, uint16_t col)
{
    tft->fillRect(0, LIST_TOP, SCR_W, CONTENT_BOTTOM - LIST_TOP, COL_BG);
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(col, COL_BG);
    tft->drawString(msg, SCR_W / 2, (LIST_TOP + CONTENT_BOTTOM) / 2, 4);
}

static void doScan()
{
    overlay("scanning...", COL_AMBER);
    netCount  = wifiScan(nets, 24);
    selected  = 0;
    scrollTop = 0;
}

static void connectTo(const char *ssid, const char *pass)
{
    overlay("connecting...", COL_AMBER);
    bool ok = wifiConnect(ssid, pass, 10000);
    if (!ok) { overlay("failed", COL_AMBER); delay(900); }
    statusbarDraw();           // обновить иконку Wi-Fi
    drawStatusLine();
    drawList();
}

// Диалог по тапу на подключённую сеть: SpeedTest / Disconnect / Back.
// Окно крупное, по центру контента (24..204): запас сверху над заголовком и снизу
// под последней кнопкой, кнопки с воздухом между собой.
static const int CD_W = 190, CD_H = 168;
static const int CD_X0 = (SCR_W - CD_W) / 2, CD_Y0 = 30;
static const int CD_BH = 30, CD_GAP = 12, CD_BY0 = CD_Y0 + 44;

static void drawConnDialog(const char *ssid, int sel)
{
    modalPanel(CD_X0, CD_Y0, CD_W, CD_H, 10, COL_GREEN);
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_AMBER, COL_BG);
    tft->drawString(ssid, SCR_W / 2, CD_Y0 + 22, 2);

    const char *items[3] = { "SpeedTest", "Disconnect", "Back" };
    const int bx = CD_X0 + 14, bw = CD_W - 28;
    for (int i = 0; i < 3; i++) {
        int by = CD_BY0 + i * (CD_BH + CD_GAP);
        if (i == sel) tft->fillRoundRect(bx, by, bw, CD_BH, 6, COL_GREEN_DIM);
        tft->drawRoundRect(bx, by, bw, CD_BH, 6, i == sel ? COL_AMBER : COL_FRAME);
        tft->setTextColor(COL_GREEN, i == sel ? COL_GREEN_DIM : COL_BG);
        tft->drawString(items[i], SCR_W / 2, by + CD_BH / 2, 2);
    }
}

static void connectedDialog(const char *ssid)
{
    // Системное правило: свайп ↑↓ выбирает пункт, тап/клик активирует ВЫДЕЛЕННЫЙ
    // (не тот, что под пальцем). Тап мимо панели — закрыть.
    ListNav l{3, 0, 0, 0};
    modalBegin();
    drawConnDialog(ssid, l.sel);
    for (;;) {
        int16_t x, y; InputEvent e = modalPoll(x, y);
        if (e == EVT_BACK) { kernelRedraw(); return; }
        if (e == EVT_UP || e == EVT_DOWN) {
            if (listNavEvent(l, e)) drawConnDialog(ssid, l.sel);
            continue;
        }
        if (e == EVT_TAP) {
            if (x < CD_X0 || x > CD_X0 + CD_W || y < CD_Y0 || y > CD_Y0 + CD_H) {
                kernelRedraw(); return;            // тап мимо панели — закрыть
            }
            // тап внутри панели — падаем к активации выделенного (ниже)
        } else if (e != EVT_CLICK) {
            continue;                              // прочие события игнорим
        }
        // EVT_TAP внутри панели или EVT_CLICK — активировать ВЫДЕЛЕННЫЙ пункт
        if (l.sel == 0) { speedtestRun(); kernelRedraw(); return; }   // SpeedTest
        if (l.sel == 1) { wifiDisconnect(); kernelRedraw(); return; } // Disconnect
        kernelRedraw(); return;                                       // Back
    }
}

static void actSelected()
{
    if (netCount == 0) return;
    WifiNet &n = nets[selected];

    bool conn = wifiConnected() && strncmp(n.ssid, wifiCurrentSsid(), WIFI_SSID_LEN) == 0;
    if (conn) { connectedDialog(n.ssid); return; }

    if (!n.secured) { connectTo(n.ssid, ""); return; }

    if (n.saved) {
        char pass[WIFI_PASS_LEN];
        wifiSavedPassword(n.ssid, pass, sizeof(pass));
        connectTo(n.ssid, pass);
        return;
    }

    // Защищённая и не сохранённая — вводим пароль на T9-клавиатуре.
    char pass[WIFI_PASS_LEN];
    bool ok = keyboardPrompt(n.ssid, pass, sizeof(pass));
    kernelRedraw();                       // клавиатура затёрла экран — восстановить (контент + навбар)
    if (!ok) return;                      // отмена

    overlay("connecting...", COL_AMBER);
    if (wifiConnect(n.ssid, pass, 10000)) {
        wifiSave(n.ssid, pass);           // успех — запоминаем сеть
        n.saved = true;
    } else {
        overlay("failed", COL_AMBER);
        delay(900);
    }
    statusbarDraw();
    drawStatusLine();
    drawList();
}

// ─────────────────── Программа ───────────────────
static void wifiEnter()
{
    if (!acquired) {            // первый вход: включаем Wi-Fi и сканируем
        wifiAcquire();
        acquired = true;
        tft->fillScreen(COL_BG);   // разово чистим весь экран (навбар ядро нарисует после onEnter)
        drawAll();
        doScan();
        drawAll();
    } else {                    // возврат/пробуждение: просто перерисовать
        drawAll();
    }
}

static void wifiExit()
{
    if (acquired) { wifiRelease(); acquired = false; }
}

static void wifiEvent(InputEvent e, int16_t x, int16_t y)
{
    if (netCount == 0 && e != EVT_NONE) return;
    switch (e) {
    case EVT_UP:
    case EVT_DOWN: {
        ListNav l{netCount, VISIBLE, selected, scrollTop};
        if (listNavEvent(l, e)) { selected = l.sel; scrollTop = l.top; drawList(); }
        break;
    }
    case EVT_TAP:
        actSelected();                 // тап — применить ВЫДЕЛЕННЫЙ пункт (выбор — свайпами)
        break;
    default: break;
    }
}

static void navRescan() { doScan(); drawAll(); }
static void navForget()
{
    if (netCount == 0) return;
    if (nets[selected].saved) {
        wifiForget(nets[selected].ssid);
        nets[selected].saved = false;
        drawList();
    }
}

static void wifiIcon(TFT_eSPI &g, int cx, int cy, int r)
{
    drawWifiGlyph(g, cx, cy + r / 2, r, COL_GREEN);
}

static const NavButton wifiNav[] = {
    { "Back",   kernelBack },
    { "Scan",   navRescan },
    { "Forget", navForget },
};

const Program wifiProgram = {
    "Wi-Fi", wifiEnter, nullptr, wifiEvent, wifiIcon, wifiNav, 3, nullptr, wifiExit
};
