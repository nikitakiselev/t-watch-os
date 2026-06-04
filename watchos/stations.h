#pragma once

// Список станций из /stations.txt (SPIFFS). Файл заливается отдельно: make fs.
void  stationsBegin();             // смонтировать SPIFFS и загрузить список
void  stationsReload();            // перечитать /stations.txt (после правки веб-редактором)
int   stationsCount();
const char *stationsUrl(int i);
