#ifndef DRIVERS_VESA_H
#define DRIVERS_VESA_H

#include <stdint.h>

// Типы цветов
typedef uint32_t color_t;

// Структура информации о фреймбуфере
struct fb_info {
    uint32_t* address;
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    uint32_t pitch;
    uint8_t found;
};

// Dirty rectangle
typedef struct {
    uint32_t x, y, w, h;
} dirty_rect_t;

// Прототипы функций фреймбуфера
int vesa_init(void);
uint32_t vesa_get_width(void);
uint32_t vesa_get_height(void);
uint32_t* vesa_get_framebuffer(void);
struct fb_info* vesa_get_info(void);

// Примитивы рисования
void vesa_put_pixel(uint32_t x, uint32_t y, color_t color);
color_t vesa_get_pixel(uint32_t x, uint32_t y);
void vesa_draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, color_t color);
void vesa_draw_line(uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2, color_t color);
void vesa_draw_circle(uint32_t cx, uint32_t cy, uint32_t radius, color_t color);
void vesa_fill(color_t color);
void vesa_clear(void);

// Текст
void vesa_draw_char(uint32_t x, uint32_t y, uint16_t unicode, color_t fg, color_t bg);
void vesa_draw_text(uint32_t x, uint32_t y, const char* text, color_t fg, color_t bg);
void vesa_draw_text_cp866(uint32_t x, uint32_t y, const char* text, color_t fg, color_t bg);
void vesa_draw_text_rus(uint32_t x, uint32_t y, const char* text, color_t fg, color_t bg);

// Двойная буферизация
int vesa_enable_double_buffer(void);
void vesa_disable_double_buffer(void);
void vesa_swap_buffers(void);
void vesa_clear_back_buffer(color_t color);
uint8_t vesa_is_double_buffer_enabled(void);
uint32_t* vesa_get_back_buffer(void);

// Dirty rectangles система
void vesa_init_dirty(void);
void vesa_mark_dirty(uint32_t x, uint32_t y, uint32_t w, uint32_t h);
void vesa_mark_dirty_all(void);
void vesa_update_dirty(void);
void vesa_clear_dirty(void);
uint32_t vesa_get_dirty_count(void);
void vesa_debug_dirty(void);
uint8_t vesa_get_dirty_rect(uint32_t index, uint32_t* x, uint32_t* y, uint32_t* w, uint32_t* h);

// Кэширование фона
void vesa_cache_background(void);
void vesa_restore_background(void);
void vesa_restore_background_dirty(void);
void vesa_free_background_cache(void);
uint8_t vesa_is_background_cached(void);

// Функции курсора
void vesa_cursor_init(void);
void vesa_cursor_update(void);
void vesa_cursor_force_update(void);
void vesa_cursor_set_visible(uint8_t visible);
uint8_t vesa_cursor_is_visible(void);
void vesa_cursor_enable(uint8_t enable);
uint8_t vesa_cursor_is_enabled(void);
void vesa_cursor_get_area(uint32_t* x, uint32_t* y, uint32_t* w, uint32_t* h);

// Старые функции курсора (для совместимости)
void vesa_draw_cursor(uint32_t x, uint32_t y);
void vesa_hide_cursor(void);
void vesa_show_cursor(void);
void vesa_set_cursor_pos(uint32_t x, uint32_t y);
void vesa_get_cursor_pos(uint32_t* x, uint32_t* y);

#endif // DRIVERS_VESA_H