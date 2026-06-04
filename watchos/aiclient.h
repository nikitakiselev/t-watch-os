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
