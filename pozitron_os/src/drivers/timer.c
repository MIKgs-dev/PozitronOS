#include "drivers/timer.h"
#include "kernel/ports.h"
#include "drivers/vga.h"
#include "drivers/pic.h"
#include "drivers/serial.h"
#include "core/event.h"

static uint32_t timer_ticks = 0;

// Инициализация таймера
void timer_init(uint32_t frequency) {
    // Устанавливаем обработчик прерывания таймера (IRQ0)
    irq_install_handler(0, timer_handler);
    
    // Вычисляем делитель
    uint32_t divisor = 1193180 / frequency;
    
    // Отправляем команду настройки в PIT
    outb(PIT_COMMAND, PIT_MODE3);
    
    // Отправляем делитель (младший и старший байты)
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0, (uint8_t)((divisor >> 8) & 0xFF));
    
    serial_puts("[TIMER] Initialized at ");
    serial_puts_num(frequency);
    serial_puts(" Hz\n");
}

// Обработчик прерывания таймера
void timer_handler(registers_t* regs) {
    (void)regs;
    
    timer_ticks++;
    
    // ===== СОБЫТИЕ ТАЙМЕРА =====
    // Отправляем событие каждые 10 тиков (100ms при 100Hz таймере)
    if (timer_ticks % 10 == 0) {
        event_t event;
        event.type = EVENT_TIMER_TICK;
        event.data1 = timer_ticks;
        event.data2 = 0;
        event_post(event);
    }
    
    // Отправляем EOI
    pic_send_eoi(0);
}

// Получить текущее количество тиков
uint32_t timer_get_ticks(void) {
    return timer_ticks;
}

// Ждать указанное количество тиков
void timer_wait(uint32_t ticks) {
    uint32_t end_ticks = timer_ticks + ticks;
    while (timer_ticks < end_ticks) {
        asm volatile ("hlt");
    }
}

// Задержка в миллисекундах
void timer_sleep_ms(uint32_t milliseconds) {
    // 100Hz таймер = 10ms на тик
    uint32_t ticks_to_wait = milliseconds / 10;
    if (ticks_to_wait == 0) ticks_to_wait = 1;
    
    timer_wait(ticks_to_wait);
}