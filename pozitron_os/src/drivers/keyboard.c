#include "drivers/keyboard.h"
#include "drivers/ports.h"
#include "drivers/vga.h"
#include "drivers/pic.h"
#include "drivers/serial.h"
#include "core/event.h"
#include <stddef.h>

// Состояние клавиатуры
static keyboard_state_t kbd_state = {0};

// Таблицы скан-кодов (только печатные символы)
static const char keyboard_map[128] = {
    0,    0,    '1',  '2',  '3',  '4',  '5',  '6',
    '7',  '8',  '9',  '0',  '-',  '=',  '\b', '\t',
    'q',  'w',  'e',  'r',  't',  'y',  'u',  'i',
    'o',  'p',  '[',  ']',  '\n', 0,    'a',  's',
    'd',  'f',  'g',  'h',  'j',  'k',  'l',  ';',
    '\'', '`',  0,    '\\', 'z',  'x',  'c',  'v',
    'b',  'n',  'm',  ',',  '.',  '/',  0,    '*',
    0,    ' ',  0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    '7',
    '8',  '9',  '-',  '4',  '5',  '6',  '+',  '1',
    '2',  '3',  '0',  '.',  0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0
};

static const char keyboard_map_shift[128] = {
    0,    0,    '!',  '@',  '#',  '$',  '%',  '^',
    '&',  '*',  '(',  ')',  '_',  '+',  '\b', '\t',
    'Q',  'W',  'E',  'R',  'T',  'Y',  'U',  'I',
    'O',  'P',  '{',  '}',  '\n', 0,    'A',  'S',
    'D',  'F',  'G',  'H',  'J',  'K',  'L',  ':',
    '"',  '~',  0,    '|',  'Z',  'X',  'C',  'V',
    'B',  'N',  'M',  '<',  '>',  '?',  0,    '*',
    0,    ' ',  0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    '7',
    '8',  '9',  '-',  '4',  '5',  '6',  '+',  '1',
    '2',  '3',  '0',  '.',  0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0
};

// Инициализация клавиатуры
void keyboard_init(void) {
    serial_puts("[KEYBOARD] Initializing...\n");
    
    // Устанавливаем обработчик прерывания клавиатуры (IRQ1)
    irq_install_handler(1, keyboard_handler);
    
    // Сброс состояния
    kbd_state.shift = 0;
    kbd_state.ctrl = 0;
    kbd_state.alt = 0;
    kbd_state.caps = 0;
    kbd_state.numlock = 0;
    kbd_state.scrolllock = 0;
    
    serial_puts("[KEYBOARD] Initialized\n");
}

// Обработчик прерывания клавиатуры
void keyboard_handler(registers_t* regs) {
    (void)regs;
    
    uint8_t status = inb(KEYBOARD_STATUS_PORT);
    if (!(status & 0x01)) return;
    
    uint8_t scancode = inb(KEYBOARD_DATA_PORT);
    
    // Создаём событие
    event_t event;
    
    if (scancode & 0x80) { // Отпускание клавиши
        event.type = EVENT_KEY_RELEASE;
        event.data1 = scancode & 0x7F; // Код клавиши без флага отпускания
    } else { // Нажатие клавиши
        event.type = EVENT_KEY_PRESS;
        event.data1 = scancode;
        
        // Для печатных символов сохраняем символ в data2
        if (scancode < sizeof(keyboard_map)) {
            char c = keyboard_scancode_to_char(scancode, kbd_state);
            event.data2 = (uint32_t)c;
        }
    }
    
    event.data2 = 0; // Пока не используется
    
    // Отправляем событие в очередь
    event_post(event);
    
    // Для обратной совместимости - оставляем вывод в VGA
    if (!(scancode & 0x80)) { // Только нажатия
        switch (scancode) {
            case KEY_ENTER:
                vga_putchar('\n');
                break;
            case KEY_BACKSPACE:
                vga_puts("\b \b");
                break;
            default:
                if (scancode < sizeof(keyboard_map)) {
                    char c = keyboard_scancode_to_char(scancode, kbd_state);
                    if (c != 0) {
                        vga_putchar(c);
                    }
                }
                break;
        }
    }
    else {
        // Обновляем состояние модификаторов
        switch (scancode) {
            case KEY_LSHIFT:
            case KEY_RSHIFT:
                kbd_state.shift = 1;
                break;
            case KEY_LCTRL:
                kbd_state.ctrl = 1;
                break;
            case KEY_LALT:
                kbd_state.alt = 1;
                break;
            case KEY_CAPSLOCK:
                kbd_state.caps = !kbd_state.caps;
                vga_puts(kbd_state.caps ? "[CAPS ON] " : "[CAPS OFF] ");
                break;
            case KEY_NUMLOCK:
                kbd_state.numlock = !kbd_state.numlock;
                break;
            case KEY_SCROLLLOCK:
                kbd_state.scrolllock = !kbd_state.scrolllock;
                break;
                
            // Специальные клавиши
            case KEY_ENTER:
                vga_putchar('\n');
                serial_write('\n');
                break;
                
            case KEY_BACKSPACE:
                vga_puts("\b \b"); // Стираем символ
                break;
                
            case KEY_TAB:
                vga_putchar('\t');
                break;
                
            case KEY_ESC:
                vga_puts("[ESC] ");
                break;
                
            case KEY_F1:
                vga_puts("[F1] ");
                break;
                
            case KEY_F2:
                vga_puts("[F2] ");
                break;
                
            case KEY_F3:
                vga_puts("[F3] ");
                break;
                
            case KEY_F4:
                vga_puts("[F4] ");
                break;
                
            case KEY_F5:
                vga_puts("[F5] ");
                break;
                
            case KEY_F6:
                vga_puts("[F6] ");
                break;
                
            case KEY_F7:
                vga_puts("[F7] ");
                break;
                
            case KEY_F8:
                vga_puts("[F8] ");
                break;
                
            case KEY_F9:
                vga_puts("[F9] ");
                break;
                
            case KEY_F10:
                vga_puts("[F10] ");
                break;
                
            // Печатные символы
            default:
                if (scancode < sizeof(keyboard_map)) {
                    char c;
                    
                    // Выбираем правильную таблицу в зависимости от SHIFT
                    if (kbd_state.shift) {
                        c = keyboard_map_shift[scancode];
                    } else {
                        c = keyboard_map[scancode];
                    }
                    
                    // Учитываем CAPS LOCK только для букв
                    if (kbd_state.caps) {
                        if (c >= 'a' && c <= 'z') {
                            c -= 32; // В верхний регистр
                        } else if (c >= 'A' && c <= 'Z') {
                            c += 32; // В нижний регистр
                        }
                    }
                    
                    // Выводим символ если он не нулевой
                    if (c != 0) {
                        vga_putchar(c);
                        serial_write(c); // Только символ в сериал
                    }
                }
                break;
        }
    }
    
    // Отправляем EOI контроллеру прерываний
    pic_send_eoi(0);
}

// Преобразование скан-кода в символ (для внешнего использования)
char keyboard_scancode_to_char(uint8_t scancode, keyboard_state_t state) {
    if (scancode >= sizeof(keyboard_map)) {
        return 0;
    }
    
    char c;
    if (state.shift) {
        c = keyboard_map_shift[scancode];
    } else {
        c = keyboard_map[scancode];
    }
    
    // Учитываем CAPS LOCK
    if (state.caps) {
        if (c >= 'a' && c <= 'z') {
            c -= 32;
        } else if (c >= 'A' && c <= 'Z') {
            c += 32;
        }
    }
    
    return c;
}

// Получить последний скан-код (блокирующий)
int keyboard_get_scancode(void) {
    // Ждём данные
    while (!(inb(KEYBOARD_STATUS_PORT) & 0x01)) {
        asm volatile ("pause");
    }
    
    return inb(KEYBOARD_DATA_PORT);
}

// Ожидание нажатия любой клавиши
void keyboard_wait_for_key(void) {
    vga_puts("Press any key to continue...");
    
    // Сбрасываем буфер клавиатуры
    while (inb(KEYBOARD_STATUS_PORT) & 0x01) {
        inb(KEYBOARD_DATA_PORT);
    }
    
    // Ждём новое нажатие
    while (1) {
        if (inb(KEYBOARD_STATUS_PORT) & 0x01) {
            uint8_t scancode = inb(KEYBOARD_DATA_PORT);
            if (!(scancode & 0x80)) { // Только нажатие, не отпускание
                break;
            }
        }
        asm volatile ("hlt");
    }
    
    vga_puts("\n");
}