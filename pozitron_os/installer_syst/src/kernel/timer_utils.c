#include "kernel/timer_utils.h"

void udelay(uint32_t us) {
    for (volatile uint32_t i = 0; i < us * 100; i++);
}

void mdelay(uint32_t ms) {
    for (volatile uint32_t i = 0; i < ms * 1000; i++);
}

void ndelay(uint32_t ns) {
    for (volatile uint32_t i = 0; i < ns / 10; i++);
}

void yield(void) {
    asm volatile("sti\n\t"
                 "hlt\n\t"
                 "cli");
}