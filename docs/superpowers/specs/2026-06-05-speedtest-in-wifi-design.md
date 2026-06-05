# SpeedTest внутри Wi-Fi приложения — дизайн

Дата: 2026-06-05

## Цель

Дать пользователю замер скорости интернета через Wi-Fi прямо из существующего
приложения **Wi-Fi**, без отдельного приложения. Тап по уже подключённой сети
открывает диалог с действиями, одно из которых — запуск SpeedTest (download /
upload / ping) с живой цифрой в реальном времени и итоговыми результатами.

## Не-цели (YAGNI)

- Отдельное приложение в списке `appList` — НЕ создаём.
- История замеров, графики, сохранение результатов — нет.
- Выбор сервера из UI — адрес хоста хардкодится в `config.h`.
- iperf3 / сырой кастомный TCP-протокол — используем HTTP к своему серверу.
- Учёт джиттера, multi-stream, прогрев — один поток, простое среднее.

## Реальность железа (чтобы ожидания совпали)

ESP32 на Arduino-ядре 2.0.17 по Wi-Fi реально выдаёт ~0.5–3 МБ/с TCP-throughput
(упор в сетевой стек, не в канал). «SpeedTest» здесь измеряет в первую очередь
способность самих часов, а не ширину интернета. Единицы — **MB/s десятичные**
(байты / 1 000 000); в результатах дополнительно Mbps мелким шрифтом.

---

## Архитектура

Три зоны изменений:

1. **`server/app.py`** (FastAPI) — три новых эндпоинта под тем же `X-API-Key`.
2. **Прошивка, новый модуль `watchos/speedtest.{h,cpp}`** — самодостаточная
   блокирующая полноэкранная процедура `speedtestRun()` (сеть + UI внутри).
3. **`watchos/src/programs/prog_wifi.cpp`** — диалог по тапу на подключённой сети,
   вызывающий `speedtestRun()`. Плюс константы в `config.h`.

### Границы модулей

- `speedtest.cpp` ничего не знает про список сетей; ему нужен лишь факт активного
  подключения (`wifiConnected()`), хост из `config.h` и API-ключ.
- `prog_wifi.cpp` ничего не знает про протокол замера; он лишь показывает диалог и
  зовёт `speedtestRun()`, после чего перерисовывает себя.
- API-ключ берётся из уже существующего SPIFFS-файла `/ai_server.txt` (2-я строка),
  через публичный хелпер `aiApiKey()` (выносится в `aiclient.h`). Хост остаётся
  хардкодом в `config.h` — секрет в прошивку не попадает.

---

## Серверная часть (`server/app.py`, FastAPI)

Все три эндпоинта защищены тем же `_api_key_ok(request, settings)`, что и `/talk`
(при пустом ключе в настройках проверка выключена — локальная разработка).

### `GET /speedtest/down`

Бесконечно стримит нули, пока клиент не закроет сокет.

```python
from fastapi.responses import StreamingResponse

@app.get("/speedtest/down")
def speedtest_down(request: Request):
    settings = get_settings()
    if not _api_key_ok(request, settings):
        return JSONResponse({"error": "unauthorized"}, status_code=401)

    CHUNK = b"\0" * 16384
    def gen():
        try:
            while True:
                yield CHUNK            # рвётся, когда клиент отключится
        except (BrokenPipeError, ConnectionResetError, GeneratorExit):
            return
    return StreamingResponse(gen(), media_type="application/octet-stream")
```

### `POST /speedtest/up`

Читает и выбрасывает тело (Starlette прозрачно разбирает `Transfer-Encoding:
chunked`), отвечает `200` с числом принятых байт.

```python
@app.post("/speedtest/up")
async def speedtest_up(request: Request):
    settings = get_settings()
    if not _api_key_ok(request, settings):
        return JSONResponse({"error": "unauthorized"}, status_code=401)
    total = 0
    async for chunk in request.stream():
        total += len(chunk)
    return JSONResponse({"received": total})
```

### `GET /speedtest/ping`

Мгновенный ответ для замера latency.

```python
@app.get("/speedtest/ping")
def speedtest_ping(request: Request):
    settings = get_settings()
    if not _api_key_ok(request, settings):
        return JSONResponse({"error": "unauthorized"}, status_code=401)
    return Response(content=b"ok", media_type="text/plain")
```

### Тесты сервера (`server/tests/test_app.py`)

- `down` отдаёт `200` и непустое тело; обрывается при закрытии клиента.
- `up` принимает chunked-тело и возвращает `received == отправленному`.
- `ping` отдаёт `200 ok`.
- Все три без ключа (при включённом ключе) → `401`.

---

## Прошивка: `config.h`

```c
// SpeedTest (приложение Wi-Fi). Хост тот же, что у AI-сервера, но задаётся явно,
// т.к. speedtest читает его без SPIFFS. Ключ берётся из /ai_server.txt (aiApiKey()).
#define SPEEDTEST_HOST   "twatch.nikitakiselev.ru"
#define SPEEDTEST_PORT   80
#define SPEEDTEST_SECS   8        // длительность каждой фазы (download/upload), c
#define SPEEDTEST_PINGS  5        // число проб ping, берётся минимум
```

## Прошивка: `aiclient.h/.cpp`

Сделать `aiApiKey()` публичной (объявить в `aiclient.h`, убрать `static` в `.cpp`),
чтобы `speedtest.cpp` переиспользовал ту же логику чтения ключа.

```c
// aiclient.h
String aiApiKey();   // API-ключ из 2-й строки /ai_server.txt ("" если не задан)
```

## Прошивка: `speedtest.h`

```c
#pragma once
// Блокирующая полноэкранная процедура SpeedTest. Вызывается из приложения Wi-Fi
// (диалог подключённой сети). Предполагает активное Wi-Fi-подключение; сама
// рисует весь экран (статусбар + свой нижний Exit), по выходу возвращает
// управление — вызывающий делает kernelRedraw() и перерисовку своего UI.
void speedtestRun();
```

## Прошивка: `speedtest.cpp`

### Транспорт

Сырой `WiFiClient` (http, без TLS — как замечено в `aiclient.cpp`: TLS-контекст
жрёт ~32 КБ кучи и не нужен на LAN/http). HTTP-запросы пишутся вручную ради
точного контроля по времени. Каждый запрос несёт заголовок `X-API-Key:
<aiApiKey()>`, если ключ непустой.

Буфер для upload — 16 КБ из PSRAM (`ps_malloc`), заполняется один раз (содержимое
не важно). Освобождается по выходу из `speedtestRun()`.

### Сетевое ядро (внутренние функции)

```c
struct StResult { float downMBs, upMBs; int pingMs; bool ok; const char *err; };
```

- `bool stPing(int &outMs)` — `SPEEDTEST_PINGS` раз: коннект к
  `SPEEDTEST_HOST:SPEEDTEST_PORT`, `GET /speedtest/ping`, замер времени до первого
  байта ответа, закрыть. Берём минимум, мс. Неудача коннекта → false.
- `float stDownload(ProgressCb cb)` — коннект, `GET /speedtest/down`, прочитать и
  отбросить заголовки до `\r\n\r\n`, затем `SPEEDTEST_SECS` секунд читать тело в
  буфер, считая байты. Каждые ~250 мс звать `cb(mgnMBs)` с мгновенной скоростью
  (байты за окно). Возврат — среднее за всю фазу (всего байт / факт. время).
- `float stUpload(ProgressCb cb)` — коннект, отправить
  `POST /speedtest/up` с `Transfer-Encoding: chunked` + `X-API-Key`, затем
  `SPEEDTEST_SECS` секунд слать чанки из буфера, считая payload и зовя `cb` каждые
  ~250 мс; завершить нулевым чанком `0\r\n\r\n`, прочитать ответ. Возврат —
  среднее.

`ProgressCb` = `void (*)(float mbs)` — рисует живую цифру. Внутри фаз также
проверяется боковая кнопка для прерывания (через неблокирующий опрос ввода);
прерывание возвращает «отменено» в UI-слой.

Прерывание/ошибка коннекта: фаза возвращает признак ошибки, UI показывает
`server unreachable` и уходит в READY.

### UI (полный экран, статусбар сверху, свой нижний Exit)

Состояния, крутятся в цикле `modalBegin()` / `modalPoll()`:

1. **READY** — заголовок `SPEEDTEST`, строка `host: <SPEEDTEST_HOST>`, крупная
   круглая кнопка **START** по центру, снизу кнопка **Exit**.
2. **PING…** — короткий замер, надпись `ping...`.
3. **DOWN** — метка `DOWN`, крупная живая цифра `x.xx MB/s` (мгновенная сглажённая,
   обновление ~250 мс), индикатор прогресса фазы (elapsed / `SPEEDTEST_SECS`).
4. **UP** — то же с меткой `UP`.
5. **RESULTS** — три строки:
   - `Down  x.xx MB/s`  + мелким `(yy.y Mbps)`
   - `Up    x.xx MB/s`  + мелким `(yy.y Mbps)`
   - `Ping  zz ms`
   Ниже — кнопка **Repeat**. Снизу — **Exit**.

Mbps = MB/s × 8 (десятичные мегабиты).

### Ввод

- Тап по **START** / **Repeat** → запустить последовательность ping → down → up →
  RESULTS.
- Тап по нижнему **Exit** или боковая кнопка (`EVT_BACK`) в READY/RESULTS →
  выход из `speedtestRun()`.
- Боковая кнопка во время DOWN/UP → прервать текущий замер, вернуться в READY с
  пометкой (`aborted`).

### Энергосбережение

Пока `speedtestRun()` блокирует главный луп, `powerTick()` не вызывается → часы не
уснут. `modalPoll()` дополнительно сбрасывает таймер сна. Спец-`keepAwake` не нужен.

---

## Прошивка: `prog_wifi.cpp`

### Диалог подключённой сети

В `actSelected()` заменить ветку (строки 118–119), где тап по подключённой сети
сразу делает `wifiDisconnect()`, на вызов `connectedDialog(n.ssid)`.

`connectedDialog()` — модальное меню по паттерну `gameMenu()` (`prog_game.cpp:1264`):
панель `modalPanel`, заголовок = SSID, три пункта со свайп-выбором ↑↓ и тапом для
подтверждения; тап мимо панели / `EVT_BACK` = закрыть.

Пункты:
- **SpeedTest** → `speedtestRun()`; по возврату `kernelRedraw()` + `drawAll()`.
- **Disconnect** → `wifiDisconnect()`, `statusbarDraw()`, `drawStatusLine()`,
  `drawList()`.
- **Back** → закрыть диалог.

Подключаем `#include "../../speedtest.h"`.

### Краевые случаи

- К моменту запуска Wi-Fi мог отвалиться → `speedtestRun()` на входе проверяет
  `wifiConnected()`, иначе сразу показывает ошибку и ждёт Exit.
- Сервер недоступен → сообщение в UI, возврат в READY.

---

## Сборка / прошивка

- Код прошивки — `make build` / `make flash` (стандартно).
- Эндпоинты сервера — отдельный деплой `server/` (вне Makefile прошивки).
- SPIFFS не меняется (адрес хоста в `config.h`, ключ уже лежит в `/ai_server.txt`).

## Тестирование

**Сервер:** pytest в `server/tests/test_app.py` (down/up/ping + 401 без ключа).

**Прошивка** (ручное, на железе — юнит-тестов в проекте нет):
1. Wi-Fi → подключиться к сети → тап по ней → появляется диалог SpeedTest/Disconnect/Back.
2. Back закрывает; Disconnect отключает; SpeedTest открывает полноэкранный тест.
3. START → ping → живая цифра DOWN → живая цифра UP → RESULTS с тремя строками и Mbps.
4. Repeat повторяет; Exit и боковая кнопка возвращают в список Wi-Fi (экран корректно перерисован).
5. Прерывание боковой кнопкой во время фазы → возврат в READY.
6. Сервер выключен → `server unreachable`, без зависания.
