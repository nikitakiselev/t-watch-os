/**
 * Простой пример для LilyGo T-Watch-2020 V3.
 * Инициализирует часы (питание AXP202 + дисплей ST7789), включает подсветку
 * и выводит на экран текст со счётчиком секунд.
 *
 * Дисплей: 240x240, ST7789. Подсветка: GPIO15. Питание: AXP202.
 * Стандартный шрифт TFT_eSPI не содержит кириллицы — поэтому текст на латинице.
 */
#include "config.h"

TTGOClass *ttgo = nullptr;

void setup()
{
    Serial.begin(115200);
    Serial.println("T-Watch 2020 V3: start");

    // Создаём объект часов и инициализируем железо:
    // AXP202 (питание), ST7789 (дисплей), подсветку, датчики.
    ttgo = TTGOClass::getWatch();
    ttgo->begin();

    // Включаем подсветку и ставим яркость на максимум (0..255).
    ttgo->openBL();
    ttgo->setBrightness(255);

    TFT_eSPI *tft = ttgo->tft;

    // Поворот так, чтобы кнопка была справа (0..3 — можно поиграться).
    tft->setRotation(0);
    tft->fillScreen(TFT_BLACK);

    // Заголовок по центру.
    tft->setTextDatum(MC_DATUM);            // выравнивание по центру строки
    tft->setTextColor(TFT_CYAN, TFT_BLACK);
    tft->setTextSize(2);
    tft->drawString("Hello,", 120, 90);
    tft->drawString("T-Watch!", 120, 120);

    // Рамка для красоты.
    tft->drawRect(8, 8, 224, 224, TFT_DARKGREY);

    Serial.println("Setup done");
}

void loop()
{
    static uint32_t seconds = 0;
    TFT_eSPI *tft = ttgo->tft;

    // Обновляем счётчик секунд внизу экрана.
    char buf[24];
    snprintf(buf, sizeof(buf), "uptime: %lu s", (unsigned long)seconds);

    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(TFT_GREEN, TFT_BLACK);
    tft->setTextSize(2);
    // Подложка, чтобы старый текст не «наслаивался».
    tft->fillRect(20, 165, 200, 24, TFT_BLACK);
    tft->drawString(buf, 120, 177);

    seconds++;
    delay(1000);
}
