#include "drivers/mouse.h"
#include "drivers/ports.h"
#include "drivers/serial.h"
#include "drivers/pic.h"
#include "core/event.h"
#include "drivers/vesa.h"
#include <stddef.h>

typedef struct {
    mouse_state_t public;
    uint8_t cycle;
    uint8_t packet[3];
    uint32_t screen_width;
    uint32_t screen_height;
    uint8_t initialized;
    uint8_t last_buttons;
} mouse_private_t;

static mouse_private_t mouse = {0};

static void ps2_wait(uint8_t type) {
    uint32_t timeout = 100000;
    if(type == 0) {
        while(timeout--) {
            if((inb(0x64) & 0x02) == 0) return;
        }
    } else {
        while(timeout--) {
            if((inb(0x64) & 0x01) == 1) return;
        }
    }
}

static void ps2_write(uint8_t value) {
    ps2_wait(0);
    outb(0x64, 0xD4);
    ps2_wait(0);
    outb(0x60, value);
}

static uint8_t ps2_read(void) {
    ps2_wait(1);
    return inb(0x60);
}

void mouse_init(void) {
    serial_puts("[MOUSE] Initializing PS/2 mouse...\n");

    mouse.screen_width = vesa_get_width();
    mouse.screen_height = vesa_get_height();
    mouse.public.x = mouse.screen_width / 2;
    mouse.public.y = mouse.screen_height / 2;
    mouse.public.buttons = 0;
    mouse.public.dx = 0;
    mouse.public.dy = 0;
    mouse.cycle = 0;
    mouse.initialized = 0;
    mouse.last_buttons = 0;
    
    ps2_wait(0);
    outb(0x64, 0xA8);
    
    ps2_wait(0);
    outb(0x64, 0x20);
    ps2_wait(1);
    uint8_t config = inb(0x60);
    config |= 0x02;
    config |= 0x01;
    
    ps2_wait(0);
    outb(0x64, 0x60);
    ps2_wait(0);
    outb(0x60, config);
    
    ps2_write(0xF6);
    ps2_read();
    
    ps2_write(0xF4);
    ps2_read();
    
    irq_install_handler(12, mouse_handler);
    
    mouse.initialized = 1;
    serial_puts("[MOUSE] PS/2 mouse initialized\n");
}

void mouse_handler(registers_t* regs) {
    (void)regs;
    
    if(!(inb(0x64) & 0x20)) return;
    
    uint8_t data = inb(0x60);
    mouse.packet[mouse.cycle++] = data;
    
    if(mouse.cycle == 3) {
        mouse.cycle = 0;
        
        if(mouse.packet[0] & 0x08) {
            uint8_t old_buttons = mouse.public.buttons;
            mouse.public.buttons = mouse.packet[0] & 0x07;
            
            int8_t dx = mouse.packet[1];
            if(mouse.packet[0] & 0x10) dx = mouse.packet[1] - 256;
            
            int8_t dy = mouse.packet[2];
            if(mouse.packet[0] & 0x20) dy = mouse.packet[2] - 256;
            dy = -dy;
            
            int32_t new_x = mouse.public.x + dx;
            int32_t new_y = mouse.public.y + dy;
            
            if(new_x < 0) new_x = 0;
            if(new_y < 0) new_y = 0;
            if(new_x >= (int32_t)mouse.screen_width) new_x = mouse.screen_width - 1;
            if(new_y >= (int32_t)mouse.screen_height) new_y = mouse.screen_height - 1;
            
            mouse.public.x = new_x;
            mouse.public.y = new_y;
            mouse.public.dx = dx;
            mouse.public.dy = dy;
            
            vesa_set_cursor_pos(mouse.public.x, mouse.public.y);
            
            event_t move_event;
            move_event.type = EVENT_MOUSE_MOVE;
            move_event.data1 = mouse.public.x;
            move_event.data2 = mouse.public.y;
            event_post(move_event);
           
            uint8_t button_changes = mouse.public.buttons ^ old_buttons;
            if (button_changes) {
                for (int i = 0; i < 3; i++) {
                    uint8_t mask = 1 << i;
                    if (button_changes & mask) {
                        event_t click_event;
                        if (mouse.public.buttons & mask) click_event.type = EVENT_MOUSE_CLICK;
                        else click_event.type = EVENT_MOUSE_RELEASE;
                        click_event.data1 = mouse.public.x;
                        click_event.data2 = mouse.public.y | (i << 16);
                        event_post(click_event);
                    }
                }
            }
        }
        pic_send_eoi(12);
    }
}

void mouse_update(void) {}

mouse_state_t mouse_get_state(void) { return mouse.public; }

void mouse_get_position(int32_t* x, int32_t* y) {
    if(x) *x = mouse.public.x;
    if(y) *y = mouse.public.y;
}

uint8_t mouse_get_buttons(void) { return mouse.public.buttons; }

void mouse_set_position(uint32_t x, uint32_t y) {
    mouse.public.x = x;
    mouse.public.y = y;
    vesa_set_cursor_pos(x, y);
}

void mouse_clamp_to_screen(uint32_t width, uint32_t height) {
    mouse.screen_width = width;
    mouse.screen_height = height;
    if(mouse.public.x < 0) mouse.public.x = 0;
    if(mouse.public.y < 0) mouse.public.y = 0;
    if(mouse.public.x >= (int32_t)width) mouse.public.x = width - 1;
    if(mouse.public.y >= (int32_t)height) mouse.public.y = height - 1;
}