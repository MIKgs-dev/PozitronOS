#ifndef TIMER_H
#define TIMER_H

#include "../kernel/types.h"
#include "../core/isr.h"

#define TIMER_FREQUENCY 100

#define PIT_CHANNEL0 0x40
#define PIT_CHANNEL1 0x41
#define PIT_CHANNEL2 0x42
#define PIT_COMMAND  0x43

#define PIT_MODE3 0x36

void timer_init(uint32_t frequency);
void timer_wait(uint32_t ticks);
uint32_t timer_get_ticks(void);
void timer_handler(registers_t* regs);

// Добавляем новую функцию
void timer_sleep_ms(uint32_t milliseconds);

#endif