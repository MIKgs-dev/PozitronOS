#ifndef GUI_ICON_H
#define GUI_ICON_H

#include <stdint.h>

typedef struct {
    uint32_t* pixels;
    int width;
    int height;
} icon_t;

icon_t* icon_load(const char* path);
void icon_free(icon_t* icon);
void icon_draw(icon_t* icon, int x, int y);

#endif