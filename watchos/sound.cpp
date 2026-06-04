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

// Пины PDM-микрофона T-Watch 2020 V3.
static const int MIC_DATA  = 2;     // GPIO2 — PDM data (data_in)
static const int MIC_CLOCK = 0;     // GPIO0 — PDM clock (на ws_io_num)

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

// Парность begin/end: защищает от двойного begin и осиротевшего suspend
// (иначе незакрытая запись навсегда оставит аудио-задачу suspended → звука нет до ребута).
static bool micActive = false;

// Установить I2S0 в режим PDM RX (приостановив аудио-задачу, владеющую I2S0).
bool micCaptureBegin()
{
    if (micActive) return false;             // уже идёт запись — не дублируем suspend
    if (taskH) vTaskSuspend(taskH);
    i2s_driver_uninstall(I2S_NUM_0);

    i2s_config_t cfg = {};
    cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM);
    cfg.sample_rate = 16000;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB);
    cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count = 4;
    cfg.dma_buf_len = 256;
    if (i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL) != ESP_OK) {
        if (taskH) vTaskResume(taskH);
        return false;
    }
    i2s_pin_config_t pin = {};
    pin.bck_io_num = I2S_PIN_NO_CHANGE;
    pin.ws_io_num = MIC_CLOCK;
    pin.data_out_num = I2S_PIN_NO_CHANGE;
    pin.data_in_num = MIC_DATA;
    i2s_set_pin(I2S_NUM_0, &pin);
    i2s_set_clk(I2S_NUM_0, 16000, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_MONO);
    micActive = true;
    return true;
}

// Выгрести доступные сэмплы (неблокирующе: таймаут 0). Возвращает число int16-сэмплов.
int micCaptureRead(int16_t *dst, int maxSamples)
{
    size_t bytesRead = 0;
    i2s_read(I2S_NUM_0, (char *)dst, maxSamples * sizeof(int16_t), &bytesRead, 0);
    return (int)(bytesRead / sizeof(int16_t));
}

// Вернуть I2S0 в TX-конфиг библиотеки Audio и пины динамика, resume аудио-задачи.
void micCaptureEnd()
{
    if (!micActive) return;                  // не было записи — нечего восстанавливать
    micActive = false;
    i2s_driver_uninstall(I2S_NUM_0);

    i2s_config_t cfg = {};
    cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate = 16000;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_STAND_I2S);  // ядро 2.0.17
    cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count = 8;
    cfg.dma_buf_len = 1024;
    cfg.use_apll = false;
    cfg.tx_desc_auto_clear = true;
    cfg.fixed_mclk = I2S_PIN_NO_CHANGE;
    i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL);

    audio.setPinout(I2S_BCK, I2S_WS, I2S_DOUT);   // вернуть пины динамика
    audio.setVolume((uint8_t)volume);

    if (taskH) vTaskResume(taskH);
}
