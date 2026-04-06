#include "drivers/vesa.h"
#include "drivers/serial.h"
#include "drivers/timer.h"
#include "gui/gui.h"

static uint32_t cursor_backup[16 * 16];
static uint32_t cursor_x = 400;
static uint32_t cursor_y = 300;
static uint8_t cursor_visible = 1;
static uint8_t cursor_enabled = 1;
static uint8_t cursor_need_update = 1;
static uint8_t cursor_is_drawn = 0;
static uint32_t last_cursor_x = 0;
static uint32_t last_cursor_y = 0;
static cursor_type_t current_cursor = CURSOR_DEFAULT;
static uint32_t last_window_resize_check = 0;

// ============ БИТМАПЫ КУРСОРОВ ============

// Курсор 1: Обычная стрелка (DEFAULT)
static const uint16_t cursor_arrow[] = {
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

// Курсор 2: Перемещение (MOVE) - 4 стрелки
static const uint16_t cursor_move[] = {
    0b0000000000000000,
    0b0000000110000000,
    0b0000001111000000,
    0b0000011111100000,
    0b0000000110000000,
    0b0010000110000100,
    0b0110000110000110,
    0b1111111111111111,
    0b1111111111111111,
    0b0110000110000110,
    0b0010000110000100,
    0b0000000110000000,
    0b0000011111100000,
    0b0000001111000000,
    0b0000000110000000,
    0b0000000000000000
};

// Курсор 3: Вертикальная стрелка (NS - Север-Юг)
static const uint16_t cursor_ns[] = {
    0b0000000000000000,
    0b0000001000000000,
    0b0000011100000000,
    0b0000111110000000,
    0b0001111111000000,
    0b0011111111100000,
    0b0000011100000000,
    0b0000011100000000,
    0b0000011100000000,
    0b0000011100000000,
    0b0011111111100000,
    0b0001111111000000,
    0b0000111110000000,
    0b0000011100000000,
    0b0000001000000000,
    0b0000000000000000
};

// Курсор 4: Горизонтальная стрелка (EW - Восток-Запад)
static const uint16_t cursor_ew[] = {
    0b0000000000000000,
    0b0000000000000000,
    0b0000000000000000,
    0b0000000000000000,
    0b0001000000001000,
    0b0011000000001100,
    0b0111000000001110,
    0b1111111111111111,
    0b0111000000001110,
    0b0011000000001100,
    0b0001000000001000,
    0b0000000000000000,
    0b0000000000000000,
    0b0000000000000000,
    0b0000000000000000,
    0b0000000000000000
};

// Курсор 5: Диагональ NW-SE (Северо-Запад - Юго-Восток)
static const uint16_t cursor_nwse[] = {
    0b1111110000000000,
    0b1111100000000000,
    0b1111000000000000,
    0b1111000000000000,
    0b1100100000000000,
    0b1000010000000000,
    0b0000001000000000,
    0b0000000100000000,
    0b0000000010000000,
    0b0000000001000000,
    0b0000000000100001,
    0b0000000000010011,
    0b0000000000001111,
    0b0000000000001101,
    0b0000000000011111,
    0b0000000000111111
};

// Курсор 6: Диагональ NE-SW (Северо-Восток - Юго-Запад)
static const uint16_t cursor_nesw[] = {
    0b0000000000111111,
    0b0000000000011111,
    0b0000000000001111,
    0b0000000000001111,
    0b0000000000010011,
    0b0000000000100001,
    0b0000000001000000,
    0b0000000010000000,
    0b0000000100000000,
    0b0000001000000000,
    0b1000010000000000,
    0b1100100000000000,
    0b1111000000000000,
    0b1111000000000000,
    0b1111100000000000,
    0b1111110000000000
};

// Курсор 7: Текстовый (I-образный)
static const uint16_t cursor_text[] = {
    0b0000011100000000,
    0b0000011100000000,
    0b0000011100000000,
    0b0000011100000000,
    0b0000011100000000,
    0b0000011100000000,
    0b0000011100000000,
    0b0000011100000000,
    0b0000011100000000,
    0b0000011100000000,
    0b0000011100000000,
    0b0000011100000000,
    0b0000011100000000,
    0b0000011100000000,
    0b0000011100000000,
    0b0000011100000000
};

// Курсор 8: Указатель (для кнопок)
static const uint16_t cursor_hand[] = {
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

// Курсор 9: Ожидание (песочные часы - упрощенно)
static const uint16_t cursor_wait[] = {
    0b1111111111111111,
    0b1000000000000001,
    0b1000000000000001,
    0b1000000000000001,
    0b0100000000000010,
    0b0010000000000100,
    0b0001000000001000,
    0b0001000000001000,
    0b0010000000000100,
    0b0100000000000010,
    0b1000000000000001,
    0b1000000000000001,
    0b1000000000000001,
    0b1000000000000001,
    0b1000000000000001,
    0b1111111111111111
};

// Курсор 10: Нельзя (крест)
static const uint16_t cursor_forbidden[] = {
    0b0000000000000000,
    0b0100000000000010,
    0b0111000000001110,
    0b0011100000011100,
    0b0001110000111000,
    0b0000111001110000,
    0b0000011111100000,
    0b0000001111000000,
    0b0000001111000000,
    0b0000011111100000,
    0b0000111001110000,
    0b0001110000111000,
    0b0011100000011100,
    0b0111000000001110,
    0b0100000000000010,
    0b0000000000000000,
};

// ============ ПОЛУЧИТЬ БИТМАП ПО ТИПУ ============
static const uint16_t* get_cursor_bitmap(cursor_type_t type) {
    switch (type) {
        case CURSOR_DEFAULT:     return cursor_arrow;
        case CURSOR_MOVE:        return cursor_move;
        case CURSOR_RESIZE_NS:   return cursor_ns;
        case CURSOR_RESIZE_EW:   return cursor_ew;
        case CURSOR_RESIZE_NWSE: return cursor_nwse;
        case CURSOR_RESIZE_NESW: return cursor_nesw;
        case CURSOR_TEXT:        return cursor_text;
        case CURSOR_HAND:        return cursor_hand;
        case CURSOR_WAIT:        return cursor_wait;
        case CURSOR_FORBIDDEN:   return cursor_forbidden;
        default:                  return cursor_arrow;
    }
}

// ============ СОХРАНЕНИЕ/ВОССТАНОВЛЕНИЕ ФОНА ============
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
        if(py >= fb->height) {
            for(int dx = 0; dx < 16; dx++) {
                cursor_backup[dy * 16 + dx] = 0;
            }
            continue;
        }
        
        for(int dx = 0; dx < 16; dx++) {
            uint32_t px = x + dx;
            if(px >= fb->width) {
                cursor_backup[dy * 16 + dx] = 0;
                continue;
            }
            
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
        }
    }
}

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

// ============ ОПРЕДЕЛЕНИЕ ТИПА КУРСОРА ============
static cursor_type_t determine_cursor_type(void) {
    if (!gui_state.initialized || !gui_state.focused_window) {
        return CURSOR_DEFAULT;
    }
    
    Window* win = gui_state.focused_window;
    uint32_t mx, my;
    vesa_get_cursor_pos(&mx, &my);
    
    if (win->resizing) {
        switch (win->resize_corner) {
            case 1: return CURSOR_RESIZE_NWSE;
            case 2: return CURSOR_RESIZE_NESW;
            case 3: return CURSOR_RESIZE_NESW;
            case 4: return CURSOR_RESIZE_NWSE;
            case 5:
            case 6:
                return CURSOR_RESIZE_EW;
            case 8:
                return CURSOR_RESIZE_NS;
        }
    }
    
    if (win->resizable && !win->maximized && !win->minimized) {
        uint32_t corner_size = 12;
        uint32_t edge_size = 8;
        uint32_t title_h = win->title_height;
        
        // Проверяем углы
        if (mx <= win->x + corner_size && my <= win->y + corner_size) {
            return CURSOR_RESIZE_NWSE;
        }
        if (mx >= win->x + win->width - corner_size && my <= win->y + corner_size) {
            return CURSOR_RESIZE_NESW;
        }
        if (mx <= win->x + corner_size && my >= win->y + win->height - corner_size) {
            return CURSOR_RESIZE_NESW;
        }
        if (mx >= win->x + win->width - corner_size && my >= win->y + win->height - corner_size) {
            return CURSOR_RESIZE_NWSE;
        }
        
        // Проверяем стороны (ТОЛЬКО если не в углах)
        if (mx <= win->x + edge_size && my > win->y + title_h) {
            return CURSOR_RESIZE_EW;  // левый край
        }
        if (mx >= win->x + win->width - edge_size && my > win->y + title_h) {
            return CURSOR_RESIZE_EW;  // правый край
        }
        if (my >= win->y + win->height - edge_size) {
            return CURSOR_RESIZE_NS;  // нижний край
        }
    }
    
    // Проверяем заголовок для перемещения
    if (win->has_titlebar && win->movable && !win->maximized) {
        uint32_t title_y = win->y;
        uint32_t title_h = win->title_height;
        
        if (my >= title_y && my < title_y + title_h) {
            uint32_t button_area_start = win->x + win->width - 80;
            if (mx < button_area_start) {
                return CURSOR_MOVE;
            }
        }
    }
    
    // Проверяем виджеты под курсором
    Widget* widget = win->first_widget;
    while (widget) {
        if (widget->visible && widget->enabled &&
            mx >= widget->x && mx < widget->x + widget->width &&
            my >= widget->y && my < widget->y + widget->height) {
            
            switch (widget->type) {
                case WIDGET_INPUT:
                    return CURSOR_TEXT;
                    
                case WIDGET_BUTTON:
                case WIDGET_CHECKBOX:
                    return CURSOR_HAND;
                    
                case WIDGET_SLIDER:
                    return CURSOR_RESIZE_EW;
                    
                case WIDGET_SCROLLBAR: {
                    ScrollbarData* sb = (ScrollbarData*)widget->data;
                    if (sb) {
                        return sb->vertical ? CURSOR_RESIZE_NS : CURSOR_RESIZE_EW;
                    }
                    break;
                }
                    
                case WIDGET_LIST:
                case WIDGET_DROPDOWN:
                    return CURSOR_HAND;
                    
                default:
                    break;
            }
            break;
        }
        widget = widget->next;
    }
    
    return CURSOR_DEFAULT;
}

// ============ РИСОВАНИЕ КУРСОРА ============
static void cursor_draw(uint32_t x, uint32_t y, cursor_type_t type) {
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
    
    const uint16_t* bitmap = get_cursor_bitmap(type);
    
    // Сохраняем фон
    cursor_save_background(x, y);
    
    // === СНАЧАЛА ЧЁРНАЯ ОБВОДКА ===
    for(int dy = 0; dy < 16; dy++) {
        uint16_t row = bitmap[dy];
        uint32_t py = y + dy;
        if(py >= fb->height) continue;
        
        for(int dx = 0; dx < 16; dx++) {
            if(row & (1 << (15 - dx))) {
                uint32_t px = x + dx;
                if(px >= fb->width) continue;
                
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
        uint16_t row = bitmap[dy];
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

// ============ ПУБЛИЧНЫЕ ФУНКЦИИ ============
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
    current_cursor = CURSOR_DEFAULT;
    
    serial_puts("[CURSOR] Initialized with multiple cursor types\n");
}

void vesa_cursor_update(void) {
    if (!cursor_enabled) return;
    
    struct fb_info* fb = vesa_get_info();
    if(!fb || !fb->found) return;
    
    int32_t new_x = cursor_x;
    int32_t new_y = cursor_y;
    int32_t fb_width = fb->width;
    int32_t fb_height = fb->height;
    
    // Железобетонные ограничения
    if (new_x < 0) new_x = 0;
    if (new_y < 0) new_y = 0;
    if (new_x >= fb_width) new_x = fb_width - 1;
    if (new_y >= fb_height) new_y = fb_height - 1;
    
    cursor_x = new_x;
    cursor_y = new_y;
    
    cursor_type_t new_type = determine_cursor_type();
    if (new_type != current_cursor) {
        current_cursor = new_type;
        cursor_need_update = 1;
    }
    
    if (cursor_need_update || cursor_x != last_cursor_x || cursor_y != last_cursor_y) {
        if (cursor_is_drawn) {
            cursor_restore_background(last_cursor_x, last_cursor_y);
        }
        
        if (cursor_visible) {
            cursor_draw(cursor_x, cursor_y, current_cursor);
        }
        
        cursor_need_update = 0;
    }
}

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

void vesa_cursor_set_type(cursor_type_t type) {
    if (type >= CURSOR_DEFAULT && type <= CURSOR_FORBIDDEN) {
        current_cursor = type;
        cursor_need_update = 1;
    }
}