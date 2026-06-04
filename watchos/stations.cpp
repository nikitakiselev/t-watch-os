#include "stations.h"
#include <Arduino.h>
#include <SPIFFS.h>

#define MAX_STATIONS 32
#define URL_LEN      200

static char urls[MAX_STATIONS][URL_LEN];
static int  count = 0;

void stationsBegin()
{
    count = 0;
    if (!SPIFFS.begin(true)) return;          // true — отформатировать, если ФС нет
    File f = SPIFFS.open("/stations.txt", "r");
    if (!f) return;
    while (f.available() && count < MAX_STATIONS) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0 || line[0] == '#') continue;
        line.toCharArray(urls[count], URL_LEN);
        count++;
    }
    f.close();
}

int stationsCount() { return count; }

const char *stationsUrl(int i) { return (i >= 0 && i < count) ? urls[i] : ""; }
