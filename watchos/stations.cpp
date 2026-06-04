#include "stations.h"
#include <Arduino.h>
#include <SPIFFS.h>

#define MAX_STATIONS 32
#define URL_LEN      200

static char urls[MAX_STATIONS][URL_LEN];
static int  count = 0;

static void loadList()
{
    count = 0;
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

void stationsBegin()
{
    if (!SPIFFS.begin(true)) return;          // true — отформатировать, если ФС нет
    loadList();
}

void stationsReload() { loadList(); }         // перечитать после правки веб-редактором

int stationsCount() { return count; }

const char *stationsUrl(int i) { return (i >= 0 && i < count) ? urls[i] : ""; }
