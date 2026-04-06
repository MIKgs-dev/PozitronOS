#ifndef KERNEL_SEMAPHORE_H
#define KERNEL_SEMAPHORE_H

#include <stdint.h>

typedef struct semaphore {
    volatile uint32_t count;
    volatile uint32_t waiters;
} semaphore_t;

void semaphore_init(semaphore_t *s, uint32_t count);
void semaphore_wait(semaphore_t *s);
void semaphore_signal(semaphore_t *s);
int semaphore_trywait(semaphore_t *s);

#endif