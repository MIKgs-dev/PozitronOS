#ifndef PIC_H
#define PIC_H

#include "../kernel/types.h"
#include "../core/isr.h"  // Добавляем для registers_t

#define PIC1_COMMAND 0x20
#define PIC1_DATA 0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA 0xA1

#define PIC_EOI 0x20

#define IRQ0  32
#define IRQ1  33
#define IRQ2  34
#define IRQ3  35
#define IRQ4  36
#define IRQ5  37
#define IRQ6  38
#define IRQ7  39
#define IRQ8  40
#define IRQ9  41
#define IRQ10 42
#define IRQ11 43
#define IRQ12 44
#define IRQ13 45
#define IRQ14 46
#define IRQ15 47

// Функции
void pic_init(void);
void pic_disable(void);
void pic_send_eoi(uint8_t irq);
void irq_install_handler(uint8_t irq, isr_handler_t handler);
void irq_uninstall_handler(uint8_t irq);
void irq_handler(registers_t* r);

#endif