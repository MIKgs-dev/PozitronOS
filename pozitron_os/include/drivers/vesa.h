#ifndef DRIVERS_VESA_H
#define DRIVERS_VESA_H

#include <stdint.h>
#include "kernel/multiboot.h"

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

// VBE mode info structure
typedef struct {
    uint16_t attributes;
    uint8_t window_a, window_b;
    uint16_t granularity;
    uint16_t window_size;
    uint16_t segment_a, segment_b;
    uint32_t win_func_ptr;
    uint16_t pitch;
    uint16_t width, height;
    uint8_t w_char, h_char, planes, bpp;
    uint8_t banks, memory_model, bank_size, image_pages;
    uint8_t reserved0;
    uint8_t red_mask, red_position;
    uint8_t green_mask, green_position;
    uint8_t blue_mask, blue_position;
    uint8_t reserved_mask, reserved_position;
    uint8_t direct_color_attributes;
    uint32_t framebuffer;
    uint32_t off_screen_mem_ptr;
    uint16_t off_screen_mem_size;
    uint8_t reserved1[206];
} __attribute__((packed)) vbe_mode_info_t;

// ===== BASIC FUNCTIONS =====
int vesa_init(multiboot_info_t* mb_info);
uint32_t vesa_get_width(void);
uint32_t vesa_get_height(void);
uint32_t* vesa_get_framebuffer(void);
struct fb_info* vesa_get_info(void);

// Drawing primitives
void vesa_put_pixel(uint32_t x, uint32_t y, color_t color);
color_t vesa_get_pixel(uint32_t x, uint32_t y);
void vesa_draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, color_t color);
void vesa_draw_line(uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2, color_t color);
void vesa_draw_circle(uint32_t cx, uint32_t cy, uint32_t radius, color_t color);
void vesa_fill(color_t color);
void vesa_clear(void);

// Text
void vesa_draw_char(uint32_t x, uint32_t y, uint16_t unicode, color_t fg, color_t bg);
void vesa_draw_text(uint32_t x, uint32_t y, const char* text, color_t fg, color_t bg);
void vesa_draw_text_cp866(uint32_t x, uint32_t y, const char* text, color_t fg, color_t bg);
void vesa_draw_text_rus(uint32_t x, uint32_t y, const char* text, color_t fg, color_t bg);

// ===== ENHANCED FUNCTIONS =====
// Mode management
void vesa_list_modes(void);
int vesa_set_mode(uint32_t width, uint32_t height, uint32_t bpp);
void vesa_print_info(void);

// Color conversion
color_t vbe_rgb_to_color(uint8_t r, uint8_t g, uint8_t b);
void vesa_put_pixel_rgb(uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b);
void vesa_get_pixel_rgb(uint32_t x, uint32_t y, uint8_t* r, uint8_t* g, uint8_t* b);

// Blitting operations
void vesa_blit(uint32_t dst_x, uint32_t dst_y, 
               uint32_t* src, uint32_t src_width, uint32_t src_height,
               uint32_t src_x, uint32_t src_y, uint32_t blit_width, uint32_t blit_height);

void vesa_blit_alpha(uint32_t dst_x, uint32_t dst_y,
                    uint32_t* src, uint32_t src_width, uint32_t src_height,
                    uint32_t src_x, uint32_t src_y, uint32_t blit_width, uint32_t blit_height,
                    uint8_t alpha);

// Special effects
void vesa_draw_gradient(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                       color_t color1, color_t color2, uint8_t vertical);

// ===== DOUBLE BUFFERING =====
int vesa_enable_double_buffer(void);
void vesa_disable_double_buffer(void);
uint8_t vesa_is_double_buffer_enabled(void);

// ВНИМАНИЕ: Эти функции возвращают void*!
// НЕ КАСТУЙТЕ В uint32_t* без проверки bpp!
void* vesa_get_back_buffer(void);
void vesa_swap_buffers(void);
void vesa_clear_back_buffer(color_t color);

// ===== DIRTY RECTANGLES =====
#define MAX_DIRTY_RECTS 32

void vesa_init_dirty(void);
void vesa_mark_dirty(uint32_t x, uint32_t y, uint32_t w, uint32_t h);
void vesa_mark_dirty_all(void);
void vesa_update_dirty(void);
void vesa_clear_dirty(void);
uint32_t vesa_get_dirty_count(void);
void vesa_debug_dirty(void);
uint8_t vesa_get_dirty_rect(uint32_t index, uint32_t* x, uint32_t* y, uint32_t* w, uint32_t* h);

// ===== BACKGROUND CACHING =====
void vesa_cache_background(void);
void vesa_restore_background(void);
void vesa_restore_background_dirty(void);
void vesa_free_background_cache(void);
uint8_t vesa_is_background_cached(void);

// ===== CURSOR FUNCTIONS =====
void vesa_cursor_init(void);
void vesa_cursor_update(void);
void vesa_cursor_force_update(void);
void vesa_cursor_set_visible(uint8_t visible);
uint8_t vesa_cursor_is_visible(void);
void vesa_cursor_enable(uint8_t enable);
uint8_t vesa_cursor_is_enabled(void);
void vesa_cursor_get_area(uint32_t* x, uint32_t* y, uint32_t* w, uint32_t* h);

// Old cursor functions (for compatibility)
void vesa_draw_cursor(uint32_t x, uint32_t y);
void vesa_hide_cursor(void);
void vesa_show_cursor(void);
void vesa_set_cursor_pos(uint32_t x, uint32_t y);
void vesa_get_cursor_pos(uint32_t* x, uint32_t* y);

#endif // DRIVERS_VESA_H