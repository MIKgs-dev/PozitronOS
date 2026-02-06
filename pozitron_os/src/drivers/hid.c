#include "drivers/hid.h"
#include "drivers/serial.h"
#include "drivers/usb.h"
#include "kernel/memory.h"
#include "core/event.h"
#include <stddef.h>
#include "lib/string.h"

// Максимальное количество HID устройств
#define MAX_HID_DEVICES 8

// Глобальные переменные HID
static hid_device_t hid_devices[MAX_HID_DEVICES];
static uint8_t hid_device_count = 0;
static uint8_t keyboard_count = 0;
static uint8_t mouse_count = 0;

// Таблица преобразования scancode в ASCII (без Shift)
static const char keycode_to_ascii[128] = {
    0,   0,   0,   0,   'a', 'b', 'c', 'd',  // 0x00-0x07
    'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l',  // 0x08-0x0F
    'm', 'n', 'o', 'p', 'q', 'r', 's', 't',  // 0x10-0x17
    'u', 'v', 'w', 'x', 'y', 'z', '1', '2',  // 0x18-0x1F
    '3', '4', '5', '6', '7', '8', '9', '0',  // 0x20-0x27
    '\n', 0, '\b', '\t', ' ', '-', '=', '[', // 0x28-0x2F
    ']', '\\', 0, ';', '\'', '`', ',', '.',  // 0x30-0x37
    '/', 0,   0,   0,   0,   0,   0,   0,    // 0x38-0x3F
    0,   0,   0,   0,   0,   0,   0,   0,    // 0x40-0x47
    0,   0,   0,   0,   0,   0,   0,   0,    // 0x48-0x4F
    0,   0,   0,   0,   0,   0,   0,   0,    // 0x50-0x57
    0,   0,   0,   0,   0,   0,   0,   0,    // 0x58-0x5F
};

// Таблица с Shift
static const char keycode_to_ascii_shift[128] = {
    0,   0,   0,   0,   'A', 'B', 'C', 'D',  // 0x00-0x07
    'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L',  // 0x08-0x0F
    'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T',  // 0x10-0x17
    'U', 'V', 'W', 'X', 'Y', 'Z', '!', '@',  // 0x18-0x1F
    '#', '$', '%', '^', '&', '*', '(', ')',  // 0x20-0x27
    '\n', 0, '\b', '\t', ' ', '_', '+', '{', // 0x28-0x2F
    '}', '|', 0, ':', '"', '~', '<', '>',   // 0x30-0x37
    '?', 0,   0,   0,   0,   0,   0,   0,    // 0x38-0x3F
    0,   0,   0,   0,   0,   0,   0,   0,    // 0x40-0x47
    0,   0,   0,   0,   0,   0,   0,   0,    // 0x48-0x4F
    0,   0,   0,   0,   0,   0,   0,   0,    // 0x50-0x57
    0,   0,   0,   0,   0,   0,   0,   0,    // 0x58-0x5F
};

// Инициализация HID системы
void hid_init(void) {
    serial_puts("[HID] Initializing HID subsystem\n");
    
    memset(hid_devices, 0, sizeof(hid_devices));
    hid_device_count = 0;
    keyboard_count = 0;
    mouse_count = 0;
    
    // Сканируем USB устройства на предмет HID
    uint8_t usb_count = usb_get_device_count();
    
    for (uint8_t i = 0; i < usb_count; i++) {
        usb_device_t* usb_dev = usb_get_device(i);
        
        if (!usb_dev->present || !usb_dev->is_hid) {
            continue;
        }
        
        if (hid_device_count >= MAX_HID_DEVICES) {
            serial_puts("[HID] WARNING: Too many HID devices\n");
            break;
        }
        
        hid_device_t* hid_dev = &hid_devices[hid_device_count];
        memset(hid_dev, 0, sizeof(hid_device_t));
        
        hid_dev->usb_dev = usb_dev;
        hid_dev->enabled = 1;
        
        // Определяем тип устройства
        if (usb_dev->protocol == HID_PROTOCOL_KEYBOARD) {
            hid_dev->type = 1; // Keyboard
            hid_dev->protocol = 0; // Boot protocol
            keyboard_count++;
            
            serial_puts("[HID] Keyboard detected\n");
        } else if (usb_dev->protocol == HID_PROTOCOL_MOUSE) {
            hid_dev->type = 2; // Mouse
            hid_dev->protocol = 0; // Boot protocol
            mouse_count++;
            
            serial_puts("[HID] Mouse detected\n");
        } else {
            // Другие HID устройства пока не поддерживаем
            continue;
        }
        
        hid_device_count++;
    }
    
    serial_puts("[HID] Found ");
    serial_puts_num(keyboard_count);
    serial_puts(" keyboard(s) and ");
    serial_puts_num(mouse_count);
    serial_puts(" mouse(s)\n");
}

// Обработка HID отчетов
void hid_poll(void) {
    for (uint8_t i = 0; i < hid_device_count; i++) {
        hid_device_t* hid_dev = &hid_devices[i];
        
        if (!hid_dev->enabled || !hid_dev->usb_dev || !hid_dev->usb_dev->present) {
            continue;
        }
        
        // Читаем отчет с устройства
        uint8_t report_buffer[64];
        int result = usb_interrupt_transfer(hid_dev->usb_dev,
                                          hid_dev->usb_dev->hid_endpoint_in,
                                          report_buffer,
                                          hid_dev->usb_dev->hid_report_size,
                                          0); // Неблокирующий
        
        if (result > 0) {
            // Обрабатываем отчет
            if (hid_dev->type == 1) {
                // Клавиатура
                hid_keyboard_report_t* report = (hid_keyboard_report_t*)report_buffer;
                
                // Проверяем изменения
                if (memcmp(&hid_dev->last_keyboard_report, report, sizeof(hid_keyboard_report_t)) != 0) {
                    // Обновляем состояние клавиш
                    uint8_t old_modifiers = hid_dev->last_keyboard_report.modifiers;
                    uint8_t new_modifiers = report->modifiers;
                    
                    // Проверяем изменения в модификаторах
                    for (uint8_t mod = 0; mod < 8; mod++) {
                        uint8_t mod_bit = 1 << mod;
                        uint8_t was_pressed = old_modifiers & mod_bit;
                        uint8_t is_pressed = new_modifiers & mod_bit;
                        
                        if (was_pressed != is_pressed) {
                            // Отправляем событие
                            event_t event;
                            event.type = is_pressed ? EVENT_KEY_PRESS : EVENT_KEY_RELEASE;
                            
                            // Преобразуем модификатор в scancode
                            switch(mod_bit) {
                                case HID_MOD_LCTRL: event.data1 = 0x1D; break;
                                case HID_MOD_LSHIFT: event.data1 = 0x2A; break;
                                case HID_MOD_LALT: event.data1 = 0x38; break;
                                case HID_MOD_LGUI: event.data1 = 0x5B; break;
                                case HID_MOD_RCTRL: event.data1 = 0x1D; break;
                                case HID_MOD_RSHIFT: event.data1 = 0x36; break;
                                case HID_MOD_RALT: event.data1 = 0x38; break;
                                case HID_MOD_RGUI: event.data1 = 0x5C; break;
                            }
                            
                            event.data2 = is_pressed;
                            event_post(event);
                        }
                    }
                    
                    // Проверяем обычные клавиши
                    uint8_t old_keys[6];
                    uint8_t new_keys[6];
                    memcpy(old_keys, hid_dev->last_keyboard_report.keycode, 6);
                    memcpy(new_keys, report->keycode, 6);
                    
                    // Проверяем отпущенные клавиши
                    for (uint8_t j = 0; j < 6; j++) {
                        uint8_t old_key = old_keys[j];
                        if (old_key != 0) {
                            uint8_t still_pressed = 0;
                            for (uint8_t k = 0; k < 6; k++) {
                                if (new_keys[k] == old_key) {
                                    still_pressed = 1;
                                    break;
                                }
                            }
                            
                            if (!still_pressed) {
                                // Клавиша отпущена
                                hid_dev->key_states[old_key] = 0;
                                
                                event_t event;
                                event.type = EVENT_KEY_RELEASE;
                                event.data1 = old_key;
                                event.data2 = 0;
                                event_post(event);
                            }
                        }
                    }
                    
                    // Проверяем нажатые клавиши
                    for (uint8_t j = 0; j < 6; j++) {
                        uint8_t new_key = new_keys[j];
                        if (new_key != 0) {
                            uint8_t was_pressed = 0;
                            for (uint8_t k = 0; k < 6; k++) {
                                if (old_keys[k] == new_key) {
                                    was_pressed = 1;
                                    break;
                                }
                            }
                            
                            if (!was_pressed) {
                                // Новая клавиша нажата
                                hid_dev->key_states[new_key] = 1;
                                
                                event_t event;
                                event.type = EVENT_KEY_PRESS;
                                event.data1 = new_key;
                                event.data2 = new_modifiers;
                                event_post(event);
                                
                                // Для печатных символов можно добавить специальную обработку в GUI
                                // но пока просто используем стандартные KEY_PRESS/KEY_RELEASE
                            }
                        }
                    }
                    
                    // Сохраняем новый отчет
                    memcpy(&hid_dev->last_keyboard_report, report, sizeof(hid_keyboard_report_t));
                }
            }
            else if (hid_dev->type == 2) {
                // Мышь
                hid_mouse_report_t* report = (hid_mouse_report_t*)report_buffer;
                
                // Проверяем изменения
                if (memcmp(&hid_dev->last_mouse_report, report, sizeof(hid_mouse_report_t)) != 0) {
                    // Проверяем кнопки
                    uint8_t old_buttons = hid_dev->last_mouse_report.buttons;
                    uint8_t new_buttons = report->buttons;
                    
                    for (uint8_t btn = 0; btn < 3; btn++) {
                        uint8_t btn_bit = 1 << btn;
                        uint8_t was_pressed = old_buttons & btn_bit;
                        uint8_t is_pressed = new_buttons & btn_bit;
                        
                        if (was_pressed != is_pressed) {
                            event_t event;
                            if (is_pressed) {
                                event.type = EVENT_MOUSE_CLICK;
                            } else {
                                event.type = EVENT_MOUSE_RELEASE;
                            }
                            event.data1 = btn + 1; // 1 = левая, 2 = правая, 3 = средняя
                            event.data2 = 0;
                            event_post(event);
                        }
                    }
                    
                    // Проверяем движение
                    if (report->x != 0 || report->y != 0) {
                        hid_dev->x_accum += report->x;
                        hid_dev->y_accum += report->y;
                        
                        event_t event;
                        event.type = EVENT_MOUSE_MOVE;
                        event.data1 = hid_dev->x_accum;
                        event.data2 = hid_dev->y_accum;
                        event_post(event);
                        
                        // Сбрасываем накопленные значения после отправки
                        hid_dev->x_accum = 0;
                        hid_dev->y_accum = 0;
                    }
                    
                    // Колесико мыши (используем то же событие MOVE, но в data1 ставим значение колеса)
                    if (report->wheel != 0) {
                        event_t event;
                        event.type = EVENT_MOUSE_MOVE;
                        event.data1 = report->wheel; // Отрицательное = вниз, положительное = вверх
                        event.data2 = 0x80000000; // Флаг что это колесико
                        event_post(event);
                    }
                    
                    // Сохраняем новый отчет
                    memcpy(&hid_dev->last_mouse_report, report, sizeof(hid_mouse_report_t));
                }
            }
        }
    }
}

// Получение количества клавиатур
uint8_t hid_get_keyboard_count(void) {
    return keyboard_count;
}

// Получение количества мышей
uint8_t hid_get_mouse_count(void) {
    return mouse_count;
}

// Получение клавиатуры по индексу
hid_device_t* hid_get_keyboard(uint8_t index) {
    uint8_t found = 0;
    for (uint8_t i = 0; i < hid_device_count; i++) {
        if (hid_devices[i].type == 1) {
            if (found == index) {
                return &hid_devices[i];
            }
            found++;
        }
    }
    return NULL;
}

// Получение мыши по индексу
hid_device_t* hid_get_mouse(uint8_t index) {
    uint8_t found = 0;
    for (uint8_t i = 0; i < hid_device_count; i++) {
        if (hid_devices[i].type == 2) {
            if (found == index) {
                return &hid_devices[i];
            }
            found++;
        }
    }
    return NULL;
}

// Проверка нажатия клавиши
uint8_t hid_keyboard_get_key(hid_device_t* keyboard, uint8_t keycode) {
    if (!keyboard || keyboard->type != 1) return 0;
    return keyboard->key_states[keycode];
}

// Получение модификаторов
uint8_t hid_keyboard_get_modifiers(hid_device_t* keyboard) {
    if (!keyboard || keyboard->type != 1) return 0;
    return keyboard->last_keyboard_report.modifiers;
}

// Очистка буфера клавиатуры
void hid_keyboard_clear_buffer(hid_device_t* keyboard) {
    if (!keyboard || keyboard->type != 1) return;
    memset(&keyboard->last_keyboard_report, 0, sizeof(hid_keyboard_report_t));
    memset(keyboard->key_states, 0, sizeof(keyboard->key_states));
}

// Получение координаты X мыши
int32_t hid_mouse_get_x(hid_device_t* mouse) {
    if (!mouse || mouse->type != 2) return 0;
    return mouse->last_mouse_report.x;
}

// Получение координаты Y мыши
int32_t hid_mouse_get_y(hid_device_t* mouse) {
    if (!mouse || mouse->type != 2) return 0;
    return mouse->last_mouse_report.y;
}

// Получение колесика мыши
int32_t hid_mouse_get_wheel(hid_device_t* mouse) {
    if (!mouse || mouse->type != 2) return 0;
    return mouse->last_mouse_report.wheel;
}

// Получение состояния кнопок мыши
uint8_t hid_mouse_get_buttons(hid_device_t* mouse) {
    if (!mouse || mouse->type != 2) return 0;
    return mouse->last_mouse_report.buttons;
}

// Очистка движения мыши
void hid_mouse_clear_movement(hid_device_t* mouse) {
    if (!mouse || mouse->type != 2) return;
    mouse->x_accum = 0;
    mouse->y_accum = 0;
    mouse->wheel_accum = 0;
}

// Получение имени клавиши
const char* hid_get_key_name(uint8_t keycode) {
    static const char* key_names[] = {
        "None", "Error", "Error", "Error", "A", "B", "C", "D",
        "E", "F", "G", "H", "I", "J", "K", "L",
        "M", "N", "O", "P", "Q", "R", "S", "T",
        "U", "V", "W", "X", "Y", "Z", "1", "2",
        "3", "4", "5", "6", "7", "8", "9", "0",
        "Enter", "Esc", "Backspace", "Tab", "Space", "-", "=", "[",
        "]", "\\", "Error", ";", "'", "`", ",", ".",
        "/", "CapsLock", "F1", "F2", "F3", "F4", "F5", "F6",
        "F7", "F8", "F9", "F10", "F11", "F12", "PrintScr", "ScrollLock",
        "Pause", "Insert", "Home", "PageUp", "Delete", "End", "PageDown", "Right",
        "Left", "Down", "Up"
    };
    
    if (keycode < sizeof(key_names) / sizeof(key_names[0])) {
        return key_names[keycode];
    }
    return "Unknown";
}

// Проверка, печатный ли символ
uint8_t hid_is_printable(uint8_t keycode) {
    if (keycode >= HID_KEY_A && keycode <= HID_KEY_Z) return 1;
    if (keycode >= HID_KEY_1 && keycode <= HID_KEY_0) return 1;
    if (keycode == HID_KEY_SPACE) return 1;
    if (keycode == HID_KEY_ENTER) return 1;
    if (keycode == HID_KEY_TAB) return 1;
    if (keycode >= HID_KEY_MINUS && keycode <= HID_KEY_SLASH) return 1;
    return 0;
}

// Преобразование клавиши в ASCII
char hid_key_to_ascii(uint8_t keycode, uint8_t modifiers) {
    if (keycode >= 128) return 0;
    
    uint8_t shift = (modifiers & (HID_MOD_LSHIFT | HID_MOD_RSHIFT)) ? 1 : 0;
    
    if (shift) {
        return keycode_to_ascii_shift[keycode];
    } else {
        return keycode_to_ascii[keycode];
    }
}