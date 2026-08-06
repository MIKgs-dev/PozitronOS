#include "core/cpu.h"
#include "drivers/serial.h"

void cpu_init_fpu(void) {
    uint32_t cr0;
    
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1 << 2);
    cr0 |= (1 << 1);
    cr0 |= (1 << 5);
    asm volatile("mov %0, %%cr0" : : "r"(cr0));
    
    asm volatile("fninit");
}

void cpu_init_sse(void) {
    uint32_t cr4;
    
    asm volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1 << 9);
    cr4 |= (1 << 10);
    asm volatile("mov %0, %%cr4" : : "r"(cr4));
}

void cpu_init(void) {
    serial_puts("[CPU] Hardware sub-systems initialization starting...\n");
    
    cpu_init_fpu();
    serial_puts("[CPU] FPU x87 sub-processor initialized.\n");
    
    cpu_init_sse();
    serial_puts("[CPU] SSE extension activated.\n");
    
    serial_puts("[CPU] All CPU features initialized successfully.\n");
}