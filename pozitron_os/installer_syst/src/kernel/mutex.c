#include <kernel/mutex.h>
#include <drivers/serial.h>

void mutex_init(mutex_t *m) {
    m->locked = 0;
    m->flags = 0;
}

void mutex_lock(mutex_t *m) {
    uint32_t eflags;
    
    asm volatile("pushfl; popl %0" : "=r"(eflags));
    asm volatile("cli");
    
    while (__sync_lock_test_and_set(&m->locked, 1)) {
        asm volatile("sti; pause; cli");
    }
    
    asm volatile("sti");
}

void mutex_unlock(mutex_t *m) {
    asm volatile("cli");
    __sync_lock_release(&m->locked);
    asm volatile("sti");
}

int mutex_trylock(mutex_t *m) {
    return __sync_lock_test_and_set(&m->locked, 1) == 0;
}

void mutex_destroy(mutex_t *m) {
    // В простейшем случае ничего делать не надо
    (void)m;
}