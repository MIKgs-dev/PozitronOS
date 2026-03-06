#ifndef CORE_EVENT_H
#define CORE_EVENT_H

#include <stdint.h>

typedef enum {
    EVENT_NONE = 0,
    EVENT_KEY_PRESS,
    EVENT_KEY_RELEASE,
    EVENT_KEY_MODIFIER,
    EVENT_MOUSE_MOVE,
    EVENT_MOUSE_CLICK,
    EVENT_MOUSE_RELEASE,
    EVENT_MOUSE_WHEEL,
    EVENT_TIMER_TICK,
    EVENT_USB_DEVICE_ATTACH,
    EVENT_USB_DEVICE_DETACH,
    EVENT_USB_MSC_ATTACH,
    EVENT_USB_MSC_DETACH,
    EVENT_QUIT,
    
    // Новые события для GUI
    EVENT_TEXT_INPUT,           // Ввод текста (с учетом раскладки)
    EVENT_FOCUS_CHANGE,         // Изменение фокуса
    EVENT_WINDOW_CLOSE,         // Закрытие окна
    EVENT_WINDOW_MINIMIZE,      // Сворачивание
    EVENT_WINDOW_MAXIMIZE,      // Разворачивание
    EVENT_WINDOW_RESTORE,       // Восстановление
    EVENT_SCROLL,               // Скролл (колесико или полоса)
    EVENT_DRAG_START,           // Начало перетаскивания
    EVENT_DRAG,                 // Перетаскивание
    EVENT_DRAG_END              // Конец перетаскивания
} event_type_t;

typedef struct {
    event_type_t type;
    uint32_t data1;
    uint32_t data2;
    uint32_t timestamp;
    
    // Дополнительные поля для сложных событий
    union {
        struct {
            uint32_t x, y;
            uint32_t delta;
            uint8_t button;
        } mouse;
        struct {
            uint8_t scancode;
            uint8_t ascii;
            uint8_t modifiers;
        } key;
        struct {
            uint32_t from_id;
            uint32_t to_id;
        } focus;
        struct {
            uint32_t window_id;
            uint32_t widget_id;
        } target;
    };
} event_t;

// Макросы для упаковки координат мыши (для обратной совместимости)
#define EVENT_PACK_MOUSE(x, y, btn) ( \
    ((uint32_t)((int16_t)(x) & 0xFFFF)) | \
    (((uint32_t)((int16_t)(y) & 0xFFFF)) << 16) | \
    (((uint32_t)((btn) & 0xFF)) << 24) \
)

#define EVENT_UNPACK_X(data) ((int16_t)((data) & 0xFFFF))
#define EVENT_UNPACK_Y(data) ((int16_t)(((data) >> 16) & 0xFFFF))
#define EVENT_UNPACK_BUTTON(data) (((data) >> 24) & 0xFF)

void event_init(void);
void event_post(event_t event);
int event_poll(event_t* event);
int event_available(void);
void event_clear(void);

#endif // CORE_EVENT_H