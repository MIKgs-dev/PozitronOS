#include "drivers/mouse.h"
#include "drivers/ports.h"
#include "drivers/serial.h"
#include "drivers/pic.h"
#include "core/event.h"
#include "drivers/vesa.h"
#include <stddef.h>

// Приватная структура для внутреннего состояния
typedef struct {
    mouse_state_t public;       // Публичное состояние
    uint8_t cycle;              // Счётчик байтов пакета
    uint8_t packet[3];          // Буфер пакета мыши
    uint32_t screen_width;
    uint32_t screen_height;
    uint8_t initialized;
    uint8_t last_buttons;       // Для отслеживания кликов
} mouse_private_t;

static mouse_private_t mouse = {0};

// Вспомогательные функции
static void ps2_wait(uint8_t type) {
    uint32_t timeout = 100000;
    if(type == 0) { // Ожидаем, когда можно писать
        while(timeout--) {
            if((inb(0x64) & 0x02) == 0) return;
        }
    } else { // Ожидаем, когда можно читать
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

// Инициализация мыши
void mouse_init(void) {
    serial_puts("[MOUSE] Initializing PS/2 mouse...\n");

    // Инициализируем структуру
    mouse.screen_width = 1024;
    mouse.screen_height = 768;
    mouse.public.x = 400;
    mouse.public.y = 300;
    mouse.public.buttons = 0;
    mouse.public.dx = 0;
    mouse.public.dy = 0;
    mouse.cycle = 0;
    mouse.initialized = 0;
    mouse.last_buttons = 0;
    
    // Включаем вспомогательное устройство (мышь)
    ps2_wait(0);
    outb(0x64, 0xA8);
    
    // Включаем прерывания мыши
    ps2_wait(0);
    outb(0x64, 0x20);
    ps2_wait(1);
    uint8_t config = inb(0x60);
    config |= 0x02; // Включаем IRQ12 (мышь)
    config |= 0x01; // Включаем прерывания клавиатуры
    
    ps2_wait(0);
    outb(0x64, 0x60);
    ps2_wait(0);
    outb(0x60, config);
    
    // Устанавливаем режим по умолчанию
    ps2_write(0xF6);
    ps2_read(); // ACK
    
    // Включаем мышь
    ps2_write(0xF4);
    ps2_read(); // ACK
    
    // Устанавливаем обработчик прерывания
    irq_install_handler(12, mouse_handler);
    
    mouse.initialized = 1;
    serial_puts("[MOUSE] PS/2 mouse initialized\n");
}

void mouse_handler(registers_t* regs) {
    (void)regs;
    
    // Проверяем, что данные от мыши
    if(!(inb(0x64) & 0x20)) return;
    
    // Читаем данные
    uint8_t data = inb(0x60);
    
    // Сохраняем в буфер пакета
    mouse.packet[mouse.cycle++] = data;
    
    // Когда накопили полный пакет (3 байта)
    if(mouse.cycle == 3) {
        mouse.cycle = 0;
        
        // Проверяем валидность пакета
        if(mouse.packet[0] & 0x08) {
            // Сохраняем старые кнопки для сравнения
            uint8_t old_buttons = mouse.public.buttons;
            
            // Обновляем состояние кнопок
            mouse.public.buttons = mouse.packet[0] & 0x07;
            
            // Движение по X
            int8_t dx = mouse.packet[1];
            if(mouse.packet[0] & 0x10) {
                // Отрицательное значение
                dx = mouse.packet[1] - 256;
            }
            
            // Движение по Y
            int8_t dy = mouse.packet[2];
            if(mouse.packet[0] & 0x20) {
                // Отрицательное значение
                dy = mouse.packet[2] - 256;
            }
            dy = -dy; // Инвертируем Y
            
            // Обновляем позицию
            mouse.public.x += dx;
            mouse.public.y += dy;
            
            // ВАЖНО: Используем vesa_set_cursor_pos, которая
            // автоматически скрывает старый курсор и показывает новый
            vesa_set_cursor_pos(mouse.public.x, mouse.public.y);
            vesa_mark_dirty(mouse.public.x, mouse.public.y, 16, 16);
            
            // Сохраняем дельты
            mouse.public.dx = dx;
            mouse.public.dy = dy;
            
            // Ограничиваем экраном
             if(mouse.public.x < 0) mouse.public.x = 0;
             if(mouse.public.y < 0) mouse.public.y = 0;
            
            event_t move_event;
            move_event.type = EVENT_MOUSE_MOVE;
            move_event.data1 = mouse.public.x;
            move_event.data2 = mouse.public.y;
            event_post(move_event);
           
            // ===== СОБЫТИЯ КЛИКОВ =====
            uint8_t button_changes = mouse.public.buttons ^ old_buttons;
            
            if (button_changes) {
                // Проверяем каждую кнопку (0=левая, 1=правая, 2=средняя)
                for (int i = 0; i < 3; i++) {
                    uint8_t mask = 1 << i;
                    if (button_changes & mask) {
                        event_t click_event;
                        if (mouse.public.buttons & mask) {
                            click_event.type = EVENT_MOUSE_CLICK;
                        } else {
                            click_event.type = EVENT_MOUSE_RELEASE;
                        }
                        click_event.data1 = mouse.public.x;
                        click_event.data2 = mouse.public.y | (i << 16); // Кнопка в старших битах
                        event_post(click_event);
                    }
                }
            }
        }
        
        // Отправляем EOI
        pic_send_eoi(12);
    }
}

// Обновление состояния мыши (теперь пустая функция для совместимости)
void mouse_update(void) {
    // Всё уже сделано в обработчике
}

// Получить копию состояния мыши
mouse_state_t mouse_get_state(void) {
    return mouse.public;
}

// Старые функции для совместимости
void mouse_get_position(int32_t* x, int32_t* y) {
    if(x) *x = mouse.public.x;
    if(y) *y = mouse.public.y;
}

uint8_t mouse_get_buttons(void) {
    return mouse.public.buttons;
}

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