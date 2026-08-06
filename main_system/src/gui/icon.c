#include "gui/icon.h"
#include "kernel/memory.h"
#include "drivers/vesa.h"
#include "lib/bmp.h"
#include <stddef.h>

icon_t* icon_load(const char* path) {
    int w = 0, h = 0;
    
    uint32_t* bmp_pixels = bmp_load(path, &w, &h);
    if (!bmp_pixels) return NULL;
    
    icon_t* icon = (icon_t*)kmalloc(sizeof(icon_t));
    if (!icon) {
        kfree(bmp_pixels);
        return NULL;
    }
    
    icon->width = w;
    icon->height = h;
    icon->pixels = bmp_pixels; 
    
    return icon;
}

void icon_free(icon_t* icon) {
    if (icon) {
        if (icon->pixels) kfree(icon->pixels);
        kfree(icon);
    }
}

void icon_draw(icon_t* icon, int x, int y) {
    if (!icon || !icon->pixels) return;
    
    for (int py = 0; py < icon->height; py++) {
        for (int px = 0; px < icon->width; px++) {
            uint32_t color = icon->pixels[py * icon->width + px];
            uint8_t a = (color >> 24) & 0xFF;
            
            if (a == 255) {
                vesa_put_pixel(x + px, y + py, color & 0xFFFFFF);
            } else if (a > 0) {
                uint32_t bg = vesa_get_pixel(x + px, y + py);
                uint8_t r = ((color >> 16) & 0xFF) * a / 255 + ((bg >> 16) & 0xFF) * (255 - a) / 255;
                uint8_t g = ((color >> 8) & 0xFF) * a / 255 + ((bg >> 8) & 0xFF) * (255 - a) / 255;
                uint8_t b = (color & 0xFF) * a / 255 + (bg & 0xFF) * (255 - a) / 255;
                vesa_put_pixel(x + px, y + py, (r << 16) | (g << 8) | b);
            }
        }
    }
}