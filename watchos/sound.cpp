#include "sound.h"
#include "Audio.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/i2s.h>
#include <SPIFFS.h>
#include <string.h>

// Пины I2S для T-Watch 2020 V3 (хардкод, чтобы не подключать LilyGoWatch.h рядом с Audio.h).
static const int I2S_BCK  = 26;
static const int I2S_WS   = 25;
static const int I2S_DOUT = 33;

static Audio        audio;
static TaskHandle_t taskH = nullptr;

// Команды задаче (флаги; объект Audio трогает только задача).
static volatile bool wantPlay = false;
static volatile bool wantStop = false;
static volatile bool wantFile = false;
static volatile int  wantVol  = -1;
static char          pendingUrl[256];
static char          pendingPath[64];

static volatile bool playing = false;
static int           volume  = 12;       // громкость радио (0..21), задаёт пользователь
static const int     SFX_VOL = 6;        // громкость коротких WAV-эффектов (тише ~вдвое)
static char          title[128] = "";

// Метаданные ICY — библиотека зовёт эти слабые функции, если они определены.
void audio_showstreamtitle(const char *info)
{
    strncpy(title, info ? info : "", sizeof(title));
    title[sizeof(title) - 1] = 0;
}

// ─── Захват PCM для спектроанализатора. Библиотека (Audio.cpp/playSample) зовёт
//     audio_pcm_tap на каждый ~4-й сэмпл моно-микса; складываем в кольцевой буфер. ───
static volatile int16_t  capBuf[SPEC_N];
static volatile uint16_t capPos = 0;

void audio_pcm_tap(int16_t mono)
{
    capBuf[capPos] = mono;
    capPos = (capPos + 1) % SPEC_N;
}

void audioSpectrumCopy(int16_t *out)        // старые → новые, начиная с текущей позиции записи
{
    uint16_t p = capPos;
    for (int i = 0; i < SPEC_N; i++) out[i] = capBuf[(p + i) % SPEC_N];
}

static void audioTask(void *)
{
    for (;;) {
        if (wantStop) { audio.stopSong(); playing = false; wantStop = false; }
        if (wantPlay) { wantPlay = false; playing = audio.connecttohost(pendingUrl); }
        if (wantFile) { wantFile = false; playing = audio.connecttoFS(SPIFFS, pendingPath); }
        if (wantVol >= 0) { audio.setVolume((uint8_t)wantVol); wantVol = -1; }

        audio.loop();
        if (playing && !audio.isRunning()) playing = false;   // поток оборвался
        vTaskDelay(1);
    }
}

void audioBegin()
{
    audio.setPinout(I2S_BCK, I2S_WS, I2S_DOUT);
    audio.setVolume((uint8_t)volume);
    xTaskCreatePinnedToCore(audioTask, "audio", 8192, nullptr, 2, &taskH, 1);
}

void audioStart(const char *url)
{
    strncpy(pendingUrl, url, sizeof(pendingUrl));
    pendingUrl[sizeof(pendingUrl) - 1] = 0;
    title[0] = 0;
    wantStop = false;
    wantVol  = volume;        // радио — на пользовательской громкости (после SFX могла измениться)
    wantPlay = true;
}

void audioStop()
{
    wantPlay = false;
    wantStop = true;
}

void audioSetVolume(int v)
{
    if (v < 0)  v = 0;
    if (v > 21) v = 21;
    volume  = v;
    wantVol = v;
}

int  audioVolume()    { return volume; }
bool audioIsPlaying() { return playing; }
const char *audioTitle() { return title; }
uint32_t audioSampleRate() { return audio.getSampleRate(); }

// Проиграть короткий звук из SPIFFS (wav/mp3). Возвращает false, если файла нет
// (вызывающий может сыграть beep-фоллбэк). Воспроизведение асинхронное (задачей).
bool soundPlayFile(const char *path)
{
    if (!SPIFFS.exists(path)) return false;
    strncpy(pendingPath, path, sizeof(pendingPath));
    pendingPath[sizeof(pendingPath) - 1] = 0;
    wantStop = false;
    wantVol  = SFX_VOL;       // эффекты тише радио
    wantFile = true;
    return true;
}

// Прямоугольный тон через I2S (тот же порт 0, что у Audio; драйвер уже установлен
// setPinout). Использовать, когда поток не играет.
void soundBeep(int freq, int durMs)
{
    if (freq <= 0 || durMs <= 0 || playing) return;

    if (taskH) vTaskSuspend(taskH);          // чтобы аудио-задача не мешала I2S
    const int sr = 16000;
    i2s_set_clk(I2S_NUM_0, sr, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);
    i2s_start(I2S_NUM_0);

    int total = sr * durMs / 1000;
    int half  = sr / (2 * freq); if (half < 1) half = 1;
    const int16_t amp = 3000;            // ~вдвое тише
    int16_t cur = amp;
    int phase = 0, written = 0;
    int16_t buf[256 * 2];
    size_t bw;
    while (written < total) {
        int n = 0;
        while (n < 256 && written < total) {
            buf[n * 2] = cur; buf[n * 2 + 1] = cur;
            n++; written++;
            if (++phase >= half) { phase = 0; cur = -cur; }
        }
        i2s_write(I2S_NUM_0, buf, n * 2 * sizeof(int16_t), &bw, portMAX_DELAY);
    }
    // хвост тишины — дать доиграть и не зациклить последний буфер
    memset(buf, 0, sizeof(buf));
    i2s_write(I2S_NUM_0, buf, sizeof(buf), &bw, portMAX_DELAY);

    if (taskH) vTaskResume(taskH);
}
