#ifndef GDT_H
#define GDT_H

#include "../kernel/types.h"

// Структура дескриптора GDT
struct gdt_entry {
    uint16_t limit_low;     // Младшие 16 бит лимита
    uint16_t base_low;      // Младшие 16 бит базы
    uint8_t base_middle;    // Следующие 8 бит базы
    uint8_t access;         // Флаги доступа
    uint8_t granularity;    // Гранулярность и старшие биты лимита
    uint8_t base_high;      // Старшие 8 бит базы
} __attribute__((packed));

// Структура указателя на GDT для загрузки в GDTR
struct gdt_ptr {
    uint16_t limit;         // Размер GDT - 1
    uint32_t base;          // Адрес GDT
} __attribute__((packed));

// Индексы сегментов в GDT
#define GDT_NULL    0
#define GDT_CODE    1
#define GDT_DATA    2
#define GDT_USER_CODE 3
#define GDT_USER_DATA 4
#define GDT_TSS     5

// Флаги доступа
#define GDT_ACCESS_PRESENT     0x80
#define GDT_ACCESS_RING0       0x00
#define GDT_ACCESS_RING3       0x60
#define GDT_ACCESS_CODE_SEG    0x18
#define GDT_ACCESS_DATA_SEG    0x10
#define GDT_ACCESS_CODE_READ   0x02
#define GDT_ACCESS_DATA_WRITE  0x02
#define GDT_ACCESS_DIRECTION   0x04
#define GDT_ACCESS_CONFORMING  0x04

// Флаги гранулярности
#define GDT_GRAN_4KB   0x80
#define GDT_GRAN_32BIT 0x40
#define GDT_GRAN_64BIT 0x20

// Функции
void gdt_init(void);
void gdt_set_entry(int index, uint32_t base, uint32_t limit, uint8_t access, uint8_t granularity);
extern void gdt_flush(uint32_t gdt_ptr); // Из ассемблера

#endif