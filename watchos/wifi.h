#pragma once
#include "config.h"

// Сервис Wi-Fi (не зависит от UI). Приложение `Wi-Fi` — тонкий UI поверх него,
// будущее «радио» использует wifiAcquire/wifiAutoConnect/wifiRelease.

#define WIFI_MAX_KNOWN 8
#define WIFI_SSID_LEN  33
#define WIFI_PASS_LEN  65

struct WifiNet {
    char    ssid[WIFI_SSID_LEN];
    int8_t  rssi;
    bool    secured;
    bool    saved;
};

void  wifiBegin();                 // загрузить известные сети из NVS (радио выключено)

// Счётчик потребителей: радио включается при 0→1, выключается при 1→0.
void  wifiAcquire();
void  wifiRelease();
bool  wifiActive();                // радио включено (для иконки в статусбаре)

bool  wifiConnected();
const char *wifiCurrentSsid();     // SSID текущего подключения ("" если нет)
int   wifiRssi();                  // RSSI текущего подключения (dBm; -100 если нет связи)

int   wifiScan(WifiNet *out, int maxN);                           // синхронный скан
bool  wifiConnect(const char *ssid, const char *pass, uint32_t timeoutMs);
void  wifiDisconnect();
// Для радио: подключиться к сильнейшей известной сети. onTry (если задан) вызывается
// с SSID перед каждой попыткой — для индикации «connecting: X».
bool  wifiAutoConnect(uint32_t timeoutMs, void (*onTry)(const char *ssid) = nullptr);

// Хранилище известных сетей (NVS / Preferences).
bool  wifiIsSaved(const char *ssid);
bool  wifiSavedPassword(const char *ssid, char *out, int outLen);
void  wifiSave(const char *ssid, const char *pass);
void  wifiForget(const char *ssid);
