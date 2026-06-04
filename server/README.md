# watchos AI assistant — сервер

Голосовой ассистент для часов: принимает LPCM-аудио + UUID сессии, делает
Yandex SpeechKit STT → YandexGPT (с историей) → TTS, отдаёт URL mp3-ответа.

## Запуск
1. `cp server/.env.example server/.env` и заполнить ключи Yandex + `PUBLIC_BASE_URL`
   (LAN-IP машины, видимый часам — узнать через `ipconfig getifaddr en0`).
2. `docker compose up --build`

## Защита (для публичного сервера)
- `API_KEY` — общий ключ на `POST /talk`. Часы шлют его в заголовке `X-API-Key`
  (2-я строка `watchos/data/ai_server.txt`), дебаг-страница получает встроенным.
  Пусто → проверка выключена (локалка).
- `WEB_PASSWORD` — пароль на дебаг-страницу `/` (Basic Auth, имя пользователя любое).
- `/audio/<uuid>/<n>.mp3` защищён самим неугадываемым UUID сессии (capability-URL).

## Браузерный debug-стенд (рекомендуется до прошивки)
Открой **`http://localhost:8080/`** в браузере (нужен микрофон; работает на
`localhost` или по HTTPS — иначе браузер не даст доступ к микрофону).
Зажми «hold to talk», скажи фразу, отпусти — стенд запишет сырой LPCM 16 кГц
(ровно как будущие часы), отправит на `/talk` и проиграет голосовой ответ.
Вторая фраза учитывает контекст первой (история сессии). Кнопка «new» сбрасывает
сессию. Ошибки сервера (напр. неверный ключ) показываются текстом в логе.

## Проверка через curl (без браузера)
```
ffmpeg -i sample.wav -f s16le -ar 16000 -ac 1 sample.pcm
curl -s --data-binary @sample.pcm "http://localhost:8080/talk?session=test" | jq
```
→ `{"url": "http://<ip>:8080/audio/test/0.mp3"}`; открыть URL — играет ответ.

## Тесты
```
cd server && python -m venv .venv && . .venv/bin/activate
pip install -r requirements.txt && python -m pytest -v
```

## Публикация образа в ghcr.io
Из корня репозитория:
```
./build-push.sh           # тег = короткий git sha + latest
./build-push.sh v1.0      # явный тег + latest
```
Собирает multi-arch образ (`linux/amd64,linux/arm64`) и пушит в
`ghcr.io/nikitakiselev/twatch-assistant`. Логин берётся из `gh auth token`
(нужен scope `write:packages`) либо из env `GHCR_TOKEN`. Деплой на сервере:
```
docker pull ghcr.io/nikitakiselev/twatch-assistant:latest
```
(пакет приватный по умолчанию — на сервере сделать `docker login ghcr.io` или
переключить видимость пакета на public в настройках GitHub Packages).
