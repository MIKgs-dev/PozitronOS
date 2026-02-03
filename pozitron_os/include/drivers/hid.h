#ifndef HID_H
#define HID_H

#include <stdint.h>
#include "drivers/usb.h"

// HID Usage Pages
#define HID_USAGE_PAGE_GENERIC_DESKTOP  0x01
#define HID_USAGE_PAGE_KEYBOARD_KEYPAD  0x07
#define HID_USAGE_PAGE_LEDS             0x08
#define HID_USAGE_PAGE_BUTTON           0x09

// HID Usages (Generic Desktop)
#define HID_USAGE_POINTER   0x01
#define HID_USAGE_MOUSE     0x02
#define HID_USAGE_JOYSTICK  0x04
#define HID_USAGE_GAMEPAD   0x05
#define HID_USAGE_KEYBOARD  0x06
#define HID_USAGE_KEYPAD    0x07
#define HID_USAGE_X         0x30
#define HID_USAGE_Y         0x31
#define HID_USAGE_Z         0x32
#define HID_USAGE_RX        0x33
#define HID_USAGE_RY        0x34
#define HID_USAGE_RZ        0x35
#define HID_USAGE_SLIDER    0x36
#define HID_USAGE_DIAL      0x37
#define HID_USAGE_WHEEL     0x38
#define HID_USAGE_HATSWITCH 0x39

// Keyboard scancodes (Boot Protocol)
#define HID_KEY_NONE        0x00
#define HID_KEY_A           0x04
#define HID_KEY_B           0x05
#define HID_KEY_C           0x06
#define HID_KEY_D           0x07
#define HID_KEY_E           0x08
#define HID_KEY_F           0x09
#define HID_KEY_G           0x0A
#define HID_KEY_H           0x0B
#define HID_KEY_I           0x0C
#define HID_KEY_J           0x0D
#define HID_KEY_K           0x0E
#define HID_KEY_L           0x0F
#define HID_KEY_M           0x10
#define HID_KEY_N           0x11
#define HID_KEY_O           0x12
#define HID_KEY_P           0x13
#define HID_KEY_Q           0x14
#define HID_KEY_R           0x15
#define HID_KEY_S           0x16
#define HID_KEY_T           0x17
#define HID_KEY_U           0x18
#define HID_KEY_V           0x19
#define HID_KEY_W           0x1A
#define HID_KEY_X           0x1B
#define HID_KEY_Y           0x1C
#define HID_KEY_Z           0x1D
#define HID_KEY_1           0x1E
#define HID_KEY_2           0x1F
#define HID_KEY_3           0x20
#define HID_KEY_4           0x21
#define HID_KEY_5           0x22
#define HID_KEY_6           0x23
#define HID_KEY_7           0x24
#define HID_KEY_8           0x25
#define HID_KEY_9           0x26
#define HID_KEY_0           0x27
#define HID_KEY_ENTER       0x28
#define HID_KEY_ESCAPE      0x29
#define HID_KEY_BACKSPACE   0x2A
#define HID_KEY_TAB         0x2B
#define HID_KEY_SPACE       0x2C
#define HID_KEY_MINUS       0x2D
#define HID_KEY_EQUALS      0x2E
#define HID_KEY_LEFTBRACE   0x2F
#define HID_KEY_RIGHTBRACE  0x30
#define HID_KEY_BACKSLASH   0x31
#define HID_KEY_SEMICOLON   0x33
#define HID_KEY_APOSTROPHE  0x34
#define HID_KEY_GRAVE       0x35
#define HID_KEY_COMMA       0x36
#define HID_KEY_DOT         0x37
#define HID_KEY_SLASH       0x38
#define HID_KEY_CAPSLOCK    0x39
#define HID_KEY_F1          0x3A
#define HID_KEY_F2          0x3B
#define HID_KEY_F3          0x3C
#define HID_KEY_F4          0x3D
#define HID_KEY_F5          0x3E
#define HID_KEY_F6          0x3F
#define HID_KEY_F7          0x40
#define HID_KEY_F8          0x41
#define HID_KEY_F9          0x42
#define HID_KEY_F10         0x43
#define HID_KEY_F11         0x44
#define HID_KEY_F12         0x45
#define HID_KEY_PRTSCR      0x46
#define HID_KEY_SCROLLLOCK  0x47
#define HID_KEY_PAUSE       0x48
#define HID_KEY_INSERT      0x49
#define HID_KEY_HOME        0x4A
#define HID_KEY_PAGEUP      0x4B
#define HID_KEY_DELETE      0x4C
#define HID_KEY_END         0x4D
#define HID_KEY_PAGEDOWN    0x4E
#define HID_KEY_RIGHT       0x4F
#define HID_KEY_LEFT        0x50
#define HID_KEY_DOWN        0x51
#define HID_KEY_UP          0x52

// Modifier keys
#define HID_MOD_LCTRL       0x01
#define HID_MOD_LSHIFT      0x02
#define HID_MOD_LALT        0x04
#define HID_MOD_LGUI        0x08
#define HID_MOD_RCTRL       0x10
#define HID_MOD_RSHIFT      0x20
#define HID_MOD_RALT        0x40
#define HID_MOD_RGUI        0x80

// Структура Boot Keyboard Report
typedef struct {
    uint8_t modifiers;
    uint8_t reserved;
    uint8_t keycode[6];
} __attribute__((packed)) hid_keyboard_report_t;

// Структура Boot Mouse Report
typedef struct {
    uint8_t buttons;
    int8_t x;
    int8_t y;
    int8_t wheel;
} __attribute__((packed)) hid_mouse_report_t;

// Структура HID устройства
typedef struct {
    usb_device_t* usb_dev;
    uint8_t type;           // 1 = keyboard, 2 = mouse
    uint8_t protocol;       // Boot or Report protocol
    uint8_t report_size;
    uint8_t enabled;
    
    // Для клавиатуры
    hid_keyboard_report_t last_keyboard_report;
    uint8_t key_states[256];
    
    // Для мыши
    hid_mouse_report_t last_mouse_report;
    int32_t x_accum;
    int32_t y_accum;
    int32_t wheel_accum;
} hid_device_t;

// Функции HID драйвера
void hid_init(void);
void hid_poll(void);
uint8_t hid_get_keyboard_count(void);
uint8_t hid_get_mouse_count(void);
hid_device_t* hid_get_keyboard(uint8_t index);
hid_device_t* hid_get_mouse(uint8_t index);

// Функции для работы с клавиатурой
uint8_t hid_keyboard_get_key(hid_device_t* keyboard, uint8_t keycode);
uint8_t hid_keyboard_get_modifiers(hid_device_t* keyboard);
void hid_keyboard_clear_buffer(hid_device_t* keyboard);

// Функции для работы с мышью
int32_t hid_mouse_get_x(hid_device_t* mouse);
int32_t hid_mouse_get_y(hid_device_t* mouse);
int32_t hid_mouse_get_wheel(hid_device_t* mouse);
uint8_t hid_mouse_get_buttons(hid_device_t* mouse);
void hid_mouse_clear_movement(hid_device_t* mouse);

// Вспомогательные функции
const char* hid_get_key_name(uint8_t keycode);
uint8_t hid_is_printable(uint8_t keycode);
char hid_key_to_ascii(uint8_t keycode, uint8_t modifiers);

#endif // HID_H