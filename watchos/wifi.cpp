#include "wifi.h"
#include <WiFi.h>
#include <Preferences.h>
#include <string.h>

// ─────────────────── Известные сети (в RAM, зеркало NVS) ───────────────────
struct Known { char ssid[WIFI_SSID_LEN]; char pass[WIFI_PASS_LEN]; };
static Known    known[WIFI_MAX_KNOWN];
static int      knownCount = 0;

static int      refCount = 0;
static char     curSsid[WIFI_SSID_LEN] = "";

static int findKnown(const char *ssid)
{
    for (int i = 0; i < knownCount; i++)
        if (strncmp(known[i].ssid, ssid, WIFI_SSID_LEN) == 0) return i;
    return -1;
}

static void persist()
{
    Preferences p;
    p.begin("wnets", false);
    p.clear();
    p.putInt("n", knownCount);
    char k[8];
    for (int i = 0; i < knownCount; i++) {
        snprintf(k, sizeof(k), "s%d", i); p.putString(k, known[i].ssid);
        snprintf(k, sizeof(k), "p%d", i); p.putString(k, known[i].pass);
    }
    p.end();
}

void wifiBegin()
{
    Preferences p;
    p.begin("wnets", true);
    knownCount = p.getInt("n", 0);
    if (knownCount > WIFI_MAX_KNOWN) knownCount = WIFI_MAX_KNOWN;
    char k[8];
    for (int i = 0; i < knownCount; i++) {
        snprintf(k, sizeof(k), "s%d", i); p.getString(k, "").toCharArray(known[i].ssid, WIFI_SSID_LEN);
        snprintf(k, sizeof(k), "p%d", i); p.getString(k, "").toCharArray(known[i].pass, WIFI_PASS_LEN);
    }
    p.end();

    WiFi.mode(WIFI_OFF);
    refCount = 0;
}

// ─────────────────── Включение по счётчику потребителей ───────────────────
void wifiAcquire()
{
    if (refCount++ == 0) {
        WiFi.mode(WIFI_STA);
        WiFi.setAutoReconnect(true);   // ОС сама поднимет линк после light-sleep (модем гас)
        WiFi.disconnect(false);
    }
}

void wifiRelease()
{
    if (refCount > 0 && --refCount == 0) {
        WiFi.disconnect(true);     // отключиться и выключить радио
        WiFi.mode(WIFI_OFF);
        curSsid[0] = 0;
    }
}

bool wifiActive()    { return refCount > 0; }
bool wifiConnected() { return WiFi.status() == WL_CONNECTED; }

const char *wifiCurrentSsid()
{
    if (wifiConnected()) { WiFi.SSID().toCharArray(curSsid, WIFI_SSID_LEN); return curSsid; }
    return "";
}

int wifiRssi()
{
    return wifiConnected() ? (int)WiFi.RSSI() : -100;
}

// ─────────────────── Скан / подключение ───────────────────
int wifiScan(WifiNet *out, int maxN)
{
    int n = WiFi.scanNetworks();          // синхронно (несколько секунд)
    if (n < 0) n = 0;
    int cnt = 0;
    for (int i = 0; i < n; i++) {
        char ssid[WIFI_SSID_LEN];
        WiFi.SSID(i).toCharArray(ssid, WIFI_SSID_LEN);
        if (ssid[0] == 0) continue;                     // пропускаем скрытые/пустые
        int8_t rssi = (int8_t)WiFi.RSSI(i);

        // Дедуп по SSID: один и тот же SSID часто на нескольких каналах/BSSID.
        int dup = -1;
        for (int j = 0; j < cnt; j++)
            if (strncmp(out[j].ssid, ssid, WIFI_SSID_LEN) == 0) { dup = j; break; }
        if (dup >= 0) {
            if (rssi > out[dup].rssi) out[dup].rssi = rssi;   // оставляем сильнейший
            continue;
        }
        if (cnt >= maxN) continue;

        strncpy(out[cnt].ssid, ssid, WIFI_SSID_LEN);
        out[cnt].rssi    = rssi;
        out[cnt].secured = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
        out[cnt].saved   = (findKnown(ssid) >= 0);
        cnt++;
    }
    WiFi.scanDelete();
    return cnt;
}

bool wifiConnect(const char *ssid, const char *pass, uint32_t timeoutMs)
{
    WiFi.begin(ssid, (pass && pass[0]) ? pass : nullptr);
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start) < timeoutMs) {
        delay(100);
    }
    return WiFi.status() == WL_CONNECTED;
}

void wifiDisconnect()
{
    WiFi.disconnect(false);       // радио оставляем включённым (счётчик > 0)
    curSsid[0] = 0;
}

bool wifiAutoConnect(uint32_t timeoutMs, void (*onTry)(const char *ssid))
{
    WifiNet nets[24];
    int n = wifiScan(nets, 24);

    // Перебираем известные сети по убыванию сигнала.
    for (;;) {
        int best = -1;
        for (int i = 0; i < n; i++) {
            if (!nets[i].saved) continue;
            if (best < 0 || nets[i].rssi > nets[best].rssi) best = i;
        }
        if (best < 0) return false;                 // известных в эфире нет

        if (onTry) onTry(nets[best].ssid);
        char pass[WIFI_PASS_LEN];
        wifiSavedPassword(nets[best].ssid, pass, sizeof(pass));
        if (wifiConnect(nets[best].ssid, pass, timeoutMs)) return true;

        nets[best].saved = false;                   // не вышло — исключаем, пробуем следующую
    }
}

// ─────────────────── Хранилище ───────────────────
bool wifiIsSaved(const char *ssid) { return findKnown(ssid) >= 0; }

bool wifiSavedPassword(const char *ssid, char *out, int outLen)
{
    int i = findKnown(ssid);
    if (i < 0) { if (outLen) out[0] = 0; return false; }
    strncpy(out, known[i].pass, outLen);
    out[outLen - 1] = 0;
    return true;
}

void wifiSave(const char *ssid, const char *pass)
{
    int i = findKnown(ssid);
    if (i < 0) {
        if (knownCount >= WIFI_MAX_KNOWN) return;   // лимит
        i = knownCount++;
        strncpy(known[i].ssid, ssid, WIFI_SSID_LEN); known[i].ssid[WIFI_SSID_LEN - 1] = 0;
    }
    strncpy(known[i].pass, pass, WIFI_PASS_LEN); known[i].pass[WIFI_PASS_LEN - 1] = 0;
    persist();
}

void wifiForget(const char *ssid)
{
    int i = findKnown(ssid);
    if (i < 0) return;
    for (int j = i; j < knownCount - 1; j++) known[j] = known[j + 1];
    knownCount--;
    persist();
}
