#ifndef LOGO_H
#define LOGO_H

#include <stdint.h>

// Размер логотипа
#define LOGO_WIDTH 256
#define LOGO_HEIGHT 256

// Цвет логотипа
#define LOGO_COLOR 0x3F47CC  // Синий PozitronOS

// Настройки анимации
#define STRETCH_X 1.25f          // Растяжение по ширине
#define DISPLAY_TIME 20000       // Время показа (20 секунд в мс)
#define FADE_STEPS 30            // Шагов анимации
#define FADE_DELAY 10000         // Задержка между шагами (мкс)

// Объявления функций
void show_boot_logo(void);
void draw_logo(uint32_t x, uint32_t y);
void draw_test_logo(uint32_t x, uint32_t y);

#endif