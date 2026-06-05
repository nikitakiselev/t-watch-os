# SpeedTest внутри Wi-Fi приложения — план реализации

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Встроить замер скорости интернета (download/upload/ping) в приложение Wi-Fi: тап по подключённой сети открывает диалог SpeedTest / Disconnect / Back, SpeedTest запускает полноэкранный тест с живой цифрой и итогами.

**Architecture:** Три эндпоинта в FastAPI-сервере (`server/app.py`) под тем же `X-API-Key`, что и `/talk`. В прошивке — самодостаточный блокирующий модуль `watchos/speedtest.{h,cpp}` (сырой `WiFiClient`, сеть + UI). `prog_wifi.cpp` добавляет модальный диалог, вызывающий `speedtestRun()`.

**Tech Stack:** Сервер — FastAPI + uvicorn, pytest (TestClient). Прошивка — Arduino C++ (ESP32 core 2.0.17), TFT_eSPI, WiFiClient. Сборка прошивки — `make build`; тесты сервера — pytest.

---

## Структура файлов

| Файл | Действие | Ответственность |
|---|---|---|
| `server/app.py` | Modify | +3 эндпоинта `/speedtest/{down,up,ping}` |
| `server/tests/test_app.py` | Modify | Тесты эндпоинтов + проверка 401 |
| `watchos/config.h` | Modify | Константы `SPEEDTEST_*` |
| `watchos/aiclient.h` | Modify | Объявить публичный `aiApiKey()` |
| `watchos/aiclient.cpp` | Modify | Убрать `static` у `aiApiKey()` |
| `watchos/speedtest.h` | Create | Объявление `void speedtestRun();` |
| `watchos/speedtest.cpp` | Create | Сеть (ping/down/up) + полноэкранный UI |
| `watchos/src/programs/prog_wifi.cpp` | Modify | Диалог подключённой сети → `speedtestRun()` |

Прошивка юнит-тестов не имеет (как и весь проект) — firmware-задачи проверяются `make build` (компиляция) и ручным тестом на железе в финальной задаче. Серверные задачи — TDD на pytest.

---

## Task 1: Сервер — эндпоинт `/speedtest/ping`

**Files:**
- Modify: `server/app.py`
- Test: `server/tests/test_app.py`

- [ ] **Step 1: Написать падающий тест**

Добавить в конец `server/tests/test_app.py`:

```python
def test_speedtest_ping_ok(tmp_path, monkeypatch):
    c = make_client(tmp_path, monkeypatch)
    r = c.get("/speedtest/ping")
    assert r.status_code == 200
    assert r.content == b"ok"


def test_speedtest_ping_requires_api_key_when_set(tmp_path, monkeypatch):
    monkeypatch.setenv("API_KEY", "secret123")
    c = make_client(tmp_path, monkeypatch)
    assert c.get("/speedtest/ping").status_code == 401
    assert c.get("/speedtest/ping", headers={"X-API-Key": "secret123"}).status_code == 200
```

- [ ] **Step 2: Запустить — убедиться, что падает**

Run: `cd /Users/nikitakiselev/code/twatch/server && .venv/bin/python -m pytest tests/test_app.py::test_speedtest_ping_ok -v`
Expected: FAIL — 404 (эндпоинт не существует).

- [ ] **Step 3: Реализовать эндпоинт**

В `server/app.py` после функции `talk` (после строки 65) добавить:

```python
@app.get("/speedtest/ping")
def speedtest_ping(request: Request):
    settings = get_settings()
    if not _api_key_ok(request, settings):
        return JSONResponse({"error": "unauthorized"}, status_code=401)
    return Response(content=b"ok", media_type="text/plain")
```

`Response` и `JSONResponse` уже импортированы (строки 4–5).

- [ ] **Step 4: Запустить — убедиться, что проходит**

Run: `cd /Users/nikitakiselev/code/twatch/server && .venv/bin/python -m pytest tests/test_app.py -k speedtest_ping -v`
Expected: PASS (2 теста).

- [ ] **Step 5: Коммит**

```bash
cd /Users/nikitakiselev/code/twatch
git add server/app.py server/tests/test_app.py
git commit -m "feat(server): эндпоинт /speedtest/ping"
```

---

## Task 2: Сервер — эндпоинт `/speedtest/down`

**Files:**
- Modify: `server/app.py`
- Test: `server/tests/test_app.py`

- [ ] **Step 1: Написать падающий тест**

Добавить в конец `server/tests/test_app.py`:

```python
def test_speedtest_down_streams(tmp_path, monkeypatch):
    c = make_client(tmp_path, monkeypatch)
    # ?mb=1 — поток ограничен 1 MiB, читаем только первый чанк и закрываем соединение
    with c.stream("GET", "/speedtest/down?mb=1") as r:
        assert r.status_code == 200
        got = next(r.iter_bytes())
        assert len(got) > 0


def test_speedtest_down_requires_api_key_when_set(tmp_path, monkeypatch):
    monkeypatch.setenv("API_KEY", "secret123")
    c = make_client(tmp_path, monkeypatch)
    with c.stream("GET", "/speedtest/down?mb=1") as r:
        assert r.status_code == 401
    with c.stream("GET", "/speedtest/down?mb=1", headers={"X-API-Key": "secret123"}) as r:
        assert r.status_code == 200
```

- [ ] **Step 2: Запустить — убедиться, что падает**

Run: `cd /Users/nikitakiselev/code/twatch/server && .venv/bin/python -m pytest tests/test_app.py::test_speedtest_down_streams -v`
Expected: FAIL — 404.

- [ ] **Step 3: Реализовать эндпоинт**

В `server/app.py` сверху расширить импорт ответов (строка 5) — добавить `StreamingResponse`:

```python
from fastapi.responses import JSONResponse, FileResponse, HTMLResponse, StreamingResponse
```

После `speedtest_ping` добавить:

```python
@app.get("/speedtest/down")
async def speedtest_down(request: Request, mb: int = 128):
    settings = get_settings()
    if not _api_key_ok(request, settings):
        return JSONResponse({"error": "unauthorized"}, status_code=401)

    # Поток ЖЁСТКО ограничен сверху (никогда не бесконечный — иначе клиент, который
    # не закрылся, заставил бы сервер генерировать вечно, а TestClient вычитал бы тело
    # целиком → OOM). Дефолт 128 MiB перекрывает 8 c фазы download с запасом; реальный
    # клиент закрывает сокет раньше — is_disconnected/GeneratorExit останавливают цикл.
    CHUNK = b"\0" * 16384
    max_chunks = max(1, mb) * 64        # 64 чанка по 16 KiB = 1 MiB

    async def gen():
        try:
            for _ in range(max_chunks):
                if await request.is_disconnected():
                    return
                yield CHUNK
        except (BrokenPipeError, ConnectionResetError, GeneratorExit):
            return

    return StreamingResponse(gen(), media_type="application/octet-stream")
```

- [ ] **Step 4: Запустить — убедиться, что проходит**

Run: `cd /Users/nikitakiselev/code/twatch/server && .venv/bin/python -m pytest tests/test_app.py -k speedtest_down -v`
Expected: PASS (2 теста).

- [ ] **Step 5: Коммит**

```bash
cd /Users/nikitakiselev/code/twatch
git add server/app.py server/tests/test_app.py
git commit -m "feat(server): эндпоинт /speedtest/down (стрим)"
```

---

## Task 3: Сервер — эндпоинт `/speedtest/up`

**Files:**
- Modify: `server/app.py`
- Test: `server/tests/test_app.py`

- [ ] **Step 1: Написать падающий тест**

Добавить в конец `server/tests/test_app.py`:

```python
def test_speedtest_up_counts_bytes(tmp_path, monkeypatch):
    c = make_client(tmp_path, monkeypatch)
    body = b"x" * 50000
    r = c.post("/speedtest/up", content=body)
    assert r.status_code == 200
    assert r.json() == {"received": len(body)}


def test_speedtest_up_requires_api_key_when_set(tmp_path, monkeypatch):
    monkeypatch.setenv("API_KEY", "secret123")
    c = make_client(tmp_path, monkeypatch)
    assert c.post("/speedtest/up", content=b"abc").status_code == 401
    assert c.post("/speedtest/up", content=b"abc",
                  headers={"X-API-Key": "secret123"}).status_code == 200
```

- [ ] **Step 2: Запустить — убедиться, что падает**

Run: `cd /Users/nikitakiselev/code/twatch/server && .venv/bin/python -m pytest tests/test_app.py::test_speedtest_up_counts_bytes -v`
Expected: FAIL — 404.

- [ ] **Step 3: Реализовать эндпоинт**

В `server/app.py` после `speedtest_down` добавить:

```python
@app.post("/speedtest/up")
async def speedtest_up(request: Request):
    settings = get_settings()
    if not _api_key_ok(request, settings):
        return JSONResponse({"error": "unauthorized"}, status_code=401)
    total = 0
    async for chunk in request.stream():     # прозрачно разбирает chunked
        total += len(chunk)
    return JSONResponse({"received": total})
```

- [ ] **Step 4: Запустить весь файл — убедиться, что всё зелёное**

Run: `cd /Users/nikitakiselev/code/twatch/server && .venv/bin/python -m pytest tests/test_app.py -v`
Expected: PASS — все тесты (включая прежние) зелёные.

- [ ] **Step 5: Коммит**

```bash
cd /Users/nikitakiselev/code/twatch
git add server/app.py server/tests/test_app.py
git commit -m "feat(server): эндпоинт /speedtest/up (приём chunked)"
```

---

## Task 4: Прошивка — константы `config.h` и публичный `aiApiKey()`

**Files:**
- Modify: `watchos/config.h`
- Modify: `watchos/aiclient.h`
- Modify: `watchos/aiclient.cpp:23`

- [ ] **Step 1: Добавить константы в `config.h`**

Найти в `watchos/config.h` блок с `SCR_W`/`STATUSBAR_H` (строки 9–14) и после него (после строки 14) добавить:

```c
// ── SpeedTest (в приложении Wi-Fi) ──
// Хост тот же, что у AI-сервера, но задаётся явно: speedtest читает его без SPIFFS.
// API-ключ берётся из /ai_server.txt (через aiApiKey()), в прошивку не зашит.
#define SPEEDTEST_HOST   "twatch.nikitakiselev.ru"
#define SPEEDTEST_PORT   80
#define SPEEDTEST_SECS   8        // длительность каждой фазы (download/upload), c
#define SPEEDTEST_PINGS  5        // число проб ping, в результат идёт минимум
```

- [ ] **Step 2: Объявить `aiApiKey()` в `aiclient.h`**

В `watchos/aiclient.h` после объявления `String aiServerUrl();` (строка 12) добавить:

```c
// API-ключ из 2-й строки /ai_server.txt ("" если не задан). Публичный — его
// переиспользует speedtest для заголовка X-API-Key.
String   aiApiKey();
```

- [ ] **Step 3: Снять `static` с `aiApiKey()` в `aiclient.cpp`**

В `watchos/aiclient.cpp` строку 23 заменить.

Было:
```c
static String aiApiKey()
```
Стало:
```c
String aiApiKey()
```

- [ ] **Step 4: Собрать — убедиться, что компилируется**

Run: `cd /Users/nikitakiselev/code/twatch && make build`
Expected: сборка без ошибок (изменения чисто декларативные).

- [ ] **Step 5: Коммит**

```bash
cd /Users/nikitakiselev/code/twatch
git add watchos/config.h watchos/aiclient.h watchos/aiclient.cpp
git commit -m "feat(core): константы SpeedTest + публичный aiApiKey()"
```

---

## Task 5: Прошивка — модуль `speedtest.{h,cpp}`

**Files:**
- Create: `watchos/speedtest.h`
- Create: `watchos/speedtest.cpp`

- [ ] **Step 1: Создать `watchos/speedtest.h`**

```c
#pragma once
// Полноэкранная блокирующая процедура SpeedTest. Вызывается из приложения Wi-Fi
// (диалог подключённой сети). Предполагает активное Wi-Fi-подключение; рисует весь
// экран (статусбар + свой нижний Exit) и крутит собственный цикл modalPoll. По
// выходу возвращает управление — вызывающий делает kernelRedraw().
void speedtestRun();
```

- [ ] **Step 2: Создать `watchos/speedtest.cpp`**

```c
#include "speedtest.h"
#include "config.h"
#include "hw.h"          // tft, watch
#include "theme.h"       // COL_*, RGB565
#include "statusbar.h"   // statusbarDraw
#include "input.h"       // InputEvent, inputPoll
#include "modal.h"       // modalBegin, modalPoll
#include "wifi.h"        // wifiConnected
#include "aiclient.h"    // aiApiKey
#include <Arduino.h>
#include <WiFi.h>        // WiFiClient
#include <string.h>

// ───────────────────── Геометрия ─────────────────────
static const int TITLE_Y  = STATUSBAR_H + 14;
static const int HOST_Y   = STATUSBAR_H + 32;
static const int CENTER_Y = (CONTENT_TOP + CONTENT_BOTTOM) / 2;   // ~114
static const int BTN_R    = 52;                                   // круг START
static const int REP_X0   = SCR_W / 2 - 60, REP_Y0 = 150, REP_W = 120, REP_H = 28;

// ───────────────────── Сеть ─────────────────────
static const int UP_BUF = 16384;
static uint8_t  *buf = nullptr;          // общий буфер чтения/записи (PSRAM)

typedef void (*ProgressCb)(float mbs);

// Строка запроса + общие заголовки (Host + X-API-Key, если ключ задан).
static void writeReqHead(WiFiClient &c, const char *method, const char *path)
{
    c.printf("%s %s HTTP/1.1\r\n", method, path);
    c.printf("Host: %s\r\n", SPEEDTEST_HOST);
    String key = aiApiKey();
    if (key.length()) { c.print("X-API-Key: "); c.print(key); c.print("\r\n"); }
}

// Пропустить заголовки ответа до пустой строки "\r\n\r\n". true — дошли до тела.
static bool skipHeaders(WiFiClient &c, uint32_t deadline)
{
    int st = 0;
    while (millis() < deadline) {
        while (c.available()) {
            int ch = c.read();
            if      (st == 0 && ch == '\r') st = 1;
            else if (st == 1 && ch == '\n') st = 2;
            else if (st == 2 && ch == '\r') st = 3;
            else if (st == 3 && ch == '\n') return true;
            else st = (ch == '\r') ? 1 : 0;
        }
        if (!c.connected() && !c.available()) return false;
        delay(1);
    }
    return false;
}

// Неблокирующая проверка прерывания боковой кнопкой во время фазы.
static bool abortPressed()
{
    int16_t tx, ty;
    InputEvent e = inputPoll(tx, ty);
    return e == EVT_BACK || e == EVT_CLICK;
}

// Ping: SPEEDTEST_PINGS проб, в результат — минимум (мс). -1 если ни одна не удалась.
static int stPing()
{
    int best = -1;
    for (int i = 0; i < SPEEDTEST_PINGS; i++) {
        WiFiClient c;
        uint32_t t = millis();
        if (!c.connect(SPEEDTEST_HOST, SPEEDTEST_PORT, 3000)) { c.stop(); continue; }
        writeReqHead(c, "GET", "/speedtest/ping");
        c.print("Connection: close\r\n\r\n");
        bool got = false;
        uint32_t dl = millis() + 3000;
        while (millis() < dl) {
            if (c.available()) { got = true; break; }
            if (!c.connected()) break;
            delay(1);
        }
        int ms = (int)(millis() - t);
        c.stop();
        if (got && (best < 0 || ms < best)) best = ms;
    }
    return best;
}

// Download: читает SPEEDTEST_SECS секунд, среднее МБ/с. <0 — ошибка/прерывание.
static float stDownload(ProgressCb cb, bool *aborted)
{
    *aborted = false;
    WiFiClient c;
    if (!c.connect(SPEEDTEST_HOST, SPEEDTEST_PORT, 8000)) return -1;
    writeReqHead(c, "GET", "/speedtest/down");
    c.print("Connection: close\r\n\r\n");
    if (!skipHeaders(c, millis() + 8000)) { c.stop(); return -1; }

    uint32_t t0 = millis(), tEnd = t0 + SPEEDTEST_SECS * 1000;
    uint32_t winT = t0, winBytes = 0;
    uint64_t total = 0;
    while (millis() < tEnd) {
        int n = c.read(buf, UP_BUF);
        if (n > 0) { total += n; winBytes += n; }
        else if (!c.connected()) break;
        uint32_t now = millis();
        if (now - winT >= 250) {
            if (cb) cb(winBytes / 1e6f / ((now - winT) / 1000.0f));
            winBytes = 0; winT = now;
            if (abortPressed()) { *aborted = true; break; }
        }
    }
    uint32_t dt = millis() - t0;
    c.stop();
    if (*aborted) return -1;
    return total / 1e6f / (dt / 1000.0f);
}

// Upload: шлёт chunked SPEEDTEST_SECS секунд, среднее МБ/с. <0 — ошибка/прерывание.
static float stUpload(ProgressCb cb, bool *aborted)
{
    *aborted = false;
    WiFiClient c;
    if (!c.connect(SPEEDTEST_HOST, SPEEDTEST_PORT, 8000)) return -1;
    writeReqHead(c, "POST", "/speedtest/up");
    c.print("Transfer-Encoding: chunked\r\n");
    c.print("Content-Type: application/octet-stream\r\n");
    c.print("Connection: close\r\n\r\n");

    char hdr[12];
    int hdrLen = snprintf(hdr, sizeof(hdr), "%X\r\n", UP_BUF);

    uint32_t t0 = millis(), tEnd = t0 + SPEEDTEST_SECS * 1000;
    uint32_t winT = t0, winBytes = 0;
    uint64_t total = 0;
    while (millis() < tEnd) {
        c.write((const uint8_t *)hdr, hdrLen);
        int w = c.write(buf, UP_BUF);
        c.write((const uint8_t *)"\r\n", 2);
        if (w > 0) { total += w; winBytes += w; }
        else if (!c.connected()) break;
        uint32_t now = millis();
        if (now - winT >= 250) {
            if (cb) cb(winBytes / 1e6f / ((now - winT) / 1000.0f));
            winBytes = 0; winT = now;
            if (abortPressed()) { *aborted = true; break; }
        }
    }
    uint32_t dt = millis() - t0;
    c.print("0\r\n\r\n");                       // завершающий чанк
    uint32_t rd = millis() + 1000;             // коротко вычитать ответ
    while (millis() < rd && c.connected()) { while (c.available()) c.read(); delay(5); }
    c.stop();
    if (*aborted) return -1;
    return total / 1e6f / (dt / 1000.0f);
}

// ───────────────────── UI ─────────────────────
static void drawExitBtn()
{
    int y0 = CONTENT_BOTTOM, h = SCR_H - CONTENT_BOTTOM;
    tft->fillRect(0, y0, SCR_W, h, COL_BG);
    tft->drawRoundRect(8, y0 + 4, SCR_W - 16, h - 8, 6, COL_FRAME);
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_AMBER, COL_BG);
    tft->drawString("Exit", SCR_W / 2, y0 + h / 2, 2);
}

static void drawHeader()
{
    tft->fillRect(0, 0, SCR_W, CONTENT_BOTTOM, COL_BG);
    statusbarDraw();
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_AMBER, COL_BG);
    tft->drawString("SPEEDTEST", SCR_W / 2, TITLE_Y, 2);
    tft->setTextColor(COL_GREEN_DIM, COL_BG);
    tft->drawString("host: " SPEEDTEST_HOST, SCR_W / 2, HOST_Y, 1);
}

static void drawReady(const char *note)
{
    drawHeader();
    tft->drawCircle(SCR_W / 2, CENTER_Y, BTN_R, COL_GREEN);
    tft->drawCircle(SCR_W / 2, CENTER_Y, BTN_R - 1, COL_GREEN);
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_GREEN, COL_BG);
    tft->drawString("START", SCR_W / 2, CENTER_Y, 4);
    if (note && note[0]) {
        tft->setTextColor(COL_AMBER, COL_BG);
        tft->drawString(note, SCR_W / 2, CENTER_Y + BTN_R + 16, 2);
    }
    drawExitBtn();
}

static bool inStartBtn(int16_t x, int16_t y)
{
    int dx = x - SCR_W / 2, dy = y - CENTER_Y;
    return dx * dx + dy * dy <= BTN_R * BTN_R;
}

static void drawPhase(const char *label)
{
    drawHeader();
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_GREEN_HI, COL_BG);
    tft->drawString(label, SCR_W / 2, STATUSBAR_H + 52, 4);
    drawExitBtn();
}

// Живая цифра (мгновенная скорость). Перерисовывает только полосу цифры.
static void liveNumber(float mbs)
{
    char b[16];
    snprintf(b, sizeof(b), "%.2f", mbs < 0 ? 0 : mbs);
    tft->fillRect(0, CENTER_Y - 26, SCR_W, 64, COL_BG);
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_GREEN, COL_BG);
    tft->drawString(b, SCR_W / 2, CENTER_Y, 6);            // 48px
    tft->setTextColor(COL_GREEN_DIM, COL_BG);
    tft->drawString("MB/s", SCR_W / 2, CENTER_Y + 34, 2);
}

static void drawResults(float down, float up, int ping)
{
    drawHeader();
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_AMBER, COL_BG);
    tft->drawString("RESULTS", SCR_W / 2, STATUSBAR_H + 26, 2);

    char b[40];
    int y = STATUSBAR_H + 56, dy = 30;
    tft->setTextDatum(ML_DATUM);

    tft->setTextColor(COL_GREEN, COL_BG);
    snprintf(b, sizeof(b), "Down %.2f MB/s", down);
    tft->drawString(b, 18, y, 2);
    tft->setTextColor(COL_GREEN_DIM, COL_BG);
    snprintf(b, sizeof(b), "%.1f Mbps", down * 8);
    tft->drawString(b, 168, y, 1);
    y += dy;

    tft->setTextColor(COL_GREEN, COL_BG);
    snprintf(b, sizeof(b), "Up   %.2f MB/s", up);
    tft->drawString(b, 18, y, 2);
    tft->setTextColor(COL_GREEN_DIM, COL_BG);
    snprintf(b, sizeof(b), "%.1f Mbps", up * 8);
    tft->drawString(b, 168, y, 1);
    y += dy;

    tft->setTextColor(COL_GREEN, COL_BG);
    snprintf(b, sizeof(b), "Ping %d ms", ping);
    tft->drawString(b, 18, y, 2);

    tft->drawRoundRect(REP_X0, REP_Y0, REP_W, REP_H, 6, COL_GREEN);
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_GREEN_HI, COL_BG);
    tft->drawString("Repeat", SCR_W / 2, REP_Y0 + REP_H / 2, 2);

    drawExitBtn();
}

static bool inRepeatBtn(int16_t x, int16_t y)
{
    return x >= REP_X0 && x <= REP_X0 + REP_W && y >= REP_Y0 && y <= REP_Y0 + REP_H;
}

// Прогон ping→down→up. Возврат: true — успех (результаты в out-параметрах);
// false — ошибка/прерывание (note заполнен текстом для READY).
static bool runSequence(float &down, float &up, int &ping, const char **note)
{
    drawHeader();
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_AMBER, COL_BG);
    tft->drawString("ping...", SCR_W / 2, CENTER_Y, 4);
    drawExitBtn();
    ping = stPing();

    bool ab = false;
    drawPhase("DOWN"); liveNumber(0);
    down = stDownload(liveNumber, &ab);
    if (ab)        { *note = "aborted";           return false; }
    if (down < 0)  { *note = "server unreachable"; return false; }

    drawPhase("UP"); liveNumber(0);
    up = stUpload(liveNumber, &ab);
    if (ab)        { *note = "aborted";           return false; }
    if (up < 0)    { *note = "server unreachable"; return false; }

    return true;
}

// ───────────────────── Точка входа ─────────────────────
void speedtestRun()
{
    if (!buf) buf = (uint8_t *)ps_malloc(UP_BUF);
    if (buf)  memset(buf, 0xA5, UP_BUF);

    bool haveResults = false;
    float rDown = 0, rUp = 0; int rPing = 0;
    const char *note = wifiConnected() ? "" : "no wifi";

    modalBegin();
    drawReady(note);

    for (;;) {
        int16_t x, y; InputEvent e = modalPoll(x, y);
        if (e == EVT_BACK) break;
        if (e != EVT_TAP) continue;

        if (y >= CONTENT_BOTTOM) break;                       // нижний Exit
        bool start = (!haveResults && inStartBtn(x, y)) ||
                     ( haveResults && inRepeatBtn(x, y));
        if (!start) continue;

        if (!wifiConnected()) { note = "no wifi"; haveResults = false; drawReady(note); continue; }

        if (runSequence(rDown, rUp, rPing, &note)) {
            haveResults = true;
            drawResults(rDown, rUp, rPing);
        } else {
            haveResults = false;
            drawReady(note);
        }
    }

    if (buf) { free(buf); buf = nullptr; }
}
```

- [ ] **Step 3: Собрать — убедиться, что компилируется**

Run: `cd /Users/nikitakiselev/code/twatch && make build`
Expected: сборка без ошибок. (Модуль ещё ниоткуда не вызывается — Arduino компилирует все `.cpp` в корне скетча, так что ошибки компиляции всплывут здесь.)

- [ ] **Step 4: Коммит**

```bash
cd /Users/nikitakiselev/code/twatch
git add watchos/speedtest.h watchos/speedtest.cpp
git commit -m "feat(speedtest): сетевое ядро + полноэкранный UI"
```

---

## Task 6: Прошивка — диалог подключённой сети в `prog_wifi.cpp`

**Files:**
- Modify: `watchos/src/programs/prog_wifi.cpp`

- [ ] **Step 1: Добавить include'ы**

В шапке `watchos/src/programs/prog_wifi.cpp` после `#include "../../listnav.h"` (строка 7) добавить:

```c
#include "../../modal.h"
#include "../../speedtest.h"
```

- [ ] **Step 2: Добавить функции диалога**

Перед функцией `actSelected()` (перед строкой 113) вставить:

```c
// Диалог по тапу на подключённую сеть: SpeedTest / Disconnect / Back.
static const int CD_W = 180, CD_H = 132, CD_X0 = (SCR_W - 180) / 2, CD_Y0 = 40;
static const int CD_BH = 28, CD_GAP = 6, CD_BY0 = 40 + 34;   // = CD_Y0 + 34

static void drawConnDialog(const char *ssid, int sel)
{
    modalPanel(CD_X0, CD_Y0, CD_W, CD_H, 10, COL_GREEN);
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_AMBER, COL_BG);
    tft->drawString(ssid, SCR_W / 2, CD_Y0 + 16, 2);

    const char *items[3] = { "SpeedTest", "Disconnect", "Back" };
    const int bx = CD_X0 + 12, bw = CD_W - 24;
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
    ListNav l{3, 0, 0, 0};        // свайп ↑↓ — выбор, тап — подтвердить
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
                kernelRedraw(); return;                  // тап мимо панели — закрыть
            }
            int row = (y - CD_BY0) / (CD_BH + CD_GAP);
            if (row < 0 || row > 2) continue;
            if (row == 0) { speedtestRun(); kernelRedraw(); return; }   // SpeedTest
            if (row == 1) { wifiDisconnect(); kernelRedraw(); return; } // Disconnect
            kernelRedraw(); return;                                     // Back
        }
    }
}
```

- [ ] **Step 3: Вызвать диалог из `actSelected()`**

В `watchos/src/programs/prog_wifi.cpp` заменить строку 119.

Было:
```c
    if (conn) { wifiDisconnect(); statusbarDraw(); drawStatusLine(); drawList(); return; }
```
Стало:
```c
    if (conn) { connectedDialog(n.ssid); return; }
```

- [ ] **Step 4: Собрать**

Run: `cd /Users/nikitakiselev/code/twatch && make build`
Expected: сборка без ошибок.

- [ ] **Step 5: Прошить и проверить на железе**

Run: `cd /Users/nikitakiselev/code/twatch && make flash`

Ручная проверка (сначала запустить сервер с новыми эндпоинтами на `SPEEDTEST_HOST`):
1. Открыть Wi-Fi, подключиться к сети → тап по ней → появляется диалог `SSID / SpeedTest / Disconnect / Back`.
2. Свайпы ↑↓ двигают выделение; тап мимо панели и долгое нажатие кнопки закрывают диалог.
3. **Disconnect** → отключение, список обновился (метка `<` пропала).
4. **SpeedTest** → экран SPEEDTEST с кнопкой START и нижним Exit.
5. START → `ping...` → фаза DOWN с живой цифрой MB/s → фаза UP → RESULTS (Down/Up MB/s + Mbps мелким, Ping ms).
6. **Repeat** повторяет прогон; **Exit** (нижняя кнопка) и долгое нажатие кнопки возвращают в список Wi-Fi (экран корректно перерисован).
7. Во время DOWN/UP короткое/долгое нажатие боковой кнопки → `aborted`, возврат в READY.
8. Выключить сервер → START → `server unreachable`, без зависания.

- [ ] **Step 6: Коммит**

```bash
cd /Users/nikitakiselev/code/twatch
git add watchos/src/programs/prog_wifi.cpp
git commit -m "feat(wifi): диалог подключённой сети + запуск SpeedTest"
```

---

## Self-review (выполнено при написании)

- **Покрытие спеки:** сервер — 3 эндпоинта под `_api_key_ok` (Tasks 1–3); `config.h` + публичный `aiApiKey()` (Task 4); модуль `speedtest` с ping/down/up, живой цифрой, abort, READY/phase/RESULTS, Mbps мелким (Task 5); диалог SpeedTest/Disconnect/Back в `prog_wifi` + замена ветки disconnect (Task 6). Краевые случаи (`no wifi`, `server unreachable`, abort) — в Task 5/6.
- **Плейсхолдеров нет:** весь код приведён целиком, команды и ожидаемый результат указаны.
- **Согласованность имён:** `speedtestRun`, `aiApiKey`, `writeReqHead`, `liveNumber`, `runSequence`, `connectedDialog`, `drawConnDialog`, `CD_*`/`REP_*` константы — используются единообразно между задачами. `kernelRedraw()` восстанавливает Wi-Fi-экран через `wifiEnter`→`drawAll()` (проверено по `kernel.cpp`).
