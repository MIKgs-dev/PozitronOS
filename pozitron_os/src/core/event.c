#include "core/event.h"
#include "drivers/serial.h"
#include "drivers/timer.h"
#include <stddef.h>

#define EVENT_QUEUE_SIZE 128

static event_t event_queue[EVENT_QUEUE_SIZE];
static uint32_t queue_head = 0;
static uint32_t queue_tail = 0;
static uint32_t queue_count = 0;

void event_init(void) {
    queue_head = 0;
    queue_tail = 0;
    queue_count = 0;
    serial_puts("[EVENT] Event system initialized\n");
}

void event_post(event_t event) {
    asm volatile("cli");
    
    event.timestamp = timer_get_ticks();
    
    if (queue_count >= EVENT_QUEUE_SIZE) {
        queue_head = (queue_head + 1) % EVENT_QUEUE_SIZE;
        queue_count--;
    }
    
    event_queue[queue_tail] = event;
    queue_tail = (queue_tail + 1) % EVENT_QUEUE_SIZE;
    queue_count++;
    
    asm volatile("sti");
}

int event_poll(event_t* event) {
    int result = 0;
    
    asm volatile("cli");
    
    if (queue_count > 0) {
        *event = event_queue[queue_head];
        queue_head = (queue_head + 1) % EVENT_QUEUE_SIZE;
        queue_count--;
        result = 1;
    }
    
    asm volatile("sti");
    
    return result;
}

int event_available(void) {
    int available;
    asm volatile("cli");
    available = (queue_count > 0);
    asm volatile("sti");
    return available;
}

void event_clear(void) {
    asm volatile("cli");
    queue_head = 0;
    queue_tail = 0;
    queue_count = 0;
    asm volatile("sti");
}