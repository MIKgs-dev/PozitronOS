#ifndef DRIVERS_MOUSE_H
#define DRIVERS_MOUSE_H

#include <stdint.h>
#include "../core/isr.h"

// Структура состояния мыши
typedef struct {
    int32_t x, y;
    uint8_t buttons;
    int8_t dx, dy;
} mouse_state_t;

// Прототипы функций
void mouse_init(void);
void mouse_handler(registers_t* regs);
void mouse_get_position(int32_t* x, int32_t* y);
uint8_t mouse_get_buttons(void);
void mouse_set_position(uint32_t x, uint32_t y);
void mouse_clamp_to_screen(uint32_t width, uint32_t height);

// Новые функции для безопасного доступа
void mouse_update(void);  // Обновляет состояние (вызывается в главном цикле)
mouse_state_t mouse_get_state(void);  // Возвращает копию состояния

#endif // DRIVERS_MOUSE_H