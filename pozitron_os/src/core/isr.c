#include "core/isr.h"
#include "drivers/vga.h"
#include "drivers/serial.h"

// Массив обработчиков прерываний
static isr_handler_t interrupt_handlers[256];

// Обработчик прерываний по умолчанию
void isr_default_handler(registers_t* r) {
    serial_puts("[ISR] Unhandled interrupt: ");
    
    // Вывод номера прерывания
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
    
    // Вешаем систему для критических ошибок
    if (r->int_no < 32) {
        vga_puts("\nCPU Exception! System halted.\n");
        serial_puts("[ISR] CPU Exception - halting\n");
        
        asm volatile ("cli");
        while (1) {
            asm volatile ("hlt");
        }
    }
}

// Установка обработчика прерывания
void isr_install_handler(uint8_t num, isr_handler_t handler) {
    interrupt_handlers[num] = handler;
}

// Удаление обработчика
void isr_uninstall_handler(uint8_t num) {
    interrupt_handlers[num] = 0;
}

// Обработчик, вызываемый из ассемблера
void isr_handler(registers_t* r) {
    serial_puts("[ISR] Handler called, int_no=");
    
    // Вывод номера
    if (r->int_no < 10) {
        char c = '0' + r->int_no;
        serial_write(c);
    }
    serial_puts("\n");
    
    if (interrupt_handlers[r->int_no] != 0) {
        isr_handler_t handler = interrupt_handlers[r->int_no];
        handler(r);
    } else {
        isr_default_handler(r);
    }
}

// Инициализация ISR
void isr_init(void) {
    serial_puts("[ISR] Initializing...\n");
    
    // Инициализируем все обработчики как NULL
    for (int i = 0; i < 256; i++) {
        interrupt_handlers[i] = 0;
    }
    
    // Устанавливаем обработчики для исключений процессора
    for (int i = 0; i < 32; i++) {
        isr_install_handler(i, isr_default_handler);
    }
    
    serial_puts("[ISR] Default handlers installed\n");
    
    // Устанавливаем обработчики прерываний
    isr_install();
}