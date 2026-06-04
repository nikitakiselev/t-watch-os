#pragma once
#include <Arduino.h>

// Веб-редактор станций: поднимает HTTP-сервер на текущем Wi-Fi-адресе часов.
// Страница позволяет править /stations.txt и сохранять (с перезагрузкой списка).
// Сервер ОБЯЗАН выключаться при выходе из радио (webEditStop в radioExit).
void   webEditStart();      // поднять сервер (Wi-Fi уже подключён)
void   webEditStop();       // остановить и освободить
bool   webEditRunning();
void   webEditTick();       // опрашивать в loop, пока сервер поднят
String webEditIP();         // адрес для браузера (192.168.x.x)
