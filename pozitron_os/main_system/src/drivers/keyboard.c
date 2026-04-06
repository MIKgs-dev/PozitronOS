#include "drivers/keyboard.h"
#include "drivers/ports.h"
#include "drivers/vga.h"
#include "drivers/pic.h"
#include "drivers/serial.h"
#include "core/event.h"
#include <stddef.h>

static keyboard_state_t kbd_state = {0};

// Скан-коды клавиш (PC/AT)
#define SCAN_ESC        0x01
#define SCAN_1          0x02
#define SCAN_2          0x03
#define SCAN_3          0x04
#define SCAN_4          0x05
#define SCAN_5          0x06
#define SCAN_6          0x07
#define SCAN_7          0x08
#define SCAN_8          0x09
#define SCAN_9          0x0A
#define SCAN_0          0x0B
#define SCAN_MINUS      0x0C
#define SCAN_EQUAL      0x0D
#define SCAN_BACKSPACE  0x0E
#define SCAN_TAB        0x0F
#define SCAN_Q          0x10
#define SCAN_W          0x11
#define SCAN_E          0x12
#define SCAN_R          0x13
#define SCAN_T          0x14
#define SCAN_Y          0x15
#define SCAN_U          0x16
#define SCAN_I          0x17
#define SCAN_O          0x18
#define SCAN_P          0x19
#define SCAN_LBRACKET   0x1A
#define SCAN_RBRACKET   0x1B
#define SCAN_ENTER      0x1C
#define SCAN_LCTRL      0x1D
#define SCAN_A          0x1E
#define SCAN_S          0x1F
#define SCAN_D          0x20
#define SCAN_F          0x21
#define SCAN_G          0x22
#define SCAN_H          0x23
#define SCAN_J          0x24
#define SCAN_K          0x25
#define SCAN_L          0x26
#define SCAN_SEMICOLON  0x27
#define SCAN_QUOTE      0x28
#define SCAN_BACKTICK   0x29
#define SCAN_LSHIFT     0x2A
#define SCAN_BACKSLASH  0x2B
#define SCAN_Z          0x2C
#define SCAN_X          0x2D
#define SCAN_C          0x2E
#define SCAN_V          0x2F
#define SCAN_B          0x30
#define SCAN_N          0x31
#define SCAN_M          0x32
#define SCAN_COMMA      0x33
#define SCAN_DOT        0x34
#define SCAN_SLASH      0x35
#define SCAN_RSHIFT     0x36
#define SCAN_KP_STAR    0x37
#define SCAN_LALT       0x38
#define SCAN_SPACE      0x39
#define SCAN_CAPSLOCK   0x3A
#define SCAN_F1         0x3B
#define SCAN_F2         0x3C
#define SCAN_F3         0x3D
#define SCAN_F4         0x3E
#define SCAN_F5         0x3F
#define SCAN_F6         0x40
#define SCAN_F7         0x41
#define SCAN_F8         0x42
#define SCAN_F9         0x43
#define SCAN_F10        0x44
#define SCAN_NUMLOCK    0x45
#define SCAN_SCROLLLOCK 0x46
#define SCAN_HOME       0x47
#define SCAN_KP_7       0x47
#define SCAN_UP         0x48
#define SCAN_KP_8       0x48
#define SCAN_PAGEUP     0x49
#define SCAN_KP_9       0x49
#define SCAN_KP_MINUS   0x4A
#define SCAN_LEFT       0x4B
#define SCAN_KP_4       0x4B
#define SCAN_KP_5       0x4C
#define SCAN_RIGHT      0x4D
#define SCAN_KP_6       0x4D
#define SCAN_KP_PLUS    0x4E
#define SCAN_END        0x4F
#define SCAN_KP_1       0x4F
#define SCAN_DOWN       0x50
#define SCAN_KP_2       0x50
#define SCAN_PAGEDOWN   0x51
#define SCAN_KP_3       0x51
#define SCAN_INSERT     0x52
#define SCAN_KP_0       0x52
#define SCAN_DELETE     0x53
#define SCAN_KP_DOT     0x53
#define SCAN_F11        0x57
#define SCAN_F12        0x58

// Таблица преобразования скан-кодов в ASCII (без Shift)
static const uint8_t scancode_to_ascii[128] = {
    0,      0,      '1',    '2',    '3',    '4',    '5',    '6',    // 0-7
    '7',    '8',    '9',    '0',    '-',    '=',    '\b',   '\t',   // 8-15
    'q',    'w',    'e',    'r',    't',    'y',    'u',    'i',    // 16-23
    'o',    'p',    '[',    ']',    '\n',   0,      'a',    's',    // 24-31
    'd',    'f',    'g',    'h',    'j',    'k',    'l',    ';',    // 32-39
    '\'',   '`',    0,      '\\',   'z',    'x',    'c',    'v',    // 40-47
    'b',    'n',    'm',    ',',    '.',    '/',    0,      '*',    // 48-55
    0,      ' ',    0,      0,      0,      0,      0,      0,      // 56-63
    0,      0,      0,      0,      0,      0,      0,      '7',    // 64-71
    '8',    '9',    '-',    '4',    '5',    '6',    '+',    '1',    // 72-79
    '2',    '3',    '0',    '.',    0,      0,      0,      0,      // 80-87
    0,      0,      0,      0,      0,      0,      0,      0,      // 88-95
    0,      0,      0,      0,      0,      0,      0,      0,      // 96-103
    0,      0,      0,      0,      0,      0,      0,      0,      // 104-111
    0,      0,      0,      0,      0,      0,      0,      0,      // 112-119
    0,      0,      0,      0,      0,      0,      0,      0       // 120-127
};

// Таблица для Shift
static const uint8_t scancode_to_ascii_shift[128] = {
    0,      0,      '!',    '@',    '#',    '$',    '%',    '^',    // 0-7
    '&',    '*',    '(',    ')',    '_',    '+',    '\b',   '\t',   // 8-15
    'Q',    'W',    'E',    'R',    'T',    'Y',    'U',    'I',    // 16-23
    'O',    'P',    '{',    '}',    '\n',   0,      'A',    'S',    // 24-31
    'D',    'F',    'G',    'H',    'J',    'K',    'L',    ':',    // 32-39
    '"',    '~',    0,      '|',    'Z',    'X',    'C',    'V',    // 40-47
    'B',    'N',    'M',    '<',    '>',    '?',    0,      '*',    // 48-55
    0,      ' ',    0,      0,      0,      0,      0,      0,      // 56-63
    0,      0,      0,      0,      0,      0,      0,      '7',    // 64-71
    '8',    '9',    '-',    '4',    '5',    '6',    '+',    '1',    // 72-79
    '2',    '3',    '0',    '.',    0,      0,      0,      0,      // 80-87
    0,      0,      0,      0,      0,      0,      0,      0,      // 88-95
    0,      0,      0,      0,      0,      0,      0,      0,      // 96-103
    0,      0,      0,      0,      0,      0,      0,      0,      // 104-111
    0,      0,      0,      0,      0,      0,      0,      0,      // 112-119
    0,      0,      0,      0,      0,      0,      0,      0       // 120-127
};

void keyboard_init(void) {
    serial_puts("[KEYBOARD] Initializing...\n");
    
    irq_install_handler(1, keyboard_handler);
    
    kbd_state.shift = 0;
    kbd_state.ctrl = 0;
    kbd_state.alt = 0;
    kbd_state.caps = 0;
    kbd_state.numlock = 1; // По умолчанию включен
    kbd_state.scrolllock = 0;
    
    serial_puts("[KEYBOARD] Initialized\n");
}

// Определяем, является ли скан-код печатным символом
static int is_printable(uint8_t scancode) {
    if (scancode >= 0x47 && scancode <= 0x53) return 0; // Стрелки, Home, End и т.д.
    if (scancode >= 0x3B && scancode <= 0x44) return 0; // F1-F10
    if (scancode == SCAN_ESC) return 0;
    if (scancode == SCAN_BACKSPACE) return 0;
    if (scancode == SCAN_ENTER) return 0;
    if (scancode == SCAN_TAB) return 0;
    if (scancode == SCAN_LCTRL || scancode == SCAN_LALT) return 0;
    if (scancode == SCAN_LSHIFT || scancode == SCAN_RSHIFT) return 0;
    if (scancode == SCAN_CAPSLOCK || scancode == SCAN_NUMLOCK) return 0;
    if (scancode == SCAN_SCROLLLOCK) return 0;
    if (scancode >= 0x54) return 0; // Всё, что выше 0x53 - спецклавиши
    
    return 1;
}

// ============ УПРАВЛЕНИЕ СВЕТОДИОДАМИ ============

// Ожидание готовности контроллера к отправке команды
static void keyboard_wait_write(void) {
    uint32_t timeout = 100000;
    while (--timeout) {
        if (!(inb(KEYBOARD_STATUS_PORT) & 0x02)) { // Бит 2 = Input buffer full
            return;
        }
        asm volatile("pause");
    }
    serial_puts("[KBD] Timeout waiting for write\n");
}

// Ожидание данных от клавиатуры (ACK)
static uint8_t keyboard_wait_read(void) {
    uint32_t timeout = 100000;
    while (--timeout) {
        if (inb(KEYBOARD_STATUS_PORT) & 0x01) { // Бит 1 = Output buffer full
            return inb(KEYBOARD_DATA_PORT);
        }
        asm volatile("pause");
    }
    serial_puts("[KBD] Timeout waiting for read\n");
    return 0;
}

// Отправка команды установки светодиодов
static void keyboard_update_leds(void) {
    // Не отправляем команду, если клавиатура ещё не готова (на всякий случай)
    // Но в целом это должно работать.
    
    serial_puts("[KBD] Updating LEDs: Caps=");
    serial_puts_num(kbd_state.caps);
    serial_puts(" Num=");
    serial_puts_num(kbd_state.numlock);
    serial_puts(" Scroll=");
    serial_puts_num(kbd_state.scrolllock);
    serial_puts("\n");

    // 1. Ждем готовности к отправке команды
    keyboard_wait_write();
    
    // 2. Отправляем команду 0xED
    outb(KEYBOARD_DATA_PORT, 0xED);
    
    // 3. Ждем ACK (0xFA) от клавиатуры
    uint8_t ack = keyboard_wait_read();
    if (ack != 0xFA) {
        serial_puts("[KBD] LED command failed (no ACK). Response: 0x");
        serial_puts_num_hex(ack);
        serial_puts("\n");
        return;
    }
    
    // 4. Формируем байт светодиодов
    uint8_t led_byte = 0;
    if (kbd_state.scrolllock) led_byte |= 1; // Scroll Lock = бит 0
    if (kbd_state.numlock)     led_byte |= 2; // Num Lock    = бит 1
    if (kbd_state.caps)        led_byte |= 4; // Caps Lock   = бит 2
    
    // 5. Ждем готовности к отправке данных
    keyboard_wait_write();
    
    // 6. Отправляем байт светодиодов
    outb(KEYBOARD_DATA_PORT, led_byte);
    
    // 7. Ждем финальный ACK (опционально, но для надежности)
    ack = keyboard_wait_read();
    if (ack != 0xFA) {
        serial_puts("[KBD] LED data command failed (no ACK). Response: 0x");
        serial_puts_num_hex(ack);
        serial_puts("\n");
    }
}

void keyboard_handler(registers_t* regs) {
    (void)regs;
    
    uint8_t status = inb(KEYBOARD_STATUS_PORT);
    if (!(status & 0x01)) return;
    
    uint8_t scancode = inb(KEYBOARD_DATA_PORT);
    
    uint8_t key = scancode & 0x7F;
    uint8_t released = scancode & 0x80;
    
    // Обновляем состояние модификаторов
    switch (key) {
        case SCAN_LSHIFT:
        case SCAN_RSHIFT:
            kbd_state.shift = !released;
            break;
        case SCAN_LCTRL:
            kbd_state.ctrl = !released;
            break;
        case SCAN_LALT:
            kbd_state.alt = !released;
            break;
        case SCAN_CAPSLOCK:
            if (!released) {
                kbd_state.caps = !kbd_state.caps;
                keyboard_update_leds();
            }
            break;
        case SCAN_NUMLOCK:
            if (!released) {
                kbd_state.numlock = !kbd_state.numlock;
                keyboard_update_leds();
            }
            break;
        case SCAN_SCROLLLOCK:
            if (!released) {
                kbd_state.scrolllock = !kbd_state.scrolllock;
                keyboard_update_leds();
            }
            break;
    }
    
    // Создаём событие нажатия/отпускания для всех клавиш
    event_t event;
    event.type = released ? EVENT_KEY_RELEASE : EVENT_KEY_PRESS;
    event.data1 = key;
    event.data2 = (kbd_state.shift ? 0x01 : 0) | 
                  (kbd_state.ctrl ? 0x02 : 0) | 
                  (kbd_state.alt ? 0x04 : 0);
    event.key.scancode = key;
    event.key.ascii = 0;
    event.key.modifiers = event.data2;
    
    event_post(event);
    
    // Для печатных символов (только при нажатии) создаём EVENT_TEXT_INPUT
    if (!released && is_printable(key)) {
        uint8_t ascii;
        
        // Выбираем таблицу в зависимости от Shift
        if (kbd_state.shift) {
            ascii = scancode_to_ascii_shift[key];
        } else {
            ascii = scancode_to_ascii[key];
        }
        
        // Учитываем Caps Lock (только для букв)
        if (kbd_state.caps && ascii >= 'a' && ascii <= 'z') {
            ascii -= 32;
        } else if (kbd_state.caps && ascii >= 'A' && ascii <= 'Z') {
            ascii += 32;
        }
        
        // Отправляем только если получили осмысленный символ
        if (ascii >= 32 && ascii <= 126) {
            event_t text_event;
            text_event.type = EVENT_TEXT_INPUT;
            text_event.data1 = (uint32_t)ascii;
            text_event.data2 = 0;
            text_event.key.scancode = key;
            text_event.key.ascii = ascii;
            text_event.key.modifiers = event.data2;
            event_post(text_event);
        }
        
        // Для обратной совместимости с VGA
        if (ascii == '\n') vga_putchar('\n');
        else if (ascii == '\b') vga_puts("\b \b");
        else if (ascii >= ' ') vga_putchar(ascii);
    }
    // Специальные клавиши обрабатываются в core.c через handle_input_special_key
    
    pic_send_eoi(0);
}

char keyboard_scancode_to_char(uint8_t scancode, keyboard_state_t state) {
    if (scancode >= 128) return 0;
    if (!is_printable(scancode)) return 0;
    
    char c = state.shift ? scancode_to_ascii_shift[scancode] : scancode_to_ascii[scancode];
    
    if (state.caps) {
        if (c >= 'a' && c <= 'z') c -= 32;
        else if (c >= 'A' && c <= 'Z') c += 32;
    }
    
    return c;
}

int keyboard_get_scancode(void) {
    while (!(inb(KEYBOARD_STATUS_PORT) & 0x01)) {
        asm volatile ("pause");
    }
    return inb(KEYBOARD_DATA_PORT);
}

void keyboard_wait_for_key(void) {
    vga_puts("Press any key to continue...");
    
    while (inb(KEYBOARD_STATUS_PORT) & 0x01) {
        inb(KEYBOARD_DATA_PORT);
    }
    
    while (1) {
        if (inb(KEYBOARD_STATUS_PORT) & 0x01) {
            uint8_t scancode = inb(KEYBOARD_DATA_PORT);
            if (!(scancode & 0x80)) break;
        }
        asm volatile ("hlt");
    }
    
    vga_puts("\n");
}