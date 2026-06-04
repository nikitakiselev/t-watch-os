#include "aiclient.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <SPIFFS.h>

static const char *DEFAULT_SERVER = "http://twatch.nikitakiselev.ru";

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

    String url = aiServerUrl() + "/talk?session=" + uuid;

    // Scheme-aware: для http:// — обычный WiFiClient (без TLS, без mbedTLS-кучи).
    // TLS на ESP32 жрёт ~32 КБ, а аудио-библиотека держит свой TLS-контекст после
    // HTTPS-плейбека → два контекста не влезают. На LAN-HTTP проблема исчезает.
    WiFiClient       plain;
    WiFiClientSecure secure;
    WiFiClient      *client = &plain;
    if (url.startsWith("https://")) { secure.setInsecure(); client = &secure; }

    HTTPClient http;
    if (!http.begin(*client, url)) return r;
    http.addHeader("Content-Type", "application/octet-stream");
    http.setTimeout(20000);                     // сервер STT+GPT+TTS ~5-10 c

    int code = http.POST((uint8_t *)pcm, (size_t)samples * sizeof(int16_t));
    r.code = code;
    if (code == 200) r.url = extractUrl(http.getString());
    http.end();
    return r;
}
