#ifndef CORE_EVENT_H
#define CORE_EVENT_H

#include <stdint.h>

// Типы событий
typedef enum {
    EVENT_NONE = 0,
    EVENT_KEY_PRESS,
    EVENT_KEY_RELEASE,
    EVENT_MOUSE_MOVE,
    EVENT_MOUSE_CLICK,
    EVENT_MOUSE_RELEASE,
    EVENT_TIMER_TICK,
    EVENT_QUIT
} event_type_t;

// Структура события
typedef struct {
    event_type_t type;
    uint32_t data1;
    uint32_t data2;
    uint32_t timestamp;
} event_t;

// Инициализация системы событий
void event_init(void);

// Добавить событие в очередь (вызывается из обработчиков прерываний)
void event_post(event_t event);

// Получить событие из очереди (вызывается из главного цикла)
int event_poll(event_t* event);

// Проверить, есть ли события в очереди
int event_available(void);

// Очистить очередь событий
void event_clear(void);

#endif // CORE_EVENT_H