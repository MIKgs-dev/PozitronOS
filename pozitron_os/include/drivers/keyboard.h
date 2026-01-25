#ifndef DRIVERS_KEYBOARD_H
#define DRIVERS_KEYBOARD_H

#include <stdint.h>
#include "../core/isr.h"

// Порты клавиатуры
#define KEYBOARD_DATA_PORT 0x60
#define KEYBOARD_STATUS_PORT 0x64

// Скан-коды клавиш (только основные)
#define KEY_ESC         0x01
#define KEY_BACKSPACE   0x0E
#define KEY_TAB         0x0F
#define KEY_ENTER       0x1C
#define KEY_LCTRL       0x1D
#define KEY_LSHIFT      0x2A
#define KEY_RSHIFT      0x36
#define KEY_LALT        0x38
#define KEY_CAPSLOCK    0x3A
#define KEY_F1          0x3B
#define KEY_F2          0x3C
#define KEY_F3          0x3D
#define KEY_F4          0x3E
#define KEY_F5          0x3F
#define KEY_F6          0x40
#define KEY_F7          0x41
#define KEY_F8          0x42
#define KEY_F9          0x43
#define KEY_F10         0x44
#define KEY_NUMLOCK     0x45
#define KEY_SCROLLLOCK  0x46
#define KEY_SPACE       0x39

// Модификаторы
#define KEY_LEFT_SHIFT  KEY_LSHIFT
#define KEY_RIGHT_SHIFT KEY_RSHIFT
#define KEY_CAPS_LOCK   KEY_CAPSLOCK

// Макросы для проверки
#define KEY_RELEASED(scancode) ((scancode) & 0x80)
#define KEY_CODE(scancode) ((scancode) & 0x7F)

// Структура состояния клавиатуры
typedef struct {
    uint8_t shift : 1;
    uint8_t ctrl  : 1;
    uint8_t alt   : 1;
    uint8_t caps  : 1;
    uint8_t numlock : 1;
    uint8_t scrolllock : 1;
} keyboard_state_t;

// Прототипы функций
void keyboard_init(void);
void keyboard_handler(registers_t* regs);
int keyboard_get_scancode(void);
void keyboard_wait_for_key(void);
char keyboard_scancode_to_char(uint8_t scancode, keyboard_state_t state);

#endif // DRIVERS_KEYBOARD_H