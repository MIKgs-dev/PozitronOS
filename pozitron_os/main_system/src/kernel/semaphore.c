#include <kernel/semaphore.h>
#include <drivers/serial.h>

void semaphore_init(semaphore_t *s, uint32_t count) {
    s->count = count;
    s->waiters = 0;
}

void semaphore_wait(semaphore_t *s) {
    while (1) {
        if (s->count > 0) {
            if (__sync_lock_test_and_set(&s->count, s->count - 1) > 0) {
                return;
            }
        }
        asm volatile("pause");
    }
}

void semaphore_signal(semaphore_t *s) {
    __sync_add_and_fetch(&s->count, 1);
}

int semaphore_trywait(semaphore_t *s) {
    if (s->count > 0) {
        if (__sync_lock_test_and_set(&s->count, s->count - 1) > 0) {
            return 0;
        }
    }
    return -1;
}