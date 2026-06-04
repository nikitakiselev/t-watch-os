# watchos AI assistant — сервер

Голосовой ассистент для часов: принимает LPCM-аудио + UUID сессии, делает
Yandex SpeechKit STT → YandexGPT (с историей) → TTS, отдаёт URL mp3-ответа.

## Запуск
1. `cp server/.env.example server/.env` и заполнить ключи Yandex + `PUBLIC_BASE_URL`
   (LAN-IP машины, видимый часам — узнать через `ipconfig getifaddr en0`).
2. `docker compose up --build`
3. Проверка с тестовым LPCM (16 кГц/моно/16 бит):
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
