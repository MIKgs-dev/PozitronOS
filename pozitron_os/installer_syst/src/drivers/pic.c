#include "drivers/pic.h"
#include "kernel/ports.h"
#include "drivers/vga.h"
#include "drivers/serial.h"
#include "core/isr.h"

static isr_handler_t irq_handlers[16] = {0};

void pic_init(void) {
    serial_puts("[PIC] Initializing...\n");
    
    outb(PIC1_COMMAND, 0x11);
    outb(PIC2_COMMAND, 0x11);
    
    outb(PIC1_DATA, 0x20);
    outb(PIC2_DATA, 0x28);
    
    outb(PIC1_DATA, 0x04);
    outb(PIC2_DATA, 0x02);
    
    outb(PIC1_DATA, 0x01);
    outb(PIC2_DATA, 0x01);
    
    outb(PIC1_DATA, 0x00);
    outb(PIC2_DATA, 0x00);
    
    serial_puts("[PIC] Initialized\n");
}

void pic_send_eoi(uint8_t irq) {
    if (irq >= 8) {
        outb(PIC2_COMMAND, PIC_EOI);
    }
    outb(PIC1_COMMAND, PIC_EOI);
}

void irq_install_handler(uint8_t irq, isr_handler_t handler) {
    serial_puts("[PIC] Installing IRQ handler ");
    serial_puts_num(irq);
    serial_puts("\n");
    irq_handlers[irq] = handler;
}

void irq_uninstall_handler(uint8_t irq) {
    irq_handlers[irq] = 0;
}

void irq_handler(registers_t* r) {
    uint32_t int_no = r->int_no;
    
    if (int_no >= 32 && int_no <= 47) {
        uint8_t irq_num = int_no - 32;
        
        if (irq_handlers[irq_num] != 0) {
            isr_handler_t handler = irq_handlers[irq_num];
            handler(r);
        }
        
        pic_send_eoi(irq_num);
    }
}