#include "drivers/vga.h"
#include "kernel/ports.h"

static uint16_t* vga_buffer = (uint16_t*)0xB8000;
static uint32_t cursor_x = 0;
static uint32_t cursor_y = 0;
static uint8_t current_color = 0x07;

void vga_init(void) {
    cursor_x = 0;
    cursor_y = 0;
    current_color = vga_entry_color(VGA_COLOR_LIGHT_GRAY, VGA_COLOR_BLACK);
    vga_clear();
}

void vga_clear(void) {
    vga_clear_color(current_color);
}

void vga_clear_color(uint8_t color) {
    uint16_t blank = vga_entry(' ', color);
    for (uint32_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga_buffer[i] = blank;
    }
    cursor_x = 0;
    cursor_y = 0;
}

void vga_set_color(uint8_t color) {
    current_color = color;
}

void vga_putchar(char c) {
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
        if (cursor_y >= VGA_HEIGHT) {
            cursor_y = VGA_HEIGHT - 1;
        }
        return;
    }
    
    vga_buffer[cursor_y * VGA_WIDTH + cursor_x] = vga_entry(c, current_color);
    
    cursor_x++;
    if (cursor_x >= VGA_WIDTH) {
        cursor_x = 0;
        cursor_y++;
        if (cursor_y >= VGA_HEIGHT) {
            cursor_y = VGA_HEIGHT - 1;
        }
    }
}

void vga_puts(const char* str) {
    while (*str) {
        vga_putchar(*str++);
    }
}

// Функции для GUI

void vga_putchar_at(char c, uint8_t color, uint32_t x, uint32_t y) {
    if (x >= VGA_WIDTH || y >= VGA_HEIGHT) return;
    vga_buffer[y * VGA_WIDTH + x] = vga_entry(c, color);
}

void vga_puts_at(const char* str, uint8_t color, uint32_t x, uint32_t y) {
    uint32_t orig_x = x;
    while (*str) {
        if (*str == '\n') {
            y++;
            x = orig_x;
        } else {
            vga_putchar_at(*str, color, x, y);
            x++;
            if (x >= VGA_WIDTH) {
                x = 0;
                y++;
            }
        }
        str++;
    }
}

void vga_draw_hline(uint32_t x, uint32_t y, uint32_t length, uint8_t color) {
    for (uint32_t i = 0; i < length; i++) {
        vga_putchar_at(0xC4, color, x + i, y); // ─
    }
}

void vga_draw_vline(uint32_t x, uint32_t y, uint32_t length, uint8_t color) {
    for (uint32_t i = 0; i < length; i++) {
        vga_putchar_at(0xB3, color, x, y + i); // │
    }
}

void vga_draw_box(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint8_t color) {
    // Углы
    vga_putchar_at(0xDA, color, x, y); // ┌
    vga_putchar_at(0xBF, color, x + width - 1, y); // ┐
    vga_putchar_at(0xC0, color, x, y + height - 1); // └
    vga_putchar_at(0xD9, color, x + width - 1, y + height - 1); // ┘
    
    // Горизонтальные линии
    for (uint32_t i = 1; i < width - 1; i++) {
        vga_putchar_at(0xC4, color, x + i, y); // ─
        vga_putchar_at(0xC4, color, x + i, y + height - 1); // ─
    }
    
    // Вертикальные линии
    for (uint32_t i = 1; i < height - 1; i++) {
        vga_putchar_at(0xB3, color, x, y + i); // │
        vga_putchar_at(0xB3, color, x + width - 1, y + i); // │
    }
    
    // Заливка
    vga_fill_rect(x + 1, y + 1, width - 2, height - 2, color);
}

void vga_fill_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint8_t color) {
    for (uint32_t i = 0; i < height; i++) {
        for (uint32_t j = 0; j < width; j++) {
            vga_putchar_at(' ', color, x + j, y + i);
        }
    }
}

// Функции для курсора
uint16_t vga_get_char_at(uint32_t x, uint32_t y) {
    if (x >= VGA_WIDTH || y >= VGA_HEIGHT) return 0;
    return vga_buffer[y * VGA_WIDTH + x];
}

void vga_save_char(uint32_t x, uint32_t y, uint16_t* saved) {
    if (x >= VGA_WIDTH || y >= VGA_HEIGHT) return;
    *saved = vga_buffer[y * VGA_WIDTH + x];
}

void vga_restore_char(uint32_t x, uint32_t y, uint16_t saved) {
    if (x >= VGA_WIDTH || y >= VGA_HEIGHT) return;
    vga_buffer[y * VGA_WIDTH + x] = saved;
}

void vga_draw_cursor(uint32_t x, uint32_t y) {
    if (x >= VGA_WIDTH || y >= VGA_HEIGHT) return;
    vga_putchar_at(0xDB, vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK), x, y);
}

void vga_putchar_safe(char c, uint8_t color, uint32_t x, uint32_t y) {
    // Если пытаемся нарисовать что-то поверх курсора - игнорируем
    // (курсор сам сохраняет и восстанавливает фон)
    volatile uint16_t* vga = (volatile uint16_t*)0xB8000;
    uint16_t current = vga[y * VGA_WIDTH + x];
    
    // Если здесь курсор (символ 255) - не трогаем
    if ((current & 0xFF) == 255) {
        return;
    }
    
    // Иначе рисуем нормально
    vga_putchar_at(c, color, x, y);
}