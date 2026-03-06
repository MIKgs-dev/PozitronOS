#ifndef POZITRON_CALLOUT_H
#define POZITRON_CALLOUT_H

#include <stdint.h>

typedef struct callout {
    void (*func)(void*);
    void *arg;
    uint32_t expire_tick;
    uint8_t active;
    struct callout *next;
} callout_t;

void callout_init(callout_t *c);
void callout_reset(callout_t *c, int ticks, void (*func)(void*), void *arg);
void callout_stop(callout_t *c);
void callout_process(uint32_t current_tick);

#endif