# AI Assistant — Прошивка часов (Фаза 2) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Приложение `AI Assistant` для watchos (T-Watch-2020 V3): зажал кнопку — говоришь, отпустил — запись (сырой LPCM 16 кГц) уходит на сервер `https://twatch.nikitakiselev.ru/talk`, ответ-mp3 по URL проигрывается. Контекст беседы держится в рамках запуска (UUID сессии).

**Architecture:** Новая `Program` (`prog_assistant`) с конечным автоматом состояний; захват PDM-микрофона добавляется в `sound.{cpp,h}` (делит I2S0 с выводом — переключаем RX↔TX); сеть (HTTPS POST LPCM + парс URL) в новом `aiclient.{cpp,h}`; воспроизведение ответа — существующий `audioStart(url)` (`connecttohost` умеет https). Адрес сервера — в SPIFFS (`/ai_server.txt`).

**Tech Stack:** Arduino C++ (ESP32 core 2.0.17), TTGO_TWatch_Library, ESP32-audioI2S@2.0.0, driver/i2s (PDM RX), HTTPClient + WiFiClientSecure, SPIFFS.

> **Тестирование прошивки:** автоматический критерий каждой задачи — **`make build` компилируется без ошибок** (железо не нужно). Функциональные проверки — **ручные на часах** (флешит пользователь: `make flash`), отмечены как «На устройстве». Юнит-тестов на on-device код нет.

> **Подтверждённые факты (из разбора железа/библиотек):**
> - PDM-микрофон: `I2S_NUM_0`, clk `GPIO0` (ws), data `GPIO2` (data_in). Режим `MASTER|RX|PDM`, 16 кГц.
> - `Audio` (ESP32-audioI2S) ставит свой I2S0 TX-драйвер в конструкторе и **не** переустанавливает его на `connecttohost`. Поэтому после записи I2S0 надо вернуть в TX вручную: `i2s_driver_uninstall` → `i2s_driver_install(TX)` → `audio.setPinout(...)`. TX-формат на ядре 2.0.17 — `I2S_COMM_FORMAT_STAND_I2S`.
> - `connecttohost` поддерживает **https** (detect `https://` → 443, `setInsecure()`), поэтому ответ по `https://…/audio/…mp3` играется без изменений.

---

## Структура файлов

- `watchos/sound.h` / `watchos/sound.cpp` — **modify**: добавить захват PDM-микрофона (`micCaptureBegin/Read/End`), рядом с аудио-задачей, владеющей I2S0.
- `watchos/aiclient.h` / `watchos/aiclient.cpp` — **create**: адрес сервера из SPIFFS + HTTPS POST LPCM с парсом URL ответа.
- `watchos/data/ai_server.txt` — **create**: базовый URL сервера (в SPIFFS, заливается `make fs`).
- `watchos/src/programs/prog_assistant.h` / `.cpp` — **create**: UI + конечный автомат.
- `watchos/apps.cpp` — **modify**: зарегистрировать приложение.

---

## Task 1: Захват PDM-микрофона в sound.{h,cpp}

**Files:**
- Modify: `watchos/sound.h`
- Modify: `watchos/sound.cpp`

- [ ] **Step 1: Объявить API в sound.h**

Добавить в `watchos/sound.h` перед закрывающими строками (после объявления `soundPlayFile`):
```c
// ── Захват PDM-микрофона (T-Watch V3: clk GPIO0, data GPIO2) ──
// Делит I2S0 с выводом звука: запись и воспроизведение НЕ одновременно.
// micCaptureBegin переключает I2S0 в PDM RX, micCaptureEnd возвращает в TX.
bool micCaptureBegin();                       // false при неудаче установки драйвера
int  micCaptureRead(int16_t *dst, int maxSamples);   // выгрести доступные сэмплы (неблокирующе)
void micCaptureEnd();                         // вернуть I2S0 в TX (для воспроизведения)
```

- [ ] **Step 2: Реализовать в sound.cpp**

В `watchos/sound.cpp` добавить вверху (рядом с другими `#include` / константами I2S):
```c
// Пины PDM-микрофона T-Watch 2020 V3.
static const int MIC_DATA  = 2;     // GPIO2 — PDM data (data_in)
static const int MIC_CLOCK = 0;     // GPIO0 — PDM clock (на ws_io_num)
```
И добавить функции (в конец файла):
```c
// Установить I2S0 в режим PDM RX (приостановив аудио-задачу, владеющую I2S0).
bool micCaptureBegin()
{
    if (taskH) vTaskSuspend(taskH);
    i2s_driver_uninstall(I2S_NUM_0);

    i2s_config_t cfg = {};
    cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM);
    cfg.sample_rate = 16000;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB);
    cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count = 4;
    cfg.dma_buf_len = 256;
    if (i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL) != ESP_OK) {
        if (taskH) vTaskResume(taskH);
        return false;
    }
    i2s_pin_config_t pin = {};
    pin.bck_io_num = I2S_PIN_NO_CHANGE;
    pin.ws_io_num = MIC_CLOCK;
    pin.data_out_num = I2S_PIN_NO_CHANGE;
    pin.data_in_num = MIC_DATA;
    i2s_set_pin(I2S_NUM_0, &pin);
    i2s_set_clk(I2S_NUM_0, 16000, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_MONO);
    return true;
}

// Выгрести доступные сэмплы (неблокирующе: таймаут 0). Возвращает число int16-сэмплов.
int micCaptureRead(int16_t *dst, int maxSamples)
{
    size_t bytesRead = 0;
    i2s_read(I2S_NUM_0, (char *)dst, maxSamples * sizeof(int16_t), &bytesRead, 0);
    return (int)(bytesRead / sizeof(int16_t));
}

// Вернуть I2S0 в TX-конфиг библиотеки Audio и пины динамика, resume аудио-задачи.
void micCaptureEnd()
{
    i2s_driver_uninstall(I2S_NUM_0);

    i2s_config_t cfg = {};
    cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate = 16000;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_STAND_I2S);  // ядро 2.0.17
    cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count = 8;
    cfg.dma_buf_len = 1024;
    cfg.use_apll = false;
    cfg.tx_desc_auto_clear = true;
    cfg.fixed_mclk = I2S_PIN_NO_CHANGE;
    i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL);

    audio.setPinout(I2S_BCK, I2S_WS, I2S_DOUT);   // вернуть пины динамика
    audio.setVolume((uint8_t)volume);

    if (taskH) vTaskResume(taskH);
}
```

- [ ] **Step 3: Скомпилировать**

Run: `make build`
Expected: компиляция без ошибок (BUILD finished). Если ругается на типы `i2s_*` — убедиться, что `#include <driver/i2s.h>` уже есть в sound.cpp (он там есть для `soundBeep`).

- [ ] **Step 4: Commit**

```bash
git add watchos/sound.h watchos/sound.cpp
git commit -m "watchos(assistant): захват PDM-микрофона в sound (I2S0 RX<->TX)"
```

---

## Task 2: Сетевой клиент aiclient.{h,cpp} + адрес сервера в SPIFFS

**Files:**
- Create: `watchos/aiclient.h`
- Create: `watchos/aiclient.cpp`
- Create: `watchos/data/ai_server.txt`

- [ ] **Step 1: aiclient.h**

`watchos/aiclient.h`:
```c
#pragma once
#include <Arduino.h>

// Результат запроса к серверу ассистента.
//   code 200 → url заполнен (ответ-mp3); 204 → речь не распознана; иначе ошибка.
struct AiResult {
    int    code;     // HTTP-код (200/204/...) или 0 при сетевой ошибке
    String url;      // URL аудио-ответа (только при code==200)
};

// Базовый URL сервера из SPIFFS /ai_server.txt (1-я строка), иначе дефолт.
String   aiServerUrl();

// POST сырого LPCM 16 кГц/16 бит/моно на <server>/talk?session=<uuid>.
AiResult aiSend(const char *uuid, const int16_t *pcm, int samples);
```

- [ ] **Step 2: aiclient.cpp**

`watchos/aiclient.cpp`:
```c
#include "aiclient.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <SPIFFS.h>

static const char *DEFAULT_SERVER = "https://twatch.nikitakiselev.ru";

String aiServerUrl()
{
    String url;
    File f = SPIFFS.open("/ai_server.txt", "r");
    if (f) {
        url = f.readStringUntil('\n');
        f.close();
        url.trim();
    }
    if (url.length() == 0) url = DEFAULT_SERVER;
    while (url.endsWith("/")) url.remove(url.length() - 1);   // без хвостового слэша
    return url;
}

// Достать значение "url" из тела {"url":"https://..."} без JSON-зависимостей.
static String extractUrl(const String &body)
{
    int k = body.indexOf("\"url\"");
    if (k < 0) return "";
    int c = body.indexOf(':', k);
    if (c < 0) return "";
    int q1 = body.indexOf('"', c + 1);
    if (q1 < 0) return "";
    int q2 = body.indexOf('"', q1 + 1);
    if (q2 < 0) return "";
    return body.substring(q1 + 1, q2);
}

AiResult aiSend(const char *uuid, const int16_t *pcm, int samples)
{
    AiResult r = { 0, "" };

    WiFiClientSecure client;
    client.setInsecure();                       // сертификат не проверяем (как в connecttohost)

    HTTPClient http;
    String url = aiServerUrl() + "/talk?session=" + uuid;
    if (!http.begin(client, url)) return r;
    http.addHeader("Content-Type", "application/octet-stream");
    http.setTimeout(20000);                     // сервер STT+GPT+TTS ~5-10 c

    int code = http.POST((uint8_t *)pcm, (size_t)samples * sizeof(int16_t));
    r.code = code;
    if (code == 200) r.url = extractUrl(http.getString());
    http.end();
    return r;
}
```

- [ ] **Step 3: watchos/data/ai_server.txt**

Создать файл `watchos/data/ai_server.txt` с единственной строкой:
```
https://twatch.nikitakiselev.ru
```

- [ ] **Step 4: Скомпилировать**

Run: `make build`
Expected: без ошибок. (`aiclient.cpp` в корне скетча компилируется автоматически.)

- [ ] **Step 5: Commit**

```bash
git add watchos/aiclient.h watchos/aiclient.cpp watchos/data/ai_server.txt
git commit -m "watchos(assistant): aiclient — HTTPS POST LPCM + адрес сервера в SPIFFS"
```

---

## Task 3: Скелет приложения prog_assistant + регистрация

**Files:**
- Create: `watchos/src/programs/prog_assistant.h`
- Create: `watchos/src/programs/prog_assistant.cpp`
- Modify: `watchos/apps.cpp`

- [ ] **Step 1: prog_assistant.h**

`watchos/src/programs/prog_assistant.h`:
```c
#pragma once
#include "../../program.h"
extern const Program assistantProgram;
```

- [ ] **Step 2: prog_assistant.cpp — скелет (wifi, UUID, экран IDLE)**

`watchos/src/programs/prog_assistant.cpp`:
```c
#include "prog_assistant.h"
#include "../../hw.h"
#include "../../statusbar.h"
#include "../../theme.h"
#include "../../wifi.h"
#include "../../sound.h"
#include "../../aiclient.h"
#include <Arduino.h>
#include <string.h>

enum AState { ST_IDLE, ST_RECORDING, ST_SENDING, ST_SPEAKING, ST_NOWIFI };
static AState state = ST_IDLE;

static char    sessionId[40] = "";
static int16_t *recBuf = nullptr;            // PSRAM-буфер записи
static int      recSamples = 0;
static const int REC_MAX_SECONDS = 15;
static const int SR = 16000;
static const int REC_CAP = SR * REC_MAX_SECONDS;   // макс. сэмплов

// Кнопка-микрофон в центре экрана.
static const int BTN_CX = SCR_W / 2, BTN_CY = 116, BTN_R = 66;

static void genSession()
{
    uint32_t a = esp_random(), b = esp_random(), c = esp_random(), d = esp_random();
    snprintf(sessionId, sizeof(sessionId),
             "%08x-%04x-4%03x-%04x-%08x%04x",
             a, b & 0xffff, (c & 0x0fff), (d & 0x3fff) | 0x8000, b, d & 0xffff);
}

static void drawButton(uint16_t col, const char *label)
{
    tft->fillRect(0, CONTENT_TOP, SCR_W, CONTENT_BOTTOM - CONTENT_TOP, COL_BG);
    tft->drawCircle(BTN_CX, BTN_CY, BTN_R, col);
    tft->drawCircle(BTN_CX, BTN_CY, BTN_R - 1, col);
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(col, COL_BG);
    tft->drawString(label, BTN_CX, BTN_CY, 4);
}

static void drawScreen()
{
    tft->fillRect(0, 0, SCR_W, CONTENT_BOTTOM, COL_BG);
    statusbarDraw();
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(COL_AMBER, COL_BG);
    tft->drawString("AI ASSISTANT", SCR_W / 2, STATUSBAR_H + 12, 2);
    switch (state) {
    case ST_IDLE:      drawButton(COL_GREEN,    "TALK");  break;
    case ST_RECORDING: drawButton(COL_AMBER,    "REC");   break;
    case ST_SENDING:   drawButton(COL_GREEN_DIM,"...");   break;
    case ST_SPEAKING:  drawButton(RGB565(0x33,0xcc,0xff), "SPK"); break;
    case ST_NOWIFI:
        tft->setTextColor(COL_AMBER, COL_BG);
        tft->drawString("no wifi", SCR_W / 2, BTN_CY, 4);
        break;
    }
}

static void assistantEnter()
{
    wifiAcquire();
    if (!wifiConnected()) {
        tft->fillRect(0, 0, SCR_W, CONTENT_BOTTOM, COL_BG);
        statusbarDraw();
        tft->setTextDatum(MC_DATUM);
        tft->setTextColor(COL_AMBER, COL_BG);
        tft->drawString("connecting...", SCR_W / 2, BTN_CY, 4);
        wifiAutoConnect(12000, nullptr);
    }
    if (!wifiConnected()) { state = ST_NOWIFI; drawScreen(); return; }

    genSession();
    if (!recBuf) recBuf = (int16_t *)ps_malloc((size_t)REC_CAP * sizeof(int16_t));
    state = ST_IDLE;
    recSamples = 0;
    drawScreen();
}

static void assistantExit()
{
    if (state == ST_RECORDING) micCaptureEnd();
    audioStop();
    if (recBuf) { free(recBuf); recBuf = nullptr; }
    state = ST_IDLE;
    wifiRelease();
}

static bool assistantKeepAwake() { return state != ST_IDLE && state != ST_NOWIFI; }

static void assistantIcon(TFT_eSPI &g, int cx, int cy, int r)
{
    g.fillRoundRect(cx - r / 4, cy - r / 2, r / 2, r * 3 / 4, r / 6, COL_GREEN);   // капсула мик
    g.drawFastHLine(cx - r / 3, cy + r / 2, r * 2 / 3, COL_GREEN);                 // подставка
    g.drawFastVLine(cx, cy + r / 4, r / 4, COL_GREEN);
}

const Program assistantProgram = {
    "AI Assistant", assistantEnter, nullptr, nullptr, assistantIcon,
    nullptr, 0, assistantKeepAwake, assistantExit
};
```

- [ ] **Step 3: Зарегистрировать в apps.cpp**

В `watchos/apps.cpp` добавить include рядом с другими и указатель в массив `appList`:
```c
#include "src/programs/prog_assistant.h"
```
и в массив `appList[]` добавить `&assistantProgram` (например после радио).

- [ ] **Step 4: Скомпилировать**

Run: `make build`
Expected: без ошибок.

- [ ] **Step 5: На устройстве (ручная проверка)**

Run: `make flash` затем (один раз) `make fs` — залить `ai_server.txt` в SPIFFS.
Ожидается: в списке приложений есть «AI Assistant»; вход → подключается к Wi-Fi → показывает круг с «TALK». Нет связи → «no wifi».

- [ ] **Step 6: Commit**

```bash
git add watchos/src/programs/prog_assistant.h watchos/src/programs/prog_assistant.cpp watchos/apps.cpp
git commit -m "watchos(assistant): скелет приложения — wifi, UUID сессии, экран IDLE"
```

---

## Task 4: Запись по удержанию (hold-to-talk)

**Files:**
- Modify: `watchos/src/programs/prog_assistant.cpp`

- [ ] **Step 1: Добавить onTick с опросом тача и записью**

В `watchos/src/programs/prog_assistant.cpp` добавить перед определением `assistantProgram`:
```c
// Идёт ли сейчас палец по экрану (для hold-to-talk опрашиваем тач напрямую).
static bool touchDown()
{
    int16_t x, y;
    return watch->getTouch(x, y);
}

static void startRecording()
{
    if (!micCaptureBegin()) { soundBeep(400, 120); return; }
    recSamples = 0;
    state = ST_RECORDING;
    drawScreen();
}

static void stopRecording()
{
    micCaptureEnd();
    state = ST_IDLE;          // отправку добавит Task 5; пока возврат в IDLE
    drawScreen();
}

static void assistantTick()
{
    if (state == ST_IDLE && touchDown()) {
        startRecording();
        return;
    }
    if (state == ST_RECORDING) {
        if (recSamples < REC_CAP) {
            int got = micCaptureRead(recBuf + recSamples, REC_CAP - recSamples);
            recSamples += got;
        }
        if (!touchDown() || recSamples >= REC_CAP) {
            stopRecording();
        }
    }
}
```

- [ ] **Step 2: Подключить onTick в структуру Program**

Заменить строку определения `assistantProgram` на (onTick = assistantTick):
```c
const Program assistantProgram = {
    "AI Assistant", assistantEnter, assistantTick, nullptr, assistantIcon,
    nullptr, 0, assistantKeepAwake, assistantExit
};
```

- [ ] **Step 3: Скомпилировать**

Run: `make build`
Expected: без ошибок.

- [ ] **Step 4: На устройстве (ручная проверка)**

Run: `make flash`
Ожидается: касание круга → «REC» (амбер), пока держишь — запись идёт; отпустил → возврат к «TALK». Удержание >15 c само останавливает. (Звук пока никуда не уходит — проверяем только захват/переключение I2S без зависания: после нескольких записей часы не виснут, экран живой.)

- [ ] **Step 5: Commit**

```bash
git add watchos/src/programs/prog_assistant.cpp
git commit -m "watchos(assistant): запись по удержанию (hold-to-talk) в PSRAM"
```

---

## Task 5: Отправка на сервер и воспроизведение ответа (полный цикл)

**Files:**
- Modify: `watchos/src/programs/prog_assistant.cpp`

- [ ] **Step 1: Заменить stopRecording на отправку + плейбек**

В `watchos/src/programs/prog_assistant.cpp` заменить функцию `stopRecording` на:
```c
static void stopRecording()
{
    micCaptureEnd();

    if (recSamples < SR * 3 / 10) {        // < 0.3 c — игнор
        state = ST_IDLE;
        drawScreen();
        return;
    }

    state = ST_SENDING;                    // рисуем «...» ДО блокирующего POST
    drawScreen();

    AiResult res = aiSend(sessionId, recBuf, recSamples);   // блокирует ~5-10 c

    if (res.code == 200 && res.url.length() > 0) {
        audioSetVolume(18);
        audioStart(res.url.c_str());       // connecttohost (https) — асинхронно
        state = ST_SPEAKING;
        drawScreen();
    } else {
        soundBeep(res.code == 204 ? 600 : 300, 120);   // 204 — не расслышал; иначе ошибка
        state = ST_IDLE;
        drawScreen();
    }
}
```

- [ ] **Step 2: В onTick — завершать SPEAKING по окончании воспроизведения**

В функции `assistantTick` добавить ветку для `ST_SPEAKING` (после блока `ST_RECORDING`):
```c
    if (state == ST_SPEAKING) {
        if (!audioIsPlaying()) {
            state = ST_IDLE;
            drawScreen();
        }
    }
```

- [ ] **Step 3: Скомпилировать**

Run: `make build`
Expected: без ошибок.

- [ ] **Step 4: На устройстве (ручная проверка — сквозной голос)**

Run: `make flash`
Ожидается: зажал «TALK» → сказал фразу → отпустил → «...» (несколько секунд) → «SPK» и часы проигрывают голосовой ответ → возврат к «TALK». Вторая фраза учитывает первую (контекст сессии). Короткий тык (<0.3 c) — игнорируется. Нет ответа/ошибка сервера — короткий beep и возврат к «TALK».

- [ ] **Step 5: Commit**

```bash
git add watchos/src/programs/prog_assistant.cpp
git commit -m "watchos(assistant): полный цикл — отправка LPCM, воспроизведение ответа"
```

---

## Task 6: Иконки состояний, индикатор уровня, аккуратные ошибки

**Files:**
- Modify: `watchos/src/programs/prog_assistant.cpp`

- [ ] **Step 1: Заменить текстовые подписи кнопки на иконки-глифы**

В `watchos/src/programs/prog_assistant.cpp` заменить `drawButton` на версию с простыми векторными иконками (микрофон/точки/динамик), рисуемыми примитивами:
```c
static void iconMic(int cx, int cy, uint16_t c)
{
    tft->fillRoundRect(cx - 9, cy - 22, 18, 30, 9, c);     // капсула
    tft->drawFastHLine(cx - 13, cy + 14, 26, c);           // подставка
    tft->drawFastVLine(cx, cy + 8, 6, c);
}
static void iconDots(int cx, int cy, uint16_t c)
{
    tft->fillCircle(cx - 16, cy, 4, c);
    tft->fillCircle(cx,      cy, 4, c);
    tft->fillCircle(cx + 16, cy, 4, c);
}
static void iconSpeaker(int cx, int cy, uint16_t c)
{
    tft->fillRect(cx - 16, cy - 7, 10, 14, c);             // корпус
    tft->fillTriangle(cx - 6, cy - 14, cx - 6, cy + 14, cx + 6, cy, c);
    tft->drawCircle(cx + 12, cy, 7, c);                    // звуковая волна
}

static void drawButton(uint16_t col)
{
    tft->fillRect(0, CONTENT_TOP, SCR_W, CONTENT_BOTTOM - CONTENT_TOP, COL_BG);
    tft->drawCircle(BTN_CX, BTN_CY, BTN_R, col);
    tft->drawCircle(BTN_CX, BTN_CY, BTN_R - 1, col);
    switch (state) {
    case ST_IDLE:      iconMic(BTN_CX, BTN_CY, col);     break;
    case ST_RECORDING: iconMic(BTN_CX, BTN_CY, col);     break;
    case ST_SENDING:   iconDots(BTN_CX, BTN_CY, col);    break;
    case ST_SPEAKING:  iconSpeaker(BTN_CX, BTN_CY, col); break;
    default: break;
    }
}
```

- [ ] **Step 2: Обновить drawScreen под новую сигнатуру drawButton**

Заменить тело `drawScreen` (блок switch) на:
```c
    switch (state) {
    case ST_IDLE:      drawButton(COL_GREEN);                  break;
    case ST_RECORDING: drawButton(COL_AMBER);                  break;
    case ST_SENDING:   drawButton(COL_GREEN_DIM);              break;
    case ST_SPEAKING:  drawButton(RGB565(0x33, 0xcc, 0xff));   break;
    case ST_NOWIFI:
        tft->setTextDatum(MC_DATUM);
        tft->setTextColor(COL_AMBER, COL_BG);
        tft->drawString("no wifi", SCR_W / 2, BTN_CY, 4);
        break;
    }
```

- [ ] **Step 3: Индикатор времени записи под кнопкой (в onTick во время REC)**

В `assistantTick`, внутри ветки `ST_RECORDING`, после накопления сэмплов добавить отрисовку прогресс-дуги/секунд:
```c
        // секунды записи под кнопкой (обновляем точечно, без мерцания)
        static int lastSec = -1;
        int sec = recSamples / SR;
        if (sec != lastSec) {
            lastSec = sec;
            char b[8];
            snprintf(b, sizeof(b), "%ds", sec);
            tft->fillRect(BTN_CX - 30, BTN_CY + BTN_R + 6, 60, 20, COL_BG);
            tft->setTextDatum(MC_DATUM);
            tft->setTextColor(COL_AMBER, COL_BG);
            tft->drawString(b, BTN_CX, BTN_CY + BTN_R + 14, 2);
        }
```

- [ ] **Step 4: Скомпилировать**

Run: `make build`
Expected: без ошибок.

- [ ] **Step 5: На устройстве (ручная проверка)**

Run: `make flash`
Ожидается: в IDLE — иконка микрофона; запись — амбер микрофон + счётчик секунд; отправка — три точки; воспроизведение — иконка динамика; ошибки дают beep и возврат к микрофону.

- [ ] **Step 6: Commit**

```bash
git add watchos/src/programs/prog_assistant.cpp
git commit -m "watchos(assistant): иконки состояний и индикатор времени записи"
```

---

## Финальная проверка фазы

- [ ] `make build` компилируется на всех задачах.
- [ ] `make flash` + `make fs` (один раз для `ai_server.txt`).
- [ ] Сквозной сценарий: TALK→говоришь→ответ голосом; контекст сессии держится; короткий тык игнорируется; ошибки/нет-связи — beep/«no wifi» без зависаний.
- [ ] Многократная запись→ответ не вешает I2S (переключение RX↔TX стабильно). Если ответ-аудио искажён — проверить `communication_format` в `micCaptureEnd` (Task 1) против фактической версии ESP32-audioI2S.
- [ ] Выход из приложения (Back) освобождает Wi-Fi и PSRAM-буфер (`onExit`).
