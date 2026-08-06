#ifndef KERNEL_MUTEX_H
#define KERNEL_MUTEX_H

#include <stdint.h>

typedef struct mutex {
    volatile uint8_t locked;
    volatile uint32_t flags;
} mutex_t;

#define MUTEX_INIT { .locked = 0, .flags = 0 }

void mutex_init(mutex_t *m);
void mutex_lock(mutex_t *m);
void mutex_unlock(mutex_t *m);
int mutex_trylock(mutex_t *m);
void mutex_destroy(mutex_t *m);

#endif