#ifndef GDT_H
#define GDT_H

#include "../kernel/types.h"

#define GDT_ENTRIES 6

#define SEL_NULL        0x00
#define SEL_KERNEL_CODE 0x08
#define SEL_KERNEL_DATA 0x10
#define SEL_USER_CODE   0x1B
#define SEL_USER_DATA   0x23
#define SEL_TSS         0x28

#define GDT_ACCESS_PRESENT     0x80
#define GDT_ACCESS_RING0       0x00
#define GDT_ACCESS_RING3       0x60
#define GDT_ACCESS_CODE        0x18
#define GDT_ACCESS_DATA        0x10
#define GDT_ACCESS_CODE_READ   0x02
#define GDT_ACCESS_DATA_WRITE  0x02

#define GDT_FLAG_4KB   0x80
#define GDT_FLAG_32BIT 0x40

struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_middle;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

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

void gdt_init(void);
void tss_set_stack(uint32_t ss0, uint32_t esp0);
void jump_to_userspace(uint32_t entry, uint32_t stack, uint32_t page_dir);

extern struct tss_entry tss;
extern void gdt_flush(uint32_t);
extern void tss_flush(void);

#endif