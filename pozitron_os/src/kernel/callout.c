#include <kernel/callout.h>
#include <kernel/memory.h>
#include <drivers/serial.h>
#include <drivers/timer.h>

static callout_t *callout_list = NULL;

void callout_init(callout_t *c) {
    c->func = NULL;
    c->arg = NULL;
    c->expire_tick = 0;
    c->active = 0;
    c->next = NULL;
}

void callout_reset(callout_t *c, int ticks, void (*func)(void*), void *arg) {
    if (c->active) {
        callout_stop(c);
    }
    
    c->func = func;
    c->arg = arg;
    c->expire_tick = timer_get_ticks() + ticks;
    c->active = 1;
    
    c->next = callout_list;
    callout_list = c;
    
    serial_puts("[CALLOUT] Reset at tick ");
    serial_puts_num(c->expire_tick);
    serial_puts("\n");
}

void callout_stop(callout_t *c) {
    if (!c->active) return;
    
    c->active = 0;
    
    if (callout_list == c) {
        callout_list = c->next;
    } else {
        callout_t *prev = callout_list;
        while (prev && prev->next != c) {
            prev = prev->next;
        }
        if (prev) {
            prev->next = c->next;
        }
    }
    
    c->next = NULL;
}

void callout_process(uint32_t current_tick) {
    callout_t *curr = callout_list;
    callout_t *prev = NULL;
    
    while (curr) {
        if (curr->active && curr->expire_tick <= current_tick) {
            if (curr->func) {
                curr->func(curr->arg);
            }
            
            if (prev) {
                prev->next = curr->next;
            } else {
                callout_list = curr->next;
            }
            
            curr->active = 0;
            curr->next = NULL;
            
            curr = (prev) ? prev->next : callout_list;
        } else {
            prev = curr;
            curr = curr->next;
        }
    }
}