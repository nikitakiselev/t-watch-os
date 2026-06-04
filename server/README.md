# watchos AI assistant — сервер

Голосовой ассистент для часов: принимает LPCM-аудио + UUID сессии, делает
Yandex SpeechKit STT → YandexGPT (с историей) → TTS, отдаёт URL mp3-ответа.

## Запуск
1. `cp server/.env.example server/.env` и заполнить ключи Yandex + `PUBLIC_BASE_URL`
   (LAN-IP машины, видимый часам — узнать через `ipconfig getifaddr en0`).
2. `docker compose up --build`

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
