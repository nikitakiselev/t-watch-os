# AI Assistant — дизайн

Дата: 2026-06-04
Статус: одобрен к реализации

## Что это

Приложение `AI Assistant` для watchos (T-Watch-2020 V3): голосовой ассистент.
Пользователь зажимает экранную кнопку-микрофон и говорит; на отпускании запись
уходит на свой Python-сервер, тот распознаёт речь, прогоняет через YandexGPT с
учётом истории разговора, синтезирует короткий голосовой ответ и отдаёт его URL;
часы воспроизводят ответ. В рамках одного запуска приложения поддерживается
контекст беседы (сессия).

## Железо (ключевое)

- **Микрофон: PDM**, data `GPIO2`, clock `GPIO0`. Читается через I2S0 в режиме
  PDM RX. Подтверждено офиц. примером
  `TTGO_TWatch_Library/examples/BasicUnit/TwatcV3Special/Microphone/Microphone.ino`
  и докой `docs/watch_2020_v3.md`.
- **Вывод звука:** MAX98357A по I2S0 (BCK 26 / WS 25 / DOUT 33) — уже используется
  модулем `sound.{h,cpp}` (радио, beep).
- **I2S0 общий** для входа (PDM RX) и выхода (TX). Запись и воспроизведение
  **никогда не идут одновременно** (сначала говоришь, потом слушаешь ответ) —
  поэтому конфликта нет, переключаем режим I2S0 между RX и TX.
- PSRAM есть — буфер записи держим там.

## Решения (зафиксированы при брейнсторме)

- STT + TTS — **Yandex SpeechKit**; LLM — **YandexGPT** (один облачный аккаунт/ключ).
- Сессия **эфемерная**: новый UUID при каждом запуске приложения; история живёт
  только в рамках сессии, старые сессии чистятся по TTL.
- Запись: **буфер в PSRAM → один POST на отпускании** (не стриминг).
- Обработка на сервере: **синхронный один запрос** (POST держит соединение до
  готового ответа, возвращает URL).
- Ответ: сервер сохраняет TTS как **mp3** и отдаёт **URL**; часы играют его через
  существующий `audioStart(url)` (`connecttohost`) — тот же путь, что у радио.

## Архитектура — прошивка

### `prog_assistant.{h,cpp}` (новый)
UI и конечный автомат состояний. Регистрируется в `apps.cpp` (как остальные).
- `onEnter`: `wifiAcquire()`; если нет связи — `wifiAutoConnect(...)` как в радио
  (нет связи → экран "no wifi"). Генерирует UUID сессии (`esp_random` ×4 → строка
  vida `xxxxxxxx-xxxx-4xxx-...`). Рисует экран в состоянии `IDLE`.
- `onTick`: опрашивает `watch->getTouch()` напрямую (нужен сырой «зажат/отпущен»,
  которого нет в нормализованных событиях `input`). Ведёт автомат состояний; во
  время `RECORDING` каждый тик тянет сэмплы из микрофона в PSRAM-буфер.
- `onExit`: `micCaptureEnd()` (если шла запись), `audioStop()`, `wifiRelease()`.
- `keepAwake()`: `true`, пока состояние ≠ `IDLE` (не уснуть во время записи/ответа).
- `onEvent`: не используется для записи (hold ловим в onTick); side-кнопка/Back —
  штатно через ядро.

### Автомат состояний
```
IDLE (🎤)
  │  touch-down на кнопке
  ▼
RECORDING (🔴 + индикатор)      ── cap ~15 c → авто-стоп как отпускание
  │  touch-up
  ▼
UPLOADING (↑)   POST LPCM(+UUID)
  │
  ▼
PROCESSING (…)  ждём ответ сервера (STT→GPT→TTS)
  │  пришёл URL
  ▼
SPEAKING (🔊)   audioStart(url)
  │  audioIsPlaying() == false
  ▼
IDLE
```
Любая ошибка (нет связи / таймаут / пустой клип <~0.3 c / ошибка сервера /
playback не стартовал) → короткий `soundBeep` + возврат в `IDLE`.

### Расширение `sound.{h,cpp}` — захват микрофона
Добавляются функции (живут рядом с аудио-задачей, владеющей I2S0; паттерн как у
`soundBeep`: приостановить аудио-задачу → переконфигурить I2S0 → восстановить):
- `bool micCaptureBegin();` — suspend аудио-задачи, `i2s_driver_uninstall(I2S0)`,
  установка I2S0 в **PDM RX** (см. конфиг ниже). Возвращает false при неудаче.
- `int micCaptureRead(int16_t* dst, int maxSamples);` — неблокирующе выгрести
  доступные сэмплы через `i2s_read` (вызывается каждый тик в `RECORDING`).
- `void micCaptureEnd();` — `i2s_driver_uninstall(I2S0)`, вернуть I2S0 в TX,
  resume аудио-задачи.

Конкретный конфиг PDM RX (из офиц. примера, но 16 кГц вместо 44.1):
```c
#define MIC_DATA  2     // GPIO2 — PDM data
#define MIC_CLOCK 0     // GPIO0 — PDM clock (на ws_io_num)
i2s_config_t cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM),
    .sample_rate = 16000,                         // STT: 16 кГц LPCM моно
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB),
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 2, .dma_buf_len = 128,
};
i2s_pin_config_t pin = { .bck_io_num = I2S_PIN_NO_CHANGE,
    .ws_io_num = MIC_CLOCK, .data_out_num = I2S_PIN_NO_CHANGE, .data_in_num = MIC_DATA };
i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL);
i2s_set_pin(I2S_NUM_0, &pin);
i2s_set_clk(I2S_NUM_0, 16000, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_MONO);
```

**Риск восстановления I2S0 TX:** библиотека `ESP32-audioI2S` ставит свой I2S-драйвер
сама (в конструкторе/`setPinout`). После записи `setPinout(...)` может оказаться
недостаточно — возможно, нужна повторная установка драйвера в TX-конфигурации.
Проверить по внутренностям библиотеки при реализации; при необходимости — хранить
исходный TX-конфиг и переустанавливать драйвер в `micCaptureEnd`. Если 16 кГц на
PDM-входе даст артефакты — фоллбэк: писать на 44.1 кГц и даунсемплить до 16 кГц
программно.

Буфер записи (`int16_t*` в PSRAM, `ps_malloc`) живёт в `prog_assistant`; размер на
~15 c при 16 кГц/16 бит/моно ≈ 480 КБ.

### `aiclient.{h,cpp}` (новый) — сетевой протокол
- `String aiSend(const char* serverUrl, const char* uuid, const int16_t* pcm, int samples);`
  Шлёт сырой LPCM 16 кГц/16 бит/моно (нативный формат Yandex STT, без WAV-
  заголовка), POST на `serverUrl` с `?session=UUID`,
  `Content-Type: application/octet-stream`. Возвращает URL ответа
  (тело/JSON) или `""` при ошибке. `HTTPClient`, таймаут ~15 c.
- Адрес сервера читается из SPIFFS-файла `/ai_server.txt` (как `stations.txt` —
  меняется без перепрошивки), с дефолтом-константой в коде. Заливается через
  `make fs`.

### Воспроизведение
Переиспользуем `sound.h`: `audioSetVolume(...)` + `audioStart(url)` (`connecttohost`
стримит mp3). Конец ответа определяем по `audioIsPlaying() == false`.

## Архитектура — сервер (Python, docker compose)

Один сервис **FastAPI** (`server/app.py`), один контейнер, том для `data/`.

### Эндпоинты
- `POST /talk?session=UUID` — тело: сырой LPCM 16 кГц/16 бит/моно.
  1. Yandex SpeechKit **STT** → текст реплики пользователя.
  2. Дописать `user: <текст>` в `data/sessions/UUID.txt`.
  3. **YandexGPT**: system-промпт («отвечай кратко, разговорно») + история из файла
     → короткий ответ.
  4. Дописать `bot: <текст>` в историю.
  5. Yandex SpeechKit **TTS** (mp3) → сохранить `data/audio/UUID/N.mp3`.
  6. Ответ: `{"url": "<PUBLIC_BASE_URL>/audio/UUID/N.mp3"}`.
- `GET /audio/UUID/N.mp3` — отдать mp3 (StreamingResponse).

### Конфиг (env)
`YANDEX_API_KEY`, `YANDEX_FOLDER_ID`, `PUBLIC_BASE_URL` (LAN-адрес:порт, по которому
часы достанут аудио), `SESSION_TTL` (сек), `MAX_REPLY_TOKENS`/системный промпт.

### Хранение и чистка
- История: `data/sessions/UUID.txt`, формат построчно `user:`/`bot:`.
- Аудио: `data/audio/UUID/N.mp3`.
- Фоновая задача удаляет сессии и аудио старше `SESSION_TTL` (эфемерность).

### docker-compose
Один сервис `assistant` (uvicorn FastAPI), `volumes: ./data:/app/data`, проброс
порта, env-файл с ключами Yandex.

## Поток данных

```
[hold] mic→PSRAM PCM ──release──> POST LPCM(+UUID) ──> STT ──> append txt
                                                          ──> YandexGPT(history) ──> append txt
                                                          ──> TTS mp3 ──> save
   play(connecttohost) <── URL <────────────────────────────┘
```

## Сетевая досягаемость

Сервер крутится на ПК/в LAN пользователя (docker), часы — в той же Wi-Fi.
`PUBLIC_BASE_URL` и `/ai_server.txt` должны указывать на **LAN-IP** сервера, иначе
часы не дотянутся до аудио. Для v1 — без HTTPS (доверенная локальная сеть).

## Обработка ошибок

| Ситуация | Поведение |
|---|---|
| Нет Wi-Fi | экран "no wifi", приложение не активно |
| Пустой/короткий клип (<~0.3 c) | игнор, остаёмся в IDLE |
| HTTP-таймаут / сетевая ошибка | beep + IDLE |
| Сервер вернул ошибку/не-URL | beep + IDLE |
| Playback не стартовал | beep + IDLE |
| Выход (Back) в любом состоянии | `onExit`: micCaptureEnd, audioStop, wifiRelease |

## Тестирование

- **Сервер (TDD):** pytest с замоканными Yandex-вызовами (STT/GPT/TTS → фикстуры);
  интеграционный тест «POST sample.wav → валидный URL → GET отдаёт mp3»; проверка
  накопления истории (вторая реплика видит первую).
- **Прошивка (ручной план):** loopback — сказал короткую фразу, получил осмысленный
  аудио-ответ; переключение I2S RX↔TX без зависания; контекст в рамках сессии
  (вторая фраза учитывает первую); ошибки (выключить сервер → beep+IDLE).

## План реализации (две фазы)

Один design-doc, **два плана**:
1. **Сервер** — реализуется и тестируется на ПК независимо (стаб-клиент, sample.wav).
   Фиксирует протокол.
2. **Прошивка** — под готовый протокол: захват PDM, автомат UI, `aiclient`, плейбек.

## Осознанно НЕ в v1 (YAGNI)

Стриминг-аудио, async-поллинг с job_id, постоянная память сессии между запусками,
HTTPS/шифрование, правка адреса сервера из UI (только файл `/ai_server.txt`),
несколько одновременных сессий-пользователей.
