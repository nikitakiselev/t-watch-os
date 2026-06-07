#pragma once
#include <stdint.h>

// Учёт времени работы от батареи: отсчёт ведётся с момента, когда часы сняли
// с зарядки (отключили USB). Базовая метка (UNIX-эпоха RTC) хранится в NVS,
// поэтому переживает перезагрузки и перепрошивку. Сон CPU не мешает — время
// берём из RTC, а не из millis().

void     battTimeBegin();             // инициализация (после hwBegin: RTC готов)
void     battTimePoll();              // вызывать в loop(); сам троттлит опрос
bool     battTimeCharging();          // сейчас подключён USB?
uint32_t battTimeSinceChargeSec();    // секунд с момента отключения USB (0 на зарядке)
