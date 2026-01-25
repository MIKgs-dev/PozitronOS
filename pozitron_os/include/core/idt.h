#ifndef IDT_H
#define IDT_H

#include "../kernel/types.h"

// Структура дескриптора IDT
struct idt_entry {
    uint16_t base_low;
    uint16_t selector;
    uint8_t zero;
    uint8_t flags;
    uint16_t base_high;
} __attribute__((packed));

// Структура указателя на IDT
struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

// Флаги для IDT
#define IDT_FLAG_PRESENT  0x80
#define IDT_FLAG_RING0    0x00
#define IDT_FLAG_RING3    0x60
#define IDT_FLAG_32BIT_INT 0x0E
#define IDT_FLAG_32BIT_TRAP 0x0F

void idt_init(void);
void idt_set_entry(uint8_t num, uint32_t base, uint16_t selector, uint8_t flags);

#endif