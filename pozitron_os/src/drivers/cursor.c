#include "drivers/vesa.h"
#include "drivers/serial.h"

static uint32_t cursor_backup[16 * 16];
static uint32_t cursor_x = 400;
static uint32_t cursor_y = 300;
static uint8_t cursor_visible = 1;
static uint8_t cursor_enabled = 1;
static uint8_t cursor_need_update = 1;
static uint8_t cursor_is_drawn = 0;
static uint32_t last_cursor_x = 0;
static uint32_t last_cursor_y = 0;

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
    
    uint32_t bytes_per_pixel = fb->bpp / 8;
    uint8_t* buffer;
    
    if (vesa_is_double_buffer_enabled()) {
        buffer = (uint8_t*)vesa_get_back_buffer();
    } else {
        buffer = (uint8_t*)fb->address;
    }
    
    if (!buffer) return;
    
    for(int dy = 0; dy < 16; dy++) {
        uint32_t py = y + dy;
        
        for(int dx = 0; dx < 16; dx++) {
            uint32_t px = x + dx;
            
            if(px < fb->width && py < fb->height) {
                uint32_t offset = py * fb->pitch + px * bytes_per_pixel;
                
                if (bytes_per_pixel == 4) {
                    cursor_backup[dy * 16 + dx] = *(uint32_t*)(buffer + offset);
                } else if (bytes_per_pixel == 3) {
                    uint32_t b = buffer[offset];
                    uint32_t g = buffer[offset + 1];
                    uint32_t r = buffer[offset + 2];
                    cursor_backup[dy * 16 + dx] = (r << 16) | (g << 8) | b;
                } else {
                    cursor_backup[dy * 16 + dx] = 0;
                }
            } else {
                cursor_backup[dy * 16 + dx] = 0;
            }
        }
    }
}

// Восстанавливаем фон под курсором
static void cursor_restore_background(uint32_t x, uint32_t y) {
    struct fb_info* fb = vesa_get_info();
    if(!fb || !fb->found || !cursor_enabled || !cursor_is_drawn) return;
    
    uint32_t bytes_per_pixel = fb->bpp / 8;
    uint8_t* buffer;
    
    if (vesa_is_double_buffer_enabled()) {
        buffer = (uint8_t*)vesa_get_back_buffer();
    } else {
        buffer = (uint8_t*)fb->address;
    }
    
    if (!buffer) return;
    
    for(int dy = 0; dy < 16; dy++) {
        uint32_t py = y + dy;
        if(py >= fb->height) continue;
        
        for(int dx = 0; dx < 16; dx++) {
            uint32_t px = x + dx;
            if(px >= fb->width) continue;
            
            uint32_t offset = py * fb->pitch + px * bytes_per_pixel;
            uint32_t color = cursor_backup[dy * 16 + dx];
            
            if (bytes_per_pixel == 4) {
                *(uint32_t*)(buffer + offset) = color;
            } else if (bytes_per_pixel == 3) {
                buffer[offset] = color & 0xFF;
                buffer[offset + 1] = (color >> 8) & 0xFF;
                buffer[offset + 2] = (color >> 16) & 0xFF;
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
    
    uint32_t bytes_per_pixel = fb->bpp / 8;
    uint8_t* buffer;
    
    if (vesa_is_double_buffer_enabled()) {
        buffer = (uint8_t*)vesa_get_back_buffer();
    } else {
        buffer = (uint8_t*)fb->address;
    }
    
    if (!buffer) return;
    
    // Сохраняем фон
    cursor_save_background(x, y);
    
    // === СНАЧАЛА ЧЁРНАЯ ОБВОДКА ===
    for(int dy = 0; dy < 16; dy++) {
        uint16_t row = cursor_bitmap[dy];
        uint32_t py = y + dy;
        if(py >= fb->height) continue;
        
        for(int dx = 0; dx < 16; dx++) {
            if(row & (1 << (15 - dx))) {
                uint32_t px = x + dx;
                if(px >= fb->width) continue;
                
                uint32_t offset = py * fb->pitch + px * bytes_per_pixel;
                
                // Рисуем чёрный контур вокруг белого
                if(px + 1 < fb->width) {
                    uint32_t offset_r = py * fb->pitch + (px + 1) * bytes_per_pixel;
                    if (bytes_per_pixel == 4) {
                        *(uint32_t*)(buffer + offset_r) = 0x000000;
                    } else {
                        buffer[offset_r] = 0x00;
                        buffer[offset_r + 1] = 0x00;
                        buffer[offset_r + 2] = 0x00;
                    }
                }
                if(py + 1 < fb->height) {
                    uint32_t offset_d = (py + 1) * fb->pitch + px * bytes_per_pixel;
                    if (bytes_per_pixel == 4) {
                        *(uint32_t*)(buffer + offset_d) = 0x000000;
                    } else {
                        buffer[offset_d] = 0x00;
                        buffer[offset_d + 1] = 0x00;
                        buffer[offset_d + 2] = 0x00;
                    }
                }
                if(px > 0) {
                    uint32_t offset_l = py * fb->pitch + (px - 1) * bytes_per_pixel;
                    if (bytes_per_pixel == 4) {
                        *(uint32_t*)(buffer + offset_l) = 0x000000;
                    } else {
                        buffer[offset_l] = 0x00;
                        buffer[offset_l + 1] = 0x00;
                        buffer[offset_l + 2] = 0x00;
                    }
                }
                if(py > 0) {
                    uint32_t offset_u = (py - 1) * fb->pitch + px * bytes_per_pixel;
                    if (bytes_per_pixel == 4) {
                        *(uint32_t*)(buffer + offset_u) = 0x000000;
                    } else {
                        buffer[offset_u] = 0x00;
                        buffer[offset_u + 1] = 0x00;
                        buffer[offset_u + 2] = 0x00;
                    }
                }
            }
        }
    }
    
    // === ПОТОМ БЕЛЫЙ КУРСОР ===
    for(int dy = 0; dy < 16; dy++) {
        uint16_t row = cursor_bitmap[dy];
        uint32_t py = y + dy;
        if(py >= fb->height) continue;
        
        for(int dx = 0; dx < 16; dx++) {
            if(row & (1 << (15 - dx))) {
                uint32_t px = x + dx;
                if(px >= fb->width) continue;
                
                uint32_t offset = py * fb->pitch + px * bytes_per_pixel;
                
                if (bytes_per_pixel == 4) {
                    *(uint32_t*)(buffer + offset) = 0xFFFFFF;
                } else {
                    buffer[offset] = 0xFF;
                    buffer[offset + 1] = 0xFF;
                    buffer[offset + 2] = 0xFF;
                }
            }
        }
    }
    
    last_cursor_x = x;
    last_cursor_y = y;
    cursor_is_drawn = 1;
}

// Остальные функции без изменений
void vesa_cursor_init(void) {
    struct fb_info* fb = vesa_get_info();
    
    if (fb && fb->found) {
        cursor_x = fb->width / 2;
        cursor_y = fb->height / 2;
    } else {
        cursor_x = 400;
        cursor_y = 300;
    }
    
    cursor_visible = 1;
    cursor_enabled = 1;
    cursor_need_update = 1;
    cursor_is_drawn = 0;
}

void vesa_cursor_update(void) {
    if (!cursor_enabled) return;
    
    struct fb_info* fb = vesa_get_info();
    if(!fb || !fb->found) return;
    
    if(cursor_x < 0) cursor_x = 0;
    if(cursor_y < 0) cursor_y = 0;
    if(cursor_x >= fb->width) cursor_x = fb->width - 1;
    if(cursor_y >= fb->height) cursor_y = fb->height - 1;
    
    if (cursor_need_update || cursor_x != last_cursor_x || cursor_y != last_cursor_y) {
        if (cursor_is_drawn) {
            cursor_restore_background(last_cursor_x, last_cursor_y);
        }
        
        if (cursor_visible) {
            cursor_draw(cursor_x, cursor_y);
        }
        
        cursor_need_update = 0;
    }
}

// Остальные функции без изменений
void vesa_set_cursor_pos(uint32_t x, uint32_t y) {
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

void vesa_cursor_get_area(uint32_t* x, uint32_t* y, uint32_t* w, uint32_t* h) {
    if (x) *x = cursor_x;
    if (y) *y = cursor_y;
    if (w) *w = 16;
    if (h) *h = 16;
}