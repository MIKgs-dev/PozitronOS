#include "core/event.h"
#include "drivers/serial.h"
#include "drivers/timer.h"
#include <stddef.h>

#define EVENT_QUEUE_SIZE 64

static event_t event_queue[EVENT_QUEUE_SIZE];
static uint32_t queue_head = 0;
static uint32_t queue_tail = 0;
static uint32_t queue_count = 0;

// Инициализация очереди событий
void event_init(void) {
    queue_head = 0;
    queue_tail = 0;
    queue_count = 0;
    serial_puts("[EVENT] Event system initialized\n");
}

// Добавить событие в очередь (МОЖЕТ вызываться из прерываний!)
void event_post(event_t event) {
    // Простая защита от переполнения
    if (queue_count >= EVENT_QUEUE_SIZE) {
        // Удаляем самое старое событие
        queue_head = (queue_head + 1) % EVENT_QUEUE_SIZE;
        queue_count--;
    }
    
    // Добавляем время события
    event.timestamp = timer_get_ticks();
    
    // Добавляем в очередь
    event_queue[queue_tail] = event;
    queue_tail = (queue_tail + 1) % EVENT_QUEUE_SIZE;
    queue_count++;
}

// Получить событие из очереди (вызывается ТОЛЬКО из главного цикла)
int event_poll(event_t* event) {
    if (queue_count == 0) {
        return 0; // Очередь пуста
    }
    
    // Берём событие из головы очереди
    *event = event_queue[queue_head];
    queue_head = (queue_head + 1) % EVENT_QUEUE_SIZE;
    queue_count--;
    
    return 1; // Успешно
}

// Проверить, есть ли события
int event_available(void) {
    return queue_count > 0;
}

// Очистить очередь
void event_clear(void) {
    queue_head = 0;
    queue_tail = 0;
    queue_count = 0;
}