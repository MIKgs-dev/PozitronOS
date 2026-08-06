#include "core/isr.h"
#include "drivers/vga.h"
#include "drivers/serial.h"
#include "kernel/paging.h"

static isr_handler_t interrupt_handlers[256];

void isr_default_handler(registers_t* r) {
    serial_puts("[ISR] Unhandled interrupt: ");
    
    if (r->int_no < 10) {
        char c = '0' + r->int_no;
        serial_write(c);
    } else if (r->int_no < 100) {
        char c1 = '0' + (r->int_no / 10);
        char c2 = '0' + (r->int_no % 10);
        serial_write(c1);
        serial_write(c2);
    }
    serial_puts("\n");
    
    if (r->int_no < 32) {
        vga_puts("\nCPU Exception! System halted.\n");
        serial_puts("[ISR] CPU Exception - halting\n");
        
        asm volatile ("cli");
        while (1) {
            asm volatile ("hlt");
        }
    }
}

void isr_install_handler(uint8_t num, isr_handler_t handler) {
    interrupt_handlers[num] = handler;
}

void isr_uninstall_handler(uint8_t num) {
    interrupt_handlers[num] = 0;
}

void isr_handler(registers_t* r) {
    if (interrupt_handlers[r->int_no] != 0) {
        isr_handler_t handler = interrupt_handlers[r->int_no];
        handler(r);
    } else {
        isr_default_handler(r);
    }
}

void isr_init(void) {
    serial_puts("[ISR] Initializing...\n");
    
    for (int i = 0; i < 256; i++) {
        interrupt_handlers[i] = 0;
    }
    
    for (int i = 0; i < 32; i++) {
        isr_install_handler(i, isr_default_handler);
    }
    
    serial_puts("[ISR] Default handlers installed\n");
    
    isr_install();
}