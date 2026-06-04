#pragma once
#include <stdint.h>

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

// Спектр: последние SPEC_N сэмплов моно-микса из декодера (для FFT-анализатора радио).
#define SPEC_N 64
void     audioSpectrumCopy(int16_t *out);  // копирует SPEC_N сэмплов (старые→новые)
uint32_t audioSampleRate();                // частота дискретизации текущего потока (0, если нет)

// Короткий beep через I2S (игровые эффекты). Блокирует на durMs.
// Использовать, когда радио не играет (напр. в игре).
void  soundBeep(int freq, int durMs);

// Проиграть звук-файл из SPIFFS (например "/sfx/hit.wav"). Асинхронно.
// Возвращает false, если файла нет — тогда сыграй beep-фоллбэк.
bool  soundPlayFile(const char *path);

// ── Захват PDM-микрофона (T-Watch V3: clk GPIO0, data GPIO2) ──
// Делит I2S0 с выводом звука: запись и воспроизведение НЕ одновременно.
// micCaptureBegin переключает I2S0 в PDM RX, micCaptureEnd возвращает в TX.
bool micCaptureBegin();                       // false при неудаче установки драйвера
int  micCaptureRead(int16_t *dst, int maxSamples);   // выгрести доступные сэмплы (неблокирующе)
void micCaptureEnd();                         // вернуть I2S0 в TX (для воспроизведения)
