#pragma once

// ───────────────────────────── Платформа ─────────────────────────────
// LilyGo T-Watch-2020 V3
#define LILYGO_WATCH_2020_V3
#include <LilyGoWatch.h>

// ───────────────────────────── Геометрия ─────────────────────────────
#define SCR_W            240
#define SCR_H            240
#define STATUSBAR_H      24
#define NAVBAR_H         36                    // нижняя панель кнопок
#define CONTENT_TOP      STATUSBAR_H
#define CONTENT_BOTTOM   (SCR_H - NAVBAR_H)    // = 204; ниже рисовать нельзя

// ───────────────────────────── Часовые пояса ──────────────────────────
// Время RTC трактуем как базовый пояс (MSK). Остальные — смещения от него.
//   MSK = UTC+3  →  база
//   UTC = MSK-3ч = -180 мин
//   EST = UTC-5  = MSK-8ч = -480 мин   (перевод на лето DST не учитываем)
#define TZ_MSK_OFFSET_MIN   0
#define TZ_UTC_OFFSET_MIN   (-180)
#define TZ_EST_OFFSET_MIN   (-480)

// ───────────────────────────── Ввод (тач) ─────────────────────────────
#define SWIPE_MIN_DELTA  22   // мин. смещение пальца для свайпа, px (меньше — легче свайпнуть)
#define TAP_MAX_DELTA    12   // макс. смещение, при котором касание ещё считается тапом, px

// ───────────────────────────── Сон / энергосбережение ─────────────────────
#define SLEEP_IDLE_MS    15000   // бездействие до ухода в light sleep, мс
