#ifndef TIMER_UTILS_H
#define TIMER_UTILS_H

#include <stdint.h>
#include "drivers/timer.h"

#define MS_TO_TICKS(ms) ((ms) / 10)

static inline uint32_t timer_calc_ms(uint32_t ms) {
    return timer_get_ticks() + MS_TO_TICKS(ms);
}

static inline int timer_check_ms(uint32_t end_ticks) {
    return (int32_t)(timer_get_ticks() - end_ticks) >= 0;
}

void udelay(uint32_t us);
void mdelay(uint32_t ms);
void ndelay(uint32_t ns);

void yield(void);

#endif