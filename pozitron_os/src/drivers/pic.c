#include "drivers/pic.h"
#include "kernel/ports.h"
#include "drivers/vga.h"
#include "drivers/serial.h"
#include "core/isr.h"

// Обработчики IRQ
static isr_handler_t irq_handlers[16] = {0};

// Инициализация PIC
void pic_init(void) {
    serial_puts("[PIC] Initializing...\n");
    
    // ICW1 - начало инициализации
    outb(PIC1_COMMAND, 0x11);
    outb(PIC2_COMMAND, 0x11);
    
    // ICW2 - смещение векторов прерываний
    outb(PIC1_DATA, 0x20); // IRQ 0-7 -> INT 0x20-0x27
    outb(PIC2_DATA, 0x28); // IRQ 8-15 -> INT 0x28-0x2F
    
    // ICW3 - связи между контроллерами
    outb(PIC1_DATA, 0x04); // PIC1 имеет slave на IRQ2
    outb(PIC2_DATA, 0x02); // PIC2 каскадирует через IRQ2
    
    // ICW4 - дополнительная информация
    outb(PIC1_DATA, 0x01);
    outb(PIC2_DATA, 0x01);
    
    // Размаскируем все прерывания
    outb(PIC1_DATA, 0x00);
    outb(PIC2_DATA, 0x00);
    
    serial_puts("[PIC] Initialized\n");
}

// Отправка EOI
void pic_send_eoi(uint8_t irq) {
    if (irq >= 8) {
        outb(PIC2_COMMAND, PIC_EOI);
    }
    outb(PIC1_COMMAND, PIC_EOI);
}

// Установка обработчика IRQ
void irq_install_handler(uint8_t irq, isr_handler_t handler) {
    serial_puts("[PIC] Installing IRQ handler ");
    serial_puts_num(irq);
    serial_puts("\n");
    irq_handlers[irq] = handler;
}

// Удаление обработчика
void irq_uninstall_handler(uint8_t irq) {
    irq_handlers[irq] = 0;
}

// Главный обработчик IRQ
void irq_handler(registers_t* r) {
    uint32_t int_no = r->int_no;
    
    // Проверяем что это IRQ (32-47)
    if (int_no >= 32 && int_no <= 47) {
        uint8_t irq_num = int_no - 32;
        
        // Вызываем установленный обработчик
        if (irq_handlers[irq_num] != 0) {
            isr_handler_t handler = irq_handlers[irq_num];
            handler(r);
        }
        
        // Отправляем EOI
        pic_send_eoi(irq_num);
    }
}