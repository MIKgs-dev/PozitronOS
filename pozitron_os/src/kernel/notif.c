#include <kernel/notif.h>
#include <kernel/memory.h>
#include <drivers/serial.h>
#include <drivers/timer.h>
#include <drivers/vesa.h>
#include <drivers/mouse.h>
#include <gui/gui.h>
#include <gui/shutdown.h>
#include <lib/string.h>
#include <stdarg.h>
#include <lib/mini_printf.h>

#define NOTIF_WIDTH     300
#define NOTIF_HEIGHT    80
#define NOTIF_PADDING   10
#define NOTIF_BAR_WIDTH 6
#define NOTIF_MAX_COUNT 16
#define NOTIF_LIFETIME  1800
#define NOTIF_MOVE_STEPS 10

#define COLOR_INFO      0x808080
#define COLOR_WARNING   0xFF8000
#define COLOR_ERROR     0xFF0000
#define COLOR_BG        0xF0F0F0
#define COLOR_BORDER    0x404040
#define COLOR_TEXT      0x000000
#define COLOR_CLOSE     0x666666

typedef struct {
    notif_t notifs[NOTIF_MAX_COUNT];
    uint32_t next_id;
    uint32_t count;
    uint8_t initialized;
    uint8_t gui_ready;
    uint32_t screen_w;
    uint32_t screen_h;
    
    uint8_t mouse_down;
    int clicked_index;
} notif_sys_t;

static notif_sys_t notif = {0};

static uint32_t notif_get_color(notif_type_t type) {
    switch (type) {
        case NOTIF_INFO:    return COLOR_INFO;
        case NOTIF_WARNING: return COLOR_WARNING;
        case NOTIF_ERROR:   return COLOR_ERROR;
        default:            return COLOR_INFO;
    }
}

static int notif_check_close(int index, int y, uint32_t mx, uint32_t my) {
    uint32_t x = notif.screen_w - NOTIF_WIDTH - NOTIF_PADDING;
    uint32_t close_x = x + NOTIF_WIDTH - 20;
    uint32_t close_y = y + 5;
    
    return (mx >= close_x && mx < close_x + 15 &&
            my >= close_y && my < close_y + 15);
}

static void notif_recalc_targets(void) {
    uint32_t start_y = notif.screen_h - NOTIF_HEIGHT - NOTIF_PADDING - 30;
    int visible_count = 0;
    
    for (int i = 0; i < NOTIF_MAX_COUNT; i++) {
        if (notif.notifs[i].id == 0) continue;
        
        int target_y = start_y - visible_count * (NOTIF_HEIGHT + NOTIF_PADDING);
        
        if (notif.notifs[i].current_y != target_y && !notif.notifs[i].moving) {
            notif.notifs[i].moving = 1;
            notif.notifs[i].move_start_y = notif.notifs[i].current_y;
            notif.notifs[i].target_y = target_y;
            notif.notifs[i].move_step = 0;
        }
        
        visible_count++;
    }
}

static void notif_remove(int index) {
    if (index < 0 || index >= NOTIF_MAX_COUNT) return;
    if (notif.notifs[index].id == 0) return;
    
    uint32_t x = notif.screen_w - NOTIF_WIDTH - NOTIF_PADDING;
    vesa_mark_dirty(x, notif.notifs[index].current_y, NOTIF_WIDTH, NOTIF_HEIGHT);
    
    for (int i = index; i < NOTIF_MAX_COUNT - 1; i++) {
        notif.notifs[i] = notif.notifs[i + 1];
    }
    memset(&notif.notifs[NOTIF_MAX_COUNT - 1], 0, sizeof(notif_t));
    
    if (notif.count > 0) notif.count--;
    notif_recalc_targets();
}

static void notif_draw_one(notif_t* n) {
    if (n->current_y < -NOTIF_HEIGHT || n->current_y > notif.screen_h) return;
    
    uint32_t x = notif.screen_w - NOTIF_WIDTH - NOTIF_PADDING;
    uint32_t color = notif_get_color(n->type);
    
    for (int j = 0; j < NOTIF_HEIGHT; j++) {
        for (int i = 0; i < NOTIF_WIDTH; i++) {
            uint32_t px = x + i;
            uint32_t py = n->current_y + j;
            
            if (px >= notif.screen_w || py >= notif.screen_h || py < 0) continue;
            
            if (i < NOTIF_BAR_WIDTH) {
                vesa_put_pixel(px, py, color);
            } else {
                vesa_put_pixel(px, py, COLOR_BG);
            }
            
            if (j == 0 || j == NOTIF_HEIGHT-1 || i == 0 || i == NOTIF_WIDTH-1) {
                vesa_put_pixel(px, py, COLOR_BORDER);
            }
        }
    }
    
    uint32_t text_x = x + NOTIF_BAR_WIDTH + 8;
    uint32_t text_y = n->current_y + 10;
    vesa_draw_text(text_x, text_y, n->title, color, COLOR_BG);
    
    char msg[64];
    strncpy(msg, n->message, 63);
    msg[63] = '\0';
    
    if (strlen(msg) > 35) {
        msg[32] = '.';
        msg[33] = '.';
        msg[34] = '.';
        msg[35] = '\0';
    }
    
    vesa_draw_text(text_x, n->current_y + 30, msg, COLOR_TEXT, COLOR_BG);
    
    uint32_t close_x = x + NOTIF_WIDTH - 20;
    uint32_t close_y = n->current_y + 5;
    vesa_draw_line(close_x, close_y, close_x + 10, close_y + 10, COLOR_CLOSE);
    vesa_draw_line(close_x + 10, close_y, close_x, close_y + 10, COLOR_CLOSE);
    
    vesa_mark_dirty(x, n->current_y, NOTIF_WIDTH, NOTIF_HEIGHT);
}

static void notif_draw_all(void) {
    for (int i = 0; i < NOTIF_MAX_COUNT; i++) {
        if (notif.notifs[i].id == 0) continue;
        notif_draw_one(&notif.notifs[i]);
    }
}

static void notif_handle_clicks(void) {
    if (is_shutdown_mode_active()) return;
    
    mouse_state_t mouse = mouse_get_state();
    
    if (mouse.buttons & 1) {
        if (!notif.mouse_down) {
            notif.mouse_down = 1;
            notif.clicked_index = -1;
            
            uint32_t start_y = notif.screen_h - NOTIF_HEIGHT - NOTIF_PADDING - 30;
            int visible = 0;
            
            for (int i = 0; i < NOTIF_MAX_COUNT; i++) {
                if (notif.notifs[i].id == 0) continue;
                
                int y = start_y - visible * (NOTIF_HEIGHT + NOTIF_PADDING);
                if (y >= 0) {
                    if (notif_check_close(i, y, mouse.x, mouse.y)) {
                        notif.clicked_index = i;
                        break;
                    }
                }
                visible++;
            }
        }
    } else {
        if (notif.mouse_down && notif.clicked_index >= 0) {
            uint32_t start_y = notif.screen_h - NOTIF_HEIGHT - NOTIF_PADDING - 30;
            int visible = 0;
            int found = -1;
            
            for (int i = 0; i < NOTIF_MAX_COUNT; i++) {
                if (notif.notifs[i].id == 0) continue;
                
                int y = start_y - visible * (NOTIF_HEIGHT + NOTIF_PADDING);
                if (y >= 0) {
                    if (notif_check_close(i, y, mouse.x, mouse.y)) {
                        found = i;
                        break;
                    }
                }
                visible++;
            }
            
            if (found == notif.clicked_index) {
                notif_remove(found);
            }
        }
        notif.mouse_down = 0;
        notif.clicked_index = -1;
    }
}

void notif_init(void) {
    memset(&notif, 0, sizeof(notif_sys_t));
    notif.initialized = 1;
    notif.next_id = 1;
    notif.clicked_index = -1;
    notif.screen_w = vesa_get_width();
    notif.screen_h = vesa_get_height();
    serial_puts("[NOTIF] System initialized\n");
}

void notif_info(const char* title, const char* message) {
    notif_printf(NOTIF_INFO, title, "%s", message);
}

void notif_warning(const char* title, const char* message) {
    notif_printf(NOTIF_WARNING, title, "%s", message);
}

void notif_error(const char* title, const char* message) {
    notif_printf(NOTIF_ERROR, title, "%s", message);
}

void notif_printf(notif_type_t type, const char* title, const char* format, ...) {
    if (!notif.initialized || !title || !format) return;
    
    asm volatile("cli");
    
    int slot = -1;
    for (int i = 0; i < NOTIF_MAX_COUNT; i++) {
        if (notif.notifs[i].id == 0) {
            slot = i;
            break;
        }
    }
    
    if (slot < 0) {
        for (int i = 0; i < NOTIF_MAX_COUNT; i++) {
            if (notif.notifs[i].id != 0) {
                notif_remove(i);
                break;
            }
        }
        for (int i = 0; i < NOTIF_MAX_COUNT; i++) {
            if (notif.notifs[i].id == 0) {
                slot = i;
                break;
            }
        }
        if (slot < 0) {
            asm volatile("sti");
            return;
        }
    }
    
    notif_t* n = &notif.notifs[slot];
    memset(n, 0, sizeof(notif_t));
    
    n->type = type;
    n->created_tick = timer_get_ticks();
    n->id = notif.next_id++;
    
    uint32_t start_y = notif.screen_h - NOTIF_HEIGHT - NOTIF_PADDING - 30;
    int visible_count = 0;
    for (int i = 0; i < slot; i++) {
        if (notif.notifs[i].id != 0) visible_count++;
    }
    n->current_y = start_y - visible_count * (NOTIF_HEIGHT + NOTIF_PADDING);
    n->target_y = n->current_y;
    
    strncpy(n->title, title, NOTIF_MAX_TITLE - 1);
    n->title[NOTIF_MAX_TITLE - 1] = '\0';
    
    va_list args;
    va_start(args, format);
    vsprintf(n->message, format, args);
    va_end(args);
    
    n->message[NOTIF_MAX_MSG - 1] = '\0';
    
    notif.count++;
    
    asm volatile("sti");
    
    serial_puts("[NOTIF] ");
    serial_puts(title);
    serial_puts(": ");
    serial_puts(n->message);
    serial_puts("\n");
}

void notif_update(void) {
    if (!notif.initialized) return;
    
    if (!notif.gui_ready) {
        if (gui_state.initialized) {
            notif.gui_ready = 1;
            notif.screen_w = vesa_get_width();
            notif.screen_h = vesa_get_height();
            notif_recalc_targets();
        } else {
            return;
        }
    }
    
    notif_handle_clicks();
    
    uint32_t current_tick = timer_get_ticks();
    
    for (int i = 0; i < NOTIF_MAX_COUNT; i++) {
        if (notif.notifs[i].id == 0) continue;
        
        notif_t* n = &notif.notifs[i];
        
        if (n->moving) {
            n->move_step++;
            if (n->move_step >= NOTIF_MOVE_STEPS) {
                n->moving = 0;
                n->current_y = n->target_y;
            } else {
                float t = (float)n->move_step / NOTIF_MOVE_STEPS;
                n->current_y = n->move_start_y + (int32_t)((n->target_y - n->move_start_y) * t);
            }
        }
        
        if (!n->moving && current_tick - n->created_tick >= NOTIF_LIFETIME) {
            notif_remove(i);
            i--;
        }
    }
}

void notif_render(void) {
    if (is_shutdown_mode_active()) return;
    
    if (notif.count > 0) {
        notif_draw_all();
    }
}