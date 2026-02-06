#include "drivers/vesa.h"
#include "drivers/serial.h"
#include <stddef.h>
#include "kernel/multiboot.h"

// Статические переменные для состояния курсора
static uint32_t cursor_backup[16 * 16];
static uint32_t cursor_x = 400;
static uint32_t cursor_y = 300;
static uint8_t cursor_visible = 1;
static uint8_t cursor_enabled = 1;
static uint8_t cursor_need_update = 1;
static uint8_t cursor_is_drawn = 0;
static uint32_t last_cursor_x = 0;
static uint32_t last_cursor_y = 0;

// Битмап курсора
static const uint16_t cursor_bitmap[] = {
    0b1000000000000000,
    0b1100000000000000,
    0b1110000000000000,
    0b1111000000000000,
    0b1111100000000000,
    0b1111110000000000,
    0b1111111000000000,
    0b1111111100000000,
    0b1111111110000000,
    0b1111111000000000,
    0b1110110000000000,
    0b1100111000000000,
    0b1000111000000000,
    0b0000011100000000,
    0b0000011100000000,
    0b0000001100000000
};

// Сохраняем фон под курсором
static void cursor_save_background(uint32_t x, uint32_t y) {
    struct fb_info* fb = vesa_get_info();
    if(!fb || !fb->found || !cursor_enabled) return;
    
    // Если курсор полностью за пределами экрана, ничего не сохраняем
    if (x >= fb->width || y >= fb->height) {
        // Очищаем backup
        for(int i = 0; i < 16 * 16; i++) {
            cursor_backup[i] = 0;
        }
        return;
    }
    
    uint32_t* buffer = vesa_is_double_buffer_enabled() ? vesa_get_back_buffer() : fb->address;
    if (!buffer) return;
    
    for(int dy = 0; dy < 16; dy++) {
        uint32_t py = y + dy;
        
        // Проверяем границы для строки
        if(py >= fb->height) {
            // Если строка за пределами экрана, заполняем нулями
            for(int dx = 0; dx < 16; dx++) {
                cursor_backup[dy * 16 + dx] = 0;
            }
            continue;
        }
        
        for(int dx = 0; dx < 16; dx++) {
            uint32_t px = x + dx;
            
            // Проверяем границы для столбца
            if(px >= fb->width) {
                cursor_backup[dy * 16 + dx] = 0;
                continue;
            }
            
            cursor_backup[dy * 16 + dx] = buffer[py * fb->width + px];
        }
    }
}

// Восстанавливаем фон под курсором
static void cursor_restore_background(uint32_t x, uint32_t y) {
    struct fb_info* fb = vesa_get_info();
    if(!fb || !fb->found || !cursor_enabled || !cursor_is_drawn) return;
    
    // Если курсор был полностью за пределами экрана, нечего восстанавливать
    if (x >= fb->width || y >= fb->height) {
        cursor_is_drawn = 0;
        return;
    }
    
    uint32_t* buffer = vesa_is_double_buffer_enabled() ? vesa_get_back_buffer() : fb->address;
    if (!buffer) return;
    
    for(int dy = 0; dy < 16; dy++) {
        uint32_t py = y + dy;
        
        if(py >= fb->height) continue;
        
        for(int dx = 0; dx < 16; dx++) {
            uint32_t px = x + dx;
            
            if(px < fb->width && py < fb->height) {
                buffer[py * fb->width + px] = cursor_backup[dy * 16 + dx];
            }
        }
    }
    
    cursor_is_drawn = 0;
}

// Рисуем курсор
static void cursor_draw(uint32_t x, uint32_t y) {
    if (!cursor_visible || !cursor_enabled) return;
    
    struct fb_info* fb = vesa_get_info();
    if(!fb || !fb->found) return;
    
    // Проверяем, находится ли курсор хотя бы частично в пределах экрана
    if (x >= fb->width || y >= fb->height) return;
    
    // Сохраняем фон
    cursor_save_background(x, y);
    
    uint32_t* buffer = vesa_is_double_buffer_enabled() ? vesa_get_back_buffer() : fb->address;
    if (!buffer) return;
    
    // Сначала рисуем черную обводку
    for(int dy = 0; dy < 16; dy++) {
        uint16_t row = cursor_bitmap[dy];
        uint32_t py = y + dy;
        
        // Проверяем чтобы строка была в пределах экрана
        if(py >= fb->height) continue;
        
        for(int dx = 0; dx < 16; dx++) {
            if(row & (1 << (15 - dx))) {
                uint32_t px = x + dx;
                
                // Проверяем чтобы пиксель был в пределах экрана
                if(px >= fb->width) continue;
                
                // Рисуем черную обводку вокруг белого курсора
                // Основной черный контур (смещение +1)
                if(px + 1 < fb->width) buffer[py * fb->width + (px + 1)] = 0x000000;
                if(py + 1 < fb->height) buffer[(py + 1) * fb->width + px] = 0x000000;
                
                // Дополнительный контур для лучшей видимости
                if(px > 0) buffer[py * fb->width + (px - 1)] = 0x000000;
                if(py > 0) buffer[(py - 1) * fb->width + px] = 0x000000;
            }
        }
    }
    
    // Затем рисуем белый курсор поверх
    for(int dy = 0; dy < 16; dy++) {
        uint16_t row = cursor_bitmap[dy];
        uint32_t py = y + dy;
        
        if(py >= fb->height) continue;
        
        for(int dx = 0; dx < 16; dx++) {
            if(row & (1 << (15 - dx))) {
                uint32_t px = x + dx;
                
                if(px < fb->width && py < fb->height) {
                    buffer[py * fb->width + px] = 0xFFFFFF;
                }
            }
        }
    }
    
    last_cursor_x = x;
    last_cursor_y = y;
    cursor_is_drawn = 1;
}

// Инициализация системы курсора
void vesa_cursor_init(void) {
    struct fb_info* fb = vesa_get_info();
    
    if (fb && fb->found) {
        // Помещаем курсор в центр экрана
        cursor_x = fb->width / 2;
        cursor_y = fb->height / 2;
    } else {
        // Запасные значения на случай если VESA ещё не инициализирован
        cursor_x = 400;
        cursor_y = 300;
    }
    
    cursor_visible = 1;
    cursor_enabled = 1;
    cursor_need_update = 1;
    cursor_is_drawn = 0;
    last_cursor_x = 0;
    last_cursor_y = 0;
    
    if (fb && fb->found) {
        serial_puts_num(cursor_x);
        serial_puts("x");
        serial_puts_num(cursor_y);
    }
}

// Обновление курсора
void vesa_cursor_update(void) {
    if (!cursor_enabled) return;
    
    struct fb_info* fb = vesa_get_info();
    if(!fb || !fb->found) return;
    
    // === ИСПРАВЛЕННАЯ ВЕРСИЯ ===
    // Ограничиваем только отрицательные значения
    if(cursor_x < 0) cursor_x = 0;
    if(cursor_y < 0) cursor_y = 0;
    
    // УБЕРАЕМ ограничения по правому и нижнему краю!
    // Вместо этого позволяем курсору доходить до fb->width и fb->height
    // (проверка границ будет в cursor_draw())
    
    // Если нужно обновить или позиция изменилась
    if (cursor_need_update || cursor_x != last_cursor_x || cursor_y != last_cursor_y) {
        // Восстанавливаем старый фон
        if (cursor_is_drawn) {
            cursor_restore_background(last_cursor_x, last_cursor_y);
        }
        
        // Рисуем курсор в новой позиции (он сам проверит границы)
        if (cursor_visible) {
            cursor_draw(cursor_x, cursor_y);
        }
        
        cursor_need_update = 0;
    }
}

// Публичные функции
void vesa_set_cursor_pos(uint32_t x, uint32_t y) {
    struct fb_info* fb = vesa_get_info();
    
    if (cursor_x != x || cursor_y != y) {
        cursor_x = x;
        cursor_y = y;
        cursor_need_update = 1;
    }
}

void vesa_draw_cursor(uint32_t x, uint32_t y) {
    vesa_set_cursor_pos(x, y);
    cursor_visible = 1;
    cursor_need_update = 1;
}

void vesa_hide_cursor(void) {
    if (cursor_visible) {
        if (cursor_is_drawn) {
            cursor_restore_background(last_cursor_x, last_cursor_y);
        }
        cursor_visible = 0;
    }
}

void vesa_show_cursor(void) {
    if (!cursor_visible) {
        cursor_visible = 1;
        cursor_need_update = 1;
    }
}

void vesa_get_cursor_pos(uint32_t* x, uint32_t* y) {
    if(x) *x = cursor_x;
    if(y) *y = cursor_y;
}

uint8_t vesa_cursor_is_visible(void) {
    return cursor_visible && cursor_is_drawn;
}

void vesa_cursor_set_visible(uint8_t visible) {
    if (cursor_visible != visible) {
        cursor_visible = visible;
        cursor_need_update = 1;
    }
}

void vesa_cursor_enable(uint8_t enable) {
    if (cursor_enabled != enable) {
        cursor_enabled = enable;
        cursor_need_update = 1;
    }
}

uint8_t vesa_cursor_is_enabled(void) {
    return cursor_enabled;
}

void vesa_cursor_get_state(uint32_t* x, uint32_t* y, uint8_t* visible, uint8_t* enabled) {
    if(x) *x = cursor_x;
    if(y) *y = cursor_y;
    if(visible) *visible = cursor_visible;
    if(enabled) *enabled = cursor_enabled;
}

void vesa_cursor_get_area(uint32_t* x, uint32_t* y, uint32_t* w, uint32_t* h) {
    if (x) *x = cursor_x;
    if (y) *y = cursor_y;
    if (w) *w = 16;
    if (h) *h = 16;
}