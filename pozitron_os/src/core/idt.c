#include "core/idt.h"
#include "core/isr.h"

#define IDT_ENTRIES 256

static struct idt_entry idt[IDT_ENTRIES];
static struct idt_ptr idtp;

// Внешние обработчики из isr_asm.asm
extern void isr0();
extern void isr1();
extern void isr2();
extern void isr3();
extern void isr4();
extern void isr5();
extern void isr6();
extern void isr7();
extern void isr8();
extern void isr9();
extern void isr10();
extern void isr11();
extern void isr12();
extern void isr13();
extern void isr14();
extern void isr15();
extern void isr16();
extern void isr17();
extern void isr18();
extern void isr19();
extern void isr20();
extern void isr21();
extern void isr22();
extern void isr23();
extern void isr24();
extern void isr25();
extern void isr26();
extern void isr27();
extern void isr28();
extern void isr29();
extern void isr30();
extern void isr31();

// Внешние обработчики из irq_asm.asm
extern void irq0();
extern void irq1();
extern void irq2();
extern void irq3();
extern void irq4();
extern void irq5();
extern void irq6();
extern void irq7();
extern void irq8();
extern void irq9();
extern void irq10();
extern void irq11();
extern void irq12();
extern void irq13();
extern void irq14();
extern void irq15();

// Внешняя функция из ассемблера
extern void idt_load(struct idt_ptr* ptr);

// Установка записи в IDT
void idt_set_entry(uint8_t num, uint32_t base, uint16_t selector, uint8_t flags) {
    idt[num].base_low = base & 0xFFFF;
    idt[num].base_high = (base >> 16) & 0xFFFF;
    idt[num].selector = selector;
    idt[num].zero = 0;
    idt[num].flags = flags | 0x80;
}

// Инициализация IDT
void idt_init(void) {
    idtp.limit = sizeof(struct idt_entry) * IDT_ENTRIES - 1;
    idtp.base = (uint32_t)&idt;
    
    // Заполняем все записи нулями сначала
    for (int i = 0; i < IDT_ENTRIES; i++) {
        idt_set_entry(i, 0, 0x08, 0);
    }
    
    // Устанавливаем обработчики исключений процессора (0-31)
    idt_set_entry(0, (uint32_t)isr0, 0x08, IDT_FLAG_32BIT_INT);
    idt_set_entry(1, (uint32_t)isr1, 0x08, IDT_FLAG_32BIT_INT);
    idt_set_entry(2, (uint32_t)isr2, 0x08, IDT_FLAG_32BIT_INT);
    idt_set_entry(3, (uint32_t)isr3, 0x08, IDT_FLAG_32BIT_INT);
    idt_set_entry(4, (uint32_t)isr4, 0x08, IDT_FLAG_32BIT_INT);
    idt_set_entry(5, (uint32_t)isr5, 0x08, IDT_FLAG_32BIT_INT);
    idt_set_entry(6, (uint32_t)isr6, 0x08, IDT_FLAG_32BIT_INT);
    idt_set_entry(7, (uint32_t)isr7, 0x08, IDT_FLAG_32BIT_INT);
    idt_set_entry(8, (uint32_t)isr8, 0x08, IDT_FLAG_32BIT_INT);
    idt_set_entry(9, (uint32_t)isr9, 0x08, IDT_FLAG_32BIT_INT);
    idt_set_entry(10, (uint32_t)isr10, 0x08, IDT_FLAG_32BIT_INT);
    idt_set_entry(11, (uint32_t)isr11, 0x08, IDT_FLAG_32BIT_INT);
    idt_set_entry(12, (uint32_t)isr12, 0x08, IDT_FLAG_32BIT_INT);
    idt_set_entry(13, (uint32_t)isr13, 0x08, IDT_FLAG_32BIT_INT);
    idt_set_entry(14, (uint32_t)isr14, 0x08, IDT_FLAG_32BIT_INT);
    idt_set_entry(15, (uint32_t)isr15, 0x08, IDT_FLAG_32BIT_INT);
    idt_set_entry(16, (uint32_t)isr16, 0x08, IDT_FLAG_32BIT_INT);
    idt_set_entry(17, (uint32_t)isr17, 0x08, IDT_FLAG_32BIT_INT);
    idt_set_entry(18, (uint32_t)isr18, 0x08, IDT_FLAG_32BIT_INT);
    idt_set_entry(19, (uint32_t)isr19, 0x08, IDT_FLAG_32BIT_INT);
    idt_set_entry(20, (uint32_t)isr20, 0x08, IDT_FLAG_32BIT_INT);
    idt_set_entry(21, (uint32_t)isr21, 0x08, IDT_FLAG_32BIT_INT);
    idt_set_entry(22, (uint32_t)isr22, 0x08, IDT_FLAG_32BIT_INT);
    idt_set_entry(23, (uint32_t)isr23, 0x08, IDT_FLAG_32BIT_INT);
    idt_set_entry(24, (uint32_t)isr24, 0x08, IDT_FLAG_32BIT_INT);
    idt_set_entry(25, (uint32_t)isr25, 0x08, IDT_FLAG_32BIT_INT);
    idt_set_entry(26, (uint32_t)isr26, 0x08, IDT_FLAG_32BIT_INT);
    idt_set_entry(27, (uint32_t)isr27, 0x08, IDT_FLAG_32BIT_INT);
    idt_set_entry(28, (uint32_t)isr28, 0x08, IDT_FLAG_32BIT_INT);
    idt_set_entry(29, (uint32_t)isr29, 0x08, IDT_FLAG_32BIT_INT);
    idt_set_entry(30, (uint32_t)isr30, 0x08, IDT_FLAG_32BIT_INT);
    idt_set_entry(31, (uint32_t)isr31, 0x08, IDT_FLAG_32BIT_INT);
    
    // Устанавливаем обработчики IRQ (32-47)
    idt_set_entry(32, (uint32_t)irq0, 0x08, IDT_FLAG_32BIT_INT);
    idt_set_entry(33, (uint32_t)irq1, 0x08, IDT_FLAG_32BIT_INT);
    idt_set_entry(34, (uint32_t)irq2, 0x08, IDT_FLAG_32BIT_INT);
    idt_set_entry(35, (uint32_t)irq3, 0x08, IDT_FLAG_32BIT_INT);
    idt_set_entry(36, (uint32_t)irq4, 0x08, IDT_FLAG_32BIT_INT);
    idt_set_entry(37, (uint32_t)irq5, 0x08, IDT_FLAG_32BIT_INT);
    idt_set_entry(38, (uint32_t)irq6, 0x08, IDT_FLAG_32BIT_INT);
    idt_set_entry(39, (uint32_t)irq7, 0x08, IDT_FLAG_32BIT_INT);
    idt_set_entry(40, (uint32_t)irq8, 0x08, IDT_FLAG_32BIT_INT);
    idt_set_entry(41, (uint32_t)irq9, 0x08, IDT_FLAG_32BIT_INT);
    idt_set_entry(42, (uint32_t)irq10, 0x08, IDT_FLAG_32BIT_INT);
    idt_set_entry(43, (uint32_t)irq11, 0x08, IDT_FLAG_32BIT_INT);
    idt_set_entry(44, (uint32_t)irq12, 0x08, IDT_FLAG_32BIT_INT);
    idt_set_entry(45, (uint32_t)irq13, 0x08, IDT_FLAG_32BIT_INT);
    idt_set_entry(46, (uint32_t)irq14, 0x08, IDT_FLAG_32BIT_INT);
    idt_set_entry(47, (uint32_t)irq15, 0x08, IDT_FLAG_32BIT_INT);
    
    // Загружаем IDT
    idt_load(&idtp);
}