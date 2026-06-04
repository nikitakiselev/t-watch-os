#pragma once

// Сервис звука: интернет-радио через ESP32-audioI2S. Объектом Audio владеет
// отдельная FreeRTOS-задача (декодеру нужно постоянное обслуживание), команды
// передаются через флаги. I2S → MAX98357A (BCK 26, WS 25, DOUT 33).
void  audioBegin();                    // создать задачу, настроить I2S
void  audioStart(const char *url);     // начать поток
void  audioStop();                     // остановить
void  audioSetVolume(int vol);         // 0..21
int   audioVolume();
bool  audioIsPlaying();
const char *audioTitle();              // ICY stream title ("" если нет)

// Короткий beep через I2S (игровые эффекты). Блокирует на durMs.
// Использовать, когда радио не играет (напр. в игре).
void  soundBeep(int freq, int durMs);

// Проиграть звук-файл из SPIFFS (например "/sfx/hit.wav"). Асинхронно.
// Возвращает false, если файла нет — тогда сыграй beep-фоллбэк.
bool  soundPlayFile(const char *path);
