#include "prog_system.h"
#include "../../hw.h"
#include "../../statusbar.h"
#include "../../theme.h"
#include "../../batterytime.h"
#include <esp_system.h>
#include <math.h>

// ─────────────────────────────── Вкладки ───────────────────────────────
// SENSOR — диагностика ёмкостного тача (бывшая отдельная программа Touch).
// ACCEL  — тест акселерометра BMA423 (уровень + ориентация + сырые X/Y/Z).
enum { TAB_CHIP = 0, TAB_MEM, TAB_RUNTIME, TAB_BATT, TAB_SENSOR, TAB_ACCEL, NUM_TABS };
static const char *const TAB_NAME[NUM_TABS] = { "CHIP", "MEMORY", "RUNTIME", "BATTERY", "SENSOR", "ACCEL" };

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

// Геометрия вкладки SENSOR (тест тача).
static const int SENSOR_COORD_Y    = BODY_TOP + 12;      // строка координат (70)
static const int SENSOR_CANVAS_TOP = BODY_TOP + 26;      // верх холста под точки (84)

// Геометрия вкладки ACCEL (пузырьковый уровень).
static const int ACC_DIR_Y = BODY_TOP + 12;              // строка ориентации (70)
static const int LVL_CX    = SCR_W / 2;                  // центр уровня X (120)
static const int LVL_CY    = BODY_TOP + 77;              // центр уровня Y (135)
static const int LVL_R     = 50;                         // радиус уровня
static const int BALL_R    = 6;                          // радиус «пузырька»
static const int ACC_NUM_Y = CONTENT_BOTTOM - 8;         // строка X/Y/Z (196)

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

static void fmtDuration(uint32_t s, char *out, size_t n)
{
    int d = s / 86400UL; s %= 86400UL;
    int h = s / 3600UL;  s %= 3600UL;
    int m = s / 60UL;
    int sec = s % 60UL;
    if (d) snprintf(out, n, "%dd %02d:%02d:%02d", d, h, m, sec);
    else   snprintf(out, n, "%02d:%02d:%02d", h, m, sec);
}

static void fmtUptime(char *out, size_t n)
{
    fmtDuration(millis() / 1000UL, out, n);
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

    // Время от батареи (с момента отключения USB). На зарядке — «charging».
    if (battTimeCharging()) {
        drawRow(6, "On batt", "charging");
    } else {
        fmtDuration(battTimeSinceChargeSec(), buf, sizeof(buf));
        drawRow(6, "On batt", buf);
    }
}

// ─────────────────────────────── Тест тача ─────────────────────────────
// Холст под точки + строка-подсказка. Рисуем в центральной зоне (края
// отданы навигации по вкладкам — см. drawChrome/sysEvent).
static void bodySensor()
{
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_GREEN, COL_BG);
    tft->drawString("x:--- y:---", SCR_W / 2, SENSOR_COORD_Y, 2);
    tft->setTextColor(COL_GREEN_DIM, COL_BG);
    tft->drawString("touch to test", SCR_W / 2, SENSOR_CANVAS_TOP + 8, 2);
}

// Отметить касание: точка на холсте (если в зоне) + актуальные координаты.
static void sensorMark(int16_t x, int16_t y)
{
    if (x >= TAP_EDGE && x < SCR_W - TAP_EDGE && y >= SENSOR_CANVAS_TOP && y < CONTENT_BOTTOM)
        tft->fillCircle(x, y, 3, COL_GREEN);

    char b[24];
    snprintf(b, sizeof(b), "x:%3d  y:%3d", x, y);
    tft->fillRect(TAP_EDGE, SENSOR_COORD_Y - 8, SCR_W - 2 * TAP_EDGE, 16, COL_BG);
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_GREEN, COL_BG);
    tft->drawString(b, SCR_W / 2, SENSOR_COORD_Y, 2);
}

// Непрерывный опрос (рисует «след» при проведении пальцем).
static void sensorPoll()
{
    int16_t x, y;
    if (watch->getTouch(x, y)) sensorMark(x, y);
}

// ─────────────────────────────── Тест акселерометра ────────────────────
// Работа с датчиком — через hw-API (hwAccel*); поправка осей под экран и
// конфигурация BMA423 живут в слое hw, здесь — только визуализация.
static int ballX = LVL_CX, ballY = LVL_CY;   // прежняя позиция пузырька

// Калибровка нуля: полный 3D-вектор гравитации в осях экрана, принятый за «низ».
// Текущие показания поворачиваются в эту систему отсчёта (Родригес) → уровень
// работает относительно ЛЮБОГО наклона часов, а не только горизонтального.
static float g0x = 0, g0y = 0, g0z = -1;   // по умолчанию — плоско (экран вверх)

// Сырые оси датчика → единичный вектор гравитации в осях ЭКРАНА (право, низ, из экрана).
static void accelScreenVec(int16_t x, int16_t y, int16_t z, float &gx, float &gy, float &gz)
{
    float mag = sqrtf((float)((int32_t)x * x + (int32_t)y * y + (int32_t)z * z));
    if (mag < 1.0f) mag = 1.0f;
    gx =  y / mag;   // вправо
    gy = -x / mag;   // вниз
    gz =  z / mag;   // нормаль экрана
}

static float fSx, fSy;   // объявлены ниже; сбрасываются при калибровке

static void accelCalibrate()
{
    int16_t x, y, z;
    for (int i = 0; i < 8; i++) {                       // дождаться валидного семпла
        if (hwAccelRead(x, y, z)) {
            accelScreenVec(x, y, z, g0x, g0y, g0z);
            fSx = fSy = 0;                              // сброс фильтра → пузырёк в центр
            return;
        }
        delay(5);
    }
}

// Наклон относительно калиброванного центра: поворачиваем текущую гравитацию так,
// чтобы калиброванный «низ» g0 совпал с эталоном (0,0,-1), и берём X/Y результата.
// Затем экспоненциальное сглаживание — убрать шум датчика («рандомное» дёрганье».
static void accelRelative(int16_t x, int16_t y, int16_t z, float &sx, float &sy)
{
    float px, py, pz;
    accelScreenVec(x, y, z, px, py, pz);

    float rx, ry;
    float c  = -g0z;                       // cos угла между g0 и (0,0,-1)
    float kx = -g0y, ky = g0x;             // ось поворота k = g0 × (0,0,-1), kz = 0
    float s  = sqrtf(kx * kx + ky * ky);   // = sin угла
    if (s < 1e-4f) {                       // g0 почти вдоль нормали экрана — без поворота
        rx = px; ry = (c >= 0) ? py : -py;
    } else {
        kx /= s; ky /= s;                  // нормировать ось
        float kp  = kx * px + ky * py;     // k·p (kz = 0)
        float omc = 1.0f - c;
        rx = px * c + (ky * pz) * s + kx * kp * omc;
        ry = py * c - (kx * pz) * s + ky * kp * omc;
    }

    fSx += (rx - fSx) * 0.3f;              // низкочастотный фильтр
    fSy += (ry - fSy) * 0.3f;
    sx = fSx; sy = fSy;
}

// Неподвижная разметка уровня: внешнее кольцо, перекрестие, центральная мишень.
static void drawLevelFrame()
{
    tft->drawFastHLine(LVL_CX - LVL_R, LVL_CY, 2 * LVL_R, COL_GREEN_DIM);
    tft->drawFastVLine(LVL_CX, LVL_CY - LVL_R, 2 * LVL_R, COL_GREEN_DIM);
    tft->drawCircle(LVL_CX, LVL_CY, LVL_R, COL_GREEN_DIM);
    tft->drawCircle(LVL_CX, LVL_CY, BALL_R + 2, COL_GREEN_DIM);
}

// Кадр теста: читаем ускорение, двигаем пузырёк к опущенному краю, обновляем текст.
static void accelPoll()
{
    int16_t x, y, z;
    if (!hwAccelRead(x, y, z)) return;

    float sx, sy;
    accelRelative(x, y, z, sx, sy);     // наклон относительно калиброванного центра (3D)
    if (sx >  1) sx =  1; else if (sx < -1) sx = -1;
    if (sy >  1) sy =  1; else if (sy < -1) sy = -1;
    int reach = LVL_R - BALL_R;
    int bx = LVL_CX + (int)(sx * reach);
    int by = LVL_CY + (int)(sy * reach);

    // Стереть прежний пузырёк, восстановить разметку, нарисовать новый.
    tft->fillCircle(ballX, ballY, BALL_R + 1, COL_BG);
    drawLevelFrame();
    tft->fillCircle(bx, by, BALL_R, COL_AMBER);
    ballX = bx; ballY = by;

    // Ориентация (по центру сверху).
    tft->fillRect(0, ACC_DIR_Y - 8, SCR_W, 16, COL_BG);
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_AMBER, COL_BG);
    tft->drawString(hwAccelDirection(x, y, z), SCR_W / 2, ACC_DIR_Y, 2);

    // Сырые оси (мелким шрифтом снизу).
    char buf[32];
    snprintf(buf, sizeof(buf), "X%+5d Y%+5d Z%+5d", x, y, z);
    tft->fillRect(0, ACC_NUM_Y - 5, SCR_W, 11, COL_BG);
    tft->setTextColor(COL_GREEN, COL_BG);
    tft->drawString(buf, SCR_W / 2, ACC_NUM_Y, 1);
}

static void bodyAccel()
{
    hwAccelBegin();
    accelCalibrate();                 // текущее положение часов → 0-я отметка
    ballX = LVL_CX; ballY = LVL_CY;   // сброс позиции под перерисовку
    drawLevelFrame();
    accelPoll();                      // сразу показать актуальные значения
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
    case TAB_SENSOR:  bodySensor();  break;
    case TAB_ACCEL:   bodyAccel();   break;
    }
    lastBody = millis();
}

// Нижняя панель. System сама владеет навбаром (navCount=-1), т.к. на вкладке
// SENSOR нужна кнопка Clear, а на остальных — только Back.
static void drawNavbar()
{
    const int top = SCR_H - NAVBAR_H;
    tft->fillRect(0, top, SCR_W, NAVBAR_H, COL_BG);
    tft->drawFastHLine(0, top, SCR_W, COL_FRAME);
    tft->setTextDatum(MC_DATUM);

    if (curTab == TAB_SENSOR) {
        const int w = SCR_W / 2;
        tft->setTextColor(COL_AMBER, COL_BG);
        tft->drawString("Back", w / 2, top + NAVBAR_H / 2, 2);
        tft->drawFastVLine(w, top + 5, NAVBAR_H - 10, COL_FRAME);
        tft->setTextColor(COL_GREEN, COL_BG);
        tft->drawString("Clear", w + w / 2, top + NAVBAR_H / 2, 2);
    } else {
        tft->setTextColor(COL_AMBER, COL_BG);
        tft->drawString("Back", SCR_W / 2, top + NAVBAR_H / 2, 2);
    }
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
    drawNavbar();   // навбар меняется на/с вкладки SENSOR (Back ↔ Back/Clear)
}

// ─────────────────────────────── Программа ─────────────────────────────
static void sysEnter()
{
    drawChrome();
    drawBody();
    drawNavbar();   // navCount=-1: фреймворк навбар не рисует, делаем сами
}

static void sysTick()
{
    if (curTab == TAB_SENSOR) { sensorPoll(); return; }  // на тесте тача не затираем точки
    if (curTab == TAB_ACCEL)  { accelPoll();  return; }  // живой опрос акселерометра
    if (millis() - lastBody >= 800) drawBody();          // обновляем динамику ~раз в 0.8 c
}

static void sysEvent(InputEvent e, int16_t x, int16_t y)
{
    switch (e) {
    // Свайпами вкладки НЕ переключаем — иначе рисование на SENSOR перелистывает.
    // Переключение только тапом по боковым стрелкам.
    case EVT_TAP:
        if (y >= SCR_H - NAVBAR_H) {               // тап по навбару (рисуем его сами)
            if (curTab == TAB_SENSOR && x >= SCR_W / 2) drawBody();  // Clear
            else                                        kernelBack();
        } else if (x < TAP_EDGE) {
            switchTab(-1);                          // тап по левой стрелке
        } else if (x > SCR_W - TAP_EDGE) {
            switchTab(+1);                          // тап по правой стрелке
        } else if (curTab == TAB_SENSOR) {
            sensorMark(x, y);                       // тап в центре — отметка теста
        } else if (curTab == TAB_ACCEL) {
            accelCalibrate();                       // тап — текущее положение в 0-ю отметку
            ballX = LVL_CX; ballY = LVL_CY;
            drawLevelFrame();
            accelPoll();
        }
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
    "System", sysEnter, sysTick, sysEvent, sysIcon, nullptr, -1, nullptr, nullptr
};
