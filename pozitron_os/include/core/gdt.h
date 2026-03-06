#ifndef GDT_H
#define GDT_H

#include "../kernel/types.h"

// Структура дескриптора GDT
struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_middle;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
} __attribute__((packed));

// Структура указателя на GDT
struct gdt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

// Индексы сегментов в GDT
#define GDT_NULL    0
#define GDT_CODE    1
#define GDT_DATA    2
#define GDT_USER_CODE 3
#define GDT_USER_DATA 4
#define GDT_TSS     5

// Должны быть именно такие значения:
#define GDT_CODE_SELECTOR     0x08  // 1*8
#define GDT_DATA_SELECTOR     0x10  // 2*8
#define GDT_USER_CODE_SELECTOR 0x1B // 3*8 + 3
#define GDT_USER_DATA_SELECTOR 0x23 // 4*8 + 3
#define GDT_TSS_SELECTOR      0x2B // 5*8 + 3

// Флаги доступа
#define GDT_ACCESS_PRESENT     0x80
#define GDT_ACCESS_RING0       0x00
#define GDT_ACCESS_RING3       0x60
#define GDT_ACCESS_CODE_SEG    0x18
#define GDT_ACCESS_DATA_SEG    0x10
#define GDT_ACCESS_CODE_READ   0x02
#define GDT_ACCESS_DATA_WRITE  0x02

// Флаги гранулярности
#define GDT_GRAN_4KB   0x80
#define GDT_GRAN_32BIT 0x40

// TSS структура
struct tss_entry {
    uint32_t prev_tss;
    uint32_t esp0;
    uint32_t ss0;
    uint32_t esp1;
    uint32_t ss1;
    uint32_t esp2;
    uint32_t ss2;
    uint32_t cr3;
    uint32_t eip;
    uint32_t eflags;
    uint32_t eax;
    uint32_t ecx;
    uint32_t edx;
    uint32_t ebx;
    uint32_t esp;
    uint32_t ebp;
    uint32_t esi;
    uint32_t edi;
    uint32_t es;
    uint32_t cs;
    uint32_t ss;
    uint32_t ds;
    uint32_t fs;
    uint32_t gs;
    uint32_t ldt;
    uint16_t trap;
    uint16_t iomap_base;
} __attribute__((packed));

// Функции
void gdt_init(void);
void gdt_set_entry(int index, uint32_t base, uint32_t limit, uint8_t access, uint8_t granularity);
void tss_install(void);  // Переименовано чтобы не конфликтовать
void tss_set_stack(uint32_t ss0, uint32_t esp0);
void jump_to_userspace(void (*entry)(void));

extern void gdt_load(struct gdt_ptr* ptr);
extern void tss_flush(void);

#endif