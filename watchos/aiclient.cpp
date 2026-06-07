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

// API-ключ — 2-я строка /ai_server.txt (1-я — URL). Пусто, если не задан.
String aiApiKey()
{
    String key;
    File f = SPIFFS.open("/ai_server.txt", "r");
    if (f) {
        f.readStringUntil('\n');           // пропустить URL
        key = f.readStringUntil('\n');     // ключ
        f.close();
        key.trim();
    }
    return key;
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
    String key = aiApiKey();
    if (key.length()) http.addHeader("X-API-Key", key);
    http.setTimeout(20000);                     // сервер STT+GPT+TTS ~5-10 c

    int code = http.POST((uint8_t *)pcm, (size_t)samples * sizeof(int16_t));
    r.code = code;
    if (code == 200) r.url = extractUrl(http.getString());
    http.end();
    return r;
}

// Достать значение "text" из тела {"text":"..."} с минимальным un-escape
// (\" \\ \n). Сервер обязан слать сырой UTF-8 без \uXXXX.
static String extractText(const String &body)
{
    int k = body.indexOf("\"text\"");
    if (k < 0) return "";
    int c = body.indexOf(':', k);
    if (c < 0) return "";
    int q1 = body.indexOf('"', c + 1);
    if (q1 < 0) return "";
    String out;
    for (int i = q1 + 1; i < (int)body.length(); i++) {
        char ch = body[i];
        if (ch == '\\') {
            i++;
            if (i >= (int)body.length()) break;
            char e = body[i];
            if      (e == 'n') out += '\n';
            else if (e == 't') out += ' ';
            else               out += e;     // \" \\ и прочее — как есть
            continue;
        }
        if (ch == '"') break;                 // конец строки JSON
        out += ch;
    }
    return out;
}

AiText aiTranscribe(const char *uuid, const int16_t *pcm, int samples)
{
    AiText r = { 0, "" };

    String url = aiServerUrl() + "/stt?session=" + uuid;

    WiFiClient       plain;
    WiFiClientSecure secure;
    WiFiClient      *client = &plain;
    if (url.startsWith("https://")) { secure.setInsecure(); client = &secure; }

    HTTPClient http;
    if (!http.begin(*client, url)) return r;
    http.addHeader("Content-Type", "application/octet-stream");
    String key = aiApiKey();
    if (key.length()) http.addHeader("X-API-Key", key);
    http.setTimeout(20000);

    int code = http.POST((uint8_t *)pcm, (size_t)samples * sizeof(int16_t));
    r.code = code;
    if (code == 200) r.text = extractText(http.getString());
    http.end();
    return r;
}
