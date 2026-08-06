#include "drivers/timer.h"
#include "kernel/ports.h"
#include "drivers/vga.h"
#include "drivers/pic.h"
#include "drivers/serial.h"
#include "core/event.h"
#include "kernel/callout.h"
#include "kernel/task.h"

static uint32_t timer_ticks = 0;

void timer_init(uint32_t frequency) {
    irq_install_handler(0, timer_handler);
    
    uint32_t divisor = 1193180 / frequency;
    
    outb(PIT_COMMAND, PIT_MODE3);
    
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0, (uint8_t)((divisor >> 8) & 0xFF));
    
    serial_puts("[TIMER] Initialized at ");
    serial_puts_num(frequency);
    serial_puts(" Hz\n");
}

void timer_handler(registers_t* regs) {
    (void)regs;
    
    timer_ticks++;

    callout_process(timer_ticks);

    task_update_timers();
    
    if (timer_ticks % 10 == 0) {
        event_t event;
        event.type = EVENT_TIMER_TICK;
        event.data1 = timer_ticks;
        event.data2 = 0;
        event_post(event);
    }
    
    pic_send_eoi(0);
}

uint32_t timer_get_ticks(void) {
    return timer_ticks;
}

uint32_t timer_get_ticks_ms(void) {
    return timer_get_ticks() * 10; // 100 Гц = 10ms
}

void timer_wait(uint32_t ticks) {
    uint32_t end_ticks = timer_ticks + ticks;
    while (timer_ticks < end_ticks) {
        asm volatile ("hlt");
    }
}

void timer_sleep_ms(uint32_t milliseconds) {
    uint32_t ticks = milliseconds / 10;
    if (ticks < 1) ticks = 1;
    uint32_t start = timer_get_ticks();
    while (timer_get_ticks() - start < ticks) {
        asm volatile("pause");
    }
}

void timer_sleep_us(uint32_t microseconds) {
    uint32_t ticks = microseconds / 10000;
    if (ticks < 1) ticks = 1;
    uint32_t start = timer_get_ticks();
    while (timer_get_ticks() - start < ticks) {
        asm volatile("pause");
    }
}