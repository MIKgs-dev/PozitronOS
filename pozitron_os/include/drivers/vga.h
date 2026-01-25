#ifndef VGA_H
#define VGA_H

#include "../kernel/types.h"

#define VGA_WIDTH 80
#define VGA_HEIGHT 25

// Цвета VGA
enum vga_color {
    VGA_COLOR_BLACK = 0,
    VGA_COLOR_BLUE = 1,
    VGA_COLOR_GREEN = 2,
    VGA_COLOR_CYAN = 3,
    VGA_COLOR_RED = 4,
    VGA_COLOR_MAGENTA = 5,
    VGA_COLOR_BROWN = 6,
    VGA_COLOR_LIGHT_GRAY = 7,
    VGA_COLOR_DARK_GRAY = 8,
    VGA_COLOR_LIGHT_BLUE = 9,
    VGA_COLOR_LIGHT_GREEN = 10,
    VGA_COLOR_LIGHT_CYAN = 11,
    VGA_COLOR_LIGHT_RED = 12,
    VGA_COLOR_LIGHT_MAGENTA = 13,
    VGA_COLOR_YELLOW = 14,
    VGA_COLOR_WHITE = 15,
};

// Создание цвета VGA
static inline uint8_t vga_entry_color(enum vga_color fg, enum vga_color bg) {
    return fg | bg << 4;
}

// Создание символа VGA
static inline uint16_t vga_entry(unsigned char uc, uint8_t color) {
    return (uint16_t) uc | (uint16_t) color << 8;
}

// Базовые функции
void vga_init(void);
void vga_clear(void);
void vga_clear_color(uint8_t color);
void vga_putchar(char c);
void vga_puts(const char* str);

// Функции для GUI
void vga_set_color(uint8_t color);
void vga_putchar_at(char c, uint8_t color, uint32_t x, uint32_t y);
void vga_puts_at(const char* str, uint8_t color, uint32_t x, uint32_t y);
void vga_draw_box(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint8_t color);
void vga_draw_hline(uint32_t x, uint32_t y, uint32_t length, uint8_t color);
void vga_draw_vline(uint32_t x, uint32_t y, uint32_t length, uint8_t color);
void vga_fill_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint8_t color);

// Получить символ и цвет в позиции
uint16_t vga_get_char_at(uint32_t x, uint32_t y);

// Функции для курсора
void vga_save_char(uint32_t x, uint32_t y, uint16_t* saved);
void vga_restore_char(uint32_t x, uint32_t y, uint16_t saved);
void vga_draw_cursor(uint32_t x, uint32_t y);

#endif